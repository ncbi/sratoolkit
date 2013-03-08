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

#include "srapath.vers.h"

#include <sra/srapath.h>
#include <vfs/resolver.h>
#include <vfs/path.h>
#include <vfs/manager.h>
#include <kfs/directory.h>
#include <kfs/defs.h>
#include <kfg/config.h>
#include <sra/impl.h>
#include <kapp/main.h>
#include <kapp/args.h>
#include <klib/text.h>
#include <klib/log.h>
#include <klib/out.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


/* Version  EXTERN
 *  return 4-part version code: 0xMMmmrrrr, where
 *      MM = major release
 *      mm = minor release
 *    rrrr = bug-fix release
 */
ver_t CC KAppVersion ( void )
{
    return SRAPATH_VERS;
}

const char UsageDefaultName[] = "srapath";

rc_t CC UsageSummary (const char * progname)
{
    return KOutMsg (
        "\n"
        "Usage:\n"
        "  %s [options] <accession> ...\n"
        "Summary:\n"
        "  Tool to produce a list of full paths to SRA runs from list of\n"
        "  accessions.\n"
        "\n", progname);
}

rc_t CC Usage (const Args * args)
{
    const char * progname = UsageDefaultName;
    const char * fullpath = UsageDefaultName;
    rc_t rc;

    if (args == NULL)
        rc = RC (rcApp, rcArgv, rcAccessing, rcSelf, rcNull);
    else
        rc = ArgsProgram (args, &fullpath, &progname);
    if (rc)
        progname = fullpath = UsageDefaultName;

    UsageSummary (progname);

    OUTMSG (("  Output paths are ordered according to accession list.\n"
         "  with no path alteration options, the accession search path will be\n"
         "  determined according to the local installation.\n"
         "  Server and volume paths may be compound, adhering to the\n"
             "  Unix path convention of ':' separators, e.g.\n"
             "    '/server1:/server2/subdir'\n"
             "\n"));
    OUTMSG (("  The order of search paths is important: rather than alphabetical,\n"
             "  specify paths in the preferred order of discovery, usually giving the most\n"
             "  recently updated volumes first.  If a run appears more than once in the\n"
             "  matrix, it will be found according to the first 'repserver/volume'\n"
             "  combination that produces a hit.\n"
             "\n"));
    OUTMSG (("  Finally, this tool produces a path that is 'likely' to be a run, in that\n"
             "  an entry exists in the file system at the location predicted.  It is possible\n"
             "  that this path will fail to produce success upon opening a run if the path\n"
             "  does not point to a valid object.\n"
             "\n"));

    KOutMsg ("Options:\n");

    HelpOptionsStandard();

    HelpVersion (fullpath, KAppVersion());

    return rc;
}

/* KMain
 */
rc_t CC KMain ( int argc, char *argv [] )
{
    Args * args;
    rc_t rc;

    rc = ArgsMakeAndHandle (&args, argc, argv, 0);
    if (rc)
        LOGERR (klogInt, rc, "failed to parse arguments");
    else do
    {
        uint32_t acount;
        rc = ArgsParamCount (args, &acount);
        if (rc)
        {
            LOGERR (klogInt, rc, "failed to count parameters");
            break;
        }

        if (acount == 0)
        {
            rc = MiniUsage (args);
            break;
        }
        else
        {
            VFSManager* mgr;
            rc = VFSManagerMake(&mgr);
            if (rc)
                LOGERR ( klogErr, rc, "failed to create VFSManager object" );
            else
            {
                VResolver * resolver;

                rc = VFSManagerGetResolver (mgr, &resolver);
                if (rc == 0)
                {
                    uint32_t ix;
                    for ( ix = 0; ix < acount; ++ ix )
                    {
                        const char * pc;
                        rc = ArgsParamValue (args, ix, &pc );
                        if (rc)
                            LOGERR (klogInt, rc,
                                    "failed to retrieve parameter value");
                        else
                        {
                            const VPath * upath;

                            rc = VPathMakeSysPath ((VPath**)&upath, pc);
                            if (rc == 0)
                            {
                                const VPath * rpath;
                                    
                                rc = VResolverLocal (resolver, upath, &rpath);
                                if (GetRCState(rc) == rcNotFound)
                                    rc = VResolverRemote (resolver, upath, &rpath, NULL);

                                if (rc == 0)
                                {
                                    const String * s;

                                    rc = VPathMakeString (rpath, &s);
                                    if (rc == 0)
                                    {
                                        OUTMSG (("%S\n", s));
                                        free ((void*)s);
                                    }
                                }
                                else
                                {
                                    KDirectory * cwd;
                                    rc_t orc;

                                    orc = VFSManagerGetCWD (mgr, &cwd);
                                    if (orc == 0)
                                    {
                                        KPathType kpt;

                                        kpt = KDirectoryPathType (cwd, "%s", pc);
                                        switch (kpt & ~kptAlias)
                                        {
                                        case kptNotFound:
                                        case kptBadPath:
                                            break;

                                        default:
                                            OUTMSG (("./%s\n", pc));
                                            rc = 0;
                                            break;
                                        }
                                    }
                                }
                                VPathRelease (rpath);
                                VPathRelease (upath);
                            }
                        }
                    }
                    VResolverRelease (resolver);
                }
                VFSManagerRelease(mgr);
            }
        }
        ArgsWhack (args);

    } while (0);

    return rc;
}
