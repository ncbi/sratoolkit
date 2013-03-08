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

/* NOTE: uses VFS not KNS extern.h */
#include <vfs/extern.h>

struct KCurlFile;
#define KFILE_IMPL struct KCurlFile
#include <kfs/impl.h>

#include <klib/rc.h>
#include <klib/log.h>
#include <klib/out.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <kns/kns_mgr.h>

#include "kns_mgr_priv.h"

#include <sysalloc.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_RESPONSE_CODE_0_VALID false

/*--------------------------------------------------------------------------
 * KCurFile
 */

typedef struct KCurlFile
{
    KFile dad;
    uint64_t file_size;
    struct KNSManager *kns_mgr;
    CURL *curl_handle;
    bool file_size_valid;
    bool verbose;
    bool is_ftp;
    char url[ 1 ];
} KCurlFile;


typedef struct ReadContext
{
    CURL *curl_handle;
    uint8_t * buffer;
    size_t buffer_size;
    size_t num_read;
    bool curl_handle_prepared;
} ReadContext;


/* Destroy
 */
static rc_t CC KCurlFileDestroy( KCurlFile *self )
{
    self->kns_mgr->curl_easy_cleanup_fkt( self->curl_handle );
    KNSManagerRelease( self->kns_mgr );
    free ( self );
    return 0;
}


static struct KSysFile* KCurlFileGetSysFile( const KCurlFile *self, uint64_t *offset )
{
    * offset = 0;
    return NULL;
}


static rc_t KCurlFileRandomAccess( const KCurlFile *self )
{
    return 0;
}


static size_t CC dummy_callback( void *ptr, size_t size, size_t nmemb, void *data )
{
    ( void )ptr;
    ( void )data;
    return (size_t)( size * nmemb );
}


static size_t CC KCurlFileCallback( void *ptr, size_t size, size_t nmemb, void *data )
{
    size_t given_bytes = size * nmemb; /* calculate the size given in ptr */
    ReadContext *ctx = ( ReadContext * )data;
    if ( ctx != NULL && ctx->buffer != NULL && ctx->buffer_size > 0 )
    {
        if ( ( ctx->num_read + given_bytes ) > ctx->buffer_size )
        {
            /* the caller-provided buffer IS NOT enough... */
            size_t to_copy = ( ctx->buffer_size - ctx->num_read );
            if ( to_copy > 0 )
            {
                /* the caller-provided buffer can hold a part of it... */
                if ( ctx->buffer )
                {
                    memcpy( &( ctx->buffer[ ctx->num_read ] ), ptr, to_copy );
                    ctx->num_read += to_copy;
                }
            }
        }
        else
        {
            /* the caller-provided buffer IS enough... */
            if ( ctx->buffer )
            {
                memcpy( &( ctx->buffer[ ctx->num_read ] ), ptr, given_bytes );
                ctx->num_read += given_bytes;
            }
        }
    }
    return given_bytes;
}


/* helper to set a curl-option which is a long int */
static rc_t set_curl_long_option( struct KNSManager *kns_mgr, CURL *curl,
                                  CURLoption option, long int value, const char *err_txt )
{
    rc_t rc = 0;
    CURLcode rcc = kns_mgr->curl_easy_setopt_fkt( curl, option, value );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, "curl_easy_setopt( $(t), $(v) ) failed with curl-error $(e)", 
                                  "t=%s,v=%d,e=%d", err_txt, value, rcc ) );
    }
    return rc;
}


/* helper to set a curl-option which is a void pointert */
static rc_t set_curl_void_ptr( struct KNSManager *kns_mgr, CURL *curl,
                               CURLoption option, void * value, const char *err_txt )
{
    rc_t rc = 0;
    CURLcode rcc = kns_mgr->curl_easy_setopt_fkt( curl, option, value );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, "curl_easy_setopt( $(t) ... void * ptr ) failed with curl-error $(e)",
                                  "t=%s,e=%d", err_txt, rcc ) );
    }
    return rc;
}


static rc_t perform( struct KNSManager *kns_mgr, CURL *curl, const char * action, uint64_t pos, size_t len )
{
    rc_t rc = 0;
    CURLcode rcc = kns_mgr->curl_easy_perform_fkt( curl );
    if ( rcc != CURLE_OK && rcc != CURLE_PARTIAL_FILE )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, 
        "curl_easy_perform( $(a), $(b).$(c) ) failed with curl-error $(c)",
        "a=%s,b=%lu,c=%u,c=%d", action, pos, len, rcc ) );
    }
    return rc;
}


