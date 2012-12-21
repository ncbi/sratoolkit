/*==============================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/


#include <vdb/cursor.h>
#include <vdb/table.h>
#include <vdb/database.h>
#include <vdb/dependencies.h>

#include <kdb/manager.h>

#include <kfg/config.h>

#include <klib/container.h>
#include <klib/log.h>
#include <klib/out.h>
#include <klib/rc.h>

/* missing macros/function from klib/rc.h
 */
#define GetRCSTATE( rc ) \
    ( ( rc ) & 0x00003FFF )
#define RC_STATE( obj, state)                         \
    ( rc_t ) ( ( ( rc_t ) ( obj ) << 6 ) |            \
               ( ( rc_t ) ( state ) ) )



#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define RELEASE(type, obj) do { rc_t rc2 = type##Release(obj); \
    if (rc2 && !rc) { rc = rc2; } obj = NULL; } while (false)

typedef struct RefNode {
    BSTNode n;

    bool circular;
    char* name;
    uint32_t readLen;
    char* seqId;
    bool local;

    char ref[PATH_MAX + 1];
} RefNode;

static void CC bstWhack(BSTNode* n, void* ignore) {
    RefNode* sn = (RefNode*) n;

    assert(sn);

    free(sn->name);
    sn->name = NULL;

    free(sn->seqId);
    sn->seqId = NULL;

    free(sn);
}

static
int CC bstCmp(const void* item, const BSTNode* n)
{
    const char* s1 = item;
    const RefNode* sn = (const RefNode*) n;

    assert(s1 && sn);

    return strcmp(s1, sn->seqId);
}

static
int CC bstSort(const BSTNode* item, const BSTNode* n)
{
    const RefNode* sn = (const RefNode*) item;

    return bstCmp(sn->seqId, n);
}

typedef struct Column {
    uint32_t idx;
    const char* name;
    uint32_t expected;
    uint32_t elem_bits;
} Column;

static
rc_t AddColumn(rc_t rc, const VCursor* curs, Column* col)
{
    if (rc == 0) {
        assert(curs && col);

        rc = VCursorAddColumn(curs, &col->idx, col->name);
    }

    return rc;
}

/* Read a column data from a Cursor */
static
rc_t CursorRead(rc_t rc, const VCursor* curs, int64_t row_id,
    const Column* col, void* buffer, uint32_t blen, uint32_t* row_len)
{
    assert(curs);

    if (rc == 0) {
        uint32_t elem_bits = col->elem_bits;
        uint32_t expected = col->expected;
        assert(col);

        if (blen == 0) {
            assert(buffer == NULL);
            ++blen;
        }

        rc = VCursorReadDirect
            (curs, row_id, col->idx, elem_bits, buffer, blen - 1, row_len);

        if (rc != 0) {
            if (buffer == NULL &&
                rc == RC(rcVDB, rcCursor, rcReading, rcBuffer, rcInsufficient))
            {
                rc = 0;
            }
        }
        else {
            if (expected > 0 && *row_len != expected) {
                rc = RC(rcVDB, rcCursor, rcReading, rcData, rcUnexpected);
            }
            if (buffer != NULL && expected == 0)
            {   ((char*)buffer)[*row_len] = '\0'; }
        }
    }

    return rc;
}

/* Read a String from Configuration into "value" buffer */
static rc_t ReadCfgStr
(const KConfig* kfg, const char* path, char* value, size_t value_sz)
{
    rc_t rc = 0;
    const KConfigNode *node = NULL;

    rc = KConfigOpenNodeRead(kfg, &node, path);
    if (rc == 0) {
        size_t num_read = 0;
        size_t remaining = 0;
        rc = KConfigNodeRead
            (node, 0, value, value_sz - 1, &num_read, &remaining);
        if (rc == 0) {
            if (remaining != 0)
            {   rc = RC(rcVDB, rcString, rcReading, rcSize, rcExcessive); }
/* TODO: make sure check value[num_read] assignment is valid
         when remaining == 0 */
            else { value[num_read] = '\0'; }
        }
    }

    RELEASE(KConfigNode, node);

    return rc;
}

#define SIZE 4096

/* Ctx struct is used to find a file using KConfig "refseq" parameters */
typedef struct Ctx {
    KDirectory* dir;

    char servers[SIZE];
    char volumes[SIZE];
    char paths[SIZE];
} Ctx;

