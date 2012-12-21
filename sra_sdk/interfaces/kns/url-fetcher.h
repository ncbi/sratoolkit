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
#ifndef _h_url_fetcher_
#define _h_url_fetcher_

#ifndef _h_kns_extern_
#include <kns/extern.h>
#endif

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * KUrlFetcher
 *  a generic way of fetching data from a url
 */
typedef struct KUrlFetcher KUrlFetcher;

/* AddRef
 * Release
 *  ignores NULL references
 */
KNS_EXTERN rc_t CC KUrlFetcherAddRef ( const KUrlFetcher *self );
KNS_EXTERN rc_t CC KUrlFetcherRelease ( const KUrlFetcher *self );

KNS_EXTERN rc_t CC KUrlFetcherRead( KUrlFetcher *self, const char *uri,
                      void *dst, size_t to_read, size_t *num_read );

/* this typedef has to stay here for the virtual interface to work...*/
typedef struct KUrlFetcherCurl KUrlFetcherCurl;

KNS_EXTERN rc_t CC KUrlFetcherCurlMake( KUrlFetcher **fetcher, const bool verbose );

#ifdef __cplusplus
}
#endif

#endif