static rc_t check_response_code( struct KNSManager *kns_mgr, CURL *curl,  const char * action )
{
    rc_t rc = 0;
    long response_code;

    CURLcode rcc = kns_mgr->curl_easy_getinfo_fkt( curl, CURLINFO_RESPONSE_CODE, &response_code );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcAccessing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, 
        "curl_easy_getinfo( $(a) ) failed with curl-error $(c)",
        "a=%s,c=%d", action, rcc ) );
    }
    else
    {
        switch ( response_code )
        {

            case 0    : if ( !HTTP_RESPONSE_CODE_0_VALID )  /* no response code available */
                        {
                            rc = RC( rcFS, rcFile, rcAccessing, rcFileDesc, rcInvalid );
                            (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
                        }
                        break;

            case 200  :          /* OK */
            case 206  :          /* Partial Content */
            case 416  : break;   /* Requested Range Not Satisfiable */

            case 404  : rc = RC( rcFS, rcFile, rcAccessing, rcFileDesc, rcNotFound );
                        break;

            default   : rc = RC( rcFS, rcFile, rcAccessing, rcFileDesc, rcInvalid );
                        (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
        }
    }
    return rc;
}


static rc_t prepare_curl( const KCurlFile *cself, ReadContext *ctx )
{
    rc_t rc = 0;

    if ( !ctx->curl_handle_prepared )
    {
        ctx->curl_handle = cself->kns_mgr->curl_easy_init_fkt();
        if ( ctx->curl_handle == NULL )
        {
            rc = RC( rcFS, rcFile, rcConstructing, rcParam, rcNull );
            LOGERR( klogErr, rc, "cannot init curl" );
        }
        else
            ctx->curl_handle_prepared = true;
    }

    if ( rc == 0 && cself->verbose )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_VERBOSE, 1, "CURLOPT_VERBOSE" );

    if ( rc == 0 )
    {
        CURLcode rcc = cself->kns_mgr->curl_easy_setopt_fkt( ctx->curl_handle, CURLOPT_WRITEFUNCTION, KCurlFileCallback );
        if ( rcc != CURLE_OK )
        {
            rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
            (void)PLOGERR( klogErr, ( klogErr, rc, "curl_easy_setopt( $(t) ... ptr ) failed", "t=%s", "CURLOPT_WRITEFUNCTION" ) );
        }
    }

    if ( rc == 0 )
        rc = set_curl_void_ptr( cself->kns_mgr, ctx->curl_handle, CURLOPT_URL, ( void * )cself->url, "CURLOPT_URL" );

    if ( rc == 0 )
    {
        ctx->buffer = NULL;
        ctx->buffer_size = 0;
        ctx->num_read = 0;
    }
    return rc;
}


static rc_t get_content_length( struct KNSManager *kns_mgr, CURL *curl, uint64_t *size )
{
    rc_t rc = 0;
    double double_size;
    CURLcode rcc = kns_mgr->curl_easy_getinfo_fkt( curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &double_size );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        LOGERR( klogErr, rc, "curl_easy_getinfo( CURLINFO_CONTENT_LENGTH_DOWNLOAD ) failed" );
    }
    else
    {
        *size = double_size;
    }
    return rc;
}


static rc_t DiscoverFileSize_HTTP( const KCurlFile *cself, ReadContext *ctx, uint64_t *size )
{
    rc_t rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_HEADER, 1, "CURLOPT_HEADER" );

    if ( rc == 0 )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_NOBODY, 1, "CURLOPT_NOBODY" );

    if ( rc == 0 )
        rc = set_curl_void_ptr( cself->kns_mgr, ctx->curl_handle, CURLOPT_WRITEDATA, ctx, "CURLOPT_WRITEDATA" );

    if ( rc == 0 )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_FOLLOWLOCATION, 1, "CURLOPT_FOLLOWLOCATION" );

    if ( rc == 0 )
        rc = perform( cself->kns_mgr, ctx->curl_handle, "filesize http", 0, 0 );

    if ( rc == 0 )
        rc = check_response_code( cself->kns_mgr, ctx->curl_handle, "filesize http" );

    if ( rc == 0 )
        rc = get_content_length( cself->kns_mgr, ctx->curl_handle, size );

    return rc;
}

