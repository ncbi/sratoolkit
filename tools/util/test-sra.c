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

/*
SRR125365
SRR292195
SRR413283
SRR600096
SRR619505
SRR953827
/home/klymenka/ncbi/public/sra/SRR619505
/home/klymenka/ncbi/public/sra/SRR619505.sra
/netmnt/traces04/sra2/SRR/000122/SRR125365
/netmnt/traces04/sra2/SRR/000586/SRR600096
/netmnt/traces04/sra3/SRR/000403/SRR413283
/netmnt/traces04/sra4/SRR/000345/SRR353827
/netmnt/traces04/sra5/SRR/000604/SRR619505
/netmnt/traces04/sra7/SRR/000285/SRR292195
/netmnt/traces04/sra7/SRR/000285/SRR292195/col
/netmnt/traces04/sra7/SRR/000285/SRR292195/col/
/netmnt/traces04/sra7/SRR/000285/SRR292195/col/READ
/netmnt/traces04/sra7/SRR/000285/SRR292195/col/READ/md
/netmnt/traces04/sra7/SRR/000285/SRR292195/col/READ/md5
/netmnt/traces04/sra7/SRR/000285/SRR292195/idx
/netmnt/traces04/sra7/SRR/000285/SRR292195/idx/skey
/panfs/traces01/sra_backup/SRA/sra_trash/duplicatedOn_sra-s/sra0/SRR005459
http://ftp-trace.ncbi.nlm.nih.gov/sra/sra-instant/reads/ByRun/sra/SRR/SRR619/SRR619505/SRR619505.sra
*/

#include <kapp/main.h> /* KMain */
#include <kdb/manager.h> /* kptDatabase */
#include <vdb/manager.h> /* VDBManager */
#include <vdb/database.h> /* VDatabase */
#include <vdb/dependencies.h> /* VDBDependencies */
#include <kdb/manager.h> /* kptDatabase */
#include <vfs/manager.h> /* VFSManager */
#include <vfs/resolver.h> /* VResolver */
#include <vfs/path.h> /* VPath */
#include <kns/kns_mgr.h> /* KNSManager */
#include <kfg/config.h> /* KConfig */
#include <kfs/directory.h> /* KDirectory */
#include <kfs/file.h> /* KFile */
#include <klib/printf.h> /* string_vprintf */
#include <klib/log.h> /* KLogHandlerSet */
#include <klib/out.h> /* KOutMsg */
#include <klib/text.h> /* String */
#include <klib/rc.h>
#include <assert.h>
#include <ctype.h> /* isprint */
#include <stdlib.h> /* calloc */
#include <string.h> /* memset */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define RELEASE(type, obj) do { rc_t rc2 = type##Release(obj); \
    if (rc2 != 0 && rc == 0) { rc = rc2; } obj = NULL; } while (false)

typedef enum {
    eCfg = 1,
/*  eType = 2, */
    eResolve = 2,
    eDependMissing = 4,
    eDependAll = 8,
    eAll = 16
} Type;
typedef struct {
    KConfig *cfg;
    KDirectory *dir;
    const VDBManager *mgr;
    VResolver *resolver;
    uint8_t tests;
    bool recursive;
    bool noVDBManagerPathType;
} Main;

uint32_t CC KAppVersion(void) { return 0; }

const char UsageDefaultName[] = "test-sra";

rc_t CC UsageSummary(const char *prog_name) {
    return KOutMsg(
        "Usage:\n"
        "    %s [+crdDa] [-crdDa] [options] name [ name... ]\n"
        "    %s -R [-N] name [ name... ]\n",
        prog_name, prog_name);
}

