/*===========================================================================
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

#include <vdb/extern.h>

#include <ncbi/vdb-blast.h> /* VdbBlastMgr */
#include <ncbi/vdb-blast-priv.h> /* VdbBlastMgr */

#include <insdc/insdc.h> /* INSDC_read_type */
#include <insdc/sra.h> /* SRA_READ_TYPE_BIOLOGICAL */

#include <sra/srapath.h> /* SRAPath */
#include <sra/sraschema.h> /* VDBManagerMakeSRASchema */

#include <vdb/manager.h> /* VDBManager */
#include <vdb/schema.h> /* VSchema */
#include <vdb/database.h> /* VDatabase */
#include <vdb/table.h> /* VTable */
#include <vdb/cursor.h> /* VCursor */
#include <vdb/blob.h> /* VBlob */
#include <vdb/vdb-priv.h> /* VTableOpenKTableRead */

#include <kdb/kdb-priv.h> /* KTableGetPath */
#include <kdb/manager.h> /* kptDatabase */
#include <kdb/meta.h> /* KMetadataRelease */
#include <kdb/table.h> /* KTable */

#include <vfs/manager.h> /* VFSManager */
#include <vfs/resolver.h> /* VResolver */
#include <vfs/path.h> /* VPath */

#include <kfg/config.h> /* KConfig */

#include <kproc/lock.h> /* KLock */

#include <klib/printf.h> /* string_printf */
#include <klib/refcount.h> /* KRefcount */
#include <klib/log.h> /* PLOGMSG */
#include <klib/out.h> /* KOutHandlerSetStdOut */
#include <klib/debug.h> /* DBGMSG */
#include <klib/status.h>
#include <klib/text.h> /* String */
#include <klib/rc.h> /* GetRCState */

#include <sysalloc.h>

#include <assert.h>
#include <stdlib.h> /* calloc */
#include <string.h> /* memset */
#include <time.h> /* time_t */

#include <stdio.h> /* fprintf */

typedef uint32_t VdbBlastStatus;

#if _DEBUGGING
#define S STSMSG(9,(""));
#else
#define S
#endif

#define RELEASE(type, obj) do { rc_t rc2 = type##Release(obj); \
    if (rc2 != 0 && rc == 0) { rc = rc2; } obj = NULL; } while (false)

/******************************************************************************/

static void *_NotImplementedP(const char *func) {
    PLOGERR(klogErr, (klogErr, -1,
        "$(func): is not implemented", "func=%s", func));
    return 0;
}

static size_t _NotImplemented(const char *func)
{   return (size_t)_NotImplementedP(func); }

/******************************************************************************/

static
void _Packed2naReadPrint(const Packed2naRead *self, const void *blob)
{
    static char *b = NULL;
    static size_t bsz = 0;
    int last = 0;
    unsigned i = 0;
    size_t size;
    assert(self);
    fflush(stderr);
    if (b == NULL) {
        bsz = self->length_in_bases + 64;
        b = malloc(bsz);
        if (b == NULL)
        {   return; }
    }
    else if (bsz < self->length_in_bases + 64) {
        char *tmp = NULL;
        bsz = self->length_in_bases + 64;
        tmp = realloc(b, bsz);
        if (tmp == NULL)
        {   return; }
        b = tmp;
    }
    sprintf(b, "%lu\t%p\t", self->read_id, blob);
    last = string_measure(b, &size);
    {
        unsigned ib = 0;
        uint32_t offset = self->offset_to_first_bit;
        for (ib = 0; ib < self->length_in_bases; ++ib) {
            int j = 0;
            uint8_t a[4];
            uint8_t u = 0;
            if (i >= self->length_in_bases)
            {   break; }
            u = ((uint8_t*)self->starting_byte)[ib];
            a[0] = (u >> 6) & 3;
            a[1] = (u >> 4) & 3;
            a[2] = (u >> 2) & 3;
            a[3] = u & 3;
            for (j = offset / 2; j < 4; ++j) {
                const char c[] = "ACGT";
                uint8_t h = a[j];
                if (i >= self->length_in_bases)
                {   break; }
                assert(h < 4);
                b[last++] = c[h];
                ++i;
            }
            offset = 0;
        }
        b[last++] = '\n';
        b[last++] = '\0';
        fprintf(stderr, "%s", b);
        fflush(stderr);
    }
}

/******************************************************************************/

static
rc_t _VTableLogRowData(const VTable *self,
    const char *column,
    void *buffer,
    uint32_t blen)
{
    rc_t rc = 0;

#if _DEBUGGING
    if (buffer && blen == 64) {
        uint64_t data = *((uint64_t*)buffer);
        const KTable *ktbl = NULL;
        rc_t rc = VTableOpenKTableRead(self, &ktbl);
        if (rc == 0) {
            const char *path = NULL;
            rc = KTableGetPath(ktbl, &path);
            if (rc == 0) {
                DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
                    ("%s: %s: %lu\n", path, column, data));
            }
        }

        KTableRelease(ktbl);
    }
#endif

    if (rc != 0)
    {   PLOGERR(klogInt, (klogInt, rc, "Error in $(f)", "f=%s", __func__)); }

    return rc;
}

static
rc_t _VTableMakeCursorImpl(const VTable *self,
    const VCursor **curs,
    uint32_t *col_idx,
    const char *col_name, bool canBeLost)
{
    rc_t rc = 0;

    assert(curs && col_name);

    if (rc == 0) {
        rc = VTableCreateCursorRead(self, curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VTableCreateCursorRead");
        }
    }

    if (rc == 0) {
        VCursorPermitPostOpenAdd(*curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VCursorPermitPostOpenAdd");
        }
    }

    if (rc == 0) {
        rc = VCursorOpen(*curs);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", col_name));
        }
    }

    if (rc == 0) {
        assert(*curs);
        rc = VCursorAddColumn(*curs, col_idx, col_name);
        if (rc != 0 && !canBeLost) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorAddColumn($(name))", "name=%s", col_name));
        }
    }

    STSMSG(2, ("Prepared a VCursor to read '%s'", col_name));

    return rc;
}

static rc_t _VTableMakeCursor(const VTable *self,
    const VCursor **curs, uint32_t *col_idx, const char *col_name)
{
    return _VTableMakeCursorImpl(self, curs, col_idx, col_name, false);
}

static bool _VTableCSra(const VTable *self) {
    bool cSra = false;

    KNamelist *names = NULL;

    uint32_t i = 0, count = 0;

    rc_t rc = VTableListPhysColumns(self, &names);

    if (rc == 0) {
        rc = KNamelistCount(names, &count);
    }

    for (i = 0 ; i < count && rc == 0; ++i) {
        const char *name = NULL;
        rc = KNamelistGet(names, i, &name);
        if (rc == 0) {
            const char b[] = "CMP_READ";
            if (string_cmp(name, string_measure(name, NULL),
                b, sizeof b - 1, sizeof b - 1) == 0)
            {
                cSra = true;
                break;
            }
        }
    }

    RELEASE(KNamelist, names);

    return cSra;
}

typedef enum {
    eColTypeError,
    eColTypeAbsent,
    eColTypeStatic,
    eColTypeNonStatic
} EColType;
static
uint32_t _VTableReadFirstRowImpl(const VTable *self,
    const char *name,
    void *buffer,
    uint32_t blen,
    EColType *is_static, bool canBeLost)
{
    uint32_t status = eVdbBlastNoErr;

    rc_t rc = 0;

    const VCursor *curs = NULL;
    uint32_t idx = 0;
    uint32_t row_len = 0;

    assert(self && name);

    blen *= 8;

    rc = _VTableMakeCursorImpl(self, &curs, &idx, name, canBeLost);
    if (rc != 0) {
        if (rc ==
            SILENT_RC(rcVDB, rcCursor, rcOpening, rcColumn, rcUndefined)
         || rc ==
            SILENT_RC(rcVDB, rcCursor, rcUpdating, rcColumn, rcNotFound))
        {
            if (!canBeLost) {
                PLOGMSG(klogInfo, (klogInfo, "$(f): Column '$(name)' not found",
                    "f=%s,name=%s", __func__, name));
            }
            if (is_static != NULL) {
                *is_static = eColTypeAbsent;
            }
            status = eVdbBlastTooExpensive;
        }
        else {
            status = eVdbBlastErr;
            if (is_static != NULL) {
                *is_static = eColTypeError;
            }
        }
    }

    if (status == eVdbBlastNoErr && rc == 0) {
        rc = VCursorOpenRow(curs);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpenRow($(name))", "name=%s", name));
        }
    }

    if (status == eVdbBlastNoErr && rc == 0 && buffer != NULL && blen > 0) {
        rc = VCursorRead(curs, idx, 8, buffer, blen, &row_len);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorRead($(name))", "name=%s", name));
        }
    }

/* TODO: needs to be verified what row_len is expected
    if (row_len != 1) return eVdbBlastErr; */

    STSMSG(2, ("Read the first row of '%s'", name));

    if (status == eVdbBlastNoErr && rc == 0) {
        if (blen == 64) {
            _VTableLogRowData(self, name, buffer, blen);
        }
        if (is_static != NULL) {
            bool isStatic = false;
            rc = VCursorIsStaticColumn(curs, idx, &isStatic);
            if (rc != 0) {
                PLOGERR(klogInt, (klogInt, rc,
                    "Error in VCursorIsStaticColumn($(name))",
                    "name=%s", name));
            }
            else {
                *is_static = isStatic ? eColTypeStatic : eColTypeNonStatic;
            }
        }
    }

    VCursorRelease(curs);

    if (status == eVdbBlastNoErr && rc != 0) {
        status = eVdbBlastErr;
    }
    return status;
}

static uint32_t _VTableReadFirstRow(const VTable *self,
    const char *name, void *buffer, uint32_t blen, EColType *is_static)
{
    return _VTableReadFirstRowImpl(self, name, buffer, blen, is_static, false);
}

static uint32_t _VTableGetNReads(const VTable *self, uint32_t *nreads) {
    rc_t rc = 0;
    uint32_t status = eVdbBlastNoErr;
    const VCursor *curs = NULL;
    uint32_t idx = 0;

    const char name[] = "READ_LEN";

    assert(nreads);

    rc = _VTableMakeCursor(self, &curs, &idx, name);
    if (rc != 0) {
        status = eVdbBlastErr;
        if (rc ==
            SILENT_RC(rcVDB, rcCursor, rcOpening, rcColumn, rcUndefined))
        {
            PLOGMSG(klogInfo, (klogInfo, "$(f): Column '$(name)' not found",
                "f=%s,name=%s", __func__, name));
        }
        else {
            PLOGMSG(klogInfo, (klogInfo, "$(f): Cannot open column '$(name)'",
                "f=%s,name=%s", __func__, name));
        }
    }

    if (status == eVdbBlastNoErr) {
        uint32_t elem_bits, elem_off, elem_cnt;
        const void *base = NULL;
        rc = VCursorCellDataDirect(curs, 1, idx,
                &elem_bits, &base, &elem_off, &elem_cnt);
        if (rc != 0) {
            status = eVdbBlastErr;
            PLOGMSG(klogInfo, (klogInfo,
                "$(f): Cannot '$(name)' CellDataDirect",
                "f=%s,name=%s", __func__, name));
        }
        else if (elem_off != 0 || elem_bits != 32) {
            status = eVdbBlastErr;
            PLOGERR(klogInt, (klogInt, rc,
                "Bad VCursorCellDataDirect(READ_LEN) result: "
                "boff=$(elem_off), elem_bits=$(elem_bits)",
                "elem_off=%u, elem_bits=%u", elem_off, elem_bits));
        }
        else {
            *nreads = elem_cnt;
        }
    }
    
    RELEASE(VCursor, curs);

    return status;
}


/******************************************************************************/

typedef struct ReaderCols {
    uint32_t col_PRIMARY_ALIGNMENT_ID;
    uint32_t col_READ_FILTER;
    uint32_t col_READ_LEN;
    uint32_t col_TRIM_LEN;
    uint32_t col_TRIM_START;

    int64_t *primary_alignment_id;
    uint8_t *read_filter;
    uint32_t *read_len;
    INSDC_coord_len TRIM_LEN;
    INSDC_coord_val TRIM_START;

    uint8_t nReadsAllocated;
} ReaderCols;
static void ReaderColsReset(ReaderCols *self) {
    assert(self);

    self   ->col_PRIMARY_ALIGNMENT_ID
     = self->col_READ_FILTER
     = self->col_READ_LEN
     = self->col_TRIM_LEN
     = self->col_TRIM_START
     = 0;
}

static
void ReaderColsFini(ReaderCols *self)
{
    assert(self);

    free(self->primary_alignment_id);
    free(self->read_filter);
    free(self->read_len);

    memset(self, 0, sizeof *self);
}

/* readId is an index in primary_alignment_id : 0-based */
static bool ReaderColsIsReadCompressed(const ReaderCols *self, uint32_t readId)
{
    assert(self);

    if (self->col_PRIMARY_ALIGNMENT_ID == 0) {
        return false; /* not cSRA */
    }

    assert(self->primary_alignment_id);
    if (self->primary_alignment_id[readId] != 0) {
        return true; /* has alignment == compressed */
    }
    else {
        /* does not have alignment == not compressed == is stored in CMP_READ */
        return false;
    }
}

/******************************************************************************/

static
uint32_t _VCursorReadArray(const VCursor *self,
    int64_t row_id,
    uint32_t col,
    void **buffer,
    uint8_t elem_size,
    uint8_t nReads,
    const char *name)
{
    rc_t rc = 0;
    uint32_t row_len = 0;

    assert(buffer && elem_size && nReads && name);

    if (*buffer == NULL) {
        *buffer = calloc(nReads, elem_size);
        if (*buffer == NULL)
        {   return eVdbBlastMemErr; }
    }

    rc = VCursorReadDirect(self,
        row_id, col, 8, *buffer, nReads * elem_size * 8, &row_len);
    if (rc != 0) {
        PLOGERR(klogInt, (klogInt, rc,
            "Error in VCursorReadDirect($(name))", "name=%s", name));
    }

/* TODO: needs to be verified what row_len is expected
    if (row_len != 1) return eVdbBlastErr; */

    S

    return rc == 0 ? eVdbBlastNoErr : eVdbBlastErr;
}

static
uint32_t _VCursorAddCols(const VCursor *self,
    ReaderCols *cols, bool cSra)
{
    rc_t rc = 0;

    assert(self && cols);

    if (rc == 0) {
        const char name[] = "READ_FILTER";
        rc = VCursorAddColumn(self, &cols->col_READ_FILTER, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", name));
        }
        else {
            assert(cols->col_READ_FILTER);
        }
    }

    if (rc == 0) {
        const char name[] = "READ_LEN";
        rc = VCursorAddColumn(self, &cols->col_READ_LEN, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", name));
        }
        else {
            assert(cols->col_READ_LEN);
        }
    }

    if (rc == 0) {
        const char name[] = "TRIM_LEN";
        rc = VCursorAddColumn(self, &cols->col_TRIM_LEN, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", name));
        }
        else {
            assert(cols->col_TRIM_LEN);
        }
    }

    if (rc == 0) {
        const char name[] = "TRIM_START";
        rc = VCursorAddColumn(self, &cols->col_TRIM_START, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", name));
        }
        else {
            assert(cols->col_TRIM_START);
        }
    }

    if (rc == 0 && cSra) {
        const char name[] = "PRIMARY_ALIGNMENT_ID";
        rc = VCursorAddColumn(self, &cols->col_PRIMARY_ALIGNMENT_ID, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VCursorOpen($(name))", "name=%s", name));
        }
        else {
            assert(cols->col_PRIMARY_ALIGNMENT_ID);
        }
    }

    return rc ? eVdbBlastErr : eVdbBlastNoErr;
}

static
uint32_t _VCursorReadCols(const VCursor *self,
    int64_t row_id,
    ReaderCols *cols,
    uint8_t nReads)
{
    uint32_t status = eVdbBlastNoErr;
    rc_t rc = 0;
    uint32_t row_len = ~0;

    assert(cols);

    if (cols->nReadsAllocated != 0 && cols->nReadsAllocated < nReads) {
        /* LOG */

        /* TODO: find a better way/place to realloc cols data buffers */
        free(cols->primary_alignment_id);
        cols->primary_alignment_id = NULL;
        free(cols->read_filter);
        cols->read_filter = NULL;
        free(cols->read_len);
        cols->read_len = NULL;
    }

    status = _VCursorReadArray(self, row_id, cols->col_READ_LEN,
        (void **)&cols->read_len, sizeof *cols->read_len, nReads,
        "READ_LEN");
    if (status != eVdbBlastNoErr)
    {   return status; }

    status = _VCursorReadArray(self, row_id, cols->col_READ_FILTER,
        (void **)&cols->read_filter, sizeof *cols->read_filter, nReads,
        "READ_FILTER");
    if (status != eVdbBlastNoErr)
    {   return status; }

    if (cols->col_PRIMARY_ALIGNMENT_ID != 0) {
        status = _VCursorReadArray(self, row_id, cols->col_PRIMARY_ALIGNMENT_ID,
        (void **)&cols->primary_alignment_id, sizeof *cols->primary_alignment_id,
        nReads, "PRIMARY_ALIGNMENT_ID");
        if (status != eVdbBlastNoErr) {
            return status;
        }
    }

    cols->nReadsAllocated = nReads;

    rc = VCursorReadDirect(self, row_id, cols->col_TRIM_LEN,
        8 * sizeof cols->TRIM_LEN, &cols->TRIM_LEN, sizeof cols->TRIM_LEN,
        &row_len);
    if (rc != 0) {
        PLOGERR(klogInt, (klogInt, rc, "Error in VCursorReadDirect"
            " TRIM_LEN, spot=$(spot))", "spot=%ld", row_id));
        return eVdbBlastErr;
    }
    else if (row_len != 1) {
        STSMSG(1, ("Error: VCursorReadDirect(TRIM_LEN, spot=%lu) "
            "returned row_len=%u", row_id, row_len));
/* TODO */ return eVdbBlastErr;
    }

    rc = VCursorReadDirect(self, row_id, cols->col_TRIM_START,
        8 * sizeof cols->TRIM_LEN, &cols->TRIM_START, sizeof cols->TRIM_START,
        &row_len);
    if (rc != 0) {
        PLOGERR(klogInt, (klogInt, rc, "Error in VCursorReadDirect"
            " TRIM_START, spot=$(spot))", "spot=%ld", row_id));
        return eVdbBlastErr;
    }
    else if (row_len != 1) {
        STSMSG(1, ("Error: VCursorReadDirect(TRIM_START, spot=%lu) "
            "returned row_len=%u", row_id, row_len));
/* TODO */ return eVdbBlastErr;
    }

    return status;
}

