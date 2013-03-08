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

#ifndef _h_kfs_impl_
#define _h_kfs_impl_

#ifndef _h_kfs_extern_
#include <kfs/extern.h>
#endif

#ifndef _h_kfs_file_
#include <kfs/file.h>
#endif

#ifndef _h_kfs_directory_
#include <kfs/directory.h>
#endif

#ifndef _h_kfs_arrayfile_
#include <kfs/arrayfile.h>
#endif

#ifndef _h_klib_refcount_
#include <klib/refcount.h>
#endif

#ifndef _h_klib_namelist_
#include <klib/namelist.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * forwards
 */
struct KBufFile;
struct KSysDir;
struct KSysFile;
typedef union KFile_vt KFile_vt;
typedef union KDirectory_vt KDirectory_vt;


/*--------------------------------------------------------------------------
 * KFile
 *  a virtual file
 */
struct KFile
{
    const KFile_vt *vt;
    const KDirectory *dir;
    KRefcount refcount;
    uint8_t read_enabled;
    uint8_t write_enabled;
    uint8_t align [ 2 ];
};

#ifndef KFILE_IMPL
#define KFILE_IMPL struct KFile
#endif

typedef struct KFile_vt_v1 KFile_vt_v1;
struct KFile_vt_v1
{
    /* version == 1.x */
    uint32_t maj;
    uint32_t min;

    /* start minor version == 0 */
    rc_t ( CC * destroy ) ( KFILE_IMPL *self );
    struct KSysFile* ( CC * get_sysfile ) ( const KFILE_IMPL *self, uint64_t *offset );
    rc_t ( CC * random_access ) ( const KFILE_IMPL *self );
    rc_t ( CC * get_size ) ( const KFILE_IMPL *self, uint64_t *size );
    rc_t ( CC * set_size ) ( KFILE_IMPL *self, uint64_t size );
    /* num_read and num_writ are guaranteed non-NULL */
    rc_t ( CC * read ) ( const KFILE_IMPL *self, uint64_t pos,
        void *buffer, size_t bsize, size_t *num_read );
    rc_t ( CC * write ) ( KFILE_IMPL *self, uint64_t pos,
        const void *buffer, size_t size, size_t *num_writ );
    /* end minor version == 0 */

    /* start minor version == 1 */
    uint32_t ( CC * get_type ) ( const KFILE_IMPL * self );
    /* end minor version == 1 */

    /* ANY NEW ENTRIES MUST BE REFLECTED IN libs/kfs/file.c
       BY BOTH THE CORRESPONDING MESSAGE DISPATCH FUNCTION(s) AND
       VTABLE VALIDITY CHECKS IN KFileInit */
};

union KFile_vt
{
    KFile_vt_v1 v1;
};

/* Init
 *  initialize a newly allocated file object
 */
KFS_EXTERN rc_t CC KFileInit ( KFile *self, const KFile_vt *vt,
    const char *classname, const char *fname,
    bool read_enabled, bool write_enabled );

/* Destroy
 *  destroy file
 */
KFS_EXTERN rc_t CC KFileDestroy ( KFile *self );

/* GetSysFile
 *  returns an underlying system file object
 *  and starting offset to contiguous region
 *  suitable for memory mapping, or NULL if
 *  no such file is available.
 */
KFS_EXTERN struct KSysFile* CC KFileGetSysFile ( const KFile *self, uint64_t *offset );


/*--------------------------------------------------------------------------
 * KDirectory
 *  a virtual directory
 */
struct KDirectory
{
    const KDirectory_vt *vt;
    KRefcount refcount;
    uint8_t read_only;
    uint8_t align [ 3 ];
};

#ifndef KDIR_IMPL
#define KDIR_IMPL KDirectory
#endif

typedef struct KDirectory_vt_v1 KDirectory_vt_v1;
struct KDirectory_vt_v1
{
    /* version == 1.x */
    uint32_t maj;
    uint32_t min;