static rc_t DiscoverFileSize_FTP( const KCurlFile *cself, ReadContext *ctx, uint64_t *size )
{
    rc_t rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_NOBODY, 1, "CURLOPT_NOBODY" );

    if ( rc == 0 )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_FILETIME, 1, "CURLOPT_FILETIME" );

    if ( rc == 0 )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_FOLLOWLOCATION, 1, "CURLOPT_FOLLOWLOCATION" );

    if ( rc == 0 )
    {
        CURLcode rcc = cself->kns_mgr->curl_easy_setopt_fkt( ctx->curl_handle, CURLOPT_HEADERFUNCTION, dummy_callback );
        if ( rcc != CURLE_OK )
        {
            rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
            (void)PLOGERR( klogErr, ( klogErr, rc, "curl_easy_setopt( $(t) ... ptr ) failed", "t=%s", "CURLOPT_HEADERFUNCTION" ) );
        }
    }

    if ( rc == 0 )
        rc = set_curl_long_option( cself->kns_mgr, ctx->curl_handle, CURLOPT_HEADER, 0, "CURLOPT_HEADER" );

    if ( rc == 0 )
        rc = perform( cself->kns_mgr, ctx->curl_handle, "filesize ftp", 0, 0 );

    if ( rc == 0 )
        rc = get_content_length( cself->kns_mgr, ctx->curl_handle, size );

    return rc;
}


static rc_t KCurlFileDiscoverSize( const KCurlFile *cself, uint64_t *size )
{
    ReadContext ctx;
    rc_t rc;

    memset( &ctx, 0, sizeof ctx );
    rc = prepare_curl( cself, &ctx );
    if ( rc == 0 )
    {
        if ( cself->is_ftp )
            rc = DiscoverFileSize_FTP( cself, &ctx, size );
        else
            rc = DiscoverFileSize_HTTP( cself, &ctx, size );

        if ( rc == 0 )
        {
            KCurlFile *self = ( KCurlFile * )cself;

            self->file_size = *size;
            self->file_size_valid = true;
    /*        OUTMSG(( ">>>>>>>>>>>>>> and the file size is: %lu\n", self->file_size )); */
        }

        if ( ctx.curl_handle_prepared )
            cself->kns_mgr->curl_easy_cleanup_fkt( ctx.curl_handle );
    }
    return rc;
}


static rc_t KCurlFileSize( const KCurlFile *self, uint64_t *size )
{
    rc_t rc = 0;
    if ( self->file_size_valid )
        *size = self->file_size;
    else
        rc = KCurlFileDiscoverSize( self, size );
    return rc;
}


static rc_t KCurlFileSetSize( KCurlFile *self, uint64_t size )
{
    return RC ( rcFS, rcFile, rcUpdating, rcFile, rcReadonly );
}