static rc_t CtxInit(Ctx* self) {
    rc_t rc = 0;
    KConfig* cfg = NULL;

    assert(self);

    if (self->dir != NULL) {
        return rc;
    }

    rc = KDirectoryNativeDir(&self->dir);

    if (rc == 0) {
        rc = KConfigMake(&cfg, NULL);
    }

    if (rc == 0) {
        const char path[] = "refseq/servers";
        rc = ReadCfgStr(cfg, path, self->servers, sizeof self->servers);
        if (rc != 0) {
            if (GetRCState(rc) == rcNotFound)
            {   rc = 0; }
        }
        else {
            const char path[] = "refseq/volumes";
            rc = ReadCfgStr(cfg, path, self->volumes, sizeof self->volumes);
            if (rc != 0) {
                if (GetRCState(rc) == rcNotFound)
                {   rc = 0; }
            }
        }

        if (rc == 0) {
            const char path[] = "refseq/paths";
            rc = ReadCfgStr(cfg, path, self->paths, sizeof self->paths);
            if (rc != 0) {
                if (GetRCState(rc) == rcNotFound)
                {   rc = 0; }
            }
        }
    }

    RELEASE(KConfig, cfg);

    return rc;
}

typedef struct FindRefseq {
    const char* file;
    bool found;
} FindRefseq;

static rc_t CC is_file_in_dir(const KDirectory* dir,
    uint32_t type, const char* name, void* data)
{
    rc_t rc = 0;

    FindRefseq* t = data;

    assert(dir && name && data && t->file);

    if (strcmp(t->file, name) == 0) {
        uint64_t size = 0;
        rc = KDirectoryFileSize(dir, &size, name);
        if (rc == 0 && size > 0) {
            /* compensate configuration-assistant.perl behavior:
               it used to create an empty refseq file
               when failed to download it */
            t->found = true;
        }
    }

    return rc;
}

/* Find file within srv/vol. If found then copy complete path to buf.
 * Return true if found
 */
static bool FindInDir(rc_t* aRc, const KDirectory* native, const char* srv,
    const char* vol, const char* file, char* buf, size_t blen)
{
    rc_t rc = 0;

    const KDirectory* dir = NULL;

    FindRefseq t;

    assert(aRc && native && srv && file && buf && blen);

    rc = *aRc;

    if (rc != 0)
    {   return false; }

    if (vol)
    {   rc = KDirectoryOpenDirRead(native, &dir, false, "%s/%s", srv, vol); }
    else
    {   rc = KDirectoryOpenDirRead(native, &dir, false, srv); }

    if (rc == 0) {
        memset(&t, 0, sizeof t);
        t.file = file;
        rc = KDirectoryVVisit(dir, false, is_file_in_dir, &t, ".", NULL);
        if (GetRCObject(rc) == rcDirectory && GetRCState(rc) == rcUnauthorized)
        {   rc = 0; }

        if (rc == 0 && t.found) {
            if (vol) {
                rc = KDirectoryResolvePath(native,
                    true, buf, blen, "%s/%s/%s", srv, vol, file);
            }
            else {
                rc = KDirectoryResolvePath(native,
                    true, buf, blen, "%s/%s", srv, file);
            }
        }
    }
    else if (rc == SILENT_RC(rcFS, rcDirectory, rcOpening, rcPath, rcIncorrect)) {
        /* ignored nonexistent directory */
        rc = 0;
    }

    RELEASE(KDirectory, dir);

    *aRc = rc;

    return t.found;
}