/******************************************************************************/
typedef uint32_t BTableType;
enum {
      btpUndefined
    , btpSRA
    , btpWGS
    , btpREFSEQ
};
/******************************************************************************/
static const char VDB_BLAST_MGR[] = "VdbBlastMgr";

struct VdbBlastMgr {
    KRefcount refcount;
#if NO_NO_NO_NO_NO
    SRAPath *path;
#endif
    VSchema *schema;
    const VDBManager *mgr;
    const KDBManager *kmgr;
    VFSManager * vfs;
    KDirectory *dir;
    VResolver *resolver;
};

static
void _VdbBlastMgrWhack(VdbBlastMgr *self)
{
    assert(self);

    VSchemaRelease(self->schema);
    VDBManagerRelease(self->mgr);
    KDBManagerRelease(self->kmgr);
    VFSManagerRelease ( self -> vfs );
    KDirectoryRelease(self->dir);
    VResolverRelease(self->resolver);

    memset(self, 0, sizeof *self);

    free(self);

    STSMSG(1, ("Deleted VdbBlastMgr"));
}

LIB_EXPORT
VdbBlastMgr* CC VdbBlastInit(uint32_t *status)
{
    VdbBlastMgr *obj = NULL;
    rc_t rc = 0;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    *status = eVdbBlastErr;

    obj = calloc(1, sizeof *obj);
    if (obj == NULL) {
        *status = eVdbBlastMemErr;
        return obj;
    }

    if ( rc == 0 )
    {
        rc = VFSManagerMake ( & obj -> vfs );
        if ( rc != 0 )
            LOGERR ( klogInt, rc, "Error in VFSManagerMake" );
    }

    if (rc == 0) {
        rc = VFSManagerGetCWD( obj -> vfs, &obj->dir);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in VFSManagerGetCWD"); }
    }

#if 0
    if (rc == 0) {
        rc = SRAPathMake(&obj->path, NULL);
        if (rc != 0) {
            LOGERR(klogInt, rc,
                "Error in SRAPathMake: is libsra-path.so in LD_LIBRARY_PATH?");
        }
    }
#endif

    if (rc == 0) {
        rc = VDBManagerMakeRead(&obj->mgr, NULL);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in VDBManagerMakeRead"); }
    }

    if (rc == 0) {
        rc = VDBManagerOpenKDBManagerRead(obj->mgr, &obj->kmgr);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in VDBManagerOpenKDBManagerRead"); }
    }

    if (rc == 0) {
        rc = VDBManagerMakeSRASchema(obj->mgr, &obj->schema);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in VDBManagerMakeSRASchema"); }
    }

    if (rc == 0) {
        VFSManager* mgr = NULL;
        KConfig* cfg = NULL;
        if (rc == 0) {
            rc = VFSManagerMake(&mgr);
        }
        if (rc == 0) {
            rc = KConfigMake(&cfg, NULL);
        }
        if (rc == 0) {
            rc = VFSManagerMakeResolver(mgr, &obj->resolver, cfg);
        }
        RELEASE(KConfig, cfg);
        RELEASE(VFSManager, mgr);
    }

    if (rc != 0) {
        _VdbBlastMgrWhack(obj);
        obj = NULL;
        STSMSG(1, ("Error: failed to create VdbBlastMgr"));
    }
    else {
        KRefcountInit(&obj->refcount, 1, VDB_BLAST_MGR, __func__, "mgr");
        *status = eVdbBlastNoErr;
        STSMSG(1, ("Created VdbBlastMgr"));
    }

    return obj;
}

LIB_EXPORT
VdbBlastMgr* CC VdbBlastMgrAddRef(VdbBlastMgr *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlastMgrAddRef(NULL)"));
        return self;
    }

    if (KRefcountAdd(&self->refcount, VDB_BLAST_MGR) == krefOkay) {
        STSMSG(1, ("VdbBlastMgrAddRef"));
        return self;
    }

    STSMSG(1, ("Error: failed to VdbBlastMgrAddRef"));
    return NULL;
}

LIB_EXPORT
void CC VdbBlastMgrRelease(VdbBlastMgr *self)
{
    if (self == NULL)
    {   return; }

    STSMSG(1, ("VdbBlastMgrRelease"));
    if (KRefcountDrop(&self->refcount, VDB_BLAST_MGR) != krefWhack)
    {   return; }

    _VdbBlastMgrWhack(self);
}

/* schema@name in metadata for WGS runs starts with "NCBI:WGS:db:contig" */
static
bool _VdbBlastMgrIsRunWgs(VdbBlastMgr *self, const char *rundesc)
{
    bool wgs = false;
    rc_t rc = 0;
    const VDatabase *db = NULL;
    const KMetadata *meta = NULL;
    const KMDataNode *node = NULL;
    char buffer[512] = "";
    size_t size = 0;
    assert(self);
    rc = VDBManagerOpenDBRead(self->mgr, &db, NULL, rundesc);
    if (rc == 0) {
        rc = VDatabaseOpenMetadataRead(db, &meta);
    }
    if (rc == 0) {
        rc = KMetadataOpenNodeRead(meta, &node, "schema");
    }
    if (rc == 0) {
        rc = KMDataNodeReadAttr(node, "name", buffer, sizeof buffer, &size);
    }
    if (rc == 0) {
        const char database[] = "NCBI:WGS:db:contig";
        uint32_t max_chars = sizeof database - 1;
        STSMSG(2,("%s.schema@name='%.*s'", rundesc, (uint32_t)size, buffer));
        if (size >= sizeof database && 
            string_cmp(buffer, max_chars, database, max_chars, max_chars) == 0)
        {
            STSMSG(1,("%s is wgs", rundesc));
            wgs = true;
        }
    }
    RELEASE(KMDataNode, node);
    RELEASE(KMetadata, meta);
    RELEASE(VDatabase, db);
    return wgs;
}

/* TODO: should be replaced by a function to get the type from run [meta] */
static
BTableType _VdbBlastMgrBTableTypeFromPath(VdbBlastMgr *self,
    const char *rundesc)
{
    rc_t rc = 0;
    VPath *acc = NULL;
    const VPath *local = NULL;
    const String *str = NULL;
    char *s = NULL;
    BTableType t = btpSRA;
    const char *log = "UNEXPECTED";

    assert(self && rundesc);

    rc = VFSManagerMakePath ( self -> vfs, &acc, rundesc);
    if (rc == 0) {
        rc = VResolverQuery(self->resolver,
            eProtocolHttp, acc, &local, NULL, NULL);
    }
    if (rc == 0) {
        rc = VPathMakeString(local, &str);
    }

    if (rc == 0) {
        s = strstr(str->addr, "/WGS/");
        if (s != NULL) {
            t = btpWGS;
        }
        else {
            s = strstr(str->addr, "/refseq/");
            if (s != NULL) {
                t = btpREFSEQ;
            }
            else {
                if (_VdbBlastMgrIsRunWgs(self, rundesc)) {
                    t = btpWGS;
                }
            }
        }
    }

    switch (t) {
        case btpSRA:
            log = "SRA";
            break;
        case btpWGS:
            log = "WGS";
            break;
        case btpREFSEQ:
            log = "REFSEQ";
            break;
        default:
            log = "UNEXPECTED";
            break;
    }

    STSMSG(1, ("Type of %s is %s", rundesc, log));

    RELEASE(VPath, local);
    RELEASE(VPath, acc);
    free((void*)str);

    return t;
}

static
uint32_t _VdbBlastMgrOpenTable(const VdbBlastMgr *self,
    const char *path,
    const VTable **tbl)
{
    KPathType type = kptNotFound;
    VSchema *schema = NULL;

    assert(self && tbl);
    *tbl = NULL;

    /* Always use VDBManagerMakeSRASchema to VDBManagerOpenTableRead
       Otherwise CMP_BASE_COUNT column sometimes cannot be found */
    schema = self->schema;

    type = KDBManagerPathType(self->kmgr, path);
    if (type == kptNotFound) {
        STSMSG(1, ("Error: cannot find '%s'", path));
        return eVdbBlastErr;
    }

    if ((type & ~kptAlias) == kptDatabase) {
        const char *table = "SEQUENCE";
        const VDatabase *db = NULL;
        rc_t rc = VDBManagerOpenDBRead(self->mgr, &db, NULL, path);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VDBManagerOpenDBRead($(name))", "name=%s", path));
            STSMSG(1, ("Error: failed to open DB '%s'", path));
            return eVdbBlastErr;
        }
        rc = VDatabaseOpenTableRead(db, tbl, table);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VDatabaseOpenTableRead($(name), $(tbl))",
                "name=%s,tbl=%s", path, table));
            STSMSG(1, ("Error: failed to open DB table '%s/%s'", path, table));
        }
        else {
            STSMSG(1, ("Opened DB table '%s/%s'", path, table));
        }
        VDatabaseRelease(db);

        return rc != 0 ? eVdbBlastErr : eVdbBlastNoErr;
    }

    while (true) {
        rc_t rc = VDBManagerOpenTableRead(self->mgr, tbl, schema, path);
        if (rc == 0) {
            if (schema == NULL) {
                STSMSG(1, ("Opened table '%s'(schema=NULL)", path));
            }
            else {
                STSMSG(1, ("Opened table '%s'(SRASchema)", path));
            }
            return eVdbBlastNoErr;
        }

        assert(self->schema);

        if (schema == NULL)
        {   schema = self->schema; }
        else {
            PLOGERR(klogInt, (klogInt, rc,
                "Error in VDBManagerOpenTableRead($(name))", "name=%s", path));
            STSMSG(1, ("Error: failed to open table '%s'", path));
            return eVdbBlastRunErr;
        }
    }
}

static
uint32_t _VdbBlastMgrFindNOpenTable(VdbBlastMgr *self,
    const char *rundesc,
    const VTable **tbl,
    BTableType *type,
    char **fullpath)
{
    uint32_t status = eVdbBlastNoErr;
    char *path = (char*) rundesc;

    assert(self && type);

#if 0
    if (KDirectoryPathType(self->dir, path) == kptNotFound) {
        if (!SRAPathTest(self->path, rundesc)) {
            size_t bufsize = 4096;
            while (true) {
                rc_t rc = 0;
                path = malloc(bufsize);
                if (path == NULL)
                {   return eVdbBlastMemErr; }

                rc = SRAPathFind(self->path, rundesc, path, bufsize);
                if (rc == 0) {
                    STSMSG(2, ("%s -> %s", rundesc, path));
                    if (fullpath != NULL)
                    {   *fullpath = string_dup(path, bufsize); }
                    break;
                }

                free(path);
                path = NULL;
                if (GetRCState(rc) == rcNotFound) {
                    PLOGERR(klogInt, (klogInt, rc,
                        "Not found: '$(path)'", "path=%s", rundesc));
                    STSMSG(1, ("Error: not found '%s'", rundesc));
                    return eVdbBlastRunErr;
                }
                else if (GetRCState(rc) == rcInsufficient) {
                    bufsize *= 2;
                    continue;
                }
                else {
                    PLOGERR(klogInt, (klogInt, rc,
                        "Error while searching for '$(path)'",
                        "path=%s", rundesc));
                    STSMSG(1, ("Error: while searching for '%s'", rundesc));
                    return eVdbBlastErr;
                }
            }
        }
    }
#endif

    status = _VdbBlastMgrOpenTable(self, path, tbl);
    if (status == eVdbBlastNoErr) {
        STSMSG(1, ("Added run %s(%s)", rundesc, path));
    }
    else {
        STSMSG(1, ("Error: failed to add run %s(%s)", rundesc, path));
    }

    *type = _VdbBlastMgrBTableTypeFromPath(self, path);

    if (path != rundesc) {
        free(path);
        path = NULL;
    }

    return status;
}

/******************************************************************************/