rc_t CC Usage(const Args *args) {
    rc_t rc2 = 0;

    const char *progname, *fullpath;
    rc_t rc = ArgsProgram(args, &fullpath, &progname);
    if (rc != 0) {
        progname = fullpath = UsageDefaultName;
    }

    rc2 = UsageSummary(progname);
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

/*      "  t - test object's VDB type\n" */
    rc2 = KOutMsg("\n"
        "Test [SRA] object, resolve it, print dependencies, configuration\n\n"
        "[+tests] - add tests\n"
        "[-tests] - remove tests\n\n"
        "Tests:\n"
        "  c - print configuration\n"
        "  r - call VResolver\n"
        "  d - call ListDependencies(missing)\n"
        "  D - call ListDependencies(all)\n"
        "  a - all tests\n\n"
        "If no tests were specified then all tests will be run\n\n"
        "-R - check object type recursively\n"
        "-N - do not call VDBManagerPathType\n\n"
        "More options:\n");
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

    HelpOptionsStandard();

    return rc;
}

static bool testArg(const char *arg, uint8_t *testOn, uint8_t *testOff) {
    int j = 1;
    uint8_t *res = NULL;

/*  const char tests[] = "ctrdDa"; */
    const char tests[] = "crdDa";

    assert(arg && testOn && testOff);
    if (arg[0] != '+' && arg[0] != '-') {
        return false;
    }

    if (arg[0] == '-' &&
        arg[1] != '\0' && strchr(tests, arg[1]) == NULL)
    {
        return false;
    }

    res = arg[0] == '-' ? testOff : testOn;

    for (j = 1; arg[j] != '\0'; ++j) {
        char *c = strchr(tests, arg[j]);
        if (c != NULL) {
            int offset = c - tests;
            *res |= 1 << offset;
        }
    }

    return true;
}

static uint8_t Turn(uint8_t in, uint8_t tests, bool on) {
    uint8_t c = 1;
    for (c = 1; c < eAll; c <<= 1) {
        if (tests & c) {
            if (on) {
                in |= c;
            }
            else {
                in &= ~c;
            }
        }
    }
    return in;
}

static uint8_t processTests(uint8_t testsOn, uint8_t testsOff) {
    uint8_t tests = 0;

    bool allOn = false;
    bool allOff = false;

    if (testsOn & eAll && testsOff & eAll) {
        testsOn &= ~eAll;
        testsOff &= ~eAll;
    }
    else if (testsOn & eAll) {
        allOn = true;
    }
    else if (testsOff & eAll) {
        allOff = true;
    }

    if (allOn) {
        tests = ~0;
        tests = Turn(tests, testsOff, false);
    }
    else if (allOff) {
        tests = Turn(tests, testsOn, true);
    }
    else if (testsOn != 0 || testsOff != 0) {
        tests = Turn(tests, testsOff, false);
        tests = Turn(tests, testsOn, true);
    }
    else {
        tests = ~0;
    }

    return tests;
} 

static bool MainHasTest(const Main *self, Type type) {
    assert(self);
    return self->tests & type;
}

static void MainPrint(const Main *self) {
    assert(self);

    if (MainHasTest(self, eCfg)) {
        KOutMsg("eCfg\n");
    }

/*  if (MainHasTest(self, eType)) {
        KOutMsg("eType\n");
    }*/

    if (MainHasTest(self, eResolve)) {
        KOutMsg("eResolve\n");
    }

    if (MainHasTest(self, eDependMissing)) {
        KOutMsg("eDependMissing\n");
    }

    if (MainHasTest(self, eDependAll)) {
        KOutMsg("eDependAll\n");
    }
}

static rc_t MainInitObjects(Main *self) {
    rc_t rc = 0;

    VFSManager* mgr = NULL;
    VResolver *resolver = NULL;

    if (rc == 0) {
        rc = KDirectoryNativeDir(&self->dir);
    }

    if (rc == 0) {
        rc = VDBManagerMakeRead(&self->mgr, NULL);
    }

    if (rc == 0) {
        rc = KConfigMake(&self->cfg, NULL);
    }

    if (rc == 0) {
        rc = VFSManagerMake(&mgr);
    }

    if (rc == 0) {
        rc = VFSManagerGetResolver(mgr, &resolver);
    }

    if (rc == 0) {
        VResolverCacheEnable(resolver, vrAlwaysDisable);
    }

    if (rc == 0) {
        rc = VFSManagerMakeResolver(mgr, &self->resolver, self->cfg);
    }


    RELEASE(VResolver, resolver);
    RELEASE(VFSManager, mgr);

    return rc;
}