/* find remote reference using KConfig values */
static
rc_t FindRef(Ctx* ctx, const char* seqId, char* buf, size_t blen)
{
    rc_t rc = 0;
    bool found = false;
    assert(ctx && seqId && buf && blen);
    if (rc == 0) {
        rc = CtxInit(ctx);
    }
    if (rc == 0 && ctx->dir != NULL) {
        if (FindInDir(&rc, ctx->dir, ".", NULL, seqId, buf, blen)) {
            found = true;
        }
        else {
            if (ctx->paths[0]) {
                char paths[SIZE];
                char* path_rem = paths;
                char* path_sep = NULL;
                strcpy(paths, ctx->paths);
                do {
                    char const* path = path_rem;
                    path_sep = strchr(path, ':');
                    if (path_sep) {
                        path_rem = path_sep + 1;
                        *path_sep = 0;
                    }
                    if (FindInDir(&rc, ctx->dir, path, NULL,
                        seqId, buf, blen))
                    {
                        found = true;
                        break;
                    }
                } while(path_sep && rc == 0);
            }
            if (!found && ctx->servers[0] && ctx->volumes[0]) {
                char server[SIZE];
                char vol[SIZE];
                char* srv_sep = NULL;
                char* srv_rem = server;
                strcpy(server, ctx->servers);
    /* TODO check for multiple servers/volumes */
                do {
                    char* vol_rem = vol;
                    char* vol_sep = NULL;
                    strcpy(vol, ctx->volumes);
                    srv_sep = strchr(server, ':');
                    if (srv_sep) {
                        srv_rem = srv_sep + 1;
                        *srv_sep = '\0';
                    }
                    do {
                        char const* volume = vol_rem;
                        vol_sep = strchr(volume, ':');
                        if (vol_sep) {
                            vol_rem = vol_sep + 1;
                            *vol_sep = 0;
                        }
                        if (FindInDir(&rc, ctx->dir, server, volume,
                            seqId, buf, blen))
                        {
                            found = true;
                            break;
                        }
                    } while (vol_sep && rc == 0);
                } while (!found && srv_sep && rc == 0);
            }
        }
    }
    return rc;
}

typedef struct Row {
/* row values */
    uint32_t readLen;
    char circular[8];
    char name[256];
    char seqId[256];

/* we do not read CMP_READ value, just its row_len */
    uint32_t row_lenCMP_READ;
} Row;

/* Add a REFERENCE table Row to BSTree */
static
rc_t AddRow(BSTree* tr, Row* data, Ctx* ctx)
{
    rc_t rc = 0;
    bool newRemote = false;
    RefNode* sn = NULL;

    assert(tr && data && ctx);

    sn = (RefNode*) BSTreeFind(tr, data->seqId, bstCmp);
    if (sn == NULL) {
        sn = calloc(1, sizeof(*sn));
        if (sn == NULL) {
            return RC
                (rcVDB, rcStorage, rcAllocating, rcMemory, rcExhausted);
        }
        sn->seqId = strdup(data->seqId);
        if (sn->seqId == NULL) {
            bstWhack((BSTNode*) sn, NULL);
            sn = NULL;
            return RC
                (rcVDB, rcStorage, rcAllocating, rcMemory, rcExhausted);
        }

        sn->name = strdup(data->name);
        if (sn->name == NULL) {
            bstWhack((BSTNode*) sn, NULL);
            sn = NULL;
            return RC
                (rcVDB, rcStorage, rcAllocating, rcMemory, rcExhausted);
        }

        sn->circular = data->circular[0];
        sn->readLen = data->readLen;
        sn->local = (data->row_lenCMP_READ != 0);
        newRemote = ! sn->local;

        BSTreeInsert(tr, (BSTNode*)sn, bstSort);
    }
    else {
        if (data->row_lenCMP_READ != 0) {
            sn->local = true;
        }
        if (strcmp(sn->name, data->name) || sn->circular != data->circular[0]) {
            return RC(rcVDB, rcCursor, rcReading, rcData, rcInconsistent);
        }
        sn->readLen += data->readLen;
    }

    if (rc == 0 && newRemote) {
        rc = FindRef(ctx, sn->seqId, sn->ref, sizeof sn->ref);
        if (rc != 0)
        {   sn->ref[0] = '\0'; }
    }

    return rc;
}

struct VDBDependencies {
    uint32_t count;
    RefNode** dependencies;

    BSTree tr;
};

typedef struct Initializer {
    bool all;            /* IN: when false: process just missed */
    bool fill;           /* IN:
                          false: count(fill count); true: fill dep(use count) */
    uint32_t count;      /* IO: all/missed count */
    VDBDependencies* dep;/* OUT: to be filled */
    uint32_t i;          /* PRIVATE: index in dep */
    rc_t rc;             /* OUT */
} Initializer;

/* Work function to process dependencies tree
 * Input parameters are in Initializer:
 *  all (true: all dependencies, false: just missing)
 *  fill(false: count dependencies, true: fill dependencies array
 */