    /* start minor version == 0 */
    rc_t ( CC * destroy ) ( KDIR_IMPL *self );
    rc_t ( CC * list_dir ) ( const KDIR_IMPL *self, struct KNamelist **list,
         bool ( CC * f ) ( const KDirectory *dir, const char *name, void *data ),
         void *data, const char *path, va_list args );
    rc_t ( CC * visit ) ( const KDIR_IMPL *self, bool recurse,
        rc_t ( CC * f ) ( const KDirectory*, uint32_t, const char*, void* ),
        void *data, const char *path, va_list args );
    rc_t ( CC * visit_update ) ( KDIR_IMPL *self, bool recurse,
        rc_t ( CC * f ) ( KDirectory*, uint32_t, const char*, void* ),
        void *data, const char *path, va_list args );
    uint32_t ( CC * path_type ) ( const KDIR_IMPL *self, const char *path, va_list args );
    rc_t ( CC * resolve_path ) ( const KDIR_IMPL *self, bool absolute,
        char *resolved, size_t rsize, const char *path, va_list args );
    rc_t ( CC * resolve_alias ) ( const KDIR_IMPL *self, bool absolute,
        char *resolved, size_t rsize, const char *alias, va_list args );
    rc_t ( CC * rename ) ( KDIR_IMPL *self, bool force, const char *from, const char *to );
    rc_t ( CC * remove ) ( KDIR_IMPL *self, bool force, const char *path, va_list args );
    rc_t ( CC * clear_dir ) ( KDIR_IMPL *self, bool force, const char *path, va_list args );
    rc_t ( CC * access ) ( const KDIR_IMPL *self,
        uint32_t *access, const char *path, va_list args );
    rc_t ( CC * set_access ) ( KDIR_IMPL *self, bool recurse,
        uint32_t access, uint32_t mask, const char *path, va_list args );
    rc_t ( CC * create_alias ) ( KDIR_IMPL *self, uint32_t access,
        KCreateMode mode, const char *targ, const char *alias );
    rc_t ( CC * open_file_read ) ( const KDIR_IMPL *self,
        const KFile **f, const char *path, va_list args );
    rc_t ( CC * open_file_write ) ( KDIR_IMPL *self,
        KFile **f, bool update, const char *path, va_list args );
    rc_t ( CC * create_file ) ( KDIR_IMPL *self, KFile **f, bool update,
        uint32_t access, KCreateMode mode, const char *path, va_list args );
    rc_t ( CC * file_size ) ( const KDIR_IMPL *self,
        uint64_t *size, const char *path, va_list args );
    rc_t ( CC * set_size ) ( KDIR_IMPL *self,
        uint64_t size, const char *path, va_list args );
    rc_t ( CC * open_dir_read ) ( const KDIR_IMPL *self,
        const KDirectory **sub, bool chroot, const char *path, va_list args );
    rc_t ( CC * open_dir_update ) ( KDIR_IMPL *self,
        KDirectory **sub, bool chroot, const char *path, va_list args );
    rc_t ( CC * create_dir ) ( KDIR_IMPL *self, uint32_t access,
        KCreateMode mode, const char *path, va_list args );

    /* optional destructor method - leave NULL if not needed */
    rc_t ( CC * destroy_file ) ( KDIR_IMPL *self, KFile *f );
    /* end minor version == 0 */

    /* start minor version == 1 */
    rc_t ( CC * date ) ( const KDIR_IMPL *self,
        KTime_t * date, const char *path, va_list args );
    rc_t ( CC * setdate ) ( KDIR_IMPL * self, bool recurse,
        KTime_t date, const char *path, va_list args );
    struct KSysDir* ( CC * get_sysdir ) ( const KDIR_IMPL *self );
    /* end minor version == 1 */

    /* start minor version == 2 */
    rc_t ( CC * file_locator ) ( const KDIR_IMPL *self,
        uint64_t *locator, const char *path, va_list args );
    /* end minor version == 2 */