static
rc_t _MainInit(Main *self, int argc, char *argv[], int *argi, char **argv2)
{
    rc_t rc = 0;
    int i = 0;

    uint8_t testsOn = 0;
    uint8_t testsOff = 0;

    assert(self && argv && argi && argv2);

    *argi = 0;
    argv2[(*argi)++] = argv[0];

    for (i = 1; i < argc; ++i) {
        if (!testArg(argv[i], &testsOn, &testsOff)) {
            argv2[(*argi)++] = argv[i];
        }
    }

    self->tests = processTests(testsOn, testsOff);

    MainPrint(self);

    rc = MainInitObjects(self);

    return rc;
}

static char** MainInit(Main *self, rc_t *rc,
    int argc, char *argv[], int *argi)
{
    char **argv2 = calloc(argc, sizeof *argv2);

    assert(self && rc);

    memset(self, 0, sizeof *self);

    if (argv2 != NULL) {
        *rc = _MainInit(self, argc, argv, argi, argv2);
    }

    return argv2;
}

static rc_t MainPrintConfig(const Main *self) {
    rc_t rc = 0;

    assert(self);

    if (rc == 0) {
        rc = KConfigPrint(self->cfg);
        if (rc != 0) {
            OUTMSG(("KConfigPrint() = %R", rc));
        }
        OUTMSG(("\n"));
    }

    return rc;
}