static void CC bstProcess(BSTNode* n, void* data) {
    RefNode* elm = (RefNode*) n;
    Initializer* obj = (Initializer*) data;

    assert(elm && obj);

    if (obj->all ||
        (! elm->local && elm->ref[0] == '\0'))
 /* remore reference && refseq table not found */
    {
        if (!obj->fill) {
            ++obj->count;
        }
        else {
            if (obj->dep == NULL || obj->dep->dependencies == NULL) {
                return;
            }
            if (obj->i < obj->count) {
                *(obj->dep->dependencies + ((obj->i)++)) = elm;
            }
            else {
                obj->rc = RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcCorrupt);
            }
        }
    }
}

/* Read REFERENCE table; fill in BSTree */
static
rc_t CC VDatabaseDependencies(const VDatabase *self, BSTree* tr,
    bool* has_no_REFERENCE)
{
    rc_t rc = 0;

    int64_t i = 0;
    int64_t firstId = 0;
    int64_t lastId = 0;

    Column CIRCULAR = { 0, "CIRCULAR", 1,  8 };
    Column CMP_READ = { 0, "CMP_READ", 0,  8 };
    Column NAME     = { 0, "NAME"    , 0,  8 };
    Column READ_LEN = { 0, "READ_LEN", 1, 32 };
    Column SEQ_ID   = { 0, "SEQ_ID",   0,  8 };

    const VTable* tbl = NULL;
    const VCursor* curs = NULL;

    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);

    assert(self && tr && has_no_REFERENCE);

    *has_no_REFERENCE = false;

    if (rc == 0) {
        rc = VDatabaseOpenTableRead(self, &tbl, "REFERENCE");
        if (GetRCState(rc) == rcNotFound) {
            *has_no_REFERENCE = true;
        }
    }
    if (rc == 0) {
        rc = VTableCreateCursorRead(tbl, &curs);
    }

    rc = AddColumn(rc, curs, &CIRCULAR);
    rc = AddColumn(rc, curs, &CMP_READ);
    rc = AddColumn(rc, curs, &NAME    );
    rc = AddColumn(rc, curs, &READ_LEN);
    rc = AddColumn(rc, curs, &SEQ_ID  );

    if (rc == 0) {
        rc = VCursorOpen(curs);
    }
    if (rc == 0) {
        uint64_t count = 0;
        rc = VCursorIdRange(curs, 0, &firstId, &count);
        lastId = firstId + count - 1;
    }

    for (i = firstId; i <= lastId && rc == 0; ++i) {
        uint32_t row_len = 0;
        Row data;
        if (rc == 0) {
            rc = CursorRead(rc, curs, i, &CIRCULAR,
                data.circular, sizeof data.circular, &row_len);
            if (rc != 0) {
                PLOGERR(klogErr, (klogErr, rc,
                    "failed to read cursor(col='CIRCULAR', spot='$(id)')",
                    "id=%ld", i));
            }
        }
        if (rc == 0) {
            rc = CursorRead(rc, curs, i, &CMP_READ,
                NULL, 0, &data.row_lenCMP_READ);
            if (rc != 0) {
                PLOGERR(klogErr, (klogErr, rc,
                    "failed to read cursor(col='CMP_READ', spot='$(id)')",
                    "id=%ld", i));
            }
        }
        if (rc == 0) {
            rc = CursorRead(rc, curs, i, &NAME,
                data.name, sizeof data.name, &row_len);
            if (rc != 0) {
                PLOGERR(klogErr, (klogErr, rc,
                    "failed to read cursor(col='NAME', spot='$(id)')",
                    "id=%ld", i));
            }
        }
        if (rc == 0) {
            rc = CursorRead(rc, curs, i, &READ_LEN,
                &data.readLen, sizeof data.readLen, &row_len);
            if (rc != 0) {
                PLOGERR(klogErr, (klogErr, rc,
                    "failed to read cursor(col='READ_LEN', spot='$(id)')",
                    "id=%ld", i));
            }
        }
        if (rc == 0) {
            rc = CursorRead(rc, curs, i, &SEQ_ID,
                data.seqId, sizeof data.seqId, &row_len);
            if (rc != 0) {
                PLOGERR(klogErr, (klogErr, rc,
                    "failed to read cursor(col='SEQ_ID', spot='$(id)')",
                    "id=%ld", i));
            }
        }
        if (rc == 0) {
            rc = AddRow(tr, &data, &ctx);
        }
    }

    RELEASE(KDirectory, ctx.dir);

    RELEASE(VCursor, curs);
    RELEASE(VTable, tbl);
    if (*has_no_REFERENCE) {
        rc = 0;
    }
    return rc;
}

