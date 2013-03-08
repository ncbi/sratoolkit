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

#include "prefetch.vers.h"

#include <kapp/main.h> /* KAppVersion */

#include <vdb/manager.h> /* VDBManager */
#include <vdb/database.h> /* VDatabase */
#include <vdb/dependencies.h> /* VDBDependencies */

#include <vfs/manager.h> /* VFSManager */
#include <vfs/resolver.h> /* VResolver */
#include <vfs/path.h> /* VPath */

#include <kfg/config.h> /* KConfig */

#include <kfs/file.h> /* KFile */

#include <klib/printf.h> /* string_printf */
#include <klib/text.h> /* String */
#include <klib/status.h> /* STSMSG */
#include <klib/log.h> /* PLOGERR */
#include <klib/out.h> /* KOutMsg */
#include <klib/rc.h>

#include <assert.h>
#include <stdlib.h> /* free */
#include <string.h> /* memset */
#include <time.h> /* time */

#include <stdio.h> /* printf */

#define DISP_RC(rc, err) (void)((rc == 0) ? 0 : LOGERR(klogInt, rc, err))

#define DISP_RC2(rc, name, msg) (void)((rc == 0) ? 0 : \
    PLOGERR(klogInt, (klogInt,rc, "$(msg): $(name)","msg=%s,name=%s",msg,name)))

#define RELEASE(type, obj) do { rc_t rc2 = type##Release(obj); \
    if (rc2 != 0 && rc == 0) { rc = rc2; } obj = NULL; } while (false)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define STS_TOP 0
#define STS_INFO 1
#define STS_DBG 2
#define STS_FIN 3

#define rcResolver   rcTree
static bool NotFoundByResolver(rc_t rc) {
    if (GetRCModule(rc) == rcVFS) {
        if (GetRCTarget(rc) == rcResolver) {
            if (GetRCContext(rc) == rcResolving) {
                if (GetRCState(rc) == rcNotFound) {
                    return true;
                }
            }
        }
    }
    return false;
}
static rc_t VResolverResolve(VResolver *self, const char *accession,
    const String **local, const String **remote, const String **cache,
    const KFile **file)
{
    rc_t rc = 0;
    VPath* vaccession = NULL;
    const VPath* vremote = NULL;
    if (self == NULL) {
        return RC(rcVFS, rcResolver, rcResolving, rcSelf, rcNull);
    }
    if (accession == NULL) {
        return RC(rcVFS, rcResolver, rcResolving, rcParam, rcNull);
    }
    if (rc == 0) {
        rc = VPathMake(&vaccession, accession);
        DISP_RC2(rc, "VPathMake", accession);
    }
    if (rc == 0 && local != NULL) {
        const VPath* vpath = NULL;
        rc = VResolverLocal(self, vaccession, &vpath);
        if (rc == 0) {
            rc = VPathMakeString(vpath, local);
            DISP_RC2(rc, "VPathMakeString(VResolverLocal)", accession);
        }
        else if (NotFoundByResolver(rc)) {
            rc = 0;
        }
        else {
            DISP_RC2(rc, "VResolverLocal", accession);
        }
        RELEASE(VPath, vpath);
    }
    if (rc == 0 && remote != NULL) {
        rc = VResolverRemote(self, vaccession, &vremote, file);
        if (rc == 0) {
            rc = VPathMakeString(vremote, remote);
            DISP_RC2(rc, "VPathMakeString(VResolverRemote)", accession);
        }
        else if (NotFoundByResolver(rc)) {
            PLOGERR(klogErr, (klogErr, rc, "$(acc) cannot be found. "
                "Was it released?", "acc=%s", accession));
        }
        else {
            DISP_RC2(rc, "Cannot resolve remote", accession);
        }
    }
    if (rc == 0 && cache != NULL) {
        const VPath* vpath = NULL;
        uint64_t file_size = 0;
        rc = VResolverCache(self, vremote, &vpath, file_size);
        if (rc == 0) {
            rc = VPathMakeString(vpath, cache);
            DISP_RC2(rc, "VPathMakeString(VResolverCache)", accession);
        }
        else {
            DISP_RC2(rc, "Cannot resolve cache", accession);
        }
        RELEASE(VPath, vpath);
    }
    RELEASE(VPath, vremote);
    RELEASE(VPath, vaccession);
    return rc;
}

