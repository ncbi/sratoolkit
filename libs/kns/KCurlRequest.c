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

#include <kns/extern.h>

#include <klib/refcount.h>
#include <klib/rc.h>
#include <klib/log.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/data-buffer.h>
#include <kns/manager.h>
#include <kns/KCurlRequest.h>

#include "kns_mgr_priv.h"

#include <sysalloc.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>


static const char kcurlrequest_classname [] = "KCurlRequest";


typedef struct KCurlRequest
{
    KRefcount refcount;
    const struct KNSManager * kns_mgr;
    CURL * curl_handle;
    KDataBuffer fields;
    uint64_t fields_chars;
} KCurlRequest;


static rc_t KCurlRequestSetUrl( struct KCurlRequest *self, const char * url )
{
    rc_t rc = 0;
    CURLcode rcc = self->kns_mgr->curl_easy_setopt_fkt( self->curl_handle, CURLOPT_URL, (void *)url );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_URL ) failed" );
    }
    return rc;
}


static rc_t set_curl_long_option( struct KCurlRequest *self, CURLoption option, long int value, const char *err_txt )
{
    rc_t rc = 0;
    CURLcode rcc = self->kns_mgr->curl_easy_setopt_fkt( self->curl_handle, option, value );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, "curl_easy_setopt( $(t), $(v) ) failed with curl-error $(e)", 
                                  "t=%s,v=%d,e=%d", err_txt, value, rcc ) );
    }
    return rc;
}


static rc_t set_verbose( struct KCurlRequest *self )
{
    return set_curl_long_option( self, CURLOPT_VERBOSE, 1, "CURLOPT_VERBOSE" );
}


LIB_EXPORT rc_t CC KNSManagerMakeCurlRequest( const struct KNSManager *kns_mgr, struct KCurlRequest **self, const char * url, bool verbose )
{
    rc_t rc = 0;
    struct KCurlRequest *tmp = calloc ( sizeof *tmp, 1 );
    if ( tmp == NULL )
    {
        rc = RC ( rcFS, rcFile, rcConstructing, rcMemory, rcExhausted );
        LOGERR( klogErr, rc, "out of memory" );
    }
    else
    {
        rc = KNSManagerAvail( kns_mgr );
        if ( rc == 0 )
        {
            tmp->kns_mgr = kns_mgr;
            rc = KNSManagerAddRef ( kns_mgr );
            if ( rc == 0 )
            {
                tmp->curl_handle = tmp->kns_mgr->curl_easy_init_fkt();
                if ( tmp->curl_handle == NULL )
                {
                    rc = RC( rcFS, rcFile, rcConstructing, rcParam, rcNull );
                    LOGERR( klogErr, rc, "cannot init curl" );
                }
                else
                {
                    rc = KCurlRequestSetUrl( tmp, url );
                    if ( rc == 0 )
                    {
                        if ( verbose )
                            rc = set_verbose( tmp );
                        memset( &tmp->fields, 0, sizeof tmp->fields );
                        tmp->fields.elem_bits = 8;
                        if ( rc == 0 )
                        {
                            KRefcountInit( &tmp->refcount, 1, "KNS", "make", kcurlrequest_classname );
                            *self = tmp;
                            return rc;      /* !!!! */
                        }
                    }
                }
                KNSManagerRelease( kns_mgr );
            }
        }
        free( tmp );
    }
    return rc;
}


static rc_t KCurlRequestDestroy( struct KCurlRequest *self )
{
    rc_t rc;
    self->kns_mgr->curl_easy_cleanup_fkt( self->curl_handle );
    rc = KNSManagerRelease( self->kns_mgr );
    KDataBufferWhack( &self->fields );
    free ( self );
    return rc;
}


LIB_EXPORT rc_t CC KCurlRequestAddRef ( const struct KCurlRequest *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd( &self->refcount, kcurlrequest_classname ) )
        {
        case krefOkay:
            break;
        case krefZero:
            return RC( rcNS, rcFile, rcAttaching, rcRefcount, rcIncorrect);
        case krefLimit:
            return RC( rcNS, rcFile, rcAttaching, rcRefcount, rcExhausted);
        case krefNegative:
            return RC( rcNS, rcFile, rcAttaching, rcRefcount, rcInvalid);
        default:
            return RC( rcNS, rcFile, rcAttaching, rcRefcount, rcUnknown);
        }
    }
    return 0;
}


