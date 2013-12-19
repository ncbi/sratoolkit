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

#ifndef _h_http_priv_
#define _h_http_priv_

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#ifndef _h_klib_text_
#include <klib/text.h>
#endif

#ifndef _h_klib_data_buffer
#include <klib/data-buffer.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
    
struct KFile;
struct KHttp;

/*--------------------------------------------------------------------------
 * URLBlock
 *  RFC 3986
 */
typedef struct URLBlock URLBlock;
struct URLBlock
{
    String scheme;
    String host;
    String path; /* Path includes any parameter portion */
    String query;
    String fragment;

    uint32_t port;
};
extern void URLBlockInit ( URLBlock *self );
extern rc_t ParseUrl ( URLBlock * b, const char * url, size_t url_size );

/*--------------------------------------------------------------------------
 * KHttpHeader
 *  node structure to place http header lines into a BSTree
 */
typedef struct KHttpHeader KHttpHeader;
struct KHttpHeader
{
    BSTNode dad;
    String name;
    String value;
    KDataBuffer value_storage;
};
    
extern void KHttpHeaderWhack ( BSTNode *n, void *ignore );
extern rc_t KHttpGetHeaderLine ( struct KHttp *self, BSTree *hdrs, bool *blank, bool *close_connection );
extern rc_t KHttpGetStatusLine ( struct KHttp *self, String *msg, uint32_t *status, ver_t *version );

/* exported private functions
*/
extern rc_t HttpTest ( const struct KFile *input );
extern void URLBlockInitTest ( void );
extern rc_t ParseUrlTest ( void );
extern rc_t MakeRequestTest ( void );    

#ifdef __cplusplus
}
#endif

#endif /* _h_kttp_priv_ */