typedef enum {
    eForceNo, /* do not download found and complete objects */
    eForceYes,/* force download of found and complete objects */
    eForceYES /* force download; ignore lockes */
} Force;
typedef struct {
    Args *args;
    bool check_all;
    Force force;
    const VDBManager *mgr;
    KDirectory *dir;
    VResolver *resolver;

    void *buffer;
    size_t bsize;
} Main;
static rc_t StringRelease(const String *self) {
    free((String*)self);
    return 0;
}

typedef struct {
    const String *local;
    const String *remote;
    const String *cache;
    const KFile *file;

    /* path to the resolved object : either local or cache:
    should not be released */
    const String *path;

    const char *acc; /* do not release */
} Resolved;
rc_t ResolvedInit(Resolved *self, const char *acc, VResolver *resolver) {
    rc_t rc = 0;

    assert(self);

    memset(self, 0, sizeof *self);

    self->acc = acc;

    rc = VResolverResolve(resolver,
        acc, &self->local, &self->remote, &self->cache, &self->file);

    STSMSG(STS_DBG, ("Resolve(%s) = %R:", acc, rc));
    STSMSG(STS_DBG, ("local(%s)", self->local ? self->local->addr : "NULL"));
    STSMSG(STS_DBG, ("remote(%s)", self->remote ? self->remote->addr : "NULL"));
    STSMSG(STS_DBG, ("cache(%s)", self->cache ? self->cache->addr : "NULL"));

    if (rc == 0) {
        if (self->local == NULL
            && (self->cache == NULL ||
                self->remote == NULL || self->file == NULL))
        {
            rc = RC(rcExe, rcPath, rcValidating, rcParam, rcNull);
            PLOGERR(klogInt, (klogInt, rc,
                "bad VResolverResolve($(acc)) result", "acc=%s", acc));
        }
    }

    return rc;
}

static rc_t ResolvedFini(Resolved *self) {
    rc_t rc = 0;

    assert(self);

    RELEASE(String, self->local);
    RELEASE(String, self->remote);
    RELEASE(String, self->cache);

    RELEASE(KFile, self->file);

    memset(self, 0, sizeof *self);

    return rc;
}

/** isLocal is set to true when the object is found locally.
    i.e. does not need need not be [re]downloaded */
static rc_t ResolvedLocal(const Resolved *self,
    const KDirectory *dir, bool *isLocal, Force force)
{
    rc_t rc = 0;
    uint64_t sRemote = 0;
    uint64_t sLocal = 0;
    const KFile *local = NULL;

    assert(isLocal && self);

    *isLocal = false;

    if (self->local == NULL) {
        return 0;
    }

    if (KDirectoryPathType(dir, self->local->addr) != kptFile) {
        if (force == eForceNo) {
            STSMSG(STS_TOP,
                ("%s (not a file) is found locally: consider it complete",
                 self->local->addr));
            *isLocal = true;
        }
        else {
            STSMSG(STS_TOP,
                ("%s (not a file) is found locally and will be redownloaded",
                 self->local->addr));
        }
        return 0;
    }

    if (rc == 0) {
        rc = KFileSize(self->file, &sRemote);
        DISP_RC2(rc, "KFileSize(remote)", self->acc);
    }

    if (rc == 0) {
        rc = KDirectoryOpenFileRead(dir, &local, self->local->addr);
        DISP_RC2(rc, "KDirectoryOpenFileRead", self->local->addr);
    }

    if (rc == 0) {
        rc = KFileSize(local, &sLocal);
        DISP_RC2(rc, "KFileSize", self->local->addr);
    }

    if (rc == 0) {
        if (sRemote == sLocal) {
            if (force == eForceNo) {
                *isLocal = true;
                STSMSG(STS_INFO, ("%s (%lu) is found and is complete",
                    self->local->addr, sLocal));
            }
            else {
                STSMSG(STS_INFO, ("%s (%lu) is found and will be redownloaded",
                    self->local->addr, sLocal));
            }
        }
        else {
            STSMSG(STS_TOP, ("%s (%lu) is incomplete. Expected size is %lu. "
                "It will be re-downloaded",
                self->local->addr, sLocal, sRemote));
        }
    }

    RELEASE(KFile, local);

    return rc;
}