/* Get dependency number "idx" */
static
rc_t VDBDependenciesGet(const VDBDependencies* self,
    RefNode** dep, uint32_t idx)
{
    rc_t rc = 0;

    assert(dep);

    if (self == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcNull);
    }

    if (idx >= self->count) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcExcessive);
    }

    *dep = *(self->dependencies + idx);
    if (*dep == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcCorrupt);
    }

    return rc;
}

/* Count
 *  retrieve the number of dependencies
 *
 *  "count" [ OUT ] - return parameter for dependencies count
 */
LIB_EXPORT rc_t CC VDBDependenciesCount(const VDBDependencies* self,
    uint32_t* count)
{
    if (self == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcNull);
    }

    if (count == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    *count = self->count;

    return 0;
}

/* Circular */
LIB_EXPORT rc_t CC VDBDependenciesCircular(const VDBDependencies* self,
    bool* circular, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (circular == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        *circular = dep->circular;
    }

    return rc;
}

/* Local
 *  retrieve local property
 *
 *  "local" [ OUT ] - true if object is stored internally
 *
 *  "idx" [ IN ] - zero-based index of dependency
 */
LIB_EXPORT rc_t CC VDBDependenciesLocal(const VDBDependencies* self,
    bool* local, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (local == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        *local = dep->local;
    }

    return rc;
}

/* Name
 *
 * "name" [ OUT ] - returned pointed should not be released.
 *                  it becomes invalid after VDBDependenciesRelease
 */
LIB_EXPORT rc_t CC VDBDependenciesName(const VDBDependencies* self,
    const char** name, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (name == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        *name = dep->name;
    }

    return rc;
}

/* Path
 *  for remote dependencies: returns:
 *                              path for resolved dependency,
 *                              NULL for missing dependency.
 *  returns NULL for local dependencies
 *
 * "path" [ OUT ] - returned pointed should not be released.
 *                  it becomes invalid after VDBDependenciesRelease
 */
LIB_EXPORT rc_t CC VDBDependenciesPath(const VDBDependencies* self,
    const char** path, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (path == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        if (dep->local) {
            *path = NULL;
        }
        else {
            *path = dep->ref;
        }
    }

    return rc;
}

/* SeqId
 *
 * "seq_id" [ OUT ] - returned pointed should not be released.
 *                    it becomes invalid after VDBDependenciesRelease
 */
LIB_EXPORT rc_t CC VDBDependenciesSeqId(const VDBDependencies* self,
    const char** seq_id, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (seq_id == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        *seq_id = dep->seqId;
    }

    return rc;
}

/* Type
 *
 * "type" [ OUT ] - a KDBPathType from kdb/manager.h
 */