static rc_t KCurlFileRead( const KCurlFile *cself, uint64_t pos,
                           void *buffer, size_t bsize, size_t *num_read )
{
    ReadContext ctx;
    rc_t rc;

    if ( cself->file_size_valid && cself->file_size > 0 )
    {
        /* we know the size of the remote file */
        if ( pos >= cself->file_size )
        {
            /* the caller requested to read beyond the end of the file */
            *num_read = 0;
            return 0;
        }
        else if ( pos + bsize > cself->file_size )
        {
            /* the caller requested to start reading inside the file, but bsize reaches beyond the end of the file */
            bsize = ( cself->file_size - pos );
        }
    }

    ctx.curl_handle = cself->curl_handle;
    ctx.curl_handle_prepared = true;
    rc = prepare_curl( cself, &ctx );
    if ( rc == 0 )
    {
        char s_range[ 64 ];
        size_t s_range_writ;
        uint64_t to = pos;
        to += bsize;
        to --;

        /* perform range-read... */
        rc = string_printf( s_range, sizeof s_range, &s_range_writ, "%lu-%lu", pos, to );
        if ( rc == 0 )
        {
            ReadContext ctx;
            CURLcode rcc;

            ctx.buffer = buffer;
            ctx.buffer_size = bsize;
            ctx.num_read = 0;

            rcc = cself->kns_mgr->curl_easy_setopt_fkt( cself->curl_handle, CURLOPT_WRITEDATA, (void *)&ctx );
            if ( rcc != CURLE_OK )
            {
                rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_WRITEDATA ) failed" );
            }

            if ( rc == 0 )
                rc = set_curl_long_option( cself->kns_mgr, cself->curl_handle, CURLOPT_CONNECTTIMEOUT, 10, "CURLOPT_CONNECTTIMEOUT" );
            if ( rc == 0 )
                rc = set_curl_long_option( cself->kns_mgr, cself->curl_handle, CURLOPT_TIMEOUT, 10, "CURLOPT_TIMEOUT" );
            if ( rc == 0 )
                rc = set_curl_long_option( cself->kns_mgr, cself->curl_handle, CURLOPT_FOLLOWLOCATION, 1, "CURLOPT_FOLLOWLOCATION" );

            if ( rc == 0 )
            {
                rcc = cself->kns_mgr->curl_easy_setopt_fkt( cself->curl_handle, CURLOPT_RANGE, (void*)s_range );
                if ( rcc != CURLE_OK )
                {
                    rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                    LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_RANGE ) failed" );
                }
            }

            if ( rc == 0 )
                rc = perform( cself->kns_mgr, cself->curl_handle, "FileRead", pos, bsize );

            if ( rc == 0 )
            {
                long response_code;
                rcc = cself->kns_mgr->curl_easy_getinfo_fkt( cself->curl_handle, CURLINFO_RESPONSE_CODE, &response_code );
                if ( rcc != CURLE_OK )
                {
                    rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                    LOGERR( klogErr, rc, "curl_easy_getinfo( RESPONSE_CODE ) failed" );
                }
                else
                {
                    if ( cself->is_ftp )
                    {
                        switch ( response_code )
                        {
                            case 0    : if ( !HTTP_RESPONSE_CODE_0_VALID )  /* no response code available */
                                        {
                                            rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                                            (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
                                        }
                                        break;

                            case 226  :
                            case 213  :
                            case 450  :          /* Transfer aborted */
                            case 451  : break;

                            default : rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                                      (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
                        }
                    }
                    else
                    {
                        switch ( response_code )
                        {
                            case 0    : if ( !HTTP_RESPONSE_CODE_0_VALID )  /* no response code available */
                                        {
                                            rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                                            (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
                                        }
                                        break;

                            case 416 :          /* Requested Range Not Satisfiable */
                                       ctx.num_read = 0;
                            case 200 :          /* OK */
                            case 206 :          /* Partial Content */
                                       break;

                            case 404 : rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcNotFound );
                                       break;

                            default : rc = RC( rcFS, rcFile, rcReading, rcFileDesc, rcInvalid );
                                      (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
                        }
                    }
                }
            }
            if ( rc == 0 )
                *num_read = ctx.num_read;
        }
    }
    return rc;
}


static rc_t KCurlFileWrite( KCurlFile *self, uint64_t pos,
                            const void *buffer, size_t size, size_t *num_writ )
{
    return RC ( rcFS, rcFile, rcUpdating, rcInterface, rcUnsupported );
}


static KFile_vt_v1 vtKCurlFile =
{
    /* version 1.0 */
    1, 0,

    /* start minor version 0 methods */
    KCurlFileDestroy,
    KCurlFileGetSysFile,
    KCurlFileRandomAccess,
    KCurlFileSize,
    KCurlFileSetSize,
    KCurlFileRead,
    KCurlFileWrite
    /* end minor version 0 methods */
};


LIB_EXPORT rc_t CC KCurlFileMake( const KFile **fp, const char * url, bool verbose )
{
    rc_t rc = 0;
    KCurlFile *f;

    if ( url == NULL || url[ 0 ] == 0 )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        PLOGERR( klogInt, ( klogInt, rc, "invalid url >$(URL)<", "URL=%s", url ) );
    }
    else
    {
        size_t url_len = string_size( url ) + 1;
        f = calloc ( ( sizeof *f ) + url_len, 1 );
        if ( f == NULL )
        {
            rc = RC ( rcFS, rcFile, rcConstructing, rcMemory, rcExhausted );
            LOGERR( klogErr, rc, "out of memory" );
        }
        else
        {
            rc = KNSManagerMake( &f->kns_mgr );
            if ( rc == 0 )
            {
                rc = KNSManagerAvail( f->kns_mgr );
                if ( rc == 0 )
                {
                    rc = KFileInit( &f ->dad, (const union KFile_vt *)&vtKCurlFile, "KCurlFile", url, true, false );
                    if ( rc == 0 )
                    {
                        ReadContext ctx;
                        uint64_t file_size;

                        memset( &ctx, 0, sizeof( ctx ) ); /* zero-out the content */

                        string_copy( f->url, url_len, url, url_len );
                        f->verbose = verbose;
                        f->is_ftp = ( url_len >= 4 ) ? ( strcase_cmp ( f->url, url_len, "ftp:", 4, 4 ) == 0 ) : false;

                        rc = KCurlFileDiscoverSize( f, &file_size );

                        if ( rc == 0 )
                        {
                            rc = prepare_curl( f, &ctx );
                            if ( rc == 0 )
                            {
                                f->curl_handle = ctx.curl_handle;
                                *fp = & f -> dad;
                                return rc;
                            }
                            else
                            {
                                KCurlFileDestroy( f );
                                f = NULL;
                            }
                        }
                    }
                }
            }
            free( f );
        }
    }
    return rc;
}