LIB_EXPORT uint32_t CC VdbBlastMgrKLogLevelSet(const VdbBlastMgr *self,
    KLogLevel lvl)
{
    rc_t rc = KLogLevelSet(lvl);

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT uint32_t CC VdbBlastMgrKLogLevelSetInfo(
    const VdbBlastMgr *self)
{   return VdbBlastMgrKLogLevelSet(self, klogInfo); }

LIB_EXPORT uint32_t CC VdbBlastMgrKLogLevelSetWarn(
    const VdbBlastMgr *self)
{   return VdbBlastMgrKLogLevelSet(self, klogWarn); }

LIB_EXPORT uint32_t CC VdbBlastMgrKLogHandlerSetStdOut(
    const VdbBlastMgr *self)
{
    rc_t rc = KLogHandlerSetStdOut();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT
uint32_t CC VdbBlastMgrKLogLibHandlerSetStdOut(const VdbBlastMgr *self)
{
    rc_t rc = KLogLibHandlerSetStdOut();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT uint32_t CC VdbBlastMgrKLogHandlerSetStdErr(
    const VdbBlastMgr *self)
{
    rc_t rc = KLogHandlerSetStdErr();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT
uint32_t CC VdbBlastMgrKLogLibHandlerSetStdErr(const VdbBlastMgr *self)
{
    rc_t rc = KLogLibHandlerSetStdErr();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT uint32_t CC VdbBlastMgrKLogHandlerSet(const VdbBlastMgr *self,
    KWrtWriter writer, void *data)
{
    rc_t rc = KLogHandlerSet(writer, data);

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT uint32_t CC VdbBlastMgrKLogLibHandlerSet(const VdbBlastMgr *self,
    KWrtWriter writer, void *data)
{
    rc_t rc = KLogLibHandlerSet(writer, data);

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

/******************************************************************************/

LIB_EXPORT void CC VdbBlastMgrKStsLevelSet(const VdbBlastMgr *self,
    uint32_t level)
{   KStsLevelSet(level); }

LIB_EXPORT
uint32_t CC VdbBlastMgrKStsHandlerSetStdOut(const VdbBlastMgr *self)
{
    rc_t rc = KStsHandlerSetStdOut();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT
uint32_t CC VdbBlastMgrKStsHandlerSetStdErr(const VdbBlastMgr *self)
{
    rc_t rc = KStsHandlerSetStdErr();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

/******************************************************************************/
/* KOutHandlerSet
 * set output handler for different channels
 *
 * returns status code
 */
LIB_EXPORT
uint32_t CC VdbBlastMgrKOutHandlerSetStdOut(const VdbBlastMgr *self)
{
    rc_t rc = KOutHandlerSetStdOut();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT
uint32_t CC VdbBlastMgrKOutHandlerSetStdErr(const VdbBlastMgr *self)
{
    rc_t rc = KOutHandlerSetStdErr();

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

LIB_EXPORT uint32_t CC VdbBlastMgrKOutHandlerSet(const VdbBlastMgr *self,
    VdbBlastKWrtWriter writer, void *data)
{
    rc_t rc = KOutHandlerSet(writer, data);

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

/******************************************************************************/

LIB_EXPORT uint32_t CC VdbBlastMgrKDbgSetString(const VdbBlastMgr *self,
    const char *string)
{
    rc_t rc = KDbgSetString(string);

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

/******************************************************************************/

/* KConfigPrint
 * print current configuration to the output handler
 */
LIB_EXPORT uint32_t VdbBlastMgrKConfigPrint ( const VdbBlastMgr *self ) {
    KConfig *kfg = NULL;

    rc_t rc = KConfigMake(&kfg, NULL);

    if (rc == 0) {
        rc = KConfigPrint(kfg, 0);
    }

    {
        rc_t rc2 = KConfigRelease(kfg);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    return eVdbBlastNoErr;
}

/******************************************************************************/
typedef struct {
    uint64_t spotCount;
    uint8_t nReads;
    uint8_t nBioReads; /* knowing filtering (if static) and min_read_len info */
    uint64_t bioLen; /* per read. is assigned just when allStatic */
    INSDC_SRA_platform_id platform;

    uint64_t bioBaseCount; /* BIO_BASE_COUNT, ~0 if not found */
    uint64_t cmpBaseCount; /* CMP_BASE_COUNT, ~0 if not found */

    INSDC_read_type *readType;
    EColType readTypeStatic;

    uint32_t *readLen;
    EColType readLenStatic;

    uint8_t *rdFilter;
    EColType rdFilterStatic;

    bool varReadLen;
} RunDesc;
static
void _RunDescFini(RunDesc *self)
{
    assert(self);
    free(self->readLen);
    free(self->readType);
    free(self->rdFilter);
    memset(self, 0, sizeof *self);
}

static char* _CanonocalName(const char *name) {
    size_t noExtSize = 0;
    size_t namelen = 0;
    const char ext[] = ".sra";

    if (name == NULL) {
        return NULL;
    }

    noExtSize = namelen = string_size(name);

    if (namelen >= sizeof(ext)) {
        const char *tail = NULL;
        noExtSize -= sizeof ext - 1;
        tail = name + noExtSize;
        if (string_cmp(ext, sizeof ext - 1, tail, sizeof ext - 1, 99) != 0) {
            noExtSize = namelen;
        }
    }

    return string_dup(name, noExtSize);
}

/******************************************************************************/
typedef struct VdbBlastRun {
    /* rundesc; */
    char *acc;
    char *path;

    const VTable *tbl;
    BTableType type;
    bool cSra;

    uint64_t bioReads;       /* numSequences; */
    bool bioReadsTooExpensive; /* numSequences is TooExpensive */
    uint64_t bioReadsApprox; /* numSequencesApprox; */

    uint64_t bioBases;       /* length; */
    bool bioBasesTooExpensive; /* totalLength is TooExpensive */
    uint64_t bioBasesApprox; /* lengthApprox; */

    RunDesc rd;

    uint32_t min_read_length;

    /* WGS */
    const VCursor *cursACCESSION;
    uint32_t col_ACCESSION;
} VdbBlastRun;
static
uint32_t _VdbBlastRunFillRunDesc(VdbBlastRun *self)
{
    uint32_t status = eVdbBlastNoErr;
    RunDesc *rd = NULL;

    int i = 0;
    const char *col = NULL;

    assert(self);

    rd = &self->rd;

    if (rd->spotCount || rd->readType || rd->nReads || rd->nBioReads) {
        if (self->cSra && rd->cmpBaseCount == ~0) {
            rc_t rc = RC(rcSRA, rcTable, rcReading, rcColumn, rcNotFound);
            PLOGERR(klogInt, (klogInt, rc,
                "Cannot read CMP_BASE_COUNT column for $(p)",
                "p=%s", self->path));
            STSMSG(1, ("Error: failed to read %s/%s",
                self->path, "CMP_BASE_COUNT"));
            return eVdbBlastErr;
        }
        else {
            S
            return eVdbBlastNoErr;
        }
    }

    assert(self->path);

    col = "SPOT_COUNT";
    status = _VTableReadFirstRow(self->tbl,
        col, &rd->spotCount, sizeof rd->spotCount, NULL);
    if (status != eVdbBlastNoErr) {
        STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
        return status;
    }

    if (self->type == btpWGS) {
        S
        status = eVdbBlastNoErr;
        rd->nReads = rd->spotCount > 0 ? 1 : 0;
    }
    else if (self->type == btpREFSEQ) {
        S
        status = eVdbBlastNoErr;
        rd->nReads = 1;
    }
    else {
        uint32_t nreads = 0;
        status = _VTableGetNReads(self->tbl, &nreads);
        if (status == eVdbBlastNoErr) {
            rd->nReads = nreads;
        }
    }

    switch (self->type) {
        case btpSRA:
            col = "PLATFORM";
            status = _VTableReadFirstRow(self->tbl,
                col, &rd->platform, sizeof rd->platform, NULL);
            if (status != eVdbBlastNoErr) {
                STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
                return status;
            }
            switch (rd->platform) { /* TODO */
                case SRA_PLATFORM_ILLUMINA:
                case SRA_PLATFORM_ABSOLID:
                case SRA_PLATFORM_COMPLETE_GENOMICS:
                    rd->varReadLen = false;
                    break;
                case SRA_PLATFORM_UNDEFINED:
                case SRA_PLATFORM_454:
                case SRA_PLATFORM_HELICOS:
                case SRA_PLATFORM_PACBIO_SMRT:
                case SRA_PLATFORM_ION_TORRENT:
                default:
                    rd->varReadLen = true;
                    break;
            }
            break;
        case btpWGS:
            rd->varReadLen = true;
            break;
        case btpREFSEQ:
            break;
        default:
            assert(0);
            break;
    }

    col = "READ_TYPE";
    if (rd->readType == NULL) {
        rd->readType = calloc(rd->nReads, sizeof *rd->readType);
        if (rd->readType == NULL)
        {   return eVdbBlastMemErr; }
    }
    status = _VTableReadFirstRow(self->tbl, col,
        rd->readType, rd->nReads * sizeof *rd->readType, &rd->readTypeStatic);
    /* TODO: check case when ($#READ_TYPE == 0 && nreads > 0) */
    if (status != eVdbBlastNoErr) {
        if (status == eVdbBlastTooExpensive) {
            status = eVdbBlastNoErr;
        }
        else {
            STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
            return status;
        }
    }

    col = "READ_LEN";
    if (rd->readLen == NULL) {
        rd->readLen = calloc(rd->nReads, sizeof *rd->readLen);
        if (rd->readLen == NULL)
        {   return eVdbBlastMemErr; }
    }
    status = _VTableReadFirstRow(self->tbl, col,
        rd->readLen, rd->nReads * sizeof *rd->readLen, &rd->readLenStatic);
    /* TODO: check case when ($#READ_TYPE == 0 && nreads > 0) */
    if (status != eVdbBlastNoErr) {
        STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
        return status;
    }

    col = "READ_FILTER"; /* col = "RD_FILTER"; */
    if (rd->rdFilter == NULL) {
        rd->rdFilter = calloc(rd->nReads, sizeof *rd->rdFilter);
        if (rd->rdFilter == NULL)
        {   return eVdbBlastMemErr; }
    }
    status = _VTableReadFirstRow(self->tbl, col,
        rd->rdFilter, rd->nReads * sizeof *rd->rdFilter, &rd->rdFilterStatic);
    if (status != eVdbBlastNoErr) {
        if (status == eVdbBlastTooExpensive) {
            status = eVdbBlastNoErr;
        }
        else {
            STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
            return status;
        }
    }

    col = "BIO_BASE_COUNT";
    status = _VTableReadFirstRowImpl(self->tbl, col,
        &rd->bioBaseCount, sizeof rd->bioBaseCount, NULL,
        true);/*Do not generate error message when BIO_BASE_COUNT is not found*/

    if (status != eVdbBlastNoErr) {
        if (status == eVdbBlastTooExpensive) {
            status = eVdbBlastNoErr;
            rd->bioBaseCount = ~0;
        }
        else {
            STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
            return status;
        }
    }

    col = "CMP_BASE_COUNT";
    status = _VTableReadFirstRow(self->tbl, col,
        &rd->cmpBaseCount, sizeof rd->cmpBaseCount, NULL);
    if (status != eVdbBlastNoErr) {
        if (status == eVdbBlastTooExpensive) {
            /* CMP_BASE_COUNT should be always found */
            rc_t rc = RC(rcSRA, rcTable, rcReading, rcColumn, rcNotFound);
            PLOGERR(klogInt, (klogInt, rc,
                "Cannot read CMP_BASE_COUNT column for $(p)",
                "p=%s", self->path));
            STSMSG(1, ("Error: failed to read %s/%s",
                self->path, "CMP_BASE_COUNT"));
            rd->cmpBaseCount = ~0;
            return eVdbBlastErr;
        }
        else {
            STSMSG(1, ("Error: failed to read %s/%s", self->path, col));
            return status;
        }
    }

    for (rd->nBioReads = 0, i = 0; i < rd->nReads; ++i) {
        S
        if (rd->readType[i] & SRA_READ_TYPE_BIOLOGICAL) {
            if ((rd->rdFilterStatic == eColTypeStatic &&
                 rd->rdFilter[i] == READ_FILTER_PASS) 
                || (rd->rdFilterStatic == eColTypeAbsent))
            {
                ++rd->nBioReads;
                rd->bioLen += rd->readLen[i];
            }
            else {
                ++rd->nBioReads;
            }
        }
    }
    S /* LOG nBioReads */

    return status;
}

static VdbBlastStatus _VdbBlastRunInit(VdbBlastRun *self,
    const VTable *tbl, const char *rundesc, BTableType type,
    const KDirectory *dir, char *fullpath, uint32_t min_read_length)
{
    rc_t rc = 0;
    const char *acc = rundesc;
    char rbuff[4096] = "";
    size_t size = 0;

#if WINDOWS
    char slash = '\\';
#else
    char slash = '/';
#endif

    assert(self && tbl && type != btpUndefined && rundesc);

/* TODO This is obsolete and incorrect */
    rc = KDirectoryResolvePath(dir, true, rbuff, sizeof rbuff, rundesc);
    if (rc != 0) {
        S
        return eVdbBlastErr;
    }

    memset(self, 0, sizeof *self);

    self->tbl = tbl;
    self->type = type;

    self->bioReads = self->bioReadsApprox
        = self->bioBases = self->bioBasesApprox = ~0;

    acc = strrchr(rbuff, slash);
    if (acc == NULL) {
        acc = rbuff;
    }
    else if (string_measure(acc, &size) > 1) {
        ++acc;
    }
    else {
        acc = rbuff;
    }

    if (fullpath == NULL) {
        self->path = string_dup(rbuff, sizeof(rbuff));
        if (self->path == NULL) {
            return eVdbBlastMemErr;
        }
    }
    else {
        self->path = fullpath;
    }
    self->acc = _CanonocalName(acc);
    if (self->acc == NULL) {
        return eVdbBlastMemErr;
    }

    self->min_read_length = min_read_length;

    self->cSra = _VTableCSra(self->tbl);

    return eVdbBlastNoErr;
}

static
void _VdbBlastRunFini(VdbBlastRun *self)
{
    if (self == NULL)
    {   return; }

    VCursorRelease(self->cursACCESSION);
    VTableRelease(self->tbl);

    free(self->acc);
    free(self->path);

    _RunDescFini(&self->rd);

    memset(self, 0, sizeof *self);
}

/* _VdbBlastRunGetNumSequences
    returns (number of spots) * (number of biological reads in spot).
    If read_filter is not static: some reads could be filtered,
    so status is set to eVdbBlastTooExpensive */
static
uint64_t _VdbBlastRunGetNumSequences(VdbBlastRun *self,
    uint32_t *status)
{
    assert(self && status);

    *status = eVdbBlastNoErr;

    if (self->bioReads == ~0) {
        RunDesc *rd = NULL;

        if (self->type == btpREFSEQ) {
            S
            self->bioReads = 1;
        }
        else {
            *status = _VdbBlastRunFillRunDesc(self);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }

            rd = &self->rd;

            if (rd->rdFilterStatic != eColTypeStatic) {
                self->bioReadsTooExpensive = true;
            }

            if (self->cSra) {
                self->bioReadsTooExpensive = true;
            }

            self->bioReads = rd->spotCount * rd->nBioReads;
            S
        }
    }
    else {
        S
    }

    if (*status == eVdbBlastNoErr && self->bioReadsTooExpensive) {
        *status = eVdbBlastTooExpensive;
    }
    return self->bioReads;
}

static
uint64_t _VdbBlastRunGetLength(VdbBlastRun *self,
    uint32_t *status)
{
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    *status = eVdbBlastNoErr;

    if (self->bioBasesTooExpensive) {
        *status = eVdbBlastTooExpensive;
        return 0;
    }
    else if (self->bioBases == ~0) {
        if (self->cSra) {
            *status = _VdbBlastRunFillRunDesc(self);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }
            self->bioBases = self->rd.cmpBaseCount;
        }
        else {
//    if BIO_BASE_COUNT is not found then status is set to eVdbBlastTooExpensive
            *status = _VTableReadFirstRowImpl(self->tbl, "BIO_BASE_COUNT",
                &self->bioBases, sizeof self->bioBases, NULL, true);
            if (*status == eVdbBlastTooExpensive) {
                self->bioBasesTooExpensive = true;
            }
        }
    }

    if (*status == eVdbBlastNoErr) {
        S
        return self->bioBases;
    }
    else {
        S
        return 0;
    }
}
static
rc_t _VCursorCellDataDirect(const VCursor *self,
    int64_t row_id,
    uint32_t col_idx,
    uint32_t elemBits,
    const void **base,
    uint32_t nreads,
    const char *name)
{
    uint32_t elem_bits = 0;
    uint32_t row_len = 0;
    uint32_t boff = 0;

    rc_t rc = VCursorCellDataDirect(self, row_id, col_idx,
        &elem_bits, base, &boff, &row_len);

    if (rc != 0) {
        PLOGERR(klogInt, (klogInt, rc,
            "Error during VCursorCellDataDirect($(name), $(spot))",
            "name=%s,spot=%lu", name, col_idx));
    }
    else if (boff != 0 || elem_bits != elemBits) {
        rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
        PLOGERR(klogInt, (klogInt, rc,
            "Bad VCursorCellDataDirect($(name), $(spot)) result: "
            "boff=$(boff), elem_bits=$(elem_bits)",
            "name=%s,spot=%lu,boff=%u,elem_bits=%u",
            name, col_idx, boff, elem_bits));
    }
    else if (row_len != nreads) {
        rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
        PLOGERR(klogInt, (klogInt, rc,
            "Bad VCursorCellDataDirect($(name), $(spot)) result: "
            "row_len=$(row_len)",
            "name=%s,spot=%lu,row_len=%u", name, col_idx, row_len));
    }

    return rc;
}

typedef struct {
    const VCursor *curs;
    uint32_t colREAD_LEN;
    uint32_t colREAD_TYPE;
    uint64_t techBasesPerSpot;
    bool techBasesPerSpotEquals;
    uint64_t bioBasesCnt;
} ApprCnt;
static rc_t _ApprCntChunk(ApprCnt *self,
    uint64_t chunk, uint64_t l, uint64_t nspots, uint32_t nreads)
{
    rc_t rc = 0;
    uint64_t start = nspots / 10 * chunk + 1;
    uint64_t spot = 0;
    uint64_t end = start + l;
    assert(self);
    if (end - 1 > nspots) {
        end = nspots + 1;
    }
    for (spot = start; spot < end; ++spot) {
        uint64_t techBases = 0;
        uint64_t bioBases = 0;
        uint32_t read = 0;
        const uint32_t *readLen = NULL;
        const INSDC_read_type *readType = NULL;
        const void *base = NULL;
        rc = _VCursorCellDataDirect(self->curs, spot, self->colREAD_LEN,
            32, &base, nreads, "READ_LEN");
        if (rc != 0) {
            return rc;
        }
        readLen = base;
        rc = _VCursorCellDataDirect(self->curs, spot, self->colREAD_TYPE,
            8, &base, nreads, "READ_TYPE");
        if (rc != 0) {
            return rc;
        }
        readType = base;
        for (read = 0; read < nreads; ++read) {
            INSDC_read_type type = readType[read] & 1;
            if (type == READ_TYPE_BIOLOGICAL) {
                bioBases += readLen[read];
            }
            else {
                techBases += readLen[read];
            }
        }
        if (self->techBasesPerSpotEquals) {
            if (self->techBasesPerSpot != techBases) {
                if (self->techBasesPerSpot == ~0) {
                    self->techBasesPerSpot = techBases;
                }
                else {
                    self->techBasesPerSpotEquals = false;
                }
            }
        }
        self->bioBasesCnt += bioBases;
    }
    return rc;
}
static
rc_t _ApprCntInit(ApprCnt *self,
    const VTable *tbl)
{
    rc_t rc = 0;

    assert(self);

    memset(self, 0, sizeof *self);

    self->techBasesPerSpotEquals = true;
    self->techBasesPerSpot = ~0;

    if (rc == 0) {
        rc = VTableCreateCursorRead(tbl, &self->curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VTableCreateCursorRead");
        }
    }

    if (rc == 0) {
        const char name[] = "READ_LEN";
        rc = VCursorAddColumn(self->curs, &self->colREAD_LEN, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error during VCursorAddColumn($(name))", "name=%s", name));
        }
    }

    if (rc == 0) {
        const char name[] = "READ_TYPE";
        rc = VCursorAddColumn(self->curs, &self->colREAD_TYPE, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error during VCursorAddColumn($(name))", "name=%s", name));
        }
    }

    if (rc == 0) {
        rc = VCursorOpen(self->curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VCursorOpen");
        }
    }

    return rc;
}
static
rc_t _ApprCntFini(ApprCnt *self)
{
    rc_t rc = 0;
    assert(self);
    RELEASE(VCursor, self->curs);
    memset(self, 0, sizeof *self);
    return rc;
}
static uint64_t BIG = 10000 /*11*/; 
static
rc_t _VTableBioBaseCntApprox(const VTable *self,
    uint64_t nspots, uint32_t nreads, uint64_t *bio_base_count)
{
    rc_t rc = 0;
    uint64_t c = nspots;
    uint64_t d = 1;
    ApprCnt ac;
    rc = _ApprCntInit(&ac, self);
    assert(bio_base_count);
    if (rc == 0) {
        if (nspots < BIG) {
            STSMSG(2,
                ("VdbBlastRunSetGetTotalLengthApprox: counting all reads\n"));
            rc = _ApprCntChunk(&ac, 0, nspots, nspots, nreads);
        }
        else {
            uint64_t i = 0;
            uint64_t l = 0;
            uint64_t n = 10 /* 3 */;
            while (c > BIG) {
                c /= 2;
                d *= 2;
            }
            l = c / n;
            if (l == 0) {
                l = 1;
            }
            for (i = 1; i < n - 1 && rc == 0; ++i) {
                rc = _ApprCntChunk(&ac, i, l, nspots, nreads);
            }
        }
    }
    if (rc == 0) {
        uint32_t status = eVdbBlastNoErr;
        if (nspots < BIG) {
            *bio_base_count = ac.bioBasesCnt;
        }
        else {
            if (ac.techBasesPerSpotEquals && ac.techBasesPerSpot == ~0) {
                ac.techBasesPerSpotEquals = false;
            }
            if (ac.techBasesPerSpotEquals ) {
                uint64_t baseCount = 0;
                status = _VTableReadFirstRow(
                    self, "BASE_COUNT", &baseCount, sizeof baseCount, NULL);
                if (status == eVdbBlastNoErr) {
                    STSMSG(2, ("VdbBlastRunSetGetTotalLengthApprox: "
                        "fixed technical reads length\n"));
                    *bio_base_count = baseCount - nspots * ac.techBasesPerSpot;
                }
                else {
                    STSMSG(1, ("VdbBlastRunSetGetTotalLengthApprox: "
                        "cannot read BASE_COUNT\n"));
                }
            }
            if (!ac.techBasesPerSpotEquals || status != eVdbBlastNoErr) {
/*              double dd = nspots / c; */
                STSMSG(2, ("VdbBlastRunSetGetTotalLengthApprox: "
                    "extrapolating variable read length\n"));
                *bio_base_count = ac.bioBasesCnt * d;
            }
        }
    }
    _ApprCntFini(&ac);
    return rc;
}
static 
uint64_t _VdbBlastRunCountBioBaseCount(VdbBlastRun *self,
    uint32_t *status)
{
    uint64_t bio_base_count = 0;
    rc_t rc = _VTableBioBaseCntApprox(self->tbl,
        self->rd.spotCount, self->rd.nReads, &bio_base_count);
#if 0
    const VCursor *curs = NULL;
/*  uint32_t colREAD_FILTER = 0; */
    uint32_t colREAD_LEN = 0;
    uint32_t colREAD_TYPE = 0;
    uint64_t spot = 0;
    assert(self && status);
    if (rc == 0) {
        rc = VTableCreateCursorRead(self->tbl, &curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VTableCreateCursorRead");
        }
    }
/*  if (rc == 0) {
        const char name[] = "READ_FILTER";
        rc = VCursorAddColumn(curs, &colREAD_FILTER, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error during VCursorAddColumn($(name))", "name=%s", name));
        }
    }*/
    if (rc == 0) {
        const char name[] = "READ_LEN";
        rc = VCursorAddColumn(curs, &colREAD_LEN, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error during VCursorAddColumn($(name))", "name=%s", name));
        }
    }
    if (rc == 0) {
        const char name[] = "READ_TYPE";
        rc = VCursorAddColumn(curs, &colREAD_TYPE, name);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc,
                "Error during VCursorAddColumn($(name))", "name=%s", name));
        }
    }
    if (rc == 0) {
        rc = VCursorOpen(curs);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error during VCursorOpen");
        }
    }
    for (spot = 1; spot <= self->rd.spotCount && rc == 0; ++spot) {
        const void *base = NULL;
        uint32_t elem_bits = 0;
/*      uint8_t *readFilter = NULL; */
        const uint32_t *readLen = NULL;
        const INSDC_read_type *readType = NULL;
        uint32_t boff = 0;
        uint32_t row_len = 0;
        uint32_t nreads = 0;
        if (rc == 0) {
            rc = VCursorCellDataDirect(curs, spot, colREAD_LEN,
                 &elem_bits, &base, &boff, &row_len);
            if (rc != 0) {
                PLOGERR(klogInt, (klogInt, rc,
                    "Error during VCursorCellDataDirect(READ_LEN, $(spot))",
                    "spot=%lu", spot));
            }
            else if (boff != 0 || elem_bits != 32) {
                rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
                PLOGERR(klogInt, (klogInt, rc,
                    "Bad VCursorCellDataDirect(READ_LEN, $(spot)) result: "
                    "boff=$(boff), elem_bits=$(elem_bits)",
                    "spot=%lu, boff=%u, elem_bits=%u", spot, boff, elem_bits));
            }
            else {
                readLen = base;
                nreads = row_len;
            }
        }
        /*if (rc == 0) {
            rc = VCursorCellDataDirect(curs, spot, colREAD_FILTER,
                 &elem_bits, &base, &boff, &row_len);
            if (rc != 0) {
                PLOGERR(klogInt, (klogInt, rc,
                    "Error during VCursorCellDataDirect(READ_FILTER, $(spot))",
                    "spot=%lu", spot));
            }
            else if (boff != 0 || elem_bits != 8) {
                rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
                PLOGERR(klogInt, (klogInt, rc,
                    "Bad VCursorCellDataDirect(READ_FILTER, $(spot)) result: "
                    "boff=$(boff), row_len=$(row_len)",
                    "spot=%lu, boff=%u, row_len=%u", spot, boff, row_len));
            }
            else if (row_len != nreads) {
                rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
                PLOGERR(klogInt, (klogInt, rc,
                    "spot $(spot): READ_LEN[$(len)], FILTER[$(type)]",
                    "spot=%lu, len=%u, type=%u", spot, nreads, row_len));
            }
            else {
                readFilter = base;
            }
        }*/
        if (rc == 0) {
            rc = VCursorCellDataDirect(curs, spot, colREAD_TYPE,
                 &elem_bits, &base, &boff, &row_len);
            if (rc != 0) {
                PLOGERR(klogInt, (klogInt, rc,
                    "Error during VCursorCellDataDirect(READ_TYPE, $(spot))",
                    "spot=%lu", spot));
            }
            else if (boff != 0 || elem_bits != 8) {
                rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
                PLOGERR(klogInt, (klogInt, rc,
                    "Bad VCursorCellDataDirect(READ_TYPE, $(spot)) result: "
                    "boff=$(boff), row_len=$(row_len)",
                    "spot=%lu, boff=%u, row_len=%u", spot, boff, row_len));
            }
            else if (row_len != nreads) {
                rc = RC(rcSRA, rcCursor, rcReading, rcData, rcUnexpected);
                PLOGERR(klogInt, (klogInt, rc,
                    "spot $(spot): READ_LEN[$(len)], READ_TYPE[$(type)]",
                    "spot=%lu, len=%u, type=%u", spot, nreads, row_len));
            }
            else {
                readType = base;
            }
        }
        if (rc == 0) {
            uint32_t i = 0;
            for (i = 0; i < nreads; ++i) {
                INSDC_read_type type = readType[i];
                type &= 1;
                if (type == READ_TYPE_BIOLOGICAL) {
/*                  if (readFilter[i] == READ_FILTER_PASS) */
                        bio_base_count += readLen[i];
                }
            }
        }
    }
    RELEASE(VCursor, curs);
#endif
    if (rc != 0) {
        *status = eVdbBlastErr;
    }
    return bio_base_count;
}

static
uint64_t _VdbBlastSraRunGetLengthApprox(VdbBlastRun *self,
    uint32_t *status)
{
    assert(self && status);

    *status = eVdbBlastNoErr;

    if (self->bioBasesApprox == ~0) {
        RunDesc *rd = NULL;
        *status = _VdbBlastRunFillRunDesc(self);
        if (*status != eVdbBlastNoErr) {
            S
            return 0;
        }

        rd = &self->rd;
        if (rd->nReads == 0) {
            S
            self->bioBasesApprox = 0;
        }
        else if (rd->varReadLen) {
            S
            self->bioBasesApprox = _VdbBlastRunCountBioBaseCount(self, status);
        }
        else {
            if (self->type == btpREFSEQ) {
                if (rd->bioBaseCount == ~0) {
                    S
                    *status = eVdbBlastErr;
                }
                else {
                    self->bioBasesApprox = rd->bioBaseCount;
                }
            }
            else {
                uint8_t read = 0;
                S
                for (read = 0, self->bioBasesApprox = 0;
                    read < rd->nReads; ++read)
                {
                    if (rd->readType[read] & SRA_READ_TYPE_BIOLOGICAL) {
                        self->bioBasesApprox += rd->readLen[read];
                    }
                }
                self->bioBasesApprox *= rd->spotCount;
            }
        }
    }

    return self->bioBasesApprox;
}

static uint64_t _VdbBlastRunGetLengthApprox(VdbBlastRun *self, uint32_t *status)
{
    if (self->cSra) {
        return _VdbBlastRunGetLength(self, status);
    }
    else {
        return _VdbBlastSraRunGetLengthApprox(self, status);
    }
}

static
uint64_t _VdbBlastRunGetNumSequencesApprox(VdbBlastRun *self,
    uint32_t *status)
{

    assert(self && status);

    *status = eVdbBlastNoErr;

    if (self->bioReadsApprox == ~0) {
        RunDesc *rd = NULL;

        if (self->bioReads != ~0 && ! self->bioReadsTooExpensive) {
            self->bioReadsApprox = self->bioReads;
        }
        else if (self->type == btpREFSEQ) {
            S
            self->bioReadsApprox = 1;
        }
        else if (self->cSra) {
/* Number of Bio Reads for cSra == number of CMP reads
    = Number of all bio reads * CMP_BASE_COUNT / BIO_BASE_COUNT
   Number of all bio reads = nSpots * n of bio reads per spot */
            double r = 0;
            uint64_t n = 0;

            *status = _VdbBlastRunFillRunDesc(self);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }

            rd = &self->rd;

            n = _VdbBlastSraRunGetLengthApprox(self, status);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }
            r = rd->cmpBaseCount * rd->spotCount * rd->nBioReads;
            r /= n;
            self->bioReadsApprox = r;
        }
        else {
            *status = _VdbBlastRunFillRunDesc(self);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }

            rd = &self->rd;

            self->bioReadsApprox = rd->spotCount * rd->nBioReads;
            S
        }
    }
    else
    {   S }

    return self->bioReadsApprox;
}

static
uint32_t _VdbBlastRunGetWgsAccession(VdbBlastRun *self,
    int64_t spot,
    char *name_buffer,
    size_t bsize,
    size_t *num_required)
{
    rc_t rc = 0;
    uint32_t row_len = 0;

    assert(num_required);

    if (self == NULL || spot <= 0 || name_buffer == NULL || bsize == 0) {
        STSMSG(0, ("Error: some of %s parameters is NULL or 0", __func__));
        return eVdbBlastErr;
    }
    if (self->tbl == NULL) {
        STSMSG(0, ("Error: %s: VTable is NULL in VdbBlastRun", __func__));
        return eVdbBlastErr;
    }

    if (self->cursACCESSION == NULL) {
        rc = _VTableMakeCursor(self->tbl,
            &self->cursACCESSION, &self->col_ACCESSION, "ACCESSION");
        if (rc != 0) {
            VCursorRelease(self->cursACCESSION);
            self->cursACCESSION = NULL;
            return eVdbBlastErr;
        }
    }

    assert(self->cursACCESSION && rc == 0);

    rc = VCursorReadDirect(self->cursACCESSION, spot,
        self->col_ACCESSION, 8, name_buffer, bsize, &row_len);
    *num_required = row_len;
    if (row_len > 0) /* include ending '\0' */
    {   ++(*num_required); }
    if (rc == 0) {
        if (bsize > row_len)
        { name_buffer[row_len] = '\0'; }
        return eVdbBlastNoErr;
    }
    else if (rc == SILENT_RC
        (rcVDB, rcCursor, rcReading, rcBuffer, rcInsufficient))
    {   return eVdbBlastNoErr; }
    else {
        assert(self->path);
        PLOGERR(klogInt, (klogInt, rc, "Error in VCursorReadDirect"
            "$(path), ACCESSION, spot=$(spot))",
            "path=%s,spot=%ld", self->path, spot));
        return eVdbBlastErr;
    }
}

/******************************************************************************/
typedef struct ReadDesc {
    const VdbBlastRun *prev;

    VdbBlastRun *run;
    uint64_t spot; /* 1-based */
    uint32_t read; /* 1-based */

    uint64_t read_id; /* BioReadId in RunSet */
} ReadDesc;
static
bool _ReadDescNextRead(ReadDesc *self)
{
    uint32_t read = 0;
    int i = 0;
    const RunDesc *rd = NULL;

    assert(self && self->run);

    rd = &self->run->rd;

    if (rd->nBioReads == 0) {
        S
        return false;
    }

    for (i = self->read + 1; i <= rd->nReads; ++i) {
        if (rd->readType[i - 1] & SRA_READ_TYPE_BIOLOGICAL) {
            S
            read = i;
            break;
        }
    }

    if (read == 0) {
        if (++self->spot > rd->spotCount) {
            S
            return false;
        }

        for (i = 1; i <= rd->nReads; ++i) {
            if (rd->readType[i - 1] & SRA_READ_TYPE_BIOLOGICAL) {
                S
                read = i;
                break;
            }
        }
    }

    if (read) {
        S
        self->read = read;
        ++self->read_id;
    }
    else
    {   S }

    return read;
}

static
bool _ReadDescSameRun(const ReadDesc *self)
{
    assert(self);

    if (self->prev == NULL && self->run == NULL) {
        S
        return false;
    }

    S
    return self->prev == self->run;
}

#ifdef TEST_VdbBlastRunFillReadDesc
LIB_EXPORT
#else
static
#endif
uint32_t _VdbBlastRunFillReadDesc(VdbBlastRun *self,
    uint64_t read_id,
    ReadDesc *desc)
{
    const VdbBlastRun *prev = NULL;
    const RunDesc *rd = NULL;
    
    int bioIdx = 0;

    if (self == NULL || desc == NULL) {
        S
        return eVdbBlastErr;
    }

    prev = desc->run;
    memset(desc, 0, sizeof *desc);
    desc->prev = prev;
    desc->run = self;

    rd = &self->rd;

    if (rd->nReads == 0 || rd->readType == NULL) {
        uint32_t status = _VdbBlastRunFillRunDesc(self);
        if (status != eVdbBlastNoErr)
        {   return status; }
        assert(rd->nReads && rd->readType);
    }

    desc->spot = read_id / rd->nBioReads + 1;
    if (desc->spot <= rd->spotCount) {
        int idInSpot = read_id - (desc->spot - 1) * rd->nBioReads; /* 0-based */

        int i = 0;
        for (i = 0; i < rd->nReads; ++i) {
            if (rd->readType[i] & SRA_READ_TYPE_BIOLOGICAL) {
                if (bioIdx++ == idInSpot) {
                    S
                    desc->read = i + 1;
                    return eVdbBlastNoErr;
                }
            }
        }
        S
    }
    else { S }

    memset(desc, 0, sizeof *desc);
    return eVdbBlastErr;
}

static
uint32_t _VdbBlastRunGetReadId(VdbBlastRun *self,
    const char *acc,
    uint64_t spot, /* 1-based */
    uint32_t read, /* 1-based */
    uint64_t *read_id)
{
    uint64_t id = ~0;
    uint32_t status = eVdbBlastErr;
    size_t size;

    assert(self && acc && read_id && self->acc);
    assert(memcmp(self->acc, acc, string_measure(self->acc, &size)) == 0);

    if ((spot <= 0 && read > 0) || (spot > 0 && read <= 0)) {
        S
        return eVdbBlastErr;
    }

    if (spot > 0) {
        if (self->type != btpSRA) {
            return eVdbBlastErr;
        }

        for (id = (spot - 1) * self->rd.nBioReads; ; ++id) {
            ReadDesc desc;
            status = _VdbBlastRunFillReadDesc(self, id, &desc);
            if (status != eVdbBlastNoErr)
            {   return status; }
            if (desc.spot < spot) {
                S
                return eVdbBlastErr;
            }
            if (desc.spot > spot) {
                S
                return eVdbBlastErr;
            }
            if (desc.read == read) {
                *read_id = id;
                return eVdbBlastNoErr;
            }
        }
        S
        return eVdbBlastErr;
    }
    else {
        uint64_t n = ~0;
        uint64_t i = ~0;
        if (self->type == btpSRA)
        {   return eVdbBlastErr; }
        else if (self->type == btpREFSEQ) {
            *read_id = 0;
            return eVdbBlastNoErr;
        }
        else if (self->type == btpWGS) {
            n = _VdbBlastRunGetNumSequences(self, &status);
            if (status != eVdbBlastNoErr && status != eVdbBlastTooExpensive) {
                return status;
            }
            /* TODO optimize: avoid full run scan */
            for (i = 0; i < n ; ++i) {
                size_t need = ~0;
#define SZ 4096
                char name_buffer[SZ + 1];
                if (string_measure(acc, &size) > SZ) {
                    S
                    return eVdbBlastErr;
                }
#undef SZ
               status = _VdbBlastRunGetWgsAccession(
                    self, i + 1, name_buffer, sizeof name_buffer, &need);
                if (need > sizeof name_buffer) {
                    S
                    return eVdbBlastErr;
                }
                if (strcmp(name_buffer, acc) == 0) {
                    *read_id = i;
                    return eVdbBlastNoErr;
                }
            }
        }
        else { assert(0); }
        return eVdbBlastErr;
    }
}

/*
static
rc_t _VTableStatic(const VTable *self,
    const char *col_name, bool *isStatic)
{
    rc_t rc = 0;
    const VCursor *curs = NULL;
    uint32_t idx = 0;
    rc = _VTableMakeCursor(self, &curs, &idx, col_name);
    if (rc == 0) {
        rc = VCursorIsStaticColumn(curs, idx, isStatic);
    }
    RELEASE(VCursor, curs);
    return rc;
}
static
uint32_t _VdbBlastRunFillBioInfo(VdbBlastRun *self) {
    rc_t rc = 0;
    bool isStatic = true;
    bool aStatic = false;

    assert(self);

    if (self->bioReads == ~0 || self->bioReadsApprox == ~0) {
        return eVdbBlastTooExpensive;
    }
    if (self->bioReads != 0 || self->bioReadsApprox != 0) {
        return eVdbBlastNoErr;
    }
    rc = _VTableStatic(self->tbl, "READ_LEN");
    return eVdbBlastNoErr;
}*/

typedef struct Data2na {
    uint32_t irun;
    const VBlob *blob;
} Data2na;
/******************************************************************************/
typedef struct Reader2na {
    bool eor;
    ReadDesc desc;
    uint32_t col_READ;
    const VCursor *curs;
    size_t starting_base; /* 0-based, in current read */
    ReaderCols cols;
} Reader2na;
static
uint64_t _Reader2naFini(Reader2na *self)
{
    uint64_t read_id = 0;

    assert(self);

    read_id = self->desc.read_id;

    VCursorRelease(self->curs);

    ReaderColsFini(&self->cols);

    memset(self, 0, sizeof *self);

    return read_id;
}

static
bool _Reader2naEor(const Reader2na *self)
{
    assert(self);
    S
    return self->eor;
}

static
uint32_t _Reader2naReadCols(Reader2na *self)
{
    assert(self && self->desc.run);
    return _VCursorReadCols
        (self->curs, self->desc.spot, &self->cols, self->desc.run->rd.nReads);
}

static
uint32_t _Reader2naGetBlob(Reader2na *self,
    const VBlob **blob,
    const ReadDesc *desc,
    int64_t *first,
    uint64_t *count)
{
    bool fresh = false;

    assert(self && blob && desc && first && count
        && desc->run && desc->run->path);

    for (fresh = false; ;) {
        rc_t rc = 0;

        if (*blob == NULL) {
            rc = VCursorGetBlobDirect
                (self->curs, blob, desc->spot, self->col_READ);
            if (rc) {
                PLOGERR(klogInt, (klogInt, rc, "Error in VCursorGetBlobDirect"
                    "($(path), READ, spot=$(spot))",
                    "path=%s,spot=%ld", desc->run->path, desc->spot));
                return eVdbBlastErr;
            }

            fresh = true;
        }

        rc = VBlobIdRange(*blob, first, count);
        if (rc) {
            PLOGERR(klogInt, (klogInt, rc, "Error in VBlobIdRange($(path))",
                "path=%s", desc->run->path));
            return eVdbBlastErr;
        }

        if (desc->spot >= *first && desc->spot < *first + *count) {
            S
            return eVdbBlastNoErr;
        }

        if (fresh) {
            S
            return eVdbBlastErr;
        }

        rc = VBlobRelease(*blob);
        *blob = NULL;
        if (rc) {
            PLOGERR(klogInt, (klogInt, rc, "Error in VBlobRelease($(path))",
                "path=%s", desc->run->path));
            return eVdbBlastErr;
        }
    }

    S
    return eVdbBlastErr;
}

static
uint32_t _Reader2naCalcReadParams(const ReadDesc *desc,
    const ReaderCols *cols,
    uint32_t *start,
    uint32_t min_read_length)
{
    int i = ~0;
    uint32_t to_read = 0;
    assert(desc && cols && start);
    assert(desc->run && desc->run->path);

    *start = 0;

    assert(cols->read_len && cols->read_filter);
    for (i = 1; i < desc->read; ++i) {
        assert(i <= desc->run->rd.nReads);

        /* do not count CMP_READ-s where primary_alignment_id != 0
           as are not stored in CMP_READ) */
        if (!ReaderColsIsReadCompressed(cols, i - 1)) {
            *start += cols->read_len[i - 1];
        }
    }
    S

    if (cols->read_len[desc->read - 1] == 0) {
        S
        DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
            ("%s: %s:%d:%d(%d): READ_LEN=0\n",
            __func__, desc->run->path, desc->spot, desc->read, desc->read_id));
        return 0;
    }
    else if (cols->read_filter[desc->read - 1] != READ_FILTER_PASS) {
        S
        DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
            ("%s: %s:%d:%d(%d): READ_FILTER != READ_FILTER_PASS\n",
            __func__, desc->run->path, desc->spot, desc->read, desc->read_id));
        return 0;
    }
    else if (cols->TRIM_LEN > 0
        && *start >= cols->TRIM_START + cols->TRIM_LEN)
    {
        return 0;
    }
    else {
        uint32_t end = 0;

        /* do not count CMP_READ-s where primary_alignment_id != 0
           as are not stored in CMP_READ) */
        if (ReaderColsIsReadCompressed(cols, desc->read - 1)) {
            to_read = 0;
            S
            DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
                ("%s: %s:%d:%d(%d): PRIMARY_ALIGNMENT_ID != 0\n", __func__,
                 desc->run->path, desc->spot, desc->read, desc->read_id));
        }
        else {
            to_read = cols->read_len[desc->read - 1];
            end = *start + to_read;
            if (cols->TRIM_LEN > 0 && cols->TRIM_START > *start) {
                uint32_t delta = cols->TRIM_START - *start;
                if (to_read > delta) {
                    *start = cols->TRIM_START;
                    to_read -= delta;
                    assert(*start + to_read == end);
                }
                else {
                    to_read = 0;
                }
            }
        }
        if (to_read > 0) {
            if (cols->TRIM_LEN > 0 &&
                end > (cols->TRIM_START + cols->TRIM_LEN))
            {
                uint32_t delta = end - (cols->TRIM_START + cols->TRIM_LEN);
                assert(delta < to_read);
                to_read -= delta;
            }
            if (to_read < min_read_length) {
                S
                DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
                    ("%s: %s:%d:%d(%d): READ_LEN=%d: TOO SHORT (<%d)\n",
                    __func__, desc->run->path, desc->spot, desc->read,
                    desc->read_id, cols->read_len[desc->read - 1],
                    min_read_length));
                return 0;
            }
            else {
                DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
                    ("%s: %s:%d:%d(%d): READ_LEN=%d\n", __func__,
                    desc->run->path, desc->spot, desc->read,
                    desc->read_id, cols->read_len[desc->read - 1]));
            }
        }
    }

    return to_read;
}

