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

#include <vfs/extern.h>

#include <vfs/path.h>
#include "path-priv.h"

#include <klib/rc.h>
#include <klib/log.h>
#include <klib/debug.h>

#include <stdlib.h>

#include <string.h>
#include <unistd.h>

rc_t VPathTransformSysPath (VPath * self)
{
#if 1
    return 0;
#else
    size_t path_size = strlen (sys_path) + 1; /* includes NUL */
    rc_t rc = 0;
    void * temp = self->storage;

    if (self->storage == self->buffer)
    {
        if (path_size > sizeof (self->buffer))
            temp = self->storage = NULL;
    }
    else if (self->alloc_size < path_size)
    {
        temp = NULL;
    }

    if (temp == NULL)
    {
        temp = realloc (self->storage, path_size);
        if (temp == NULL)
        {
            rc = RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);
            if (self->alloc_size == 0)
                self->storage = self->buffer;
        }
        else
        {
            self->alloc_size = path_size;
            self->storage = temp;
        }
    }
    if (rc == 0)
    {
        /* replace this with a minimal path validator */
        strcpy (self->storage, sys_path);
        self->asciz_size = path_size-1;
    }
    return rc;
#endif
}


rc_t VPathGetCWD (char * buffer, size_t buffer_size)
{
    char * temp = getcwd (buffer, buffer_size);
    if (temp == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcBuffer, rcInsufficient);
    return 0;
}


rc_t VPathTransformPathHier (char ** ppath)
{
#if USE_EXPERIMENTAL_CODE && 0
/* turning this off until http urls are better handled. */
    char * pc;

    pc = *ppath;
    PATH_DEBUG (("%s: incoming path '%s'\n",__func__, *ppath));

    if ((pc[0] == '/') && (pc[1] == '/'))
    {
        pc += 2;

        if (pc[0] != '/')
        {
            static const char localhost[] = "localhost";
            size_t z = strlen (localhost);

            if (strncmp (localhost, pc, z ) == 0)
                pc += z;
            else
            {
                rc_t rc =  RC (rcFS, rcPath, rcConstructing, rcUri, rcIncorrect);
                LOGERR (klogErr, rc, "illegal host in kfs/path url");
                return rc;
            }
        }
        *ppath = pc;
    }
    PATH_DEBUG (("%s: outgoing path '%s'\n",__func__, *ppath));
#endif
    return 0;
}