LIB_EXPORT rc_t CC VDBDependenciesType(const VDBDependencies* self,
    uint32_t* type, uint32_t idx)
{
    rc_t rc = 0;
    RefNode* dep = NULL;

    if (type == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    rc = VDBDependenciesGet(self, &dep, idx);

    if (rc == 0) {
        *type = kptTable;
    }

    return rc;
}

/* Release
 *  (objects ARE NOT reference counted)
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC VDBDependenciesRelease(const VDBDependencies* cself) {
    VDBDependencies* self = (VDBDependencies*) cself;

    if (self == NULL) {
        return 0;
    }

    BSTreeWhack(&self->tr, bstWhack, NULL);
    BSTreeInit(&self->tr);

    free(self->dependencies);
    self->dependencies = NULL;

    self->count = 0;

    free(self);

    return 0;
}

/* ListDependencies
 *  create a dependencies object: list all dependencies
 *
 *  "dep" [ OUT ] - return for VDBDependencies object
 *
 *  "missing" [ IN ] - if true, list only missing dependencies
 *  otherwise, list all dependencies
 */
LIB_EXPORT rc_t CC VDatabaseListDependencies(const VDatabase* self,
    const VDBDependencies** dep, bool missing)
{
    rc_t rc = 0;
    VDBDependencies* obj;
    bool all = ! missing;
    bool has_no_REFERENCE = false;

    if (self == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcNull);
    }
    if (dep == NULL) {
        return RC(rcVDB, rcDatabase, rcAccessing, rcParam, rcNull);
    }

    obj = calloc(1, sizeof *obj);
    if (obj == NULL) {
        return RC(rcVDB, rcStorage, rcAllocating, rcMemory, rcExhausted);
    }
    BSTreeInit(&obj->tr);

    /* initialize dependencie tree */
    rc = VDatabaseDependencies(self, &obj->tr, &has_no_REFERENCE);
    if (rc == 0 && has_no_REFERENCE) {
        *dep = obj;
        return rc;
    }

    if (rc == 0) {
        Initializer init;
        memset(&init, 0, sizeof init);

        /* count all/missing dependencies */
        init.all = all;
        init.fill = false;
        BSTreeForEach(&obj->tr, false, bstProcess, &init);
        rc = init.rc;
        if (rc == 0) {
            obj->count = init.count;

            /* initialize dependencies array with pointers to tree values */
            if (obj->count > 0) {
                obj->dependencies
                    = calloc(obj->count, sizeof obj->dependencies);
                if (obj->dependencies == NULL) {
                    free(obj);
                    return RC
                        (rcVDB, rcStorage, rcAllocating, rcMemory, rcExhausted);
                }
                init.fill = true;
                init.dep = obj;
                BSTreeForEach(&obj->tr, false, bstProcess, &init);
                rc = init.rc;
                if (rc == 0 && init.i != init.count) {
                    rc = RC(rcVDB, rcDatabase, rcAccessing, rcSelf, rcCorrupt);
                }
            }
        }
    }

    if (rc == 0) {
        *dep = obj;
    }
    else {
        VDBDependenciesRelease(obj);
    }

    return rc;
}

static bool DependenciesError(rc_t rc) {
    return GetRCModule(rc) == rcAlign && GetRCObject(rc) == rcTable
          && GetRCState(rc) == rcNotFound;
}

typedef enum ErrType {
    eUnknown,
    eNoEncInKfg,
    ePwdFileNotFound,
    eBadPwdFile,
    eBadEncKey
} ErrType;



static ErrType DependenciesType(rc_t rc) {
    if (GetRCTarget(rc) == rcEncryptionKey)
    {
        switch (GetRCSTATE(rc))
        {
            /* no configuration or environment */
        case RC_STATE(rcFile, rcUnknown):
            return eNoEncInKfg;

            /* no file where told to look */
        case RC_STATE(rcFile, rcNotFound):
            return ePwdFileNotFound;

            /* after decryption the file wasn't a database object */
        case RC_STATE(rcEncryption, rcIncorrect):
            return eBadEncKey;

        default:
            break;
        }
    }
    else if (GetRCTarget(rc) == rcMgr)
    {
        switch (GetRCSTATE(rc))
        {
        case RC_STATE(rcEncryptionKey, rcTooShort): /* too short */
        case RC_STATE(rcEncryptionKey, rcTooLong): /* too long */
            return eBadPwdFile;

        default:
            break;
        }
    }
    return eUnknown;
}

LIB_EXPORT bool CC UIError( rc_t rc,
    const VDatabase* db, const VTable* table)
{
    bool retval = false;
    if( db != NULL || table != NULL ) {
      if( DependenciesError(rc) ) {
        if( db != NULL ) {
            VDatabaseAddRef(db);
        } else if( VTableOpenParentRead(table, &db) != 0 ) {
            db = NULL;
        }
        if( db != NULL ) {
            const VTable* ref;
            if( VDatabaseOpenTableRead(db, &ref, "REFERENCE") == 0 ) {
                const VCursor* c;
                if( VTableCreateCachedCursorRead(ref, &c, 0) == 0 ) {
                    uint32_t i;
                    retval = VCursorAddColumn(c, &i, "CIRCULAR") == 0 && VCursorOpen(c) == 0;
                    VCursorRelease(c);
                }
                VTableRelease(ref);
            }
            VDatabaseRelease(db);
        }
      }
    }
    else if (db == NULL && table == NULL) {
        ErrType type = DependenciesType(rc);
        if (type != eUnknown) {
            retval = true;
        }
    }
    return retval;
}

