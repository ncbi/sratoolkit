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
#ifndef _h_entrez_fetcher_
#define _h_entrez_fetcher_

#ifndef _h_url_fetcher_
#include <kns/url-fetcher.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * KEntrezFetcher
 *  everything to fetch DNA-text from entrez - server
 */
typedef struct KEntrezFetcher KEntrezFetcher;


/* Make
 *  create a fetcher object
 */
rc_t KEntrezFetcherMake ( KEntrezFetcher **fetcher, KUrlFetcher * url_fetcher );

/* Setup
 *  prepares a fetch and indicates to caller the buffer size
 *
 *  "server" [ IN ] - NUL terminated URI, e.g.
 *     "http://eutils.ncbi.nlm.nih.gov/entrez/eutils/efetch.fcgi"
 *
 *  "seq_id" [ IN ] - NUL terminated sequence identifier, e.g. "NC_000001"
 *  max_seq_len : 1024
 *  row_id      : 11
 *  row_count   : 1
 */
rc_t KEntrezFetcherSetup ( KEntrezFetcher *self,
    const char * server, const char * seq_id, 
    const size_t max_seq_len, const uint64_t row_id, const size_t row_count,
    size_t * buffsize );


/* Setup
 *  prepares a fetch by giving it a usable url
 */
rc_t KEntrezFetcherSetupUri ( KEntrezFetcher *self, const char * uri );


/* AddRef
 * Release
 */
rc_t KEntrezFetcherAddRef ( const KEntrezFetcher *self );
rc_t KEntrezFetcherRelease ( const KEntrezFetcher *self );

/* Read
 *  reads data from prepared URI
 */
rc_t KEntrezFetcherRead ( KEntrezFetcher *self,
                       void *dst, size_t dst_size, size_t *num_read );

#ifdef __cplusplus
}
#endif

#endif /* _h_entrez_fetcher_ */
