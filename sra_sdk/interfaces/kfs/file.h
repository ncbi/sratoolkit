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

#ifndef _h_kfs_file_
#define _h_kfs_file_

#ifndef _h_kfs_extern_
#include <kfs/extern.h>
#endif

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * KFileDesc
 *  describes basic file types
 */
enum KFileDesc
{
    kfdNull,
    kfdInvalid,
    kfdFile,
    kfdCharDev,
    kfdBlockDev,
    kfdFIFO,
    kfdSocket,

    /* the type enum may be extended */
    kfdLastDefined
};


/*--------------------------------------------------------------------------
 * KFile
 *  a file is normally created with a KDirectory
 *  optionally, the 3 standard i/o files may be created directly
 */
typedef struct KFile KFile;

/* AddRef
 * Release
 *  ignores NULL references
 */
KFS_EXTERN rc_t CC KFileAddRef ( const KFile *self );
KFS_EXTERN rc_t CC KFileRelease ( const KFile *self );

/* RandomAccess
 *  ALMOST by definition, the file is random access
 *
 *  certain file types will refuse random access
 *  these include FIFO and socket based files, but also
 *  wrappers that require serial access ( e.g. compression )
 *
 *  returns 0 if random access, error code otherwise
 */
KFS_EXTERN rc_t CC KFileRandomAccess ( const KFile *self );

/* Type
 *  returns a KFileDesc
 *  not intended to be a content type,
 *  but rather an implementation class
 */
KFS_EXTERN uint32_t CC KFileType ( const KFile *self );

/* Size
 *  returns size in bytes of file
 *
 *  "size" [ OUT ] - return parameter for file size
 */
KFS_EXTERN rc_t CC KFileSize ( const KFile *self, uint64_t *size );

/* SetSize
 *  sets size in bytes of file
 *
 *  "size" [ IN ] - new file size
 */
KFS_EXTERN rc_t CC KFileSetSize ( KFile *self, uint64_t size );

/* Read
 *  read file from known position
 *
 *  "pos" [ IN ] - starting position within file
 *
 *  "buffer" [ OUT ] and "bsize" [ IN ] - return buffer for read
 *
 *  "num_read" [ OUT ] - return parameter giving number of bytes
 *  actually read. when returned value is zero and return code is
 *  also zero, interpreted as end of file.
 */
KFS_EXTERN rc_t CC KFileRead ( const KFile *self, uint64_t pos,
    void *buffer, size_t bsize, size_t *num_read );

/* ReadAll
 *  read from file until "bsize" bytes have been retrieved
 *  or until end-of-input
 *
 *  "pos" [ IN ] - starting position within file
 *
 *  "buffer" [ OUT ] and "bsize" [ IN ] - return buffer for read
 *
 *  "num_read" [ OUT ] - return parameter giving number of bytes
 *  actually read. when returned value is zero and return code is
 *  also zero, interpreted as end of file.
 */
KFS_EXTERN rc_t CC KFileReadAll ( const KFile *self, uint64_t pos,
    void *buffer, size_t bsize, size_t *num_read );

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
KFS_EXTERN rc_t CC KFileWrite ( KFile *self, uint64_t pos,
    const void *buffer, size_t size, size_t *num_writ );

/* WriteAll
 *  write from file until "size" bytes have been transferred
 *  or until no further progress can be made
 *
 *  "pos" [ IN ] - starting position within file
 *
 *  "buffer" [ IN ] and "size" [ IN ] - data to be written
 *
 *  "num_writ" [ OUT, NULL OKAY ] - optional return parameter
 *  giving number of bytes actually written
 */
KFS_EXTERN rc_t CC KFileWriteAll ( KFile *self, uint64_t pos,
    const void *buffer, size_t size, size_t *num_writ );

/* MakeStdIn
 *  creates a read-only file on stdin
 */
KFS_EXTERN rc_t CC KFileMakeStdIn ( const KFile **std_in );

/* MakeStdOut
 * MakeStdErr
 *  creates a write-only file on stdout or stderr
 */
KFS_EXTERN rc_t CC KFileMakeStdOut ( KFile **std_out );
KFS_EXTERN rc_t CC KFileMakeStdErr ( KFile **std_err );

#ifdef __cplusplus
}
#endif

#endif /* _h_kfs_file_ */