static
void CC VDBDependenciesLOGMissing( rc_t rc,
    const VDatabase* db, bool log_list )
{
    static bool once = false;
    if( !once ) {
      KWrtHandler handler;

      once = true;

      handler.writer = KOutWriterGet();
      handler.data = KOutDataGet();
      KOutHandlerSetStdErr();

      if (DependenciesError(rc)) {
        OUTMSG(("This operation requires access to external"
            " reference sequence(s) that could not be located\n"));
        if (db && log_list) {
            const VDBDependencies* dep = NULL;
            uint32_t i, count = 0;
            if( VDatabaseListDependencies(db, &dep, true) == 0 &&
                VDBDependenciesCount(dep, &count) == 0 ) {
                for(i = 0; i < count; i++) {
                    const char* name = NULL, *seqId = NULL;
                    if (VDBDependenciesName(dep, &name, i) == 0
                            && VDBDependenciesSeqId(dep, &seqId, i) == 0)
                    {
                        OUTMSG(("Reference sequence %s %s was not found\n",
                                seqId, name));
                    }
                }
                VDBDependenciesRelease(dep);
            }
        }
        OUTMSG((
              "Please run \"perl configuration-assistant.perl\" and try again\n"
            ));
      }
      else {
        switch (DependenciesType(rc)) {
            case eNoEncInKfg:
                OUTMSG((
"The file you are trying to open is encrypted,\n"
"but no decryption password could be located.\n"
"To set up a password,\n"
"run the 'configuration-assistant.perl' script provided with the toolkit.\n"));
                break;
            case ePwdFileNotFound:
                OUTMSG((
"The file you are trying to open is encrypted,\n"
"but no decryption password could be obtained\n"
"from the path given in configuration.\n"
"To set up or change a password,\n"
"run the 'configuration-assistant.perl' script provided with the toolkit.\n"));
                break;
            case eBadEncKey:
                OUTMSG((
"The file you are trying to open is encrypted, but could not be opened.\n"
"Either your password is incorrect or the downloaded file is corrupt.\n"
"To set up or change a password,\n"
"run the 'configuration-assistant.perl' script provided with the toolkit.\n"
"To test the file for corruption,\n"
"run the 'nencvalid' tool provided with the toolkit.\n"));
                break;
        case eBadPwdFile:
                OUTMSG((
"The file you are trying to open is encrypted, but could not be opened.\n"
"The password in the password file in unusable either from being 0 bytes\n"
"or more than 4096 bytes long. The password starts at the beginning of\n"
"the file and ends with the first CR (\\r) pr LF (\\n) or at the end of\n"
"the file.\n"
"To set up or change a password,\n"
"run the 'configuration-assistant.perl' script provided with the toolkit.\n"));
                break;
            default:
                assert(0);
        }
      }

      OUTMSG(("\n"));
      KOutHandlerSet(handler.writer, handler.data);
    }
}

LIB_EXPORT const char* CC UIDatabaseGetErrorString(rc_t rc)
{
    if (DependenciesError(rc)) {
        return "This operation requires access to external"
            " reference sequence(s) that could not be located";
    }
    else {
        switch (DependenciesType(rc)) {
            case eNoEncInKfg:
                return "The file is encrypted, "
                    "but no decryption password could be located";
            case ePwdFileNotFound:
                return "The file is encrypted, "
                    "but no decryption password could be obtained "
                    "from the path given in configuration";
            case eBadEncKey:
                return "The file is encrypted, but could not be opened. "
                    "Either the password is incorrect or the file is corrupt";
            case eBadPwdFile:
                return "The file is encrypted, but could not be opened. "
                    "The password in the password file in unusable";
            default:
                assert(0);
                return "Unexpected Dependency Type";
        }
    }
}

LIB_EXPORT void CC UIDatabaseLOGError( rc_t rc,
    const VDatabase* db, bool log_list )
{
    VDBDependenciesLOGMissing(rc, db, log_list);
}

LIB_EXPORT void CC UITableLOGError( rc_t rc,
    const VTable* table, bool log_list )
{
    const VDatabase* db;
    if( table == NULL ) {
        VDBDependenciesLOGMissing(rc, NULL, log_list);
    }
    else if( VTableOpenParentRead(table, &db) == 0 && db != NULL ) {
        VDBDependenciesLOGMissing(rc, db, log_list);
        VDatabaseRelease(db);
    }
}