static
bool _Reader2naNextData(Reader2na *self,
    const VBlob *blob,
    uint32_t *status,
    Packed2naRead *out,
    uint32_t min_read_length)
{
    uint32_t start = 0;
    uint32_t to_read = 0;
    const ReadDesc *desc = NULL;
    assert(self && status && out && self->curs);
    desc = &self->desc;
    memset(out, 0, sizeof *out);

    *status = _Reader2naReadCols(self);
    if (*status != eVdbBlastNoErr) {
        S
        return false;
    }
    if (!self->curs || !desc->run) {
        S
        *status = eVdbBlastErr;
        return false;
    }

    assert(self->cols.read_len && self->cols.read_filter);
    assert(desc->read <= desc->run->rd.nReads);

    to_read = _Reader2naCalcReadParams(&self->desc, &self->cols,
        &start, min_read_length);
    if (to_read == 0) {
        return true;
    }
    else {
        uint32_t elem_bits = 0;
        rc_t rc = 0;

        S
        DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
            ("%s: %s:%d:%d(%d): READ_LEN=%d\n", __func__,
            self->desc.run->path, self->desc.spot, self->desc.read,
            self->desc.read_id, self->cols.read_len[desc->read - 1]));

        rc = VBlobCellData(blob, desc->spot, &elem_bits,
            (const void **)&out->starting_byte, &out->offset_to_first_bit,
            &out->length_in_bases);
        if (rc != 0) {
            S
            DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST),
               ("%s: %s:%d:%d(%d): READ_LEN=%d: "
                "ERROR WHILE READING: SKIPPED FOR NOW\n", __func__,
                self->desc.run->path, self->desc.spot, self->desc.read,
                self->desc.read_id, self->cols.read_len[desc->read - 1]));
            /* special case */
            *status = eVdbBlastErr;
            return true;
        }

        if (elem_bits != 2) {
            S
            *status = eVdbBlastErr;
            return false;
        }

        if (out->length_in_bases < start) {
            S
            *status = eVdbBlastErr;
            return false;
        }

        out->offset_to_first_bit += start * 2;
        S

        out->length_in_bases = to_read;
        while (out->offset_to_first_bit >= 8) {
            out->starting_byte = ((uint8_t*)out->starting_byte) + 1;
            out->offset_to_first_bit -= 8;
        }
        out->read_id = desc->read_id;
        S
        return true;
    }
}

