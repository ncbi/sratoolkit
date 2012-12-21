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

#include <kfs/extern.h>

/*--------------------------------------------------------------------------
 * forwards
 */
struct KSysFile;
#define KFILE_IMPL struct KSysFile

#include "sysfile-priv.h"
#include <klib/rc.h>
#include <klib/log.h>
#include <klib/text.h>
#include <sysalloc.h>

#include <Windows.h>

/*--------------------------------------------------------------------------
 * KSysFile
 *  a Windows file
 */


/* Destroy
 */
static
rc_t CC KSysFileDestroy ( KSysFile *self )
{
    rc_t rc = 0;

    if ( CloseHandle (self -> handle ) == 0 )
    {
        DWORD last_error;

        last_error = GetLastError();
        switch ( last_error )
        {
        case ERROR_INVALID_HANDLE:
            break;
        default:
            rc = RC (rcFS, rcFile, rcDestroying, rcNoObj, rcUnknown);
            PLOGERR (klogErr,
                     (klogErr, rc, "error closing system file - $(E)$($(C))",
                      "E=%!,C=%u", last_error, last_error)); 
        }
    }

    free ( self );
    return rc;
}


static
rc_t CC KSysStdIOFileDestroy ( KSysFile *self )
{
    free ( self );
    return 0;
}


/* GetSysFile
 *  returns an underlying system file object
 *  and starting offset to contiguous region
 *  suitable for memory mapping, or NULL if
 *  no such file is available.
 */
static
KSysFile *CC KSysFileGetSysFile ( const KSysFile *self, uint64_t *offset )
{
    * offset = 0;
    return ( KSysFile* ) self;
}

/* RandomAccess
 *  returns 0 if random access, error code otherwise
 */
static
rc_t CC KSysDiskFileRandomAccess ( const KSysFile *self )
{
    return 0;
}
static
rc_t CC KSysFileRandomAccess ( const KSysFile *self )
{
/*     return RC ( rcFS, rcFile, rcAccessing, rcFileDesc, rcIncorrect ); */
        return RC ( rcFS, rcFile, rcAccessing, rcFunction, rcUnsupported );
}


/* Type
 *  returns a KFileDesc
 *  not intended to be a content type,
 *  but rather an implementation class
 */
static
uint32_t CC KSysFileType ( const KSysFile *self )
{
    switch ( GetFileType ( self -> handle ) )
    {
    case FILE_TYPE_DISK:
        return kfdFile;
    case FILE_TYPE_CHAR:
        return kfdCharDev;
    case FILE_TYPE_PIPE:
        return kfdSocket;
    }

    return kfdInvalid;
}


/* Size
 *  returns size in bytes of file
 *
 *  "size" [ OUT ] - return parameter for file size
 */
static
rc_t CC KSysDiskFileSize ( const KSysFile *self, uint64_t *size )
{
    LARGE_INTEGER sz;

    if ( GetFileSizeEx ( self -> handle, & sz ) == 0 )
    {
        rc_t rc;
        DWORD last_error;

        last_error = GetLastError ();
        switch ( last_error )
        {
        case ERROR_INVALID_HANDLE:
            rc = RC ( rcFS, rcFile, rcAccessing, rcFileDesc, rcInvalid );
            break;
        default:
            rc = RC ( rcFS, rcFile, rcAccessing, rcNoObj, rcUnknown );
            break;
        }
        PLOGERR (klogErr,
                 (klogErr, rc, "error accessing file system status - $(E)($(C))",
                  "E=%!,C=%u", last_error, last_error)); 
        return rc;
    }

    *size = sz . QuadPart;
    return 0;
}


static
rc_t CC KSysFileSize ( const KSysFile *self, uint64_t *size )
{
    *size = 0;
/*     return RC ( rcFS, rcFile, rcAccessing, rcFileDesc, rcIncorrect ); */
    return RC ( rcFS, rcFile, rcAccessing, rcFunction, rcUnsupported );
}


/* SetSize
 *  sets size in bytes of file
 *
 *  "size" [ IN ] - new file size
 */
