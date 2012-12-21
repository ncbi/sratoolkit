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

#define TRACK_REFERENCES 0

#include <kdb/extern.h>
#include "libkdb.vers.h"

#define KONST const
#include "dbmgr-priv.h"
#include "kdb-priv.h"
#include <klib/checksum.h>
#include <klib/rc.h>
#undef KONST

#include <sysalloc.h>

#include <limits.h>
#include <stdlib.h>
#include <assert.h>



/*--------------------------------------------------------------------------
 * KDBManager
 *  handle to library
 */


/* MakeRead
 *  create library handle for specific use
 *  NB - only one of the functions will be implemented
 *
 *  "wd" [ IN, NULL OKAY ] - optional working directory for
 *  accessing the file system. mgr will attach its own reference.
 */
LIB_EXPORT rc_t CC KDBManagerMakeRead ( const KDBManager **mgrp, const KDirectory *wd )
{
    return KDBManagerMake ( ( KDBManager** ) mgrp, wd, "make-read" );
}


/* Writable
 *  returns 0 if object is writable
 *  or a reason why if not
 *
 *  "path" [ IN ] - NUL terminated path
 */
LIB_EXPORT rc_t CC KDBManagerVWritable ( const KDBManager *self, const char * path, va_list args )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcDB, rcMgr, rcAccessing, rcSelf, rcNull );
    else
    {
        char dbpath [ 4096 ];

        /* get full path to object */
        rc = KDirectoryVResolvePath ( self -> wd, true, dbpath, sizeof dbpath, path, args );
        if ( rc == 0 )
        {
            int type = KDBPathType ( self -> wd, NULL, path ) & ~ kptAlias;
            switch ( type )
            {
            case kptDatabase:
            case kptTable:
            case kptColumn:
            case kptIndex:
                rc = KDBWritable ( self -> wd, path );
                break;
            case kptNotFound:
                rc = RC ( rcDB, rcMgr, rcAccessing, rcPath, rcNotFound );
                break;
            case kptBadPath:
                rc = RC ( rcDB, rcMgr, rcAccessing, rcPath, rcInvalid );
                break;
            default:
                rc = RC ( rcDB, rcMgr, rcAccessing, rcPath, rcIncorrect );
            }
        }
    }
    return rc;
}

LIB_EXPORT rc_t CC KDBManagerWritable ( const KDBManager *self, const char * path, ... )
{
    rc_t rc;

    va_list args;
    va_start ( args, path );

    rc = KDBManagerVWritable ( self, path, args );

    va_end ( args );

    return rc;
}


/* RunPeriodicTasks
 *  executes periodic tasks, such as cache flushing
 */
LIB_EXPORT rc_t CC KDBManagerRunPeriodicTasks ( const KDBManager *self )
{
    if ( self == NULL )
        return RC ( rcDB, rcMgr, rcExecuting, rcSelf, rcNull );

    return 0;
}


/* PathType
 *  check the path type of an object/directory path.
 *  this is an extension of the KDirectoryPathType and will return
 *  the KDirectory values if a path type is not specifically a
 *  kdb object
 *
 * THIS IS A BADLY DESIGNED INTERFACE
 *  it used to die terribly if a NULL self were passed in, and has
 *  no way to indicate this type of error.
 */
LIB_EXPORT int CC KDBManagerVPathType ( const KDBManager * self, const char *path, va_list args )
{
    if ( self != NULL )
    {
        char full [ 4096 ];
        rc_t rc = KDirectoryVResolvePath ( self -> wd, true, full, sizeof full, path, args );
        if ( rc == 0 )
            return KDBPathType ( self -> wd, NULL, full );
    }
    return kptBadPath;
}


LIB_EXPORT int CC KDBManagerPathType ( const KDBManager * self, const char *path, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, path );

    rc = KDBManagerVPathType ( self, path, args );

    va_end (args);
    return rc;
}
