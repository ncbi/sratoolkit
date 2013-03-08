/*===========================================================================
*
*                            Public Domain Notice
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

#ifndef _h_vfs_path_priv_
#define _h_vfs_path_priv_

#ifndef _h_vfs_extern_
#include <vfs/extern.h>
#endif

#ifndef _h_vfs_path_
#include <vfs/path.h>
#endif

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
typedef int32_t VPUri_t;
enum eVPUri_t
{
    vpuri_invalid = -1,
    vpuri_none, 
    vpuri_not_supported,
    vpuri_ncbi_file,
    vpuri_ncbi_vfs = vpuri_ncbi_file,
#if SUPPORT_FILE_URL
    vpuri_file,
#endif
    vpuri_ncbi_acc,
    vpuri_http,
    vpuri_ftp,
    vpuri_ncbi_legrefseq,
    vpuri_count
};
*/

#define NCBI_FILE_SCHEME       "ncbi-file"
#define NCBI_ACCESSION_SCHEME  "ncbi-acc"
#define HTTP_SCHEME            "http"
#define FTP_SCHEME             "ftp"
#define NCBI_LEGREFSEQ_SCHEME  "x-ncbi-legrefseq"

VFS_EXTERN VPUri_t VPathGetUri_t (const VPath * self);




/* options for a VPath possibly obtained from a query string on an URI */
typedef uint32_t VPOption_t;
enum eVPOption_t
{
    vpopt_encrypted,
    vpopt_pwpath,
    vpopt_pwfd,
    vpopt_readgroup,
    vpopt_temporary_pw_hack,
    vpopt_vdb_ctx,
    vpopt_gap_ticket, 
    vpopt_count
};


/* =====
 * Much of what follows is expected to move into the interface vfs/path.h
 * once it becomes supported.
 */

/* MakeFmt
 *  make a path object from a format string plus arguments
 *
 *  "new_path" [ OUT ] - a reference to the new object.
 *
 *  "fmt" [ IN ] and "args" [ IN ] - arguments to string_printf
 *  ( see <klib/text.h> ) to build a NUL-terminated string
 *  that conforms with the rules for "posix_path"
 *
 * NB - SECURITY RISK IF USED DIRECTLY FROM EXTERNAL STRINGS.
 *      ALSO, FMT IS **NOT** PRINTF COMPATIBLE - see string_printf.
 */
VFS_EXTERN rc_t CC VPathMakeFmt ( VPath ** new_path, const char * fmt, ... );
VFS_EXTERN rc_t CC VPathMakeVFmt ( VPath ** new_path, const char * fmt, va_list args );


/* Bill - I don't know that we're ready to export these yet */
VFS_EXTERN rc_t CC VPathMakeRelative ( VPath ** new_path, const VPath * base_path,
                                       const char * relative_path );
VFS_EXTERN rc_t CC VPathMakeRelativeFmt ( VPath ** new_path, const VPath * base_path,
                                          const char * fmt, ... );
VFS_EXTERN rc_t CC VPathVMakeRelativeFmt ( VPath ** new_path, const VPath * base_path,
                                           const char * fmt, va_list args );
VFS_EXTERN rc_t CC VPathMakeCurrentPath ( VPath ** new_path );

VFS_EXTERN rc_t CC VPathMakeURI ( VPath ** new_path, const char * uri );


VFS_EXTERN rc_t CC VPathReadPath ( const VPath * self, char * buffer,
                                  size_t buffer_size, size_t * num_read);

VFS_EXTERN rc_t CC VPathOption ( const VPath * self, VPOption_t option,
                                char * buffer, size_t buffer_size,
                                size_t * num_read);

/*
 * copy the path string out of a KPath object.
 * if buffer is too short an RC (rcBuffer, rcInsufficient) is returned
 * and nothing is copied.
 * if there is a room the NUL is copied as well
 */
VFS_EXTERN rc_t CC VPathReadPath ( const VPath * self, char * buffer,
                                  size_t buffer_size, size_t * num_read);


/* get current working directory */
VFS_EXTERN rc_t CC VPathGetCWD (char * buffer, size_t buffer_size);



#ifdef __cplusplus
}
#endif

#endif /* _h_vfs_path_priv_ */