static
rc_t CC KSysDiskFileSetSize ( KSysFile *self, uint64_t size )
{
    rc_t rc = 1;
    LARGE_INTEGER p;

    p . QuadPart = size;

    if ( SetFilePointerEx ( self -> handle, p, & p, FILE_BEGIN ) )
    {
        self -> pos = size;
        if ( SetEndOfFile( self -> handle ) )
        {
            rc = 0;
        }
    }

    if ( rc != 0 )
    {
        DWORD last_error;

        last_error = GetLastError ();
        switch ( last_error  )
        {
        case ERROR_INVALID_HANDLE:
            rc = RC ( rcFS, rcFile, rcUpdating, rcFileDesc, rcInvalid );
        default:
            rc = RC ( rcFS, rcFile, rcUpdating, rcNoObj, rcUnknown );
        }
        PLOGERR (klogErr,
                 (klogErr, rc, "error accessing file system status - $(E)($(C))",
                  "E=%!,C=%u", last_error, last_error)); 
    }
    return rc;
}


static
rc_t CC KSysFileSetSize ( KSysFile *self, uint64_t size )
{
/*     return RC ( rcFS, rcFile, rcUpdating, rcFileDesc, rcIncorrect ); */
        return RC ( rcFS, rcFile, rcUpdating, rcFunction, rcUnsupported );
}


/* Read
 *  read file from known position
 *
 *  "pos" [ IN ] - starting position within file
 *
 *  "buffer" [ OUT ] and "bsize" [ IN ] - return buffer for read
 *
 *  "num_read" [ OUT, NULL OKAY ] - optional return parameter
 *  giving number of bytes actually read
 */
static
rc_t KSysFileReadCommon ( const KSysFile *cself,
    void *buffer, size_t bsize, size_t *num_read )
{
    DWORD to_read, bytes_read;
    KSysFile *self = ( KSysFile* ) cself;

    to_read = ( DWORD ) bsize;
    if ( sizeof bsize > sizeof to_read && ( size_t ) to_read != bsize )
        to_read = ~ 0U;

    for (;;)
    {
        bytes_read = 0;
        if ( ReadFile ( self -> handle, buffer, to_read, & bytes_read, NULL ) == 0 )
        {
            rc_t rc;
            DWORD last_error;

            switch ( last_error = GetLastError () )
            {
            case ERROR_HANDLE_EOF:
                break;
            case ERROR_IO_PENDING:
                continue; 
           }
        }
        
        self -> pos += bytes_read;
        * num_read = bytes_read;
        break;
    }

    return 0;
}

static
rc_t CC KSysDiskFileRead ( const KSysFile *cself, uint64_t pos,
    void *buffer, size_t bsize, size_t *num_read )
{
    DWORD to_read, bytes_read;
    KSysFile *self = ( KSysFile* ) cself;

    if ( self -> pos != pos )
    {
        LARGE_INTEGER p;

        if ( !GetFileSizeEx( self -> handle, &p ) )
        {
            rc_t rc;
            DWORD last_error;

            last_error = GetLastError ();
            switch ( last_error )
            {
            case ERROR_INVALID_HANDLE:
                rc = RC ( rcFS, rcFile, rcPositioning, rcFileDesc, rcInvalid );
                break;
            default:
                rc = RC ( rcFS, rcFile, rcPositioning, rcNoObj, rcUnknown );
                break;
            }
            PLOGERR (klogErr,
                     (klogErr, rc, "error positioning system file - $(E)($(C))",
                      "E=%!,C=%u", last_error, last_error)); 
            return rc;
        }

        /* if we try to read behind the end of the file... */
        if ( pos > p . QuadPart )
        {
            return RC ( rcFS, rcFile, rcPositioning, rcFileDesc, rcInvalid );
        }

        p . QuadPart = pos;
        if ( !SetFilePointerEx ( self -> handle, p, & p, FILE_BEGIN ) )
        {
            rc_t rc;
            DWORD last_error;

            last_error = GetLastError ();
            switch ( last_error )
            {
            case ERROR_INVALID_HANDLE:
                rc = RC ( rcFS, rcFile, rcPositioning, rcFileDesc, rcInvalid );
                PLOGERR (klogErr,
                         (klogErr, rc, "invalid system file handle - $(E)($(C))",
                          "E=%!,C=%u", last_error, last_error)); 
                return rc;
            default:
                rc = RC ( rcFS, rcFile, rcPositioning, rcNoObj, rcUnknown );
                PLOGERR (klogErr,
                         (klogErr, rc, "error positioning system file - $(E)($(C))",
                          "E=%!,C=%u", last_error, last_error)); 
                return rc;
            }
        }

        self -> pos = p . QuadPart;
        if ( pos != p . QuadPart )
        {
            if ( pos > (uint64_t)( p . QuadPart ) )
            {
                * num_read = 0;
                return 0;
            }

            return RC ( rcFS, rcFile, rcPositioning, rcNoObj, rcUnknown );
        }
    }

    return KSysFileReadCommon ( cself, buffer, bsize, num_read );
}

