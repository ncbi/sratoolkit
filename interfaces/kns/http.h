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

#ifndef _h_kns_http_
#define _h_kns_http_

#ifndef _h_kns_extern_
#include <kns/extern.h>
#endif

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * forwards
 */
struct KFile;
struct String;
struct KStream;
struct KNSManager;


/*--------------------------------------------------------------------------
 * KHttp
 *  hyper text transfer protocol
 */
typedef struct KHttp KHttp;


/* MakeHttp
 *  create an HTTP protocol
 *
 *  "http" [ OUT ] - return parameter for HTTP object
 *
 *  "conn" [ IN, NULL OKAY ] - previously opened stream for communications.
 *
 *  "vers" [ IN ] - http version
 *   the only legal types are 1.0 ( 0x01000000 ) and 1.1 ( 0x01010000 )
 *
 *  "host" [ IN ] - parameter to give the host dns name for the connection
 *
 *  "port" [ IN, DEFAULT ZERO ] - if zero, defaults to standard for scheme
 *   if non-zero, is taken as explicit port specification
 */
KNS_EXTERN rc_t CC KNSManagerMakeHttp ( struct KNSManager const *self,
    KHttp **http, struct KStream *conn, ver_t vers,
    struct String const *host, uint32_t port );


/* AddRef
 * Release
 *  ignores NULL references
 */
KNS_EXTERN rc_t CC KHttpAddRef ( const KHttp *self );
KNS_EXTERN rc_t CC KHttpRelease ( const KHttp *self );



/*--------------------------------------------------------------------------
 * KHttpFile
 *  a KFile over HTTP
 */

/* Make
 */
KNS_EXTERN rc_t CC KNSManagerMakeHttpFile ( struct KNSManager const *self,
    struct KFile const **file, struct KStream *conn, ver_t vers, const char *url, ... );
KNS_EXTERN rc_t CC KNSManagerVMakeHttpFile ( struct KNSManager const *self,
    struct KFile const **file, struct KStream *conn, ver_t vers, const char *url, va_list args );


/*--------------------------------------------------------------------------
 * KHttpRequest
 *  hyper text transfer protocol
 */
typedef struct KHttpRequest KHttpRequest;


/* MakeRequest
 *  create a request that can be used to contact HTTP server
 *
 *  "req" [ OUT ] - return parameter for HTTP request object
 *
 *  "vers" [ IN ] - http version
 *
 *  "conn" [ IN, NULL OKAY ] - previously opened stream for communications.
 *
 *  "url" [ IN ] - full resource identifier. if "conn" is NULL,
 *   the url is parsed for remote endpoint and is opened by mgr.
 */
KNS_EXTERN rc_t CC KHttpMakeRequest ( const KHttp *self,
    KHttpRequest **req, const char *url, ... );

KNS_EXTERN rc_t CC KNSManagerMakeRequest ( struct KNSManager const *self,
    KHttpRequest **req, ver_t version, struct KStream *conn, const char *url, ... );


/* AddRef
 * Release
 *  ignores NULL references
 */
KNS_EXTERN rc_t CC KHttpRequestAddRef ( const KHttpRequest *self );
KNS_EXTERN rc_t CC KHttpRequestRelease ( const KHttpRequest *self );


/* Connection
 *  sets connection management headers
 *
 *  "close" [ IN ] - if "true", inform the server to close the connection
 *   after its response ( default for version 1.0 ). when "false" ( default
 *   for version 1.1 ), ask the server to keep the connection open.
 *
 * NB - the server is not required to honor the request
 */
KNS_EXTERN rc_t CC KHttpRequestConnection ( KHttpRequest *self, bool close );


/* ByteRange
 *  set requested byte range of response
 *
 *  "pos" [ IN ] - beginning offset within remote entity
 *
 *  "bytes" [ IN ] - the number of bytes being requested
 */
KNS_EXTERN rc_t CC KHttpRequestByteRange ( KHttpRequest *self, uint64_t pos, size_t bytes );