    /* start minor version == 3 */
    rc_t ( CC * file_phys_size ) ( const KDIR_IMPL *self,
        uint64_t *phys_size, const char *path, va_list args );
    rc_t ( CC * file_contiguous ) ( const KDIR_IMPL *self,
        bool *contiguous, const char *path, va_list args );
    /* end minor version == 3 */

    /* ANY NEW ENTRIES MUST BE REFLECTED IN libs/kfs/directory.c
       BY BOTH THE CORRESPONDING MESSAGE DISPATCH FUNCTION(s) AND
       VTABLE VALIDITY CHECKS IN KDirectoryInit */
};

union KDirectory_vt
{
    KDirectory_vt_v1 v1;
};

/* Init
 *  initialize a newly allocated directory object
 */
KFS_EXTERN rc_t CC KDirectoryInit ( KDirectory *self, const KDirectory_vt *vt, 
    const char * class_name, const char * path, bool update );

/* DestroyFile
 *  does whatever is necessary with an unreferenced file
 */
KFS_EXTERN rc_t CC KDirectoryDestroyFile ( const KDirectory *self, KFile *f );

/* GetSysDir
 *  returns an underlying system file object
 */
KFS_EXTERN struct KSysDir* CC KDirectoryGetSysDir ( const KDirectory *self );


/* RealPath
 *  exposes functionality of system directory
 */
KFS_EXTERN rc_t CC KSysDirRealPath ( struct KSysDir const *self,
    char *real, size_t bsize, const char *path, ... );
KFS_EXTERN rc_t CC KSysDirVRealPath ( struct KSysDir const *self,
    char *real, size_t bsize, const char *path, va_list args );


/*--------------------------------------------------------------------------
 * KArrayFile
 *  an array-file is created from a KFile
 */
typedef union  KArrayFile_vt KArrayFile_vt;
struct KArrayFile
{
    const KArrayFile_vt *vt;
    KRefcount refcount;
    uint8_t read_enabled;
    uint8_t write_enabled;
    uint8_t align [ 2 ];
};

#ifndef KARRAYFILE_IMPL
#define KARRAYFILE_IMPL KArrayFile
#endif

typedef struct KArrayFile_vt_v1 KArrayFile_vt_v1;
struct KArrayFile_vt_v1
{
    /* version number */
    uint32_t maj, min;

    /* start minor version == 0 */
    rc_t ( CC * destroy ) ( KARRAYFILE_IMPL *self );
    rc_t ( CC * dimensionality ) ( const KARRAYFILE_IMPL *self, uint8_t *dim );
    rc_t ( CC * set_dimensionality ) ( KARRAYFILE_IMPL *self, uint8_t dim );
    rc_t ( CC * dim_extents ) ( const KARRAYFILE_IMPL *self, uint8_t dim, uint64_t *extents );
    rc_t ( CC * set_dim_extents ) ( KARRAYFILE_IMPL *self, uint8_t dim, uint64_t *extents );
    rc_t ( CC * element_size ) ( const KARRAYFILE_IMPL *self, uint64_t *elem_bits );
    rc_t ( CC * read ) ( const KARRAYFILE_IMPL *self, uint8_t dim,
        const uint64_t *pos, void *buffer, const uint64_t *elem_count,
        uint64_t *num_read );
    rc_t ( CC * write ) ( KARRAYFILE_IMPL *self, uint8_t dim,
        const uint64_t *pos, const void *buffer, const uint64_t *elem_count,
        uint64_t *num_writ );
    rc_t ( CC * get_meta ) ( const KARRAYFILE_IMPL *self, const char *key, 
                             const KNamelist **list );
    /* end minor version == 0 */
};

union KArrayFile_vt
{
    KArrayFile_vt_v1 v1;
};

/* Init
 *  initialize a newly allocated array-file object
 */
KFS_EXTERN rc_t CC KArrayFileInit ( KArrayFile *self, const KArrayFile_vt *vt, 
    bool read_enabled, bool write_enabled );


#ifdef __cplusplus
}
#endif

#endif /* _h_kfs_impl_ */