static
rc_t _KDBPathTypePrint(const char *head, KPathType type, const char *tail)
{
    rc_t rc = 0;
    rc_t rc2 = 0;
    assert(head && tail);
    {
        rc_t rc2 = OUTMSG(("%s", head));
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    switch (type) {
        case kptNotFound:
            rc2 = OUTMSG(("NotFound"));
            break;
        case kptBadPath:
            rc2 = OUTMSG(("BadPath"));
            break;
        case kptFile:
            rc2 = OUTMSG(("File"));
            break;
        case kptDir:
            rc2 = OUTMSG(("Dir"));
            break;
        case kptCharDev:
            rc2 = OUTMSG(("CharDev"));
            break;
        case kptBlockDev:
            rc2 = OUTMSG(("BlockDev"));
            break;
        case kptFIFO:
            rc2 = OUTMSG(("FIFO"));
            break;
        case kptZombieFile:
            rc2 = OUTMSG(("ZombieFile"));
            break;
        case kptDataset:
            rc2 = OUTMSG(("Dataset"));
            break;
        case kptDatatype:
            rc2 = OUTMSG(("Datatype"));
            break;
        case kptDatabase:
            rc2 = OUTMSG(("Database"));
            break;
        case kptTable:
            rc2 = OUTMSG(("Table"));
            break;
        case kptIndex:
            rc2 = OUTMSG(("Index"));
            break;
        case kptColumn:
            rc2 = OUTMSG(("Column"));
            break;
        case kptMetadata:
            rc2 = OUTMSG(("Metadata"));
            break;
        case kptPrereleaseTbl:
            rc2 = OUTMSG(("PrereleaseTbl"));
            break;
        default:
            rc2 = OUTMSG(("unexpectedFileType(%d)", type));
            assert(0);
            break;
    }
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }
    {
        rc_t rc2 = OUTMSG(("%s", tail));
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    return rc;
}

static bool isprintString(const unsigned char *s) {
    assert(s);

    while (*s) {
        int c = *(s++);
        if (!isprint(c)) {
            return false;
        }
    }

    return true;
}

static rc_t printString(const char *s) {
    rc_t rc = 0;

    const unsigned char *u = (unsigned char*)s;

    assert(u);

    if (isprintString(u)) {
        return OUTMSG(("%s", u));
    }

    while (*u) {
        rc_t rc2 = 0;
        int c = *(u++);
        if (isprint(c)) {
            rc2 = OUTMSG(("%c", c));
        }
        else {
            rc2 = OUTMSG(("\\%03o", c));
        }
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    return rc;
}

static rc_t _KDirectoryReport(const KDirectory *self,
    const char *name, int64_t *size, KPathType *type, bool *alias)
{
    rc_t rc = 0;
    const KFile *f = NULL;

    bool dummyB = false;
    int64_t dummy = 0;

    KPathType dummyT = kptNotFound;;
    if (type == NULL) {
        type = &dummyT;
    }

    if (alias == NULL) {
        alias = &dummyB;
    }
    if (size == NULL) {
        size = &dummy;
    }

    *type = KDirectoryPathType(self, name);

    if (*type & kptAlias) {
        OUTMSG(("alias|"));
        *type &= ~kptAlias;
        *alias = true;
    }

    rc = _KDBPathTypePrint("", *type, " ");

    if (*type == kptFile) {
        rc = KDirectoryOpenFileRead(self, &f, name);
        if (rc != 0) {
            OUTMSG(("KDirectoryOpenFileRead("));
            printString(name);
            OUTMSG((")=%R ", rc));
        }
        else {
            uint64_t sz = 0;
            rc = KFileSize(f, &sz);
            if (rc != 0) {
                OUTMSG(("KFileSize(%s)=%R ", name, rc));
            }
            else {
                OUTMSG(("%lu ", sz));
                *size = sz;
            }
        }
    }

    RELEASE(KFile, f);

    return rc;
}

static rc_t _VDBManagerReport(const VDBManager *self,
    const char *name, KPathType *type)
{
    KPathType dummy = kptNotFound;;

    if (type == NULL) {
        type = &dummy;
    }

    *type = VDBManagerPathType(self, name);

    *type &= ~kptAlias;

    return _KDBPathTypePrint("", *type, " ");
}

static rc_t MainReport(const Main *self,
    const char *name, int64_t *size, KPathType *type, bool *alias)
{
    rc_t rc = 0;

    assert(self);

    rc = _KDirectoryReport(self->dir, name, size, type, alias);

    if (!self->noVDBManagerPathType) { /* && MainHasTest(self, eType)) { */
        _VDBManagerReport(self->mgr, name, type);
    }

    return rc;
}

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

static rc_t MainResolveLocal(const Main *self,
    const char *name, const VPath* acc, int64_t *size)
{
    rc_t rc = 0;

    const VPath* local = NULL;

    assert(self);

    OUTMSG(("Local: "));

    rc = VResolverLocal(self->resolver, acc, &local);
    if (rc != 0) {
        if (NotFoundByResolver(rc)) {
            OUTMSG(("not found\n"));
            rc = 0;
        }
        else {
            OUTMSG(("VResolverLocal(%s) = %R\n", name, rc));
        }
    }
    else {
        const String *s = NULL;

        rc_t rc = VPathMakeString(local, &s);
        if (rc == 0) {
            OUTMSG(("%.*s ", s->size, s->addr));
            rc = MainReport(self, s->addr, size, NULL, NULL);
            OUTMSG(("\n"));
        } else {
            OUTMSG(("VPathMakeString(VResolverLocal(%s)) = %R\n",
                name, rc));
        }

        free((void*)s);
    }

    RELEASE(VPath, local);

    return rc;
}

static rc_t MainResolveRemote(const Main *self, const char *name,
    const VPath* acc, const VPath **remote, int64_t *size)
{
    rc_t rc = 0;

    const KFile* f = NULL;

    assert(self && size);

    OUTMSG(("Remote: "));

    rc = VResolverRemote(self->resolver, acc, remote, &f);
    if (rc != 0) {
        if (NotFoundByResolver(rc)) {
            OUTMSG(("not found\n"));
            rc = 0;
        }
        else {
            OUTMSG(("VResolverRemote(%s) = %R\n", name, rc));
        }
    }
    else {
        const String *s = NULL;
        rc_t rc = VPathMakeString(*remote, &s);
        if (rc == 0) {
            uint64_t sz = 0;
            OUTMSG(("%.*s ", s->size, s->addr));
            rc = KFileSize(f, &sz);
            if (rc != 0) {
                OUTMSG(("KFileSize(%s)=%R ", name, rc));
            }
            else {
                OUTMSG(("%lu ", sz));
                *size = sz;
            }
            OUTMSG(("\n"));
        } else {
            OUTMSG(("VPathMakeString(VResolverRemote(%s)) = %R\n",
                name, rc));
        }
        free((void*)s);
    }

    RELEASE(KFile, f);

    return rc;
}

static rc_t MainResolveCache(const Main *self,
    const char *name, const VPath* remote)
{
    rc_t rc = 0;

    assert(self);
    
    OUTMSG(("Cache: "));
    
    if (remote == NULL) {
        OUTMSG(("skipped\n"));
    }
    else {
        VResolverEnableState enabled = VResolverCacheEnable(self->resolver, vrAlwaysEnable);
        const VPath* cache = NULL;
        uint64_t file_size = 0;
        rc = VResolverCache(self->resolver, remote, &cache, file_size);
        VResolverCacheEnable(self->resolver, enabled);
        if (rc != 0) {
            if (NotFoundByResolver(rc)) {
                OUTMSG(("not found\n"));
                rc = 0;
            }
            else {
                OUTMSG(("VResolverCache(%s) = %R\n", name, rc));
            }
        }
        else {
            const String *s = NULL;
            rc_t rc = VPathMakeString(cache, &s);
            if (rc == 0) {
                OUTMSG(("%.*s ", s->size, s->addr));
                rc = MainReport(self, s->addr, NULL, NULL, NULL);
                OUTMSG(("\n"));
            } else {
                OUTMSG((
                    "VPathMakeString(VResolverCache(%s, %d)) = %R\n",
                    name, file_size, rc));
            }
            free((void*)s);
        }

        RELEASE(VPath, cache);
    }

    return rc;
}

static rc_t MainResolve(const Main *self,
    const char *name, int64_t *localSz, int64_t *remoteSz)
{
    rc_t rc = 0;

    VPath* acc = NULL;

    assert(self);

    if (rc == 0) {
        rc = VPathMake(&acc, name);
        if (rc != 0) {
            OUTMSG(("VPathMake(%s) = %R\n", name, rc));
        }
    }

    if (rc == 0) {
        const VPath* remote = NULL;

        rc_t rc2 = MainResolveLocal(self, name, acc, localSz);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveRemote(self, name, acc, &remote, remoteSz);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveCache(self, name, remote);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        RELEASE(VPath, remote);
    }

    RELEASE(VPath, acc);

    return rc;
}

static
rc_t MainDepend(const Main *self, const char *name, bool missing)
{
    rc_t rc = 0;

    const VDatabase *db = NULL;
    const VDBDependencies* dep = NULL;
    uint32_t count = 0;

    if (rc == 0) {
        rc = VDBManagerOpenDBRead(self->mgr, &db, NULL, name);
        if (rc != 0) {
            if (rc == SILENT_RC(rcVFS,rcMgr,rcOpening,rcDirectory,rcNotFound)) {
                return 0;
            }
            OUTMSG(("VDBManagerOpenDBRead(%s) = %R\n", name, rc));
        }
    }

    if (rc == 0) {
        rc = VDatabaseListDependencies(db, &dep, missing);
        if (rc != 0) {
            OUTMSG(("VDatabaseListDependencies(%s, %s) = %R\n",
                name, missing ? "missing" : "all", rc));
        }
    }

    if (rc == 0) {
        rc = VDBDependenciesCount(dep, &count);
        if (rc != 0) {
            OUTMSG(("VDBDependenciesCount(%s, %s) = %R\n",
                name, missing ? "missing" : "all", rc));
        }
        else {
            OUTMSG(("VDBDependenciesCount(%s)=%d\n",
                missing ? "missing" : "all", count));
        }
    }

    if (rc == 0) {
        uint32_t i = 0;
        rc_t rc2 = 0;
        for (i = 0; i < count; ++i) {
            bool b = true;
            const char *s = NULL;
            KPathType type = kptNotFound;

            OUTMSG((" %6d\t", i + 1));

            rc2 = VDBDependenciesSeqId(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("seqId=%s,", s));
            }
            else {
                OUTMSG(("VDBDependenciesSeqId(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesName(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("name=%s,", s));
            }
            else {
                OUTMSG(("VDBDependenciesName(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesCircular(dep, &b, i);
            if (rc2 == 0) {
                OUTMSG(("circular=%s,", b ? "true" : "false"));
            }
            else {
                OUTMSG(("VDBDependenciesCircular(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesType(dep, &type, i);
            if (rc2 == 0) {
                rc2 = _KDBPathTypePrint("type=", type, ",");
                if (rc2 != 0 && rc == 0) {
                    rc = rc2;
                }
            }
            else {
                OUTMSG(("VDBDependenciesType(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesLocal(dep, &b, i);
            if (rc2 == 0) {
                OUTMSG(("local=%s,", b ? "local" : "remote"));
            }
            else {
                OUTMSG(("VDBDependenciesLocal(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPath(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("pathLocal=%s,", s == NULL ? "notFound" : s));
            }
            else {
                OUTMSG(("VDBDependenciesPath(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPathRemote(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("pathRemote=%s,", s == NULL ? "notFound" : s));
            }
            else {
                OUTMSG(("VDBDependenciesPathRemote(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPathCache(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("pathCache=%s", s == NULL ? "notFound" : s));
            }
            else {
                OUTMSG(("VDBDependenciesPathCache(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            OUTMSG(("\n"));
        }
    }

    RELEASE(VDBDependencies, dep);
    RELEASE(VDatabase, db);

    return rc;
}

static rc_t PrintCurl() {
    KNSManager *mgr = NULL;
    rc_t rc = KNSManagerMake(&mgr);
    if (rc != 0) {
        OUTMSG(("KNSManagerMake = %R\n", rc));
    }
    if (rc == 0) {
        rc_t rc = KNSManagerAvail(mgr);
        OUTMSG(("KNSManagerAvail = %R", rc));
        if (rc == 0) {
            const char *version_string = NULL;
            rc = KNSManagerCurlVersion(mgr, &version_string);
            if (rc == 0) {
                OUTMSG((". Curl Version = %s\n", version_string));
            }
            else {
                OUTMSG((". KNSManagerCurlVersion = %R\n", rc));
            }
        }
    }
    RELEASE(KNSManager, mgr);
    return rc;
}

static rc_t MainExec(const Main *self, const char *aArg, ...) {
    rc_t rc = 0;
    rc_t rce = 0;

    KPathType type = kptNotFound;
    bool alias = false;
    int64_t directSz = -1;
    int64_t localSz = -1;
    int64_t remoteSz = -1;
    size_t num_writ = 0;
    char arg[PATH_MAX] = "";

    va_list args;
    va_start(args, aArg);

    assert(self);

    rc = string_vprintf(arg, sizeof arg, &num_writ, aArg, args);
    if (rc != 0) {
        OUTMSG(("string_vprintf(%s)=%R\n", aArg, rc));
        return rc;
    }
    assert(num_writ < sizeof arg);

    OUTMSG(("\n"));
    rc = printString(arg);
    if (rc != 0) {
        OUTMSG(("printString=%R\n", rc));
        return rc;
    }
    OUTMSG((" "));
    rc = MainReport(self, arg, &directSz, &type, &alias);
    OUTMSG(("\n"));

    if (self->recursive && type == kptDir && !alias) {
        uint32_t i = 0;
        uint32_t count = 0;
        KNamelist *list = NULL;
        rc = KDirectoryList(self->dir, &list, NULL, NULL, arg);
        if (rc != 0) {
            OUTMSG(("KDirectoryList(%s)=%R ", arg, rc));
        }
        else {
            rc = KNamelistCount(list, &count);
            if (rc != 0) {
                OUTMSG(("KNamelistCount(KDirectoryList(%s))=%R ", arg, rc));
            }
        }
        for (i = 0; i < count && rc == 0; ++i) {
            const char *name = NULL;
            rc = KNamelistGet(list, i, &name);
            if (rc != 0) {
                OUTMSG(("KNamelistGet(KDirectoryList(%s), %d)=%R ",
                    arg, i, rc));
            }
            else {
                rc_t rc2 = MainExec(self, "%s/%s", arg, name);
                if (rc2 != 0 && rce == 0) {
                    rce = rc2;
                }
            }
        }
        RELEASE(KNamelist, list);
    }
    else {
        if (MainHasTest(self, eResolve)) {
            rc_t rc2 = MainResolve(self, arg, &localSz, &remoteSz);
            if (rc == 0 && rc2 != 0) {
                rc = rc2;
            }
        }

        if (type == kptDatabase || type == kptNotFound) {
            if (MainHasTest(self, eDependMissing)) {
                rc_t rc2 = MainDepend(self, arg, true);
                if (rc == 0 && rc2 != 0) {
                    rc = rc2;
                }
            }

            if (MainHasTest(self, eDependAll)) {
                rc_t rc2 = MainDepend(self, arg, false);
                if (rc == 0 && rc2 != 0) {
                    rc = rc2;
                }
            }
        }

        if (MainHasTest(self, eResolve) && (
            (directSz != -1 && localSz != -1 && directSz != localSz) ||
            (remoteSz != -1 && localSz != -1 && localSz != remoteSz)))
        {
            OUTMSG(("FILE SIZES DO NOT MATCH: "));
            if (directSz != -1 && localSz != -1 && directSz != remoteSz) {
                OUTMSG(("direct=%ld != remote=%ld. ", directSz, remoteSz));
            }
            if (remoteSz != -1 && localSz != -1 && localSz != remoteSz) {
                OUTMSG(("local=%ld != remote=%ld. ", localSz, remoteSz));
            }
            OUTMSG(("\n"));
        }

        OUTMSG(("\n"));
    }

    if (rce != 0 && rc == 0) {
        rc = rce;
    }
    return rc;
}

static rc_t MainFini(Main *self) {
    rc_t rc = 0;

    assert(self);

    RELEASE(VResolver, self->resolver);
    RELEASE(KConfig, self->cfg);
    RELEASE(VDBManager, self->mgr);
    RELEASE(KDirectory, self->dir);

    return rc;
}

#define ALIAS_REC  "R"
#define OPTION_REC "recursive"
static const char* USAGE_REC[] = { "check object type recursively", NULL };

#define ALIAS_NO_VDB  "N"
#define OPTION_NO_VDB "no-vdb"
static const char* USAGE_NO_VDB[] = { "do not call VDBManagerPathType", NULL };

OptDef Options[] = {                             /* needs_value, required */
    { OPTION_REC   , ALIAS_REC   , NULL, USAGE_REC   , 1, false, false },
    { OPTION_NO_VDB, ALIAS_NO_VDB, NULL, USAGE_NO_VDB, 1, false, false }
};

rc_t CC KMain(int argc, char *argv[]) {
    rc_t rc = 0;
    uint32_t pcount = 0;
    uint32_t i = 0;
    Args *args = NULL;
    rc_t rc3 = 0;
    int argi = 0;

    Main prms;
    char **argv2 = MainInit(&prms, &rc, argc, argv, &argi);

    if (rc == 0) {
        rc = ArgsMakeAndHandle(&args, argi, argv2, 1,
            Options, sizeof Options / sizeof Options[0]);
    }

    if (MainHasTest(&prms, eCfg)) {
        rc_t rc2 = MainPrintConfig(&prms);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    PrintCurl();

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_REC, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_REC "' argument");
        }
        else {
            if (pcount > 0) {
                prms.recursive = true;
            }
        }
    }

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_NO_VDB, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_NO_VDB "' argument");
        }
        else {
            if (pcount > 0) {
                prms.noVDBManagerPathType = true;
            }
        }
    }

    if (rc == 0) {
        rc = ArgsParamCount(args, &pcount);
    }

    for (i = 0; i < pcount; ++i) {
        const char *name = NULL;
        rc3 = ArgsParamValue(args, i, &name);
        if (rc3 == 0) {
            rc_t rc2 = MainExec(&prms, name);
            if (rc == 0 && rc2 != 0) {
                rc = rc2;
            }
        }
    }
    if (rc == 0 && rc3 != 0) {
        rc = rc3;
    }

    RELEASE(Args, args);
    {
        rc_t rc2 = MainFini(&prms);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    free(argv2);

    OUTMSG(("\n"));

    return rc;
}