static
rc_t CC KSysFileRead ( const KSysFile *cself, uint64_t pos,
    void *buffer, size_t bsize, size_t *num_read )
{
    DWORD to_read, bytes_read;
    KSysFile *self = ( KSysFile* ) cself;

    if ( self -> pos != pos )
    {
        *num_read = 0;
        return RC ( rcFS, rcFile, rcPositioning, rcFileDesc, rcIncorrect );
    }

    return KSysFileReadCommon ( cself, buffer, bsize, num_read );
}


/* Write
 *  write file at known position
 *
 *  "pos" [ IN ] - starting position within file
 *
 *  "buffer" [ IN ] and "size" [ IN ] - data to be written
 *
 *  "num_writ" [ OUT, NULL OKAY ] - optional return parameter
 *  giving number of bytes actually written
 */

#define WINDOWS_HEAP_LIMIT (32*1024)

static
rc_t KSysFileWriteCommon ( KSysFile *self, 
    const void *_buffer, size_t size, size_t *num_writ)
{
    DWORD to_write;
    DWORD bytes_writ;
    DWORD iter_writ;
    const char * buffer = _buffer;

    to_write = (DWORD) size;

    if (sizeof size > sizeof to_write && ( size_t ) to_write != size )
        to_write = ~ 0U;

    for ( bytes_writ = 0; to_write > 0; )
    {
        DWORD iter_to_write = 
            (to_write > WINDOWS_HEAP_LIMIT)? WINDOWS_HEAP_LIMIT : to_write;

        
        for (iter_writ = 0;
             ! WriteFile ( self -> handle, buffer, iter_to_write, &iter_writ, NULL);
             iter_writ = 0)
        {
            rc_t rc;
            DWORD last_error;
            const char * fmt;

            last_error = GetLastError ();
            switch ( last_error )
            {
            case ERROR_IO_PENDING:
                Sleep (100); /* sure let's give it a chance to settle */
                if (iter_writ)
                {
                    buffer += iter_writ;
                    iter_to_write -= iter_writ;
                    to_write -= iter_writ;
                    bytes_writ += iter_writ;
                }
                continue; /* back to while() */
 
            case ERROR_INVALID_HANDLE:
                rc = RC ( rcFS, rcFile, rcWriting, rcFileDesc, rcInvalid );
                fmt = "invalid system file handle - $(E)($(C))";
                break;

            case ERROR_NOT_ENOUGH_MEMORY:
                rc = RC (rcFS, rcFile, rcWriting, rcMemory, rcExhausted);
                fmt = "error out of memory for WindoowsWriteFile - $(E)($(C))";
                break;

            default:
                rc = RC ( rcFS, rcFile, rcWriting, rcNoObj, rcUnknown );
                fmt = "error writing system file - $(E)($(C))";
                break;
            }

            PLOGERR (klogErr,
                     (klogErr, rc, fmt, "E=%!,C=%u", last_error, last_error)); 
            return rc;
        }
        buffer += iter_writ;
        to_write -= iter_writ;
        bytes_writ += iter_writ;
    }

    self -> pos += bytes_writ;

    if (num_writ != NULL)
        * num_writ = (size_t)bytes_writ;

    return 0;
}
static
rc_t CC KSysDiskFileWrite ( KSysFile *self, uint64_t pos,
    const void *buffer, size_t size, size_t *num_writ)
{
    if ( self -> pos != pos )
    {
        LARGE_INTEGER p;

        p . QuadPart = pos;

        if ( SetFilePointerEx ( self -> handle, p, & p, FILE_BEGIN ) == 0 )
        {
            rc_t rc;
            DWORD last_error;

            last_error = GetLastError ();
            switch ( last_error )
            {
            case ERROR_INVALID_HANDLE:
                rc = RC ( rcFS, rcFile, rcWriting, rcFileDesc, rcInvalid );
                PLOGERR (klogErr,
                         (klogErr, rc, "invalid system file handle - $(E)($(C))",
                          "E=%!,C=%u", last_error, last_error)); 
                break;
            default:
                rc = RC ( rcFS, rcFile, rcWriting, rcNoObj, rcUnknown );
                PLOGERR (klogErr,
                         (klogErr, rc, "error writing system file - $(E)($(C))",
                          "E=%!,C=%u", last_error, last_error)); 
                break;
            }
            return rc;
        }

        self -> pos = p . QuadPart;
        if ( pos != p . QuadPart )
        {
            return RC ( rcFS, rcFile, rcPositioning, rcNoObj, rcUnknown );
        }
    }

    return KSysFileWriteCommon ( self, buffer, size, num_writ );
}
static
rc_t CC KSysFileWrite ( KSysFile *self, uint64_t pos,
    const void *buffer, size_t size, size_t *num_writ)
{
    if ( self -> pos != pos )
    {
        *num_writ = 0;
        return RC ( rcFS, rcFile, rcPositioning, rcFileDesc, rcIncorrect );
    }

    return KSysFileWriteCommon ( self, buffer, size, num_writ );
}