static rc_t _KDirectoryMkTmpPrefix(const KDirectory *self,
    const String *prefix, char *out, size_t sz)
{
    size_t num_writ = 0;
    rc_t rc = string_printf(out, sz, &num_writ,
        "%.*s.tmp", prefix->size, prefix->addr);
    if (rc != 0) {
        DISP_RC2(rc, "string_printf(tmp)", prefix->addr);
        return rc;
    }

    if (num_writ > sz) {
        rc = RC(rcExe, rcFile, rcCopying, rcBuffer, rcInsufficient);
        PLOGERR(klogInt, (klogInt, rc,
            "bad string_printf($(s).tmp) result", "s=%s", prefix->addr));
        return rc;
    }

    return rc;
}

static rc_t _KDirectoryMkTmpName(const KDirectory *self,
    const String *prefix, char *out, size_t sz)
{
    rc_t rc = 0;
    int i = 0;

    assert(prefix);

    while (rc == 0) {
        size_t num_writ = 0;
        rc = string_printf(out, sz, &num_writ,
            "%.*s.tmp.%d.tmp", prefix->size, prefix->addr, rand() % 100000);
        if (rc != 0) {
            DISP_RC2(rc, "string_printf(tmp.rand)", prefix->addr);
            break;
        }

        if (num_writ > sz) {
            rc = RC(rcExe, rcFile, rcCopying, rcBuffer, rcInsufficient);
            PLOGERR(klogInt, (klogInt, rc,
                "bad string_printf($(s).tmp.rand) result",
                "s=%s", prefix->addr));
            return rc;
        }
        if (KDirectoryPathType(self, out) == kptNotFound) {
            break;
        }
        if (++i > 999) {
            rc = RC(rcExe, rcFile, rcCopying, rcName, rcUndefined);
            PLOGERR(klogInt, (klogInt, rc,
                "cannot generate unique tmp file name for $(name)", "name=%s",
                prefix->addr));
            return rc;
        }
    }

    return rc;
}

static rc_t _KDirectoryMkLockName(const KDirectory *self,
    const String *prefix, char *out, size_t sz)
{
    rc_t rc = 0;
    size_t num_writ = 0;

    assert(prefix);

    rc = string_printf(out, sz, &num_writ,
        "%.*s.lock", prefix->size, prefix->addr);
    DISP_RC2(rc, "string_printf(lock)", prefix->addr);

    if (rc == 0 && num_writ > sz) {
        rc = RC(rcExe, rcFile, rcCopying, rcBuffer, rcInsufficient);
        PLOGERR(klogInt, (klogInt, rc,
            "bad string_printf($(s).lock) result", "s=%s", prefix->addr));
        return rc;
    }

    return rc;
}

static rc_t _KDirectoryClean(KDirectory *self, const String *cache,
    const char *lock, const char *tmp, bool rmSelf)
{
    rc_t rc = 0;
    rc_t rc2 = 0;

    char tmpName[PATH_MAX] = "";
    const char *dir = tmpName;
    const char *tmpPfx = NULL;
    size_t tmpPfxLen = 0;

    assert(self && cache);

    rc = _KDirectoryMkTmpPrefix(self, cache, tmpName, sizeof tmpName);
    if (rc == 0) {
        char *slash = strrchr(tmpName, '/');
        if (slash != NULL) {
            if (strlen(tmpName) == slash + 1 - tmpName) {
                rc = RC(rcExe,
                    rcDirectory, rcSearching, rcDirectory, rcIncorrect);
                PLOGERR(klogInt, (klogInt, rc,
                    "bad file name $(path)", "path=%s", tmpName));
            }
            else {
                *slash = '\0';
                tmpPfx = slash + 1;
            }
        }
        else {
            rc = RC(rcExe, rcDirectory, rcSearching, rcChar, rcNotFound);
            PLOGERR(klogInt, (klogInt, rc,
                    "cannot extract directory from $(path)", "path=%s", dir));
        }
        tmpPfxLen = strlen(tmpPfx);

    }

    if (tmp != NULL && KDirectoryPathType(self, tmp) != kptNotFound) {
        rc_t rc3 = 0;
        STSMSG(STS_DBG, ("removing %s", tmp));
        rc3 = KDirectoryRemove(self, false, tmp);
        if (rc2 == 0 && rc3 != 0) {
            rc2 = rc3;
        }
    }

    if (rmSelf && KDirectoryPathType(self, cache->addr) != kptNotFound) {
        rc_t rc3 = 0;
        STSMSG(STS_DBG, ("removing %s", cache->addr));
        rc3 = KDirectoryRemove(self, false, cache->addr);
        if (rc2 == 0 && rc3 != 0) {
            rc2 = rc3;
        }
    }

    if (rc == 0) {
        uint32_t count = 0;
        uint32_t i = 0;
        KNamelist *list = NULL;
        STSMSG(STS_DBG, ("listing %s for old temporary files", dir));
        rc = KDirectoryList(self, &list, NULL, NULL, dir);
        DISP_RC2(rc, "KDirectoryList", dir);

        if (rc == 0) {
            rc = KNamelistCount(list, &count);
            DISP_RC2(rc, "KNamelistCount(KDirectoryList)", dir);
        }

        for (i = 0; i < count && rc == 0; ++i) {
            const char *name = NULL;
            rc = KNamelistGet(list, i, &name);
            if (rc != 0) {
                DISP_RC2(rc, "KNamelistGet(KDirectoryList)", dir);
            }
            else {
                if (strncmp(name, tmpPfx, tmpPfxLen) == 0) {
                    rc_t rc3 = 0;
                    STSMSG(STS_DBG, ("removing %s", name));
                    rc3 = KDirectoryRemove(self, false,
                        "%s%c%s", dir, '/', name);
                    if (rc2 == 0 && rc3 != 0) {
                        rc2 = rc3;
                    }
                }
            }
        }

        RELEASE(KNamelist, list);
    }

    if (lock != NULL && KDirectoryPathType(self, lock) != kptNotFound) {
        rc_t rc3 = 0;
        STSMSG(STS_DBG, ("removing %s", lock));
        rc3 = KDirectoryRemove(self, false, lock);
        if (rc2 == 0 && rc3 != 0) {
            rc2 = rc3;
        }
    }

    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

    return rc;
}

