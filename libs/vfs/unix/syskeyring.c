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

#include "keyring-priv.h"

#include <kfg/config.h>

#include <klib/text.h>
#include <klib/rc.h>

#include <kfs/file.h>
#include <kfs/directory.h>
#include <kfs/lockfile.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

static const char LockFileName[] = "/home/boshkina/.ncbi/keyring_lock";
static const char KeyRingServerExeName[] = "keyring-srv";

LIB_EXPORT bool CC KKeyRingIsServerRunning()
{   
    KDirectory* wd;
    rc_t rc = KDirectoryNativeDir (&wd);
    if (rc == 0)
    {
        KFile* lockedFile;
        rc = KDirectoryCreateExclusiveAccessFile(wd, &lockedFile, true, 0600, kcmOpen, LockFileName);
        if (rc == 0)
            KFileRelease(lockedFile);
        KDirectoryRelease(wd);
    }
    return rc == 0;
}

static
int StartServer()
{   /* create a child process that will become the server */
    char* const argv[] = {(char*)KeyRingServerExeName, NULL};
    pid_t child = fork();
    switch (child)
    {
    case 0: /* child */
        if (execvp(KeyRingServerExeName, argv) == -1)
        {   /* look around:
                - same dir as the current executable (kfg/APPPATH)
                - current dir
            */
            return -1;
        }
        /* no return if successful */
        return -1;
    case -1:
        return -1;
    default: /* parent */
        return 0;
    }
}

rc_t StartKeyRing(struct KStream** ipc)
{
/*
    1. Lock the lock file
    2. If failed, create the ipc object, connect to the server and exit (success)
    3. If succeeded, fork
        In the child:
        - execvp(kr-server) (if not in PATH, try locate and execv in fixed places (kfg:$APPPATH, ./)
        In the parent:
        - wait for the server connection to come up (= keyring server ready).
        - close the lock file (do not unlock)
        - connect to the server and exit (success)
*/
    KDirectory* wd;
    rc_t rc = KDirectoryNativeDir (&wd);
    if (rc == 0)
    {
        KFile* lockedFile;
        rc = KDirectoryCreateExclusiveAccessFile(wd, &lockedFile, true, 0600, kcmOpen, LockFileName);
        if (rc == 0)
        {   /* start the server */
            pid_t child = fork();
            switch (child)
            {
                case 0: /* child */
                {   /* fork a grandchild that will become the server */
                    exit(StartServer());
                    break;
                }
                case -1: /* error */
                {
                    rc_t rc;
                    switch (errno)
                    {
                    case EAGAIN:
                    case ENOMEM:
                        rc = RC (rcVFS, rcProcess, rcProcess, rcMemory, rcInsufficient);
                        break;
                    case ENOSYS:
                        rc = RC (rcVFS, rcProcess, rcProcess, rcInterface, rcUnsupported);
                        break;
                    default:
                        rc = RC (rcVFS, rcProcess, rcProcess, rcError, rcUnknown);
                        break;
                    }
                    break;
                }
                default: /* parent */
                {
                    waitpid(child, 0, 0);
                    break;
                }
            }
            KFileRelease(lockedFile); /* if successfully started, the grandchild will hold the lock until its exit */
        }
        KDirectoryRelease(wd);
    }
    return rc;
}

#if 0
static
rc_t GetAppPath(const char* buf, size_t bufsize)
{
    KConfig* kfg;
    rc_t rc = KConfigMake(&kfg, NULL);
    if (rc == 0)
    {
        const KConfigNode *node;
        char path[] = "APPPATH";
        char buf[4096];
        size_t num_read;
        rc_t rc2;
    
        rc_t rc=KConfigOpenNodeRead(kfg, &node, path, string_measure(path, NULL), buf);
        if (rc == 0) 
        {
            rc = KConfigNodeRead(node, 0, buf, bufsize, &num_read, NULL);
            rc2 = KConfigNodeRelease(node);
            if (rc == 0)
                rc = r2;
        }
        rc2 = KConfigRelease(kfg);
        if (rc == 0)
            rc = r2;
    }
    return rc;
}
#endif