/* Make
 *  create a new file object
 *  from file descriptor
 */
static const KFile_vt_v1 vtKSysDiskFile =
{
    /* version 1.1 */
    1, 1,

    /* start minor version 0 methods */
    KSysFileDestroy,
    KSysFileGetSysFile,
    KSysDiskFileRandomAccess,
    KSysDiskFileSize,
    KSysDiskFileSetSize,
    KSysDiskFileRead,
    KSysDiskFileWrite,
    /* end minor version 0 methods */

    /* start minor version == 1 */
    KSysFileType
    /* end minor version == 1 */
};
static const KFile_vt_v1 vtKSysOtherFile =
{
    /* version 1.1 */
    1, 1,

    /* start minor version 0 methods */
    KSysFileDestroy,
    KSysFileGetSysFile,
    KSysFileRandomAccess,
    KSysFileSize,
    KSysFileSetSize,
    KSysFileRead,
    KSysFileWrite,
    /* end minor version 0 methods */

    /* start minor version == 1 */
    KSysFileType
    /* end minor version == 1 */
};
static const KFile_vt_v1 vtKSysStdIODiskFile =
{
    /* version 1.1 */
    1, 1,

    /* start minor version 0 methods */
    KSysStdIOFileDestroy,
    KSysFileGetSysFile,
    KSysDiskFileRandomAccess,
    KSysDiskFileSize,
    KSysDiskFileSetSize,
    KSysDiskFileRead,
    KSysDiskFileWrite,
    /* end minor version 0 methods */

    /* start minor version == 1 */
    KSysFileType
    /* end minor version == 1 */
};
static const KFile_vt_v1 vtKSysStdIOOtherFile =
{
    /* version 1.1 */
    1, 1,

    /* start minor version 0 methods */
    KSysStdIOFileDestroy,
    KSysFileGetSysFile,
    KSysFileRandomAccess,
    KSysFileSize,
    KSysFileSetSize,
    KSysFileRead,
    KSysFileWrite,
    /* end minor version 0 methods */

    /* start minor version == 1 */
    KSysFileType
    /* end minor version == 1 */
};