LIB_EXPORT rc_t CC KCurlRequestRelease( const struct KCurlRequest *self )
{
    rc_t rc = 0;
    if ( self != NULL )
    {
        switch ( KRefcountDrop( &self->refcount, kcurlrequest_classname ) )
        {
        case krefOkay:
        case krefZero:
            break;
        case krefWhack:
            rc = KCurlRequestDestroy( ( struct KCurlRequest * )self );
            break;
        case krefNegative:
            return RC( rcNS, rcFile, rcAttaching, rcRefcount, rcInvalid );
        default:
            rc = RC( rcNS, rcFile, rcAttaching, rcRefcount, rcUnknown );
            break;            
        }
    }
    return rc;
}


static rc_t perform( const struct KNSManager *kns_mgr, CURL *curl, const char * action )
{
    rc_t rc = 0;
    CURLcode rcc = kns_mgr->curl_easy_perform_fkt( curl );
    if ( rcc != CURLE_OK && rcc != CURLE_PARTIAL_FILE )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, 
        "curl_easy_perform( $(a) ) failed with curl-error $(c)",
        "a=%s,b=%d", action, rcc ) );
    }
    return rc;
}


static rc_t check_response_code( const struct KNSManager *kns_mgr, CURL *curl,  const char * action )
{
    rc_t rc = 0;
    long response_code;

    CURLcode rcc = kns_mgr->curl_easy_getinfo_fkt( curl, CURLINFO_RESPONSE_CODE, &response_code );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        (void)PLOGERR( klogErr, ( klogErr, rc, 
        "curl_easy_getinfo( $(a) ) failed with curl-error $(c)",
        "a=%s,c=%d", action, rcc ) );
    }
    else
    {
        switch ( response_code )
        {
            case 0   :          /* no response code available */
            case 200 : break;   /* OK */

            case 404 : rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcNotFound );
                       break;

            default : rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
                      (void)PLOGERR( klogErr, ( klogErr, rc, "invalid response-code $(c)", "c=%d", response_code ) );
        }
    }
    return rc;
}


LIB_EXPORT rc_t CC KCurlRequestAddSFields( struct KCurlRequest *self, const String * fields )
{
    rc_t rc = 0;

    if ( self == NULL )
        return RC( rcNS, rcFile, rcReading, rcSelf, rcNull );
    if ( fields == NULL || fields->size == 0 )
        return RC( rcNS, rcFile, rcReading, rcParam, rcNull );

    if ( self->fields_chars == 0 )
    {
        rc = KDataBufferResize ( &self->fields, fields->size + 1 );
        if ( rc == 0 )
        {
            memcpy( self->fields.base, fields->addr, fields->size );
            ( (uint8_t *)self->fields.base)[ fields->size ] = 0;
            self->fields_chars = fields->size;
        }
    }
    else
    {
        rc = KDataBufferResize ( &self->fields, self->fields_chars + fields->size + 1 );
        if ( rc == 0 )
        {
            ( (uint8_t *)self->fields.base)[ self->fields_chars ] = '&';
            memcpy( &( ( (uint8_t *)self->fields.base)[ self->fields_chars + 1 ] ), fields->addr, fields->size );
            self->fields_chars += ( fields->size + 1 );
            ( (uint8_t *)self->fields.base)[ self->fields_chars ] = 0;
        }
    }
    return rc;
}


LIB_EXPORT rc_t CC KCurlRequestAddFields( struct KCurlRequest *self, const char * fields )
{
    String s;

    if ( self == NULL )
        return RC( rcNS, rcFile, rcReading, rcSelf, rcNull );
    if ( fields == NULL || fields[ 0 ] == 0 )
        return RC( rcNS, rcFile, rcReading, rcParam, rcNull );

    StringInitCString( &s, fields );
    return KCurlRequestAddSFields( self, &s );
}


LIB_EXPORT rc_t CC KCurlRequestAddSField( struct KCurlRequest *self, const String * name, const String * value )
{
    rc_t rc = 0;
    size_t needed;

    if ( self == NULL )
        return RC( rcNS, rcFile, rcReading, rcSelf, rcNull );
    if ( name == NULL || name->size == 0 || value == NULL || value->size == 0 )
        return RC( rcNS, rcFile, rcReading, rcParam, rcNull );

    needed = ( self->fields_chars + name->size + value->size + 3 );
    rc = KDataBufferResize ( &self->fields, needed );
    if ( rc == 0 )
    {
        size_t num_writ;
        if ( self->fields_chars == 0 )
        {
            rc = string_printf ( ( char * )self->fields.base, needed, &num_writ, 
                                 "%S=%S", name, value );
        }
        else
        {
            uint8_t * dst = ( uint8_t * )self->fields.base;
            rc = string_printf ( ( char * )( &dst[ self->fields_chars ] ), needed - self->fields_chars, &num_writ, 
                                 "&%S=%S", name, value );
        }
        if ( rc == 0 )
            self->fields_chars += num_writ;
    }
    return rc;
}