static
uint32_t _Reader2naData(Reader2na *self,
    Data2na *data,
    uint32_t *status,
    Packed2naRead *buffer,
    uint32_t buffer_length,
    uint32_t min_read_length)
{
    ReadDesc *desc = NULL;
    uint32_t n = 0;
    int64_t first = 0;
    uint64_t count = 0;
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }
    *status = eVdbBlastErr;
    if (buffer_length && buffer == NULL) {
        S
        return 0;
    }

    assert(self && self->curs && data);
    desc = &self->desc;
    *status = _Reader2naGetBlob(self, &data->blob, desc, &first, &count);
    if (*status == eVdbBlastErr) {
        S
        return 0;
    }

    for (n = 0; n < buffer_length; ) {
        Packed2naRead *p = buffer + n;
        bool ignorable
            = _Reader2naNextData(self, data->blob, status, p, min_read_length);
        if (*status == eVdbBlastErr) {
            if (ignorable) {
                /* special case */
                if (n > 0) { /* let's retry during next call */
                    S
                    *status = eVdbBlastNoErr;
                }
                else
                {   S }
                return n;
            }
            S
            return 0;
        }
        if (p->length_in_bases > 0)
        {   ++n; }
        if (!_ReadDescNextRead(desc)) {
            S
            self->eor = true;
            break;
        }
        if (desc->spot >= first + count) {
            S
            break;
        }
    }

    *status = eVdbBlastNoErr;
    return n;
}
/* struct Packed2naRead {
    uint64_t read_id;
    void *starting_byte;
    uint32_t offset_to_first_bit;
    uint32_t length_in_bases; }; */

static
uint64_t _Reader2naRead(Reader2na *self,
    uint32_t *status,
    uint64_t *read_id,
    size_t *starting_base,
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t min_read_length)
{
    uint32_t num_read = 0;
    uint32_t to_read = 0;
    ReadDesc *desc = NULL;
    uint32_t start = 0;
    uint32_t remaining = 0;
    rc_t rc = 0;
    assert(self && status && read_id && starting_base);
    desc = &self->desc;
    *read_id = desc->read_id;
    *starting_base = self->starting_base;

    if (_Reader2naEor(self)) {
        S
        *status = eVdbBlastNoErr;
        return 0;
    }

    *status = _Reader2naReadCols(self);
    if (*status != eVdbBlastNoErr) {
        S
        return 0;
    }

    *status = eVdbBlastErr;
    if (!self->curs || !desc->run) {
        S
        return 0;
    }

    assert(desc->run->path);

    *status = eVdbBlastNoErr;
    if (desc->run->rd.nBioReads == 0) {
        S
        return 0;
    }

    *status = eVdbBlastNoErr;

    to_read = _Reader2naCalcReadParams(&self->desc, &self->cols,
        &start, min_read_length);
    if (to_read <= self->starting_base) {
        S
        DBGMSG(DBG_BLAST, DBG_FLAG(DBG_BLAST_BLAST), (
        "%s: %s:%d:%d(%d): READ_LEN=%d: TOO SHORT (starting_base=%d)\n",
            __func__, desc->run->path, desc->spot, desc->read,
            desc->read_id, self->cols.read_len[desc->read - 1],
            self->starting_base));
        to_read = 0;
    }
    else {
        to_read -= self->starting_base;
        start += self->starting_base;
    }
    if (to_read > 0) {
        S
        rc = VCursorReadBitsDirect(self->curs, desc->spot, self->col_READ, 2,
            start, buffer, 0, buffer_size * 4, &num_read, &remaining);
        if (rc) {
            if (rc == SILENT_RC
                (rcVDB, rcCursor, rcReading, rcBuffer, rcInsufficient))
            {
                S
                rc = 0;
                num_read = buffer_size * 4;
            }
            else {
                PLOGERR(klogInt, (klogInt, rc,
                    "Error in VCursorReadBitsDirect"
                    "($(path), READ, spot=$(spot))", "path=%s,spot=%ld",
                    desc->run->path, desc->spot));
                *status = eVdbBlastErr;
                return 0;
            }
        }
        *status
            = (num_read == 0 && remaining == 0) ? eVdbBlastErr : eVdbBlastNoErr;
        S
    }

    if (num_read >= to_read) {
        self->starting_base = 0;
        num_read = to_read;
        if (!_ReadDescNextRead(desc))
        {   self->eor = true; }
        S
    }
    else {
        self->starting_base += num_read;
        S
    }

    return num_read;
}

static VdbBlastStatus _VdbBlastRunMakeCursor(const VdbBlastRun *self,
    const VCursor **curs, uint32_t *read_col_idx, bool INSDC2na,
    ReaderCols *cols)
{
    rc_t rc = 0;
    const char *read_col_name = INSDC2na ? "(INSDC:2na:packed)READ"
                                         : "(INSDC:4na:bin)READ";
    assert(self);
    if (self->cSra) {
        read_col_name = INSDC2na ? "(INSDC:2na:packed)CMP_READ"
                                 : "(INSDC:4na:bin)CMP_READ";
    }
    rc = _VTableMakeCursor(self->tbl, curs, read_col_idx, read_col_name);
    if (rc == 0) {
        assert(*curs);
        VdbBlastStatus status = _VCursorAddCols(*curs, cols, self->cSra);
        return status;
    }
    return eVdbBlastErr;
}

static
VdbBlastStatus _Reader2naOpenCursor(Reader2na *self)
{
    const ReadDesc *desc = NULL;
    const VdbBlastRun *run = NULL;

    assert(self);

    desc = &self->desc;
    run = desc->run;

    assert(run && self->curs == NULL);

    return _VdbBlastRunMakeCursor(run,
        &self->curs, &self->col_READ, true, &self->cols);
}

/******************************************************************************/
typedef struct RunSet {
    VdbBlastRun *run;
    uint32_t krun; /* number of run-s */
    uint32_t nrun; /* sizeof of run-s */
} RunSet;
static
void _RunSetFini(RunSet *self)
{
    assert(self);
    if (self->run) {
        uint32_t i = 0;
        for (i = 0; i < self->krun; ++i)
        {   _VdbBlastRunFini(&self->run[i]); }
        free(self->run);
    }
    memset(self, 0, sizeof *self);
}

static
uint32_t _RunSetAllocTbl(RunSet *self)
{
    size_t nmemb = 16;

    if (self == NULL)
    {   return eVdbBlastErr; }

    if (self->run && self->krun < self->nrun) {
        return eVdbBlastNoErr;
    }

    if (self->run == NULL) {
        self->run = calloc(nmemb, sizeof *self->run);
        if (self->run == NULL)
        {   return eVdbBlastMemErr; }
        S
    }
    else {
        void *p = NULL;
        nmemb += self->nrun;
        p = realloc(self->run, nmemb * sizeof *self->run);
        if (p == NULL)
        {   return eVdbBlastMemErr; }
        self->run = p;
        S
    }

    self->nrun = nmemb;
    return eVdbBlastNoErr;
}

static
uint32_t _RunSetAddTbl(RunSet *self,
    const VTable *tbl,
    const char *rundesc,
    BTableType type,
    const KDirectory *dir,
    char *fullpath,
    uint32_t min_read_length)
{

    VdbBlastRun* run = NULL;
    uint32_t status = _RunSetAllocTbl(self);
    if (status) {
        return status;
    }

    if (tbl == NULL) {
        return eVdbBlastNoErr;
    }

    assert(self && self->run);
    run = &self->run[self->krun++];
    status = _VdbBlastRunInit(run,
        tbl, rundesc, type, dir, fullpath, min_read_length);
    return status;
}

static
uint64_t _RunSetGetNumSequencesApprox(const RunSet *self,
    uint32_t *status)
{
    uint64_t num = 0;
    uint32_t i = 0;
    assert(self && status);
    *status = eVdbBlastNoErr;
    for (i = 0; i < self->krun; ++i) {
        VdbBlastRun *run = NULL;
        assert(self->run);
        run = &self->run[i];
        num += _VdbBlastRunGetNumSequencesApprox(run, status);
        if (*status != eVdbBlastNoErr) {
            assert(run->path);
            STSMSG(1, (
                "Error: failed to GetNumSequencesApprox(on run %s)",
                run->path));
            return 0;
        }
    }

    STSMSG(1, ("_RunSetGetNumSequencesApprox = %ld", num));

    return num;
}

static
uint64_t _RunSetGetNumSequences(const RunSet *self,
    VdbBlastStatus *aStatus)
{
    uint64_t num = 0;
    uint32_t i = 0;
    assert(self && aStatus);
    *aStatus = eVdbBlastNoErr;
    for (i = 0; i < self->krun; ++i) {
        VdbBlastStatus status = eVdbBlastNoErr;
        VdbBlastRun *run = NULL;
        assert(self->run);
        run = &self->run[i];
        num += _VdbBlastRunGetNumSequences(run, &status);
        if (status != eVdbBlastNoErr) {
            assert(run->path);
            if (*aStatus == eVdbBlastNoErr) {
                *aStatus = status; 
            }
            if (status != eVdbBlastTooExpensive) {
                STSMSG(1, (
                    "Error: failed to GetNumSequences(on run %s)", run->path));
                return 0;
            }
            assert(*aStatus == eVdbBlastTooExpensive);
        }
    }

    STSMSG(1, ("_RunSetGetNumSequences = %ld", num));

    return num;
}

static
uint64_t _RunSetGetTotalLength(const RunSet *self,
    uint32_t *status)
{
    uint64_t num = 0;
    uint32_t i = 0;
    assert(self && status);

    if (self->krun)
    {   assert(self->run); }

    for (i = 0; i < self->krun; ++i) {
        VdbBlastRun *run = &self->run[i];
        assert(run && run->path);
        num += _VdbBlastRunGetLength(run, status);
        if (*status != eVdbBlastNoErr) {
            STSMSG(1, (
                "Error: failed to _RunSetGetTotalLength(on run %s)",
                run->path));
            return 0;
        }
    }

    STSMSG(1, ("_RunSetGetTotalLength = %ld", num));

    return num;
}

static
uint64_t _RunSetGetTotalLengthApprox(const RunSet *self,
    uint32_t *status)
{
    uint64_t num = 0;
    uint32_t i = 0;

    assert(self && status);

    for (num = 0, i = 0; i < self->krun; ++i) {
        VdbBlastRun *run = NULL;
        assert(self->run);
        run = &self->run[i];
        num += _VdbBlastRunGetLengthApprox(run, status);
        if (*status != eVdbBlastNoErr) {
            STSMSG(1, ("Error: failed "
                "to _VdbBlastRunGetLengthApprox(on run %s)", run->path));
            return 0;
        }
    }

    STSMSG(1, ("VdbBlastRunSetGetTotalLengthApprox = %ld", num));
    return num;
}

static
size_t _RunSetGetName(const RunSet *self,
    uint32_t *status,
    char *name_buffer,
    size_t bsize)
{
    size_t need = 0, idx = 0;
    int i = 0;
    size_t size;
    
    assert(self && status);
    for (i = 0; i < self->krun; ++i) {
        VdbBlastRun *run = &self->run[i];
        if (run && run->acc) {
            if (i)
            {   ++need; }
            need += string_measure(run->acc, &size);
        }
        else {
            S
            return 0;
        }
    }

    if (name_buffer == NULL || bsize == 0) {
        S
        return need;
    }

    for (i = 0; i < self->krun; ++i) {
        VdbBlastRun *run = &self->run[i];
        if (run && run->acc) {
            if (i)
            {   name_buffer[idx++] = '|'; }
            if (idx >= bsize) {
                S
                return need;
            }
            string_copy(name_buffer + idx, bsize - idx,
                run->acc, string_size(run->acc));
            idx += string_measure(run->acc, &size);
            if (idx >= bsize) {
                S
                return need;
            }
        }
    }
    name_buffer[idx++] = '\0';
    *status = eVdbBlastNoErr;

    S
    return need;
}