static
rc_t KSysFileMakeVT ( KSysFile **fp, HANDLE fd, const KFile_vt *vt,
    uint64_t initial_pos, bool read_enabled, bool write_enabled )
{
    rc_t rc;
    KSysFile *f;

    if( fd == INVALID_HANDLE_VALUE ) /* ? */
        return RC ( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );

    f = malloc ( sizeof *f );
    if ( f == NULL )
        rc = RC(rcFS, rcFile, rcConstructing, rcMemory, rcExhausted);
    else
    {
        rc = KFileInit ( & f -> dad, vt, read_enabled, write_enabled );
        if ( rc == 0 )
        {
            f -> handle = fd;
	    f -> pos = initial_pos;
            *fp = f;
            return 0;
        }

        free ( f );
    }
    return rc;
}

/* bit flags */
#define ISDISK  1
#define ISSTDIO 2

static
rc_t KSysFileMakeInt ( KSysFile **fp, HANDLE fd, bool read_enabled, bool write_enabled, unsigned flags )
{
    DWORD ret;
    const KFile_vt * vt;
    uint64_t initial_pos;

    if (fp == NULL)
        return RC (rcFS, rcFile, rcConstructing, rcSelf, rcNull);

    *fp = NULL; /* pre-fail */

    ret = GetFileType (fd);

    switch (ret)
    {
    case FILE_TYPE_DISK:
        flags |= ISDISK;
	initial_pos = -1;
        break;

    case FILE_TYPE_UNKNOWN:
        ret = GetLastError();
        switch (ret)
        {
        default:
            return RC (rcFS, rcFile, rcConstructing, rcFileDesc, rcUnknown);

            /* specific errors can be added here */

        case NO_ERROR:
            break;
        }

	initial_pos = 0;
        flags &= ~ISDISK;
        break;

    default:
	initial_pos = 0;
        flags &= ~ISDISK;
        break;
    }

    switch (flags & (ISDISK|ISSTDIO))
    {
    case 0:
        vt = (const KFile_vt*)&vtKSysOtherFile;
        break;

    case ISDISK:
        vt = (const KFile_vt*)&vtKSysDiskFile;
        break;

    case ISSTDIO:
        vt = (const KFile_vt*)&vtKSysStdIOOtherFile;
        break;

    case ISDISK|ISSTDIO:
        vt = (const KFile_vt*)&vtKSysStdIODiskFile;
        break;
    }

    return KSysFileMakeVT ( fp, fd, vt, initial_pos, read_enabled, write_enabled );
}

/* extern, but internal to libkfs */
rc_t KSysFileMake ( KSysFile **fp, HANDLE fd, bool read_enabled, bool write_enabled )
{
    return KSysFileMakeInt ( fp, fd, read_enabled, write_enabled, 0 );
}


/* MakeStdIn
 *  creates a read-only file on stdin
 */
LIB_EXPORT rc_t CC KFileMakeStdIn ( const KFile **fp )
{
    HANDLE fd = GetStdHandle ( STD_INPUT_HANDLE );
    return KSysFileMakeInt ( (KSysFile**)fp, fd, true, false, ISSTDIO );
}

/* MakeStdOut
 * MakeStdErr
 *  creates a write-only file on stdout or stderr
 */
LIB_EXPORT rc_t CC KFileMakeStdOut ( KFile **fp )
{
    HANDLE fd = GetStdHandle ( STD_OUTPUT_HANDLE );
    return KSysFileMakeInt ( (KSysFile**)fp, fd, false, true, ISSTDIO );
}

LIB_EXPORT rc_t CC KFileMakeStdErr ( KFile **fp )
{
    HANDLE fd = GetStdHandle ( STD_ERROR_HANDLE );
    return KSysFileMakeInt ( (KSysFile**)fp, fd, false, true, ISSTDIO );
}

/* MakeFDFile
 *  creates a file from a file-descriptor
 *  not supported under Windows
 */
LIB_EXPORT rc_t CC KFileMakeFDFileRead ( const KFile **f, int fd )
{
    return RC (rcFS, rcFile, rcConstructing, rcFunction, rcUnsupported);
}

LIB_EXPORT rc_t CC KFileMakeFDFileWrite ( KFile **f, bool update, int fd )
{
    return RC (rcFS, rcFile, rcConstructing, rcFunction, rcUnsupported);
}