/* AddHeader
 *  allow addition of an arbitrary HTTP header to message
 */
KNS_EXTERN rc_t CC KHttpRequestAddHeader ( KHttpRequest *self,
    const char *name, const char *val, ... );


/* AddPostParam
 *  adds a parameter for POST
 */
KNS_EXTERN rc_t CC KHttpRequestAddPostParam ( KHttpRequest *self, const char *fmt, ... );
KNS_EXTERN rc_t CC KHttpRequestVAddPostParam ( KHttpRequest *self, const char *fmt, va_list args );


/*--------------------------------------------------------------------------
 * KHttpResult
 *  hyper text transfer protocol
 */
typedef struct KHttpResult KHttpResult;


/* AddRef
 * Release
 *  ignores NULL references
 */
KNS_EXTERN rc_t CC KHttpResultAddRef ( const KHttpResult *self );
KNS_EXTERN rc_t CC KHttpResultRelease ( const KHttpResult *self );


/* HEAD
 *  send HEAD message
 */
KNS_EXTERN rc_t CC KHttpRequestHEAD ( KHttpRequest *self, KHttpResult **rslt ); 

/* GET
 *  send GET message
 *  all query AND post parameters are combined in URL
 */
KNS_EXTERN rc_t CC KHttpRequestGET ( KHttpRequest *self, KHttpResult **rslt ); 

    /* POST
 *  send POST message
 *  query parameters are sent in URL
 *  post parameters are sent in body
 */
KNS_EXTERN rc_t CC KHttpRequestPOST ( KHttpRequest *self, KHttpResult **rslt ); 


/* Status
 *  access the response status code
 *  and optionally the message
 *
 *  "code" [ OUT ] - return parameter for status code
 *
 *  "msg_buff" [ IN, NULL OKAY ] and "buff_size" [ IN, ZERO OKAY ] -
 *   buffer for capturing returned message
 *
 *  "msg_size" [ OUT, NULL OKAY ] - size of returned message in bytes
 */
KNS_EXTERN rc_t CC KHttpResultStatus ( const KHttpResult *self, uint32_t *code,
    char *msg_buff, size_t buff_size, size_t *msg_size );


/* KeepAlive
 *  retrieves keep-alive property of response
 *  requires HTTP/1.1
 */
KNS_EXTERN bool CC KHttpResultKeepAlive ( const KHttpResult *self );


/* Range
 *  retrieves position and partial size for partial requests
 *
 *  "pos" [ OUT ] - offset to beginning portion of response
 *
 *  "bytes" [ OUT ] - size of range
 */
KNS_EXTERN rc_t CC KHttpResultRange ( const KHttpResult *self, uint64_t *pos, size_t *bytes );


/* Size
 *  retrieves overall size of entity, if known
 *
 *  "response_size" [ OUT ] - size in bytes of response
 *   this is the number of bytes that may be expected from the input stream
 */
KNS_EXTERN bool CC KHttpResultSize ( const KHttpResult *self, uint64_t *size );


/* AddHeader
 *  allow addition of an arbitrary HTTP header to RESPONSE
 *  this can be used to repair or normalize odd server behavior
 */
KNS_EXTERN rc_t CC KHttpResultAddHeader ( KHttpResult *self,
    const char *name, const char *val, ... );


/* GetHeader
 *  retrieve named header if present
 *  this cand potentially return a comma separated value list
 */
KNS_EXTERN rc_t CC KHttpResultGetHeader ( const KHttpResult *self, const char *name,
    char *buffer, size_t bsize, size_t *num_read );


/* GetInputStream
 *  access the body of response as a stream
 *  only reads are supported
 *
 *  "s" [ OUT ] - return parameter for input stream reference
 *   must be released via KStreamRelease
 */
KNS_EXTERN rc_t CC KHttpResultGetInputStream ( KHttpResult *self,
    struct KStream  ** s );

#ifdef __cplusplus
}
#endif

#endif /* _h_kns_http_ */