static
uint32_t _RunSetFindReadDesc(const RunSet *self,
    uint64_t read_id,
    ReadDesc *desc)
{
    uint64_t i = 0;
    uint64_t prev = 0;
    uint64_t crnt = 0;

    if (self == NULL || desc == NULL) {
        S
        return eVdbBlastErr;
    }

    for (i = 0, prev = 0; i < self->krun; ++i) {
        uint32_t status = eVdbBlastNoErr;
        VdbBlastRun *run = NULL;
        uint64_t l = 0;

        if (prev > 0 && i < prev) {
            S
            return eVdbBlastErr;
        }

        run = &self->run[i];
        if (run == NULL) {
            S
            return eVdbBlastErr;
        }

        l = _VdbBlastRunGetNumSequences(run, &status);
        if (status != eVdbBlastNoErr && status != eVdbBlastTooExpensive) {
            S
            return status;
        }

        if (crnt + l <= read_id) {
            crnt += l;
        }
        else {
            status = _VdbBlastRunFillReadDesc(run, read_id - crnt, desc);
            if (status == eVdbBlastNoErr) {
                S
                desc->read_id = read_id;
            }
            else
            {   S }

            return status;
        }

        prev = i;
    }

    S
    return eVdbBlastErr;
}

/******************************************************************************/
typedef struct Core2na {
    uint32_t min_read_length;
    bool hasReader;
    KLock *mutex;
    uint64_t initial_read_id;
    uint32_t irun; /* index in RunSet */
    bool eos;
    Reader2na reader;
} Core2na;
static void _Core2naFini(Core2na *self)
{
    assert(self);
    _Reader2naFini(&self->reader);
    KLockRelease(self->mutex);
    memset(self, 0, sizeof *self);
}

static
uint32_t _Core2naOpen1stRead(Core2na *self,
    const RunSet *runs,
    uint64_t initial_read_id)
{
    Reader2na *reader = NULL;
    uint32_t status = eVdbBlastNoErr;

    assert(self && runs);

    reader = &self->reader;

    if (reader->curs)
    {   return eVdbBlastNoErr; }

    if (runs->run && runs->krun) {
        self->initial_read_id = initial_read_id;
        status = _RunSetFindReadDesc
            (runs, initial_read_id, &reader->desc);
        if (status == eVdbBlastNoErr) {
            status = _Reader2naOpenCursor(reader);
            if (status != eVdbBlastNoErr) {
                S
                _Reader2naFini(reader);
            }
        }
        else
        {   S }
    }
    else {
        S
        self->eos = true;
    }

    return status;
}

static
uint32_t _Core2naOpenNextRead(Core2na *core,
    const RunSet *runs)
{
    uint64_t read_id = 0;
    ReadDesc *desc = NULL;
    Reader2na *reader = NULL;

    assert(core && runs);

    reader = &core->reader;
    desc = &reader->desc;

    assert(desc->run);

    read_id = _Reader2naFini(reader) + 1;

    if (core->irun >= runs->krun - 1) { /* No more runs to read */
        S
        core->eos = true;
        return eVdbBlastNoErr;
    }

    while (++core->irun < runs->krun) {
        uint32_t status = eVdbBlastNoErr;

        VdbBlastRun *run = &runs->run[core->irun];
        if (run == NULL) {
            S
            return eVdbBlastErr;
        }

        status = _VdbBlastRunFillReadDesc(run, 0, desc);
        if (status != eVdbBlastNoErr) {
            S
            return status;
        }

        desc->read_id = read_id;
        status = _Reader2naOpenCursor(reader);
        if (status == eVdbBlastNoErr)
        {      S }
        else { S }

        return status;
    }

    S
    return eVdbBlastNoErr;
}

static
uint32_t _Core2naData(Core2na *self,
    Data2na *data,
    const RunSet *runs,
    uint32_t *status,
    Packed2naRead *buffer,
    uint32_t buffer_length)
{
    uint32_t num_read = 0;

    assert(self && data && status && runs);

    *status = eVdbBlastNoErr;

    while (*status == eVdbBlastNoErr && num_read == 0) {
        if (_Reader2naEor(&self->reader) || data->irun != self->irun) {
            S
            VBlobRelease(data->blob);
            data->blob = NULL;
        }
        if (_Reader2naEor(&self->reader)) {
            S
            *status = _Core2naOpenNextRead(self, runs);
            if (*status != eVdbBlastNoErr) {
                STSMSG(1, ("Error: "
                    "failed to VdbBlast2naReaderData: cannot open next read"));
                return 0;
            }
        }
        if (data->irun != self->irun)
        {   data->irun = self->irun; }

        if (self->eos) {
            STSMSG(1, ("VdbBlast2naReaderData: End Of Set"));
            return 0;
        }

        num_read = _Reader2naData(&self->reader, data, status,
            buffer, buffer_length, self->min_read_length);
    }

    if (*status == eVdbBlastNoErr) {
        STSMSG(3, ("VdbBlast2naReaderData = %ld", num_read));
    }
    else {
        STSMSG(1, ("Error: failed to VdbBlast2naReaderData"));
    }

    return num_read;
}

static
uint64_t _Core2naRead(Core2na *self,
    const RunSet *runs,
    uint32_t *status,
    uint64_t *read_id,
    size_t *starting_base,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint64_t num_read = 0;

    assert(self && status && runs);

    if (buffer_size == 0) {
        S
        *status = eVdbBlastErr;
        return 0;
    }

    *status = eVdbBlastNoErr;

    while (*status == eVdbBlastNoErr && num_read == 0) {
        if (_Reader2naEor(&self->reader)) {
            S
            *status = _Core2naOpenNextRead(self, runs);
            if (*status != eVdbBlastNoErr) {
                S
                return 0;
            }
        }

        if (self->eos) {
            S
            return 0;
        }

        num_read = _Reader2naRead(&self->reader, status,
            read_id, starting_base, buffer, buffer_size, self->min_read_length);
        S
    }

    return num_read;
}

typedef struct Core4na {
    uint32_t min_read_length;
    KLock *mutex;
    ReadDesc desc;
    const VCursor *curs;
    const VBlob *blob; /* TODO */
    ReaderCols cols;
    uint32_t col_READ;
} Core4na;

static
void _Core4naFini(Core4na *self)
{
    assert(self);

    VCursorRelease(self->curs);
    VBlobRelease(self->blob);
    KLockRelease(self->mutex);

    ReaderColsFini(&self->cols);

    memset(self, 0, sizeof *self);
}

static
size_t _Core4naRead(Core4na *self,
    const RunSet *runs,
    uint32_t *status,
    uint64_t read_id,
    size_t starting_base,
    uint8_t *buffer,
    size_t buffer_length)
{
    uint32_t num_read = 0;
    ReadDesc *desc = NULL;
    assert(self && runs && status);
    desc = &((Core4na*)self)->desc;

    *status = _RunSetFindReadDesc(runs, read_id, desc);
    if (*status != eVdbBlastNoErr)
    {   S }
    else {
        rc_t rc = 0;
        if (!_ReadDescSameRun(desc)) {
            S
            ReaderColsReset(&self->cols);
            VCursorRelease(self->curs);
            ((Core4na*)self)->curs = NULL;
            *status = _VdbBlastRunMakeCursor(desc->run,
                &((Core4na*)self)->curs,
                &((Core4na*)self)->col_READ, false,
                &(((Core4na*)self)->cols));
            S
        }

        if (*status == eVdbBlastNoErr && rc == 0) {
            uint32_t remaining = 0;
            uint32_t start = 0;
            uint32_t to_read = 0;
            assert(desc->run && desc->read <= desc->run->rd.nReads
                && desc->run->path);
            *status = _VCursorReadCols(self->curs,
                desc->spot, &((Core4na*)self)->cols, desc->run->rd.nReads);
            if (*status == eVdbBlastNoErr) {
                assert(self->cols.read_len && self->cols.read_filter);
                to_read = _Reader2naCalcReadParams(&self->desc,
                    &self->cols, &start, self->min_read_length);
                if (to_read == 0) {
                    /* When _Reader2naCalcReadParams returns 0
                    then this read is skipped by 2na reader (usually filtered)
                    and should not be accessed by 4na reader */
                    *status = eVdbBlastInvalidId;
                    S
                }
                else {
                    if (to_read >= starting_base) {
                        to_read -= starting_base;
                        start += starting_base;
                       if (buffer_length < to_read) {
                           to_read = buffer_length;
                       }
                        S
                        rc = VCursorReadBitsDirect(self->curs,
                            desc->spot, self->col_READ, 8,
                            start, buffer, 0, to_read, &num_read, &remaining);
                        if (rc != 0) {
                            PLOGERR(klogInt, (klogInt, rc,
                                "Error in VCursorReadBitsDirect"
                                "($(path), READ, spot=$(spot))",
                                "path=%s,spot=%ld",
                                desc->run->path, desc->spot));
                        }
                    }
                    else {
                        S
                        *status = eVdbBlastErr;
                    }
                }
            }
        }

        if (*status == eVdbBlastNoErr)
        {   *status = rc ? eVdbBlastErr : eVdbBlastNoErr; }
    }

    S
    return num_read;
}

