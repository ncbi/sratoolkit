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
#ifndef _h_kns_mgr_priv_
#define _h_kns_mgr_priv_

#ifndef _h_klib_refcount_
#include <klib/refcount.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <klib/rc.h>

#include "curlhdr/curl.h"
#include "curlhdr/easy.h"

/* this is the hidden manager-struct: a refcount with some function-pointer's */
struct KNSManager
{
    KRefcount refcount;

    rc_t create_rc;
    
    /* curl-easy-function-pointers... */
    CURL*    ( CC * curl_easy_init_fkt )    ( void );
    void     ( CC * curl_easy_cleanup_fkt ) ( CURL * handle );
    CURLcode ( CC * curl_easy_setopt_fkt )  ( CURL *handle, CURLoption option, ... );
    CURLcode ( CC * curl_easy_perform_fkt ) ( CURL * handle );
    CURLcode ( CC * curl_easy_getinfo_fkt ) ( CURL *curl, CURLINFO info, ... );
    char *   ( CC * curl_version_fkt )      ( void );
    struct curl_slist* ( CC * curl_slist_append_fkt ) ( struct curl_slist * list, const char * string );
    void ( CC * curl_slist_free_all_fkt ) ( struct curl_slist * list );
    
    bool verbose;
};


rc_t KNSManagerInit ( struct KNSManager *self );    /* in kns/unix/sysmgr.c or kns/win/sysmgr.c */
void KNSManagerCleanup ( struct KNSManager *self );


#ifdef __cplusplus
}
#endif

#endif
