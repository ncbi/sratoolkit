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
#define CHECK_WINDOWS_PATH_LENGTH 0
#include <vfs/extern.h>

#include <vfs/path.h>
#include "path-priv.h"
#include <klib/defs.h>
#include <klib/rc.h>
#include <klib/log.h>
#include <klib/out.h>
#include <windows.h>
#include <direct.h>

static const
char * reserved_device_names [] =
{ 
    "CON:",
    "PRN:",
    "AUX:",
    "NUL:",
    "COM1:",
    "COM2:",
    "COM3:",
    "COM4:",
    "COM5:",
    "COM6:",
    "COM7:",
    "COM8:",
    "COM9:",
    "LPT1:",
    "LPT2:",
    "LPT3:",
    "LPT4:",
    "LPT5;",
    "LPT6:",
    "LPT7:",
    "LPT8:",
    "LPT9:",
    NULL
};

rc_t CC VPathTransformSysPath (VPath * self)
{
    char * pc;
    size_t lim;
    size_t ix;
    rc_t rc = 0;

    pc = (char *)(self->path.addr);
    lim = StringSize (&self->path);

    if (lim == 0)
        return 0;

    /* -----
     * toss out windows/dos device names
     */
    for (ix = 0; reserved_device_names[ix] != NULL; ++ix)
        if (strcmp (reserved_device_names[ix], pc) == 0)
            return RC (rcFS, rcPath, rcConstructing, rcPath, rcIncorrect);

    /* -----
     * look for for mingw and cygwin full paths and make them windowish
     * NOTE we could screw things up here with bad strings like
     * /cygdrive/haha/fooled-you
     */

    if (pc[0] == '/')
    {
        static const char cygdrive [] = "/cygdrive/";

        if (strncmp (pc, cygdrive, sizeof cygdrive - 1) == 0)
        {
            pc += sizeof cygdrive - 2;
            lim -= sizeof cygdrive - 2;
            /* NOTE: if the macro changes this could fail! */
            StringInit (&self->path, pc,
                        StringSize(&self->path) - sizeof cygdrive - 2,
                        StringLength(&self->path) - sizeof cygdrive - 2);
        }

        /* if here we had a mingw or cygwin specification */
        if (isalpha (pc[1]) && (pc[2] == '/'))
        {
            /* change it back to a windows specification */
            pc[0] = pc[1];
            pc[1] = ':';
        }
    }

    /* change from  Windows '/' to posix '/' segment dividers */
    for (ix = 0; ix < lim; ++ix)
        if (pc [ix] == '\\')
            pc [ix] = '/';

    /* look for stray ':' characters */
    for (ix = 0; ix < lim; ++ix)
    {
        if (pc[ix] == ':')
        {
            rc = RC (rcFS, rcPath, rcConstructing, rcPath, rcInvalid);
            PLOGERR (klogErr,
                     (klogErr, rc, "incorrect use of ':' in path '$(path)'",
                      "path=%s", pc));
            return rc;
        }
        if (ix == 0)
            ++ix;
    }

    if ((lim > 1) && (pc[1] == ':'))
    {
        if (!isalpha (pc[0]))
        {
            rc = RC (rcFS, rcPath, rcConstructing, rcPath, rcInvalid);
            PLOGERR (klogErr,
                     (klogErr, rc, "invalid character as drive letter '$(c)' ($(d)) in path '$(S)'",
                      "c=%c,d=%d,s=%s",pc[0],pc[0],pc));
            return rc;
        }
        /* we don't support drive relative addressing at this point
         * we should correct this in the future */
        if (pc[2] != '/')
        {
            rc = RC (rcFS, rcPath, rcConstructing, rcPath, rcIncorrect);
            PLOGERR (klogErr,
                     (klogErr, rc, "drive relative addressing not currently supported '$(path)'",
                      "path=%s", pc));
            return rc;
        }
    }

    if ((pc[0] == '/') && (pc[1] == '/')) /* UNC form */
    {
        if (pc[2] == '.') /* device space name - we don't handle these */
            return RC (rcFS, rcPath, rcConstructing, rcPath, rcIncorrect);

        if (pc[2] == '?') /* special space name - we don't handle these */
            return RC (rcFS, rcPath, rcConstructing, rcPath, rcIncorrect);
    }

/* possible future stuff? */
#if DO_MORE_STUFF_TO_VALIDATE_PATH
    /* -----
     * this size thing is turned off to allow paths into archives
     */
    ix = string_size (ipc = inpath);
    if (ix > MAX_PATH)
        return RC (rcFS, rcPath, rcConstructing, rcPath, rcTooLong);
    {
        DWORD insize = ilen;
        DWORD outlen;

        outlen = GetFullPathNameA (ipc, (DWORD)sizeof (temp), temp, NULL);

        if (outlen == 0)
            return RC (rcFS, rcPath, rcConstructing, rcPath, rcInvalid);
        if (outlen > sizeof temp)
            return RC (rcFS, rcPath, rcConstructing, rcPath, rcTooShort);

        ipc = temp;

        opc[0] = '/';
        opc[1] = ipc [0];

        ix = jx = 2;
    }

    do
    {
        switch (ipc[ix])
        {
        case '\0':
            normalize_done = true;
            /* fall through */
        default:
            opc[jx++] = ipc[ix++];
            break;

        case '\\':
            opc[jx++] = '/';
            ++ix;
            break;

        case '\x1': /* never allowed */
        case '\x2':
        case '\x3':
        case '\x4':
        case '\x5':
        case '\x6':
        case '\x7':
        case '\x8':
        case '\x9':
        case '\xA':
        case '\xB':
        case '\xC':
        case '\xD':
        case '\xE':
        case '\xF':
        case '\x10':
        case '\x11':
        case '\x12':
        case '\x13':
        case '\x14':
        case '\x15':
        case '\x16':
        case '\x17':
        case '\x18':
        case '\x19':
        case '\x1A':
        case '\x1B':
        case '\x1C':
        case '\x1D':
        case '\x1E':
        case '\x1F':
        case ':':
        case '<':
        case '>':
        case '"':
        case '|':
        case '*':
        case '?':
            rc = RC (rcFS, rcPath, rcConstructing, rcPath, rcInvalid);
            PLOGERR (klogErr,
                     (klogErr, rc, "invalid character '$(c)' ($(d)) in path '$(s)'",
                      "c=%c,d=%d,s=%s",ipc[ix],ipc[ix],ipc));
            return rc;
        }
    } while (!normalize_done);

    self->asciz_size = ix - 1;

#endif
    /* put drive based paths (back?) into our form */
    if ((lim > 1) && (pc[1] == ':'))
    {
        pc[1] = pc[0];
        pc[0] = '/';
        self->scheme = vpuri_ncbi_vfs;
    }
    return 0;
}

rc_t VPathTransformPathHier (char ** uri_path)
{
    return 0;
}

LIB_EXPORT rc_t CC VPathGetCWD (char * buffer, size_t buffer_size)
{
    char * temp = _getcwd (buffer, (int)buffer_size);
    if (temp == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcBuffer, rcInsufficient);
    return 0;
}

#if USE_EXPERIMENTAL_CODE
rc_t VPathInitAuthority (struct VPath * self, char ** next)
{
/*     static const char null_string[] = ""; */
/*     CONST_STRING (&self->authority, null_string); */
    return 0;
}
#endif