static
const uint8_t* _Core4naData(Core4na *self,
    const RunSet *runs,
    uint32_t *status,
    uint64_t read_id,
    size_t *length)
{
    ReadDesc *desc = NULL;
    assert(self && runs && status && length);
    desc = &self->desc;

    *length = 0;

    *status = _RunSetFindReadDesc(runs, read_id, desc);
    if (*status != eVdbBlastNoErr)
    {   S }
    else {
        rc_t rc = 0;
        const uint8_t *base = NULL;
        if (!_ReadDescSameRun(desc)) {
            S
            ReaderColsReset(&self->cols);
            VCursorRelease(self->curs);
            self->curs = NULL;
            *status = _VdbBlastRunMakeCursor(desc->run,
                &self->curs, &self->col_READ, false,
                &self->cols);
            S
        }

        if (*status == eVdbBlastNoErr) {
            *status = _VCursorReadCols(self->curs, desc->spot,
                &self->cols, desc->run->rd.nReads);
            S
        }

        if (rc == 0 && *status == eVdbBlastNoErr) {
            assert(self->cols.read_len && self->cols.read_filter
                && desc->run->path);

            if (self->cols.read_filter[desc->read - 1]
                != READ_FILTER_PASS)
            {   /* FILTERed reads are not returned by 2na reader:
                   4na readed should not ask for them */
                *status = eVdbBlastInvalidId;
                S
            }
            else if (ReaderColsIsReadCompressed(&self->cols, desc->read - 1)) {
                /* Compressed CMP_READs are not returned by 2na reader:
                   4na readed should not ask for them */
                *status = eVdbBlastInvalidId; /*  */
                S
            }
            else {
                if (self->blob) {
                    VBlobRelease(self->blob);
                    self->blob = NULL;
                }

                if (rc == 0 && *status == eVdbBlastNoErr) {
                    rc = VCursorGetBlobDirect
                        (self->curs, &self->blob, desc->spot, self->col_READ);
                    if (rc != 0) {
                        PLOGERR(klogInt, (klogInt, rc,
                            "Error in VCursorGetBlobDirect"
                            "($(path), READ, spot=$(spot))",
                            "path=%s,spot=%ld", desc->run->path, desc->spot));
                    }
                }

                if (rc == 0 && *status == eVdbBlastNoErr) {
                    uint32_t boff = 0;
                    uint32_t elem_bits = 0;
                    uint32_t row_len = 0;

                    rc = VBlobCellData(self->blob, desc->spot,
                        &elem_bits, (const void**)&base, &boff, &row_len);
                    if (rc != 0) {
                        PLOGERR(klogInt, (klogInt, rc, "Error in VBlobCellData"
                            "$(path), READ, spot=$(spot))",
                            "path=%s,spot=%ld", desc->run->path, desc->spot));
                    }
                    else {
                        if (elem_bits != 8) {
                            S
                            *status = eVdbBlastErr;
                            base = NULL;
                        }
                        else {
                            size_t to_read
                                = self->cols.read_len[desc->read - 1];

                            if (to_read < self->min_read_length) {
                                S
                                /* NOP */
                            }
                            else {
                                uint32_t start = 0;
                                to_read = _Reader2naCalcReadParams(&self->desc,
                                    &self->cols, &start, self->min_read_length);
/*
                                uint32_t offset = 0;
                                int i = 0;
                                assert(desc->run
                                    && desc->read <= desc->run->rd.nReads);
                                for (i = 1; i < desc->read; ++i)
                                {   offset += self->cols.read_len[i - 1]; }
                                base += boff + offset;
                                */
                                base += boff + start;
                                if (row_len >= start) {
                                    row_len -= start;
                                    if (to_read > row_len) {
                                        S
                                        *status = eVdbBlastErr;
                                    }
                                    else {
                                        S
                                        *length = to_read;
                                        *status = eVdbBlastNoErr;
                                    }
                                }
                                else {
                                    S
                                    *status = eVdbBlastErr;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (*status == eVdbBlastNoErr) {
            if (rc != 0)
            {   *status = eVdbBlastErr; }
        }

        return base;
    }

    return NULL;
}

/******************************************************************************/
static const char VDB_BLAST_RUN_SET[] = "VdbBlastRunSet";

struct VdbBlastRunSet {
    KRefcount refcount;
    bool protein;
    VdbBlastMgr *mgr;

    RunSet runs;

    bool beingRead;
    Core2na core2na;
    Core4na core4na;

    uint64_t minSeqLen;
    uint64_t avgSeqLen;
    uint64_t maxSeqLen;
};

static
void _VdbBlastRunSetWhack(VdbBlastRunSet *self)
{
    assert(self);

    STSMSG(1, ("Deleting VdbBlastRunSet(min_read_length=%d, protein=%s)",
        self->core2na.min_read_length, self->protein ? "true" : "false"));

    VdbBlastMgrRelease(self->mgr);

    _RunSetFini(&self->runs);
    _Core2naFini(&self->core2na);
    _Core4naFini(&self->core4na);

    memset(self, 0, sizeof *self);
    free(self);
}

VdbBlastRunSet *VdbBlastMgrMakeRunSet(const VdbBlastMgr *cself,
    uint32_t *status,
    uint32_t min_read_length,
    bool protein)
{
    rc_t rc = 0;
    VdbBlastRunSet *obj = NULL;
    VdbBlastMgr *self = (VdbBlastMgr*)cself;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    *status = eVdbBlastNoErr;

    obj = calloc(1, sizeof *obj);
    if (obj == NULL) {
        *status = eVdbBlastMemErr;
        return obj;
    }

    obj->protein = protein;
    obj->core2na.min_read_length = min_read_length;
    obj->core4na.min_read_length = min_read_length;

    obj->minSeqLen = ~0;
    obj->avgSeqLen = ~0;
    obj->maxSeqLen = ~0;

    if (rc == 0) {
        rc = KLockMake(&obj->core2na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockMake"); }
    }
    if (rc == 0) {
        rc = KLockMake(&obj->core4na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockMake"); }
    }
    if (rc == 0) {
        obj->mgr = VdbBlastMgrAddRef(self);
        if (obj->mgr) {
            KRefcountInit(&obj->refcount,
                1, VDB_BLAST_RUN_SET, __func__, "set");
            STSMSG(1, ("Created VdbBlastRunSet(min_read_length=%d, protein=%s)",
                min_read_length, protein ? "true" : "false"));
            return obj;
        }
    }

    STSMSG(1, ("Error: failed to create VdbBlastRunSet"));
    _VdbBlastRunSetWhack(obj);

    *status = eVdbBlastErr;

    return NULL;
}

LIB_EXPORT
VdbBlastRunSet* CC VdbBlastRunSetAddRef(VdbBlastRunSet *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetAddRef(NULL)"));
        return self;
    }

    if (KRefcountAdd(&self->refcount, VDB_BLAST_RUN_SET) == krefOkay) {
        STSMSG(1, ("VdbBlastRunSetAddRef"));
        return self;
    }

    STSMSG(1, ("Error: failed to VdbBlastRunSetAddRef"));
    return NULL;
}

LIB_EXPORT
void CC VdbBlastRunSetRelease(VdbBlastRunSet *self)
{
    if (self == NULL)
    {   return; }

    STSMSG(1, ("VdbBlastRunSetRelease"));
    if (KRefcountDrop(&self->refcount, VDB_BLAST_RUN_SET) != krefWhack)
    {   return; }

    _VdbBlastRunSetWhack(self);
}

LIB_EXPORT
uint32_t CC VdbBlastRunSetAddRun(VdbBlastRunSet *self,
    const char *rundesc)
{
    uint32_t status = eVdbBlastNoErr;
    const VTable *tbl = NULL;
    BTableType type = btpUndefined;

    /* allocated in _VdbBlastMgrFindNOpenTable()
       in _RunSetAddTbl() is assigned to VdbBlastRun::path
       freed during VdbBlastRun release */
    char *fullpath = NULL;

    if (self == NULL || self->mgr == NULL || self->beingRead) {
        S
        return eVdbBlastErr;
    }

    status =
        _VdbBlastMgrFindNOpenTable(self->mgr, rundesc, &tbl, &type, &fullpath);
    if (status) {
        S
        PLOGMSG(klogInfo,
            (klogInfo, "failed to open $(rundesc)", "rundesc=%s", rundesc));
    }
    else {
        S
        PLOGMSG(klogInfo,
            (klogInfo, "opened $(rundesc)", "rundesc=%s", rundesc));
        status = _RunSetAddTbl(&self->runs, tbl, rundesc, type,
            self->mgr->dir, fullpath, self->core2na.min_read_length);
        S
    }

    return status;
}

static
void _VdbBlastRunSetBeingRead(const VdbBlastRunSet *self)
{
    if (self == NULL)
    {   return; }
    ((VdbBlastRunSet *)self)->beingRead = true;
}

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetNumSequences(const VdbBlastRunSet *self,
    uint32_t *status)
{
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self == NULL) {
        *status = eVdbBlastErr;
        return 0;
    }

    _VdbBlastRunSetBeingRead(self);

    return _RunSetGetNumSequences(&self->runs, status);
}

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetNumSequencesApprox(
    const VdbBlastRunSet *self)
{
    uint64_t num = 0;
    uint32_t status = eVdbBlastNoErr;

    _VdbBlastRunSetBeingRead(self);

    num = _RunSetGetNumSequencesApprox(&self->runs, &status);

    STSMSG(1, ("VdbBlastRunSetGetNumSequencesApprox=%lu", num));

    return num;
}

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetTotalLength(const VdbBlastRunSet *self,
    uint32_t *status)
{
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self == NULL) {
        *status = eVdbBlastErr;
        return 0;
    }

    _VdbBlastRunSetBeingRead(self);

    return _RunSetGetTotalLength(&self->runs, status);
}

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetTotalLengthApprox(
    const VdbBlastRunSet *self)
{
    uint32_t status = eVdbBlastNoErr;

    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetGetTotalLengthApprox(self=NULL)"));
        return 0;
    }

    _VdbBlastRunSetBeingRead(self);

    return _RunSetGetTotalLengthApprox(&self->runs, &status);
}

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetAvgSeqLen(const VdbBlastRunSet *self)
{
    uint64_t num = 0;

    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetGetAvgSeqLen(self=NULL)"));
        return 0;
    }

    if (self->avgSeqLen == ~0) {
        uint64_t n = 0;
        _VdbBlastRunSetBeingRead(self);

        n = VdbBlastRunSetGetNumSequencesApprox(self);
        if (n != 0) {
            num = VdbBlastRunSetGetTotalLengthApprox(self) / n;
        }
        else {
            num = n;
        }
        ((VdbBlastRunSet*)self)->avgSeqLen = num;
    }

    STSMSG(1, ("VdbBlastRunSetGetAvgSeqLen = %ld", num));
    return self->avgSeqLen;
}

LIB_EXPORT
size_t CC VdbBlastRunSetGetName(const VdbBlastRunSet *self,
    uint32_t *status,
    char *name_buffer,
    size_t bsize)
{
    size_t sz = 0;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    *status = eVdbBlastErr;

    if (self == NULL)
    {   return 0; }

    _VdbBlastRunSetBeingRead(self);

    sz = _RunSetGetName(&self->runs, status, name_buffer, bsize);

    STSMSG(1, ("VdbBlastRunSetGetName = '%.*s'", bsize, name_buffer));

    return sz;
}

LIB_EXPORT
bool CC VdbBlastRunSetIsProtein(const VdbBlastRunSet *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetIsProtein(self=NULL)"));
        return false;
    }
    STSMSG(1, (
        "VdbBlastRunSetIsProtein = %s", self->protein ? "true" : "false"));
    return self->protein;
}

LIB_EXPORT
time_t CC VdbBlastRunSetLastUpdatedDate(const VdbBlastRunSet *self)
{
    _VdbBlastRunSetBeingRead(self);
    return _NotImplemented(__func__);
}

LIB_EXPORT
size_t CC VdbBlastRunSetGetReadName(const VdbBlastRunSet *self,
    uint64_t read_id, /* 0-based in RunSet */
    char *name_buffer,
    size_t bsize)
{
    rc_t rc = 0;
    uint32_t status = eVdbBlastNoErr;
    size_t need = 0;
    size_t num_writ = 0;

    ReadDesc desc;
    memset(&desc, 0, sizeof desc);

    if (name_buffer && bsize)
    {   name_buffer[0] = '\0'; }

    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetGetReadName(self=NULL)"));
        return 0;
    }

    _VdbBlastRunSetBeingRead(self);

    status = _RunSetFindReadDesc(&self->runs, read_id, &desc);
    if (status != eVdbBlastNoErr) {
        STSMSG(1, ("Error: failed to VdbBlastRunSetGetReadName: "
            "cannot find RunSet ReadDesc"));
        return 0;
    }

    assert(desc.run && desc.run->path && desc.run->acc && desc.spot
        && desc.read);

    if (desc.run->type == btpUndefined) {
        desc.run->type
            = _VdbBlastMgrBTableTypeFromPath(self->mgr, desc.run->path);
        assert(desc.run->type != btpUndefined);
    }
    if (desc.run->type == btpWGS) {
        if (desc.read != 1) {
            STSMSG(1, ("Error: failed to VdbBlastRunSetGetReadName: "
                "Unexpected read='%u' for run '%s', spot='%lu'",
                desc.read, desc.run->path, desc.spot));
            return 0;
        }
        status = _VdbBlastRunGetWgsAccession(
            desc.run, desc.spot, name_buffer, bsize, &need);
        if (status != eVdbBlastNoErr)
        {   need = 0; }
        return need;
    }
    else if (desc.run->type == btpREFSEQ) {
        rc = string_printf(name_buffer, bsize, &num_writ, "%s", desc.run->acc);
        if (rc == 0) {
            S
            need = num_writ;
        }
        else if (GetRCObject(rc) == (enum RCObject)rcBuffer
            && GetRCState(rc) == rcInsufficient)
        {
            size_t size;
            S
            need = string_measure(desc.run->acc, &size) + 1;
        }
    }
    else {
        rc = string_printf(name_buffer, bsize, &num_writ,
            "%s.%lu.%u", desc.run->acc, desc.spot, desc.read);
        if (rc == 0) {
            S
            need = num_writ;
        }
        else if (GetRCObject(rc) == (enum RCObject)rcBuffer
            && GetRCState(rc) == rcInsufficient)
        {
            int i = 0;
            size_t size;
            S
            need = string_measure(desc.run->acc, &size) + 2;
            i = desc.spot;
            while (i > 0) {
                ++need;
                i /= 10;
            }
            i = desc.read;
            while (i > 0) {
                ++need;
                i /= 10;
            }
        }
        else
        {   LOGERR(klogInt, rc, "Unexpecter error in string_printf"); }
    }

    STSMSG(1, ("VdbBlastRunSetGetName = '%.*s'", bsize, name_buffer));
    return need;
}

LIB_EXPORT
uint32_t CC VdbBlastRunSetGetReadId(const VdbBlastRunSet *self,
    const char *name_buffer,
    size_t bsize,
    uint64_t *read_id)
{
    uint32_t status = eVdbBlastNoErr;
    bool found = false;

    uint64_t result = 0;
    char *acc = NULL;
    uint64_t spot = 0;
    uint32_t read = 0;
    uint32_t i = ~0;
    if (self == NULL || name_buffer == NULL || name_buffer[0] == '\0' ||
        bsize == 0 || read_id == 0)
    {   return eVdbBlastErr; }

    {
        size_t n = bsize;
        const char *end = name_buffer + bsize;
        char *dot2 = NULL;
        char *dot1 = memchr(name_buffer, '.', bsize);
        if (dot1 != NULL) {
            if (dot1 == name_buffer)
            {   return eVdbBlastErr; }
            if (dot1 - name_buffer + 1 >= bsize)
            {   return eVdbBlastErr; }
            n -= (dot1 - name_buffer + 1);
            dot2 = memchr(dot1 + 1, '.', n);
            if (dot2 != NULL) {
                if (dot2 - name_buffer + 1 >= bsize)
                {   return eVdbBlastErr; }
                acc = string_dup(name_buffer, dot1 - name_buffer + 1);
                if (acc == NULL)
                {   return eVdbBlastMemErr; }
                acc[dot1 - name_buffer] = '\0';
                while (++dot1 < dot2) {
                    char c = *dot1;
                    if (c < '0' || c > '9') {
                        S
                        status = eVdbBlastErr;
                        break;
                    }
                    spot = spot * 10 + c - '0';
                }
                while (status == eVdbBlastNoErr && ++dot2 < end) {
                    char c = *dot2;
                    if (c < '0' || c > '9') {
                        S
                        status = eVdbBlastErr;
                        break;
                    }
                    read = read * 10 + c - '0';
                }
            }
            else {
                acc = malloc(bsize + 1);
                if (acc == NULL)
                {   return eVdbBlastMemErr; }
                string_copy(acc, bsize + 1, name_buffer, bsize);
                acc[bsize] = '\0';
            }
        }
        else {
            acc = malloc(bsize + 1);
            if (acc == NULL)
            {   return eVdbBlastMemErr; }
            string_copy(acc, bsize + 1, name_buffer, bsize);
            acc[bsize] = '\0';
        }
    }

    for (i = 0; i < self->runs.krun && status == eVdbBlastNoErr; ++i) {
        uint64_t id = ~0;
        VdbBlastRun *run = self->runs.run + i;
        size_t size;
        assert(run && run->acc);
        if (string_measure(run->acc, &size) == string_measure(acc, &size)) {
            if (strcmp(run->acc, acc) == 0) {
                status = _VdbBlastRunGetReadId(run, acc, spot, read, &id);
                if (status == eVdbBlastNoErr) {
                    *read_id = result + id;
                    found = true;
                }
                break;
            }
        }
        else if ((string_measure(run->acc, &size) < string_measure(acc, &size))
            && (run->type == btpWGS)
            && (memcmp(run->acc, acc, string_measure(run->acc, &size)) == 0))
        {
            status = _VdbBlastRunGetReadId(run, acc, spot, read, &id);
            if (status == eVdbBlastNoErr) {
                *read_id = result + id;
                found = true;
            }
            break;
        }
        result += _VdbBlastRunGetNumSequences(run, &status);
        if (status != eVdbBlastNoErr) {
            if (status == eVdbBlastTooExpensive) {
                status = eVdbBlastNoErr;
            }
            else {
                break;
            }
        }
    }

    if (status == eVdbBlastNoErr && !found) {
        S
        status = eVdbBlastErr;
    }

    free (acc);
    acc = NULL;

    return status;
}

/* TODO: make sure
         ReadLength is correct when there are multiple reads in the same spot */
LIB_EXPORT
uint64_t CC VdbBlastRunSetGetReadLength(const VdbBlastRunSet *self,
    uint64_t read_id)
{
    rc_t rc = 0;
    const VCursor *curs = NULL;
    uint32_t col_idx = 0;
    char buffer[84] = "";
    uint32_t row_len = 0;
    ReadDesc desc;
    uint32_t status = eVdbBlastErr;

    if (self == NULL) {
        STSMSG(1, ("VdbBlastRunSetGetReadLength(self=NULL)"));
        return 0;
    }

    status = _RunSetFindReadDesc(&self->runs, read_id, &desc);
    if (status != eVdbBlastNoErr) {
        STSMSG(1, ("Error: failed to VdbBlastRunSetGetReadLength: "
            "cannot find RunSet ReadDesc"));
        return 0;
    }
    assert(desc.run && desc.spot && desc.run->path);

    _VdbBlastRunSetBeingRead(self);

    if (rc == 0) {
        rc = _VTableMakeCursor(desc.run->tbl, &curs, &col_idx, "READ");
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc, "Error in _VTableMakeCursor"
                "$(path), READ)", "path=%s", desc.run->path));
        }
    }
    if (rc == 0) {
        rc = VCursorReadDirect
            (curs, desc.spot, col_idx, 8, buffer, sizeof buffer, &row_len);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc, "Error in VCursorReadDirect"
                "$(path), READ, spot=$(spot))",
                "path=%s,spot=%ld", desc.run->path, desc.spot));
        }
    }
    VCursorRelease(curs);
    curs = NULL;
    if (rc == 0) {
        STSMSG(1, ("VdbBlastRunSetGetReadLength = %ld", row_len));
        return row_len;
    }
    else {
        STSMSG(1, ("Error: failed to VdbBlastRunSetGetReadLength"));
        return 0;
    }
}

static
uint64_t _VdbBlastRunSet2naRead(const VdbBlastRunSet *self,
    uint32_t *status,
    uint64_t *read_id,
    size_t *starting_base,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint64_t n = 0; 
    rc_t rc = 0;
    assert(self && status);
    rc = KLockAcquire(self->core2na.mutex);
    if (rc != 0) {
        LOGERR(klogInt, rc, "Error in KLockAcquire");
    }
    else {
        n = _Core2naRead((Core2na*)&self->core2na, &self->runs,
            status, read_id, starting_base, buffer, buffer_size);
        if (n == 0 && self->core2na.eos) {
            *read_id = ~0;
        }
        rc = KLockUnlock(self->core2na.mutex);
        if (rc != 0) {
            LOGERR(klogInt, rc, "Error in KLockUnlock");
        }
    }
    if (rc) {
        *status = eVdbBlastErr;
    }
    if (*status == eVdbBlastNoErr) {
        if (read_id != NULL && starting_base != NULL) {
            STSMSG(3, (
                "VdbBlast2naReaderRead(read_id=%ld, starting_base=%ld) = %ld",
                *read_id, *starting_base, n));
        }
        else {
            STSMSG(2, ("VdbBlast2naReaderRead = %ld", n));
        }
    }
    else {
        if (read_id != NULL && starting_base != NULL) {
            STSMSG(1, ("Error: failed to "
                "VdbBlast2naReaderRead(read_id=%ld, starting_base=%ld)", n));
        }
        else {
            STSMSG(1, ("Error: failed to VdbBlast2naReaderRead"));
        }
    }
    return n;
}

/******************************************************************************/
static const char VDB_BLAST_2NA_READER[] = "VdbBlast2naReader";

struct VdbBlast2naReader {
    KRefcount refcount;
    VdbBlastRunSet *set;
    Data2na data;
};

static
VdbBlast2naReader *_VdbBlastRunSetMake2naReader(VdbBlastRunSet *self,
    uint32_t *status,
    uint64_t initial_read_id,
    Core2na *core2na)
{
    VdbBlast2naReader *obj = NULL;
    assert(self && status);
    if (core2na == NULL) {
        core2na = &self->core2na;
    }
    else {
        core2na->initial_read_id = initial_read_id;
    }
    if (!core2na->hasReader) {
        *status = _Core2naOpen1stRead(core2na, &self->runs, initial_read_id);
        if (*status != eVdbBlastNoErr) {
            STSMSG(1, ("Error: failed to create VdbBlast2naReader: "
                "cannot open the first read"));
            return NULL;
        }
        _VdbBlastRunSetBeingRead(self);
        core2na->initial_read_id = initial_read_id;
        core2na->hasReader = true;
    }
    else if (core2na->initial_read_id != initial_read_id) {
        STSMSG(1, ("Error: failed to create VdbBlast2naReader"
            "(initial_read_id=%ld): allowed initial_read_id=%ld",
            initial_read_id, core2na->initial_read_id));
        *status = eVdbBlastErr;
        return NULL;
    }
    obj = calloc(1, sizeof *obj);
    if (obj == NULL) {
        *status = eVdbBlastMemErr;
        return NULL;
    }

    obj->set = VdbBlastRunSetAddRef((VdbBlastRunSet*)self);

    KRefcountInit(&obj->refcount, 1,
        VDB_BLAST_2NA_READER, __func__, "2naReader");

    *status = eVdbBlastNoErr;

    return obj;
}

LIB_EXPORT
VdbBlast2naReader* CC VdbBlastRunSetMake2naReader(const VdbBlastRunSet *self,
    uint32_t *status,
    uint64_t initial_read_id)
{
    VdbBlast2naReader *obj = NULL;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self) {
        rc_t rc = KLockAcquire(self->core2na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockAcquire"); }
        else {
            obj = _VdbBlastRunSetMake2naReader
                ((VdbBlastRunSet*)self, status, initial_read_id, NULL);
            rc = KLockUnlock(self->core2na.mutex);
            if (rc != 0) {
                LOGERR(klogInt, rc, "Error in KLockUnlock");
                VdbBlast2naReaderRelease(obj);
                obj = NULL;
            }
        }
        if (rc != 0) {
            *status = eVdbBlastErr;
        }
    }
    else {
        *status = eVdbBlastErr;
    }

    if (obj != NULL) {
        STSMSG(1, (
            "Created VdbBlast2naReader(initial_read_id=%ld)", initial_read_id));
    }
    else {
        STSMSG(1, ("Error: failed to create "
            "VdbBlast2naReader(initial_read_id=%ld)", initial_read_id));
    }

    return obj;
}

LIB_EXPORT
VdbBlast2naReader* CC VdbBlast2naReaderAddRef(VdbBlast2naReader *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlast2naReaderAddRef(NULL)"));
        return self;
    }

    if (KRefcountAdd(&self->refcount, VDB_BLAST_2NA_READER)
        == krefOkay)
    {
        STSMSG(1, ("VdbBlast2naReaderAddRef"));
        return self;
    }

    STSMSG(1, ("Error: failed to VdbBlast2naReaderAddRef"));
    return NULL;
}

static
void _VdbBlast2naReaderWhack(VdbBlast2naReader *self)
{
    if (self == NULL)
    {   return; }

    if (self->set != NULL) {
        STSMSG(1, ("Deleting VdbBlast2naReader(initial_read_id=%ld)",
            self->set->core2na.initial_read_id));
    }
    else {
        STSMSG(1, ("Deleting VdbBlast2naReader(self->set=NULL)",
            self->set->core2na.initial_read_id));
    }

    VBlobRelease(self->data.blob);
    VdbBlastRunSetRelease(self->set);

    memset(self, 0, sizeof *self);

    free(self);
}

LIB_EXPORT
void CC VdbBlast2naReaderRelease(VdbBlast2naReader *self)
{
    if (self == NULL)
    {   return; }

    STSMSG(1, ("VdbBlast2naReaderRelease"));
    if (KRefcountDrop(&self->refcount, VDB_BLAST_2NA_READER) != krefWhack)
    {   return; }

    _VdbBlast2naReaderWhack(self);
}

