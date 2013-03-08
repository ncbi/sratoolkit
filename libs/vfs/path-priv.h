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

#ifndef _h_vfs_path_priv_libs_
#define _h_vfs_path_priv_libs_

#include <vfs/extern.h>
#include <klib/defs.h>
#include <vfs/path.h>
#include <vfs/path-priv.h>
#include <klib/text.h>
#include <klib/refcount.h>
#include <klib/container.h>

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef USE_VRESOLVER
#define USE_VRESOLVER 1
#endif

#ifndef SUPPORT_FILE_URL
#define SUPPORT_FILE_URL 1
#endif

#ifndef USE_EXPERIMENTAL_CODE
#define USE_EXPERIMENTAL_CODE 1
#endif

#ifndef USE_VPATH_OPTIONS_STRINGS
#define USE_VPATH_OPTIONS_STRINGS 0
#endif

#ifdef _DEBUGGING
#define PATH_DEBUG(msg) DBGMSG(DBG_VFS,DBG_FLAG(DBG_VFS_PATH), msg)
#else
#define PATH_DEBUG(msg)
#endif
#define OFF_PATH_DEBUG(msg)


typedef struct VPOption VPOption;
struct VPOption
{
    BSTNode node;
    VPOption_t name;
    String value;
/*     const char * value; */
};

struct VPath
{
#if USE_VPATH_OPTIONS_STRINGS
    const VPath * root;

    KRefcount refcount;

    String fullpath;

    String scheme;


#else
    const VPath * root;
    KRefcount refcount;
    String path;
    BSTree options;   /* query section of an uri; maybe set other ways as well. */
    char * query;
    char * fragment;
    size_t alloc_size;  /* how much extra space allocated for a path too long for the built in buffer */
    size_t asciz_size;  /* doubles as allocated size -1 if less than the size of the buffer below */
    VPUri_t scheme;
    char * storage;
#endif
};


/* not externally callable */
rc_t CC VPathTransformSysPath (VPath * self);
rc_t VPathTransformPathHier (char ** uri_path);

rc_t VPathInitAuthority (VPath * self, char ** next);


#ifdef __cplusplus
}
#endif

#endif /* _h_vfs_path_priv_ */