LIB_EXPORT rc_t CC KCurlRequestAddField( struct KCurlRequest *self, const char * name, const char * value )
{
    String s_name, s_value;

    if ( self == NULL )
        return RC( rcNS, rcFile, rcReading, rcSelf, rcNull );
    if ( name == NULL || name[ 0 ] == 0 || value == NULL || value[ 0 ] == 0 )
        return RC( rcNS, rcFile, rcReading, rcParam, rcNull );

    StringInitCString( &s_name, name );
    StringInitCString( &s_value, value );
    return KCurlRequestAddSField( self, &s_name, &s_value );
}


static rc_t KCurlRequestSetFields( struct KCurlRequest *self )
{
    rc_t rc = 0;
    CURLcode rcc = self->kns_mgr->curl_easy_setopt_fkt( self->curl_handle, CURLOPT_POSTFIELDS, (void *)(self->fields.base) );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_POSTFIELDS ) failed" );
    }
    return rc;
}


typedef struct ReadContext
{
    KDataBuffer * buffer;
    size_t num_read;
} ReadContext;


static size_t CC KCurlFileCallback( void *ptr, size_t size, size_t nmemb, void *data )
{
    rc_t rc = 0;
    size_t given_bytes = size * nmemb; /* calculate the size given in ptr */
    ReadContext *ctx = ( ReadContext * )data;
    if ( ctx != NULL && ctx->buffer != NULL )
    {
        rc = KDataBufferResize ( ctx->buffer, ctx->num_read + given_bytes + 2 );
        if ( rc == 0 )
        {
            memcpy( &( ( (uint8_t *)ctx->buffer->base )[ ctx->num_read ] ), ptr, given_bytes );
            ctx->num_read += given_bytes;
        }
    }
    return given_bytes;
}


LIB_EXPORT rc_t CC KCurlRequestPerform( struct KCurlRequest *self, KDataBuffer * buffer )
{
    CURLcode rcc;
    ReadContext ctx;
    rc_t rc = 0;

    if ( buffer == NULL )
        return RC( rcNS, rcFile, rcReading, rcParam, rcNull );

    memset ( buffer, 0, sizeof * buffer );

    if ( self == NULL )
        return RC( rcNS, rcFile, rcReading, rcSelf, rcNull );

    rc = KDataBufferMakeBytes ( buffer, 0 );
    if ( rc != 0 )
        return rc;

    ctx.buffer = buffer;
    ctx.num_read = 0;

    rcc = self->kns_mgr->curl_easy_setopt_fkt( self->curl_handle, CURLOPT_WRITEDATA, (void *)&ctx );
    if ( rcc != CURLE_OK )
    {
        rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
        LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_WRITEDATA ) failed" );
    }

    if ( rc == 0 )
    {
        CURLcode rcc = self->kns_mgr->curl_easy_setopt_fkt( self->curl_handle, CURLOPT_WRITEFUNCTION, KCurlFileCallback );
        if ( rcc != CURLE_OK )
        {
            rc = RC( rcFS, rcFile, rcConstructing, rcFileDesc, rcInvalid );
            LOGERR( klogErr, rc, "curl_easy_setopt( CURLOPT_WRITEFUNCTION ) failed" );
        }
    }

    if ( rc == 0 )
        rc = set_curl_long_option( self, CURLOPT_FOLLOWLOCATION, 1, "CURLOPT_FOLLOWLOCATION" );

    if ( rc == 0 )
        rc = KCurlRequestSetFields( self );

    if ( rc == 0 )
        rc = perform( self->kns_mgr, self->curl_handle, "POST request" );

    if ( rc == 0 )
        rc = check_response_code( self->kns_mgr, self->curl_handle, "POST request" );

    if ( rc == 0 )
    {
        buffer->elem_count = ctx.num_read;
        ( ( uint8_t * )buffer->base )[ ctx.num_read ] = 0;
    }

    return rc;
}