LIB_EXPORT
uint64_t CC _VdbBlast2naReaderRead(const VdbBlast2naReader *self,
    uint32_t *status,
    uint64_t *read_id,
    size_t *starting_base,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self == NULL) {
        *status = eVdbBlastErr;
        return 0;
    }

    return _VdbBlastRunSet2naRead(self->set,
        status, read_id, starting_base, buffer, buffer_size);
}

LIB_EXPORT
uint64_t CC VdbBlast2naReaderRead(const VdbBlast2naReader *self,
    uint32_t *status,
    uint64_t *read_id,
    uint8_t *buffer,
    size_t buffer_size)
{
    size_t starting_base = 0;

    return _VdbBlast2naReaderRead(self,
        status, read_id, &starting_base, buffer, buffer_size);
}

LIB_EXPORT
uint32_t CC VdbBlast2naReaderData(VdbBlast2naReader *self,
    uint32_t *status,
    Packed2naRead *buffer,
    uint32_t buffer_length)
{
    bool verbose = false;
    uint32_t n = 0;
    rc_t rc = 0;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    *status = eVdbBlastErr;

    if (self == NULL)
    {   return 0; }

    if ((buffer_length != 0 && buffer == NULL) || (buffer_length == 0))
    {   return 0; }

    *status = eVdbBlastNoErr;

    assert(self->set);

    rc = KLockAcquire(self->set->core2na.mutex);
    if (rc != 0)
    {   LOGERR(klogInt, rc, "Error in KLockAcquire"); }
    if (0 && verbose) {
        char b[256];
        string_printf(b, sizeof b, NULL, "KLockAcquire(%p)\n",
            self->set->core2na.mutex);
            assert(!rc);
        fprintf(stderr, "%s", b);
        fflush(stderr);
    }
    if (rc == 0) {
        n = _Core2naData((Core2na*)&self->set->core2na, &self->data,
            &self->set->runs, status, buffer, buffer_length);
        S
        if (n > 0 && verbose)
        {   _Packed2naReadPrint(buffer, self->data.blob); }
        if (0 && verbose) {
            char b[256];
            string_printf(b, sizeof b, NULL, "KLockUnlock(%p)\n",
                self->set->core2na.mutex);
            assert(!rc);
            fprintf(stderr, "%s", b);
            fflush(stderr);
        }
        rc = KLockUnlock(self->set->core2na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockUnlock"); }
    }
    if (rc)
    {   *status = eVdbBlastErr; }

    S
    return n;
}

static uint64_t _VdbBlastRunScan(const VdbBlastRun *self,
    uint64_t (*cmp)
        (uint64_t cand, uint64_t champ, int64_t minRead, bool *done),
    uint64_t minRead, uint64_t start, uint32_t *status)
{
    uint64_t res = start;
    uint64_t bad = start;
    uint64_t spot = 0;
    bool done = false;
    rc_t rc = 0;
    const VCursor *curs = NULL;
    uint32_t idx = 0;
    assert(self && status &&
        (self->rd.spotCount || self->rd.readType ||
         self->rd.nReads    || self->rd.nBioReads));
    rc = _VTableMakeCursor(self->tbl, &curs, &idx, "READ_LEN");
    if (rc != 0) {
        return bad;
    }
    for (spot = 1;
        spot <= self->rd.spotCount && *status == eVdbBlastNoErr && !done;
        ++spot)
    {
        uint32_t elem_bits, elem_off, elem_cnt;
        const void *base = NULL;
        rc = VCursorCellDataDirect(curs, spot, idx,
            &elem_bits, &base, &elem_off, &elem_cnt);
        if (rc != 0) {
            *status = eVdbBlastErr;
            PLOGMSG(klogInfo, (klogInfo,
                "$(f): Cannot '$(name)' CellDataDirect",
                "f=%s,name=%s", __func__, "READ_LEN"));
            res = bad;
            break;
        }
        else if (elem_off != 0 || elem_bits != 32) {
            *status = eVdbBlastErr;
            PLOGERR(klogInt, (klogInt, rc,
                "Bad VCursorCellDataDirect(READ_LEN) result: "
                "boff=$(elem_off), elem_bits=$(elem_bits)",
                "elem_off=%u,elem_bits=%u", elem_off, elem_bits));
            res = bad;
            break;
        }
        else {
            uint8_t read = 0;
            const uint32_t *readLen = base;
            assert(self->rd.readType && self->rd.readLen);
            assert(self->rd.nReads == elem_cnt);
            for (read = 0; read < self->rd.nReads; ++read) {
                if (self->rd.readType[read] & SRA_READ_TYPE_BIOLOGICAL) {
                   res = cmp(readLen[read], res, minRead, &done);
                }
            }
        }
    }
    RELEASE(VCursor, curs);
    return res;
}

/*static
uint64_t CC _VdbBlastRunSetGetSomeSeqLen2(const VdbBlastRunSet *self, bool max)
{
    uint64_t num = max ? 0 : ~0;
    bool done = false;
    Packed2naRead buffer[1024];
    uint32_t status = eVdbBlastNoErr;
    VdbBlast2naReader* r = NULL;

    Core2na core2na;
    memset(&core2na, 0, sizeof core2na);

    _VdbBlastRunSetBeingRead(self);

    r = _VdbBlastRunSetMake2naReader(
        (VdbBlastRunSet*)self, &status, 0, &core2na);
    while (status == eVdbBlastNoErr) {
        uint32_t i = 0;
        uint32_t n = _Core2naData(&core2na, &r->data,
             &self->runs, &status, buffer, sizeof buffer / sizeof buffer[0]);
        if (status != eVdbBlastNoErr) {
            num = max ? 0 : ~0;
            break;
        }
        if (n == 0) {
            break;
        }
        for (i = 0; i < n; ++i) {
            if (max) {
                if (num < buffer[i].length_in_bases) {
                    num = buffer[i].length_in_bases;
                }
            }
            else {
                if (num > buffer[i].length_in_bases) {
                    num = buffer[i].length_in_bases;
                    if (num > 0 && num - 1 == r->set->core2na.min_read_length) {
                        done = true;
                        break;
                    }
                }
            }
        }
        if (done) {
            break;
        }
    }
    _Core2naFini(&core2na);
    VdbBlast2naReaderRelease(r);
    return num;
}*/

static
uint64_t CC Min(uint64_t cand,
    uint64_t champ, int64_t minRead, bool *done)
{
    assert(done);
    if (minRead >= 0) {
        if (cand < minRead) {
            return champ;
        }
        else if (cand == minRead) {
            *done = true;
            return minRead;
        }
    } 
    return cand < champ ? cand : champ;
}

static
uint64_t CC Max(uint64_t cand,
    uint64_t champ, int64_t minRead, bool *done)
{
    return cand > champ ? cand : champ;
}

/*static
uint64_t CC Sum(uint64_t cand, uint64_t champ, int64_t minRead, bool *done)
{
    return cand + champ;
}*/

LIB_EXPORT
uint64_t CC VdbBlastRunSetGetMinSeqLen(const VdbBlastRunSet *self)
{
    if (self->minSeqLen == ~0) {
        bool empty = true;
        uint32_t status = eVdbBlastNoErr;
        uint64_t res = ~0;
        uint32_t i = 0;
        _VdbBlastRunSetBeingRead(self);
        for (i = 0; i < self->runs.krun; ++i) {
            VdbBlastRun *run = &self->runs.run[i];
            assert(run);
            if (run->type == btpREFSEQ) {
                uint64_t cand = _VdbBlastRunGetLengthApprox(run, &status);
                if (status != eVdbBlastNoErr) {
                    S
                    return ~0;
                }
                if (cand < res && cand >= self->core2na.min_read_length) {
                    res = cand;
                }
            }
            else {
                status = _VdbBlastRunFillRunDesc(run);
                if (status != eVdbBlastNoErr) {
                    S
                    return ~0;
                }
                if (!run->rd.varReadLen) {
                    uint8_t read = 0;
                    assert(run->rd.readType && run->rd.readLen);
                    for (read = 0; read < run->rd.nReads; ++read) {
                        if (run->rd.readType[i] & SRA_READ_TYPE_BIOLOGICAL) {
                            if (run->rd.readLen[i]
                                == self->core2na.min_read_length)
                            {
                                ((VdbBlastRunSet*)self)->minSeqLen
                                    = self->core2na.min_read_length;
                                return self->minSeqLen;
                            }
                            else if (run->rd.readLen[i]
                                    > self->core2na.min_read_length
                                && run->rd.readLen[i] < res)
                            {
                                res = run->rd.readLen[i];
                                empty = false;
                            }
                        }
                    }
                }
                else {
                    res = _VdbBlastRunScan(
                        run, Min, self->core2na.min_read_length, res, &status);
                    if (status != eVdbBlastNoErr) {
                        S
                        return ~0;
                    }
                }
            }
        }
        if (empty && res == ~0) {
            res = 0;
        }
        ((VdbBlastRunSet*)self)->minSeqLen = res;
    }
    return self->minSeqLen;
}
LIB_EXPORT
uint64_t CC VdbBlastRunSetGetMaxSeqLen(const VdbBlastRunSet *self)
{
    if (self->maxSeqLen == ~0) {
        uint32_t status = eVdbBlastNoErr;
        uint64_t res = 0;
        uint32_t i = 0;
        _VdbBlastRunSetBeingRead(self);
        for (i = 0; i < self->runs.krun; ++i) {
            VdbBlastRun *run = &self->runs.run[i];
            assert(run);
            if (run->type == btpREFSEQ) {
                uint64_t cand = _VdbBlastRunGetLengthApprox(run, &status);
                if (status != eVdbBlastNoErr) {
                    S
                    return ~0;
                }
                if (cand > res) {
                    res = cand;
                }
            }
            else {
                status = _VdbBlastRunFillRunDesc(run);
                if (status != eVdbBlastNoErr) {
                    S
                    return ~0;
                }
                if (!run->rd.varReadLen) {
                    uint8_t read = 0;
                    assert(run->rd.readType && run->rd.readLen);
                    for (read = 0; read < run->rd.nReads; ++read) {
                        if (run->rd.readType[i] & SRA_READ_TYPE_BIOLOGICAL) {
                            if (run->rd.readLen[i] > res) {
                                res = run->rd.readLen[i];
                            }
                        }
                    }
                }
                else {
                    res = _VdbBlastRunScan(
                        run, Max, -1, res, &status);
                    if (status != eVdbBlastNoErr) {
                        S
                        return ~0;
                    }
                }
            }
        }
        ((VdbBlastRunSet*)self)->maxSeqLen = res;
    }
    return self->maxSeqLen;
}

/******************************************************************************/
static const char VDB_BLAST_4NA_READER[] = "VdbBlast4naReader";

struct VdbBlast4naReader {
    KRefcount refcount;
    VdbBlastRunSet *set;
};

LIB_EXPORT
VdbBlast4naReader* CC VdbBlastRunSetMake4naReader(const VdbBlastRunSet *self,
    uint32_t *status)
{
    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL) {
        status = &dummy;
    }

    if (self) {
        VdbBlast4naReader *obj = calloc(1, sizeof *obj);
        if (obj == NULL) {
            *status = eVdbBlastMemErr;
            return NULL;
        }

        obj->set = VdbBlastRunSetAddRef((VdbBlastRunSet*)self);

        KRefcountInit(&obj->refcount, 1,
            VDB_BLAST_4NA_READER, __func__, "4naReader");

        _VdbBlastRunSetBeingRead(self);

        *status = eVdbBlastNoErr;

        STSMSG(1, ("Created VdbBlast4naReader"));

        return obj;
    }
    else {
        *status = eVdbBlastErr;

        STSMSG(1, ("VdbBlastRunSetMake4naReader(self=NULL)"));

        return NULL;
    }
}

LIB_EXPORT
VdbBlast4naReader* CC VdbBlast4naReaderAddRef(VdbBlast4naReader *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlast4naReaderAddRef(NULL)"));
        return self;
    }

    if (KRefcountAdd(&self->refcount, VDB_BLAST_4NA_READER)
        == krefOkay)
    {
        STSMSG(1, ("VdbBlast4naReaderAddRef"));
        return self;
    }

    STSMSG(1, ("Error: failed to VdbBlast4naReaderAddRef"));
    return NULL;
}

LIB_EXPORT
void CC VdbBlast4naReaderRelease(VdbBlast4naReader *self)
{
    if (self == NULL)
    {   return; }

    STSMSG(1, ("VdbBlast4naReaderRelease"));
    if (KRefcountDrop(&self->refcount, VDB_BLAST_4NA_READER) != krefWhack)
    {   return; }

    STSMSG(1, ("Deleting VdbBlast4naReader"));

    VdbBlastRunSetRelease(self->set);

    memset(self, 0, sizeof *self);
    free(self);
}

LIB_EXPORT
size_t CC VdbBlast4naReaderRead(const VdbBlast4naReader *self,
    uint32_t *status,
    uint64_t read_id,
    size_t starting_base,
    uint8_t *buffer,
    size_t buffer_length)
{
    size_t n = 0;
    rc_t rc = 0;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self == NULL) {
        S
        *status = eVdbBlastErr;
        return 0;
    }

    assert(self->set);

    rc = KLockAcquire(self->set->core4na.mutex);
    if (rc != 0)
    {   LOGERR(klogInt, rc, "Error in KLockAcquire"); }
    else {
        n = _Core4naRead(&self->set->core4na, &self->set->runs,
            status, read_id, starting_base, buffer, buffer_length);
        rc = KLockUnlock(self->set->core4na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockUnlock"); }
    }
    if (rc != 0)
    {   *status = eVdbBlastErr; }

    if (*status == eVdbBlastNoErr) {
        STSMSG(3, (
            "VdbBlast4naReaderRead(read_id=%ld, starting_base=%ld) = %ld",
            read_id, starting_base, n));
    }
    else {
        STSMSG(2, ("Error: failed to "
            "VdbBlast4naReaderRead(read_id=%ld, starting_base=%ld)",
            read_id, starting_base));
    }

    return n;
}

LIB_EXPORT
const uint8_t* CC VdbBlast4naReaderData(const VdbBlast4naReader *self,
    uint32_t *status,
    uint64_t read_id,
    size_t *length)
{
    const uint8_t *d = NULL;
    rc_t rc = 0;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    if (self == NULL || length == NULL) {
        if (self == NULL)
        {   STSMSG(1, ("VdbBlast4naReaderData(self=NULL)")); }
        if (length == NULL)
        {   STSMSG(1, ("VdbBlast4naReaderData(length=NULL)")); }
        *status = eVdbBlastErr;
        return 0;
    }

    assert(self->set);

    rc = KLockAcquire(self->set->core4na.mutex);
    if (rc != 0)
    {   LOGERR(klogInt, rc, "Error in KLockAcquire"); }
    else {
        d = _Core4naData(( Core4na*)&self->set->core4na,
            &self->set->runs, status, read_id, length);
        rc = KLockUnlock(self->set->core4na.mutex);
        if (rc != 0)
        {   LOGERR(klogInt, rc, "Error in KLockUnlock"); }
    }
    if (rc)
    {   *status = eVdbBlastErr; }

    if (*status == eVdbBlastNoErr) {
        STSMSG(3, ("VdbBlast4naReaderData(read_id=%ld, length=%ld)",
            read_id, *length));
    }
    else {
        STSMSG(1, ("Error: failed to VdbBlast4naReaderData(read_id=%ld)",
            read_id));
    }

    return d;
}

/******************************************************************************/
static const char VDB_BLAST_AA_READER[] = "VdbBlastStdaaReader";

struct VdbBlastStdaaReader {
    KRefcount refcount;
};

LIB_EXPORT
VdbBlastStdaaReader* CC VdbBlastRunSetMakeStdaaReader(
    const VdbBlastRunSet *self,
    uint32_t *status)
{
    VdbBlastStdaaReader *obj = NULL;

    uint32_t dummy = eVdbBlastNoErr;
    if (status == NULL)
    {   status = &dummy; }

    obj = calloc(1, sizeof *obj);
    if (obj == NULL) {
        *status = eVdbBlastMemErr;
        STSMSG(1, ("Error: failed to create VdbBlastStdaaReader"));
        return NULL;
    }

    KRefcountInit(&obj->refcount, 1, VDB_BLAST_AA_READER, __func__, "aaReader");

    _VdbBlastRunSetBeingRead(self);

    *status = eVdbBlastNoErr;

    STSMSG(1, ("Created VdbBlastStdaaReader"));

    return obj;
}

LIB_EXPORT
VdbBlastStdaaReader* CC VdbBlastStdaaReaderAddRef(
    VdbBlastStdaaReader *self)
{
    if (self == NULL) {
        STSMSG(1, ("VdbBlastStdaaReaderAddRef(NULL)"));
        return self;
    }

    if (KRefcountAdd(&self->refcount, VDB_BLAST_AA_READER) == krefOkay) {
        STSMSG(1, ("VdbBlastStdaaReaderAddRef"));
        return self;
    }

    STSMSG(1, ("Error: failed to VdbBlastStdaaReaderAddRef"));
    return NULL;
}

LIB_EXPORT
void CC VdbBlastStdaaReaderRelease(VdbBlastStdaaReader *self)
{
    if (self == NULL)
    {   return; }

    STSMSG(1, ("VdbBlastStdaaReaderRelease"));
    if (KRefcountDrop(&self->refcount, VDB_BLAST_AA_READER) != krefWhack)
    {   return; }

    STSMSG(1, ("Deleting VdbBlastStdaaReader"));
    free(self);
    memset(self, 0, sizeof *self);
}

LIB_EXPORT
size_t CC VdbBlastStdaaReaderRead(const VdbBlastStdaaReader *self,
    uint32_t *status,
    uint64_t pig,
    uint8_t *buffer,
    size_t buffer_length)
{   return _NotImplemented(__func__); }

LIB_EXPORT
const uint8_t* CC VdbBlastStdaaReaderData(const VdbBlastStdaaReader *self,
    uint32_t *status,
    uint64_t pig,
    size_t *length)
{   return _NotImplementedP(__func__); }

/* EOF */