static rc_t ResolvedDownload(const Resolved *self, const Main *main) {
    rc_t rc = 0;
    KFile *out = NULL;
    uint64_t pos = 0;
    size_t num_read = 0;
    uint64_t opos = 0;
    size_t num_writ = 0;

    char tmp[PATH_MAX] = "";
    char lock[PATH_MAX] = "";

    assert(self
        && self->cache && self->cache->size && self->cache->addr && main);

    if (rc == 0) {
        rc = _KDirectoryMkLockName(main->dir, self->cache, lock, sizeof lock);
    }

    if (rc == 0) {
        rc = _KDirectoryMkTmpName(main->dir, self->cache, tmp, sizeof tmp);
    }

    if (KDirectoryPathType(main->dir, lock) != kptNotFound) {
        if (main->force != eForceYES) {
            KTime_t date = 0;
            rc = KDirectoryDate(main->dir, &date, lock);
            if (rc == 0) {
                time_t t = time(NULL) - date;
                if (t < 60 * 60 * 24) { /* 24 hours */
                    STSMSG(STS_DBG, ("%s found: canceling download", lock));
                    rc = RC(rcExe, rcFile, rcCopying, rcLock, rcExists);
                    PLOGERR(klogWarn, (klogWarn, rc,
                        "Lock file $(file) exists: download canceled",
                        "file=%s", lock));
                    return rc;
                }
                else {
                    STSMSG(STS_DBG, ("%s found and ignored as too old", lock));
                    rc = _KDirectoryClean(main->dir,
                        self->cache, NULL, NULL, true);
                }
            }
            else {
                STSMSG(STS_DBG, ("%s found", lock));
                DISP_RC2(rc, "KDirectoryDate", lock);
            }
        }
        else {
            STSMSG(STS_DBG, ("%s found and forced to be ignored", lock));
            rc = _KDirectoryClean(main->dir, self->cache, NULL, NULL, true);
        }
    }
    else {
        STSMSG(STS_DBG, ("%s not found", lock));
    }

    if (rc == 0) {
        STSMSG(STS_DBG, ("creating %s", lock));
        rc = KDirectoryCreateFile(main->dir, &out, false, 0664, kcmInit | kcmParents, lock);
        DISP_RC2(rc, "Cannot OpenFileWrite", lock);
    }

    if (rc == 0) {
        STSMSG(STS_DBG, ("creating %s", tmp));
        rc = KDirectoryCreateFile(main->dir, &out, false, 0664, kcmInit | kcmParents, tmp);
        DISP_RC2(rc, "Cannot OpenFileWrite", tmp);
    }

    assert(self->remote);

    STSMSG(STS_INFO, ("%s -> %s", self->remote->addr, tmp));
    do {
        rc = Quitting();

        if (rc == 0) {
            STSMSG(STS_FIN,
                ("> Reading %lu bytes from pos. %lu", main->bsize, pos));
            rc = KFileRead(self->file,
                pos, main->buffer, main->bsize, &num_read);
            if (rc != 0) {
                DISP_RC2(rc, "Cannot KFileRead", self->remote->addr);
            }
            else {
                STSMSG(STS_FIN,
                    ("< Read %lu bytes from pos. %lu", num_read, pos));
                pos += num_read;
            }
        }

        if (rc == 0 && num_read > 0) {
            rc = KFileWrite(out, opos, main->buffer, num_read, &num_writ);
            DISP_RC2(rc, "Cannot KFileWrite", tmp);
            opos += num_writ;
        }
    } while (rc == 0 && num_read > 0);

    RELEASE(KFile, out);

    if (rc == 0) {
        STSMSG(STS_DBG, ("renaming %s -> %s", tmp, self->cache->addr));
        rc = KDirectoryRename(main->dir, true, tmp, self->cache->addr);
        if (rc != 0) {
            PLOGERR(klogInt, (klogInt, rc, "cannot rename $(from) to $(to)",
                "from=%s,to=%s", tmp, self->cache->addr));
        }
    }

    {
        rc_t rc2 = _KDirectoryClean(main->dir, self->cache, lock, tmp, rc != 0);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    return rc;
}

static rc_t ResolvedResolve(Resolved *self,
    const Main *main, const char *acc)
{
    static int n = 0;
    rc_t rc = 0;
    bool isLocal = false;

    assert(self && main);

    ++n;

    rc = ResolvedInit(self, acc, main->resolver);

    if (rc == 0) {
        rc = ResolvedLocal(self, main->dir, &isLocal, main->force);
    }

    if (rc == 0) {
        if (isLocal) {
            STSMSG(STS_TOP, ("%d) %s is found locally", n, acc));
            self->path = self->local;
        }
        else {
            STSMSG(STS_TOP, ("%d) Downloading %s...", n, acc));
            rc = ResolvedDownload(self, main);
            if (rc == 0) {
                STSMSG(STS_TOP, ("%d) %s was downloaded successfully", n, acc));
                self->path = self->cache;
            }
            else {
                STSMSG(STS_TOP, ("%d) failed to download %s", n, acc));
            }
        }
    }

    return rc;
}

static rc_t MainDependenciesList(const Main *self,
    const String *path, const VDBDependencies **deps)
{
    rc_t rc = 0;
    bool isDb = true;
    const VDatabase *db = NULL;

    assert(self && path && deps);

    rc = VDBManagerOpenDBRead(self->mgr, &db, NULL, path->addr);
    if (rc != 0) {
        if (rc == SILENT_RC(rcDB, rcMgr, rcOpening, rcDatabase, rcIncorrect)) {
            isDb = false;
            rc = 0;
        }
        DISP_RC2(rc, "Cannot open database", path->addr);
    }

    if (rc == 0 && isDb) {
        bool missed = !self->check_all || self->force != eForceNo;
        rc = VDatabaseListDependencies(db, deps, missed);
        DISP_RC2(rc, "VDatabaseListDependencies", path);
    }

    RELEASE(VDatabase, db);

    return rc;
}

static rc_t MainExecute(const Main *self, const char *acc) {
    rc_t rc = 0;
    rc_t rc2 = 0;
    const VDBDependencies *deps = NULL;
    uint32_t count = 0;
    uint32_t i = 0;

    Resolved resolved;

    assert(self);

    rc = ResolvedResolve(&resolved, self, acc);

    if (rc == 0) {
        rc = MainDependenciesList(self, resolved.path, &deps);
    }

    if (rc == 0 && deps != NULL) {
        rc = VDBDependenciesCount(deps, &count);
        if (rc == 0) {
            STSMSG(STS_TOP, ("%s has %d %s dependencies",
                acc, count, self->check_all ? "" : "unresolved"));
        }
        else {
            DISP_RC2(rc, "Failed to check %s's dependencies", acc);
        }
    }

    for (i = 0; i < count && rc == 0; ++i) {
        bool local = true;
        const char *seq_id = NULL;

        if (rc == 0) {
            rc = VDBDependenciesLocal(deps, &local, i);
            DISP_RC2(rc, "VDBDependenciesLocal", acc);
            if (local) {
                continue;
            }
        }

        if (rc == 0) {
            rc = VDBDependenciesSeqId(deps, &seq_id, i);
            DISP_RC2(rc, "VDBDependenciesSeqId", acc);
        }

        if (rc == 0) {
            size_t num_writ = 0;
            char ncbiAcc[512] = "";

            assert(seq_id);

            rc = string_printf(ncbiAcc, sizeof ncbiAcc, &num_writ,
                "ncbi-acc:%s?vdb-ctx=refseq", seq_id);
            DISP_RC2(rc, "string_printf(?vdb-ctx=refseq)", seq_id);
            if (rc == 0 && num_writ > sizeof ncbiAcc) {
                rc = RC(rcExe, rcFile, rcCopying, rcBuffer, rcInsufficient);
                PLOGERR(klogInt, (klogInt, rc,
                    "bad string_printf($(s)?vdb-ctx=refseq) result",
                    "s=%s", seq_id));
            }
    
            if (rc == 0) {
                Resolved resolved;
                rc = ResolvedResolve(&resolved, self, ncbiAcc);
                rc2 = ResolvedFini(&resolved);
            }
        }
    }

    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

    RELEASE(VDBDependencies, deps);

    {
        rc_t rc2 = ResolvedFini(&resolved);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    return rc;
}

#define CHECK_ALL_ALIAS  "c"
#define CHECK_ALL_OPTION "check-all"
static const char* CHECK_ALL_USAGE[] = { "double-check all refseqs", NULL };

#define FORCE_ALIAS  "f"
#define FORCE_OPTION "force"
static const char* FORCE_USAGE[] = { "force object download::",
    "no [default]: skip download if the object if found and complete;",
    "yes: download it even if it is found and is complete;", "all: ignore lock "
    "files (stale locks or it is beeing downloaded by another process)", NULL };

static OptDef Options[] = {
    /*                                                    needs_value required*/
    { CHECK_ALL_OPTION, CHECK_ALL_ALIAS, NULL, CHECK_ALL_USAGE, 1, false, false
    },
    { FORCE_OPTION    , FORCE_ALIAS    , NULL, FORCE_USAGE , 1, true, false }
};

static rc_t MainProcessArgs(Main *self, int argc, char *argv[]) {
    rc_t rc = 0;

    uint32_t pcount = 0;

    assert(self);

    rc = ArgsMakeAndHandle(&self->args, argc, argv, 1,
        Options, sizeof Options / sizeof (OptDef));
    if (rc != 0) {
        DISP_RC(rc, "ArgsMakeAndHandle");
        return rc;
    }

    do {
        rc = ArgsOptionCount (self->args, CHECK_ALL_OPTION, &pcount);
        if (rc != 0) {
            LOGERR(klogErr,
                rc, "Failure to get '" CHECK_ALL_OPTION "' argument");
            break;
        }
        if (pcount > 0) {
            self->check_all = true;
        }

        rc = ArgsOptionCount (self->args, FORCE_OPTION, &pcount);
        if (rc != 0) {
            LOGERR(klogErr, rc, "Failure to get '" FORCE_OPTION "' argument");
            break;
        }

        if (pcount > 0) {
            const char *val = NULL;
            rc = ArgsOptionValue(self->args, FORCE_OPTION, 0, &val);
            if (rc != 0) {
                LOGERR(klogErr, rc,
                    "Failure to get '" FORCE_OPTION "' argument value");
                break;
            }
            if (val == NULL || val[0] == '\0') {
                rc = RC(rcExe, rcArgv, rcParsing, rcParam, rcInvalid);
                LOGERR(klogErr, rc,
                    "Unrecognized '" FORCE_OPTION "' argument value");
                break;
            }
            switch (val[0]) {
                case 'n':
                case 'N':
                    self->force = eForceNo;
                    break;
                case 'y':
                case 'Y':
                    self->force = eForceYes;
                    break;
                case 'a':
                case 'A':
                    self->force = eForceYES;
                    break;
                default:
                    rc = RC(rcExe, rcArgv, rcParsing, rcParam, rcInvalid);
                    LOGERR(klogErr, rc,
                        "Unrecognized '" FORCE_OPTION "' argument value");
                    break;
            }
            if (rc != 0) {
                break;
            }
        }
    } while (false);
    return rc;
}

static rc_t MainInit(int argc, char *argv[], Main *self) {
    rc_t rc = 0;
    VFSManager *mgr = NULL;
    KConfig *cfg = NULL;

    assert(self);
    memset(self, 0, sizeof *self);

    if (rc == 0) {
        rc = MainProcessArgs(self, argc, argv);
    }

    if (rc == 0) {
        self->bsize = 1024 * 1024;
        self->buffer = malloc(self->bsize);
        if (self->buffer == NULL) {
            rc = RC(rcExe, rcData, rcAllocating, rcMemory, rcExhausted);
        }
    }

    if (rc == 0) {
        rc = VFSManagerMake(&mgr);
        DISP_RC(rc, "VFSManagerMake");
    }

    if (rc == 0) {
        rc = KConfigMake(&cfg, NULL);
        DISP_RC(rc, "KConfigMake");
    }

    if (rc == 0) {
        rc = VFSManagerMakeResolver(mgr, &self->resolver, cfg);
        DISP_RC(rc, "VFSManagerMakeResolver");
    }

    if (rc == 0) {
        VResolverRemoteEnable ( self -> resolver, vrAlwaysEnable );
        VResolverCacheEnable ( self -> resolver, vrAlwaysEnable );
    }

    if (rc == 0) {
        rc = VDBManagerMakeRead(&self->mgr, NULL);
        DISP_RC(rc, "VDBManagerMakeRead");
    }

    if (rc == 0) {
        rc = KDirectoryNativeDir(&self->dir);
        DISP_RC(rc, "KDirectoryNativeDir");
    }

    if (rc == 0) {
        srand(time(NULL));
    }

    RELEASE(KConfig, cfg);
    RELEASE(VFSManager, mgr);

    return rc;
}

static rc_t MainFini(Main *self) {
    rc_t rc = 0;

    assert(self);

    RELEASE(VResolver, self->resolver);
    RELEASE(VDBManager, self->mgr);
    RELEASE(KDirectory, self->dir);
    RELEASE(Args, self->args);

    free(self->buffer);

    memset(self, 0, sizeof *self);

    return rc;
}

const char UsageDefaultName[] = "prefetch";
rc_t UsageSummary(const char *progname) {
    return OUTMSG((
        "Usage:\n"
        "  %s [options] <SRA accession> [ ...]\n"
        "\n"
        "Summary:\n"
        "  Download SRA file and its dependencies\n",
        progname));
}

rc_t CC Usage(const Args *args) {
    rc_t rc = 0;
    int i = 0;

    const char *progname = UsageDefaultName;
    const char *fullpath = UsageDefaultName;

    if (args == NULL) {
        rc = RC(rcApp, rcArgv, rcAccessing, rcSelf, rcNull);
    }
    else {
        rc = ArgsProgram(args, &fullpath, &progname);
    }
    if (rc != 0) {
        progname = fullpath = UsageDefaultName;
    }

    UsageSummary(progname);
    OUTMSG(("\n"));

    OUTMSG(("Options:\n"));
    for (i = 0; i < sizeof(Options) / sizeof(Options[0]); i++ ) {
        const char *param = NULL;
        if (strcmp(Options[i].aliases, FORCE_ALIAS) == 0) {
            param = "value";
        }
        HelpOptionLine(Options[i].aliases, Options[i].name,
            param, Options[i].help);
    }

    OUTMSG(("\n"));
    HelpOptionsStandard();
    HelpVersion(fullpath, KAppVersion());

    return rc;
}

ver_t CC KAppVersion(void) { return PREFETCH_VERS; }

/******************************************************************************/

rc_t CC KMain(int argc, char *argv[]) {
    rc_t rc = 0;
    uint32_t pcount = 0;
    uint32_t i = ~0;

    Main pars;
    rc = MainInit(argc, argv, &pars);

    if (rc == 0) {
        rc = ArgsParamCount(pars.args, &pcount);
    }

    if (rc == 0 && pcount == 0) {
        rc = UsageSummary(UsageDefaultName);
    }

    for (i = 0; rc == 0 && i < pcount; ++i) {
        const char *path = NULL;
        rc = ArgsParamValue(pars.args, i, &path);
        DISP_RC(rc, "ArgsParamValue");
        if (rc == 0) {
            rc = MainExecute(&pars, path);
        }
    }

    {
        rc_t rc2 = MainFini(&pars);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }
    }

    return rc;
}
