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

#define KSTREAM_IMPL KHttpStream
typedef struct KHttpStream KHttpStream;

#define KFILE_IMPL KHttpFile
typedef struct KHttpFile KHttpFile;
#include <kfs/impl.h>

#include <kns/manager.h>
#include <kns/http.h>
#include <kns/adapt.h>
#include <kns/endpoint.h>
#include <kns/socket.h>
#include <kns/stream.h>
#include <kns/impl.h>
#include <kfs/file.h>
#include <kfs/directory.h>

#include <klib/debug.h> /* DBGMSG */
#include <klib/text.h>
#include <klib/container.h>
#include <klib/out.h>
#include <klib/log.h>
#include <klib/refcount.h>
#include <klib/rc.h>
#include <klib/printf.h>
#include <klib/vector.h>

#include <strtol.h>
#include <va_copy.h>

#include "stream-priv.h"

#include <sysalloc.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "http-priv.h"


static 
void  KDataBufferClear ( KDataBuffer *buf )
{
    memset ( buf, 0, sizeof *buf );
    buf -> elem_bits = 8;
}

static
bool KDataBufferContainsString ( const KDataBuffer *buf, const String *str )
{
    return str -> addr >= ( const char* ) buf -> base &&
        & str -> addr [ str -> size ] <= & ( ( const char* ) buf -> base ) [ buf -> elem_count ];
}

/*--------------------------------------------------------------------------
 * KDataBufferVPrintf
 *  Populate a KDataBuffer
 *  If buffer is empty - resize it to 4k
 *  Else determine the correct multiple of 4k and add/append data
 */

#if 0
/* TBD - add to the printf file permanently */
static
rc_t KDataBufferVPrintf ( KDataBuffer * buf, const char * fmt, va_list args )
{
    rc_t rc;
    size_t bsize;
    char *buffer;
    size_t content;
    size_t num_writ;

    /* the C library ruins a va_list upon use
       in case we ever need to use it a second time,
       make a copy first */
    va_list args_copy;
    va_copy ( args_copy, args );

    /* begin to calculate content and bsize */
    content = ( size_t ) buf -> elem_count;

    /* check for an empty buffer */
    if ( content == 0 )
    {
        rc = KDataBufferResize ( buf, bsize = 4096 );
        if ( rc != 0 )
            return rc;
    }
    else
    {
        /* generate even multiple of 4K */
        bsize = ( content + 4095 ) & ~ ( size_t ) 4095;

        /* discount NUL byte */
        content -= 1;
    }
            
    /* convert the 2-part url into a flat string */
    buffer = buf -> base;
    rc = string_vprintf ( &buffer [ content ], bsize - content, & num_writ, fmt, args );
    /* Make sure there is enough room to store data including NUL */
    if ( rc != 0 || ( content + num_writ ) == bsize )
    {
        bsize = ( content + num_writ + 4095 + 1 ) & ~ ( size_t ) 4095;
        rc = KDataBufferResize ( buf, bsize );
        if ( rc == 0 )
        {
            /* try again with the newly sized buffer */
            rc = string_vprintf ( &buffer [ content ], bsize - content, & num_writ, fmt, args_copy );
        }
    }
    va_end ( args_copy );
    
    /* size down to bsize + NULL */
    if ( rc == 0 )
        KDataBufferResize ( buf, content + num_writ + 1 );

    return rc;
}

/* forward to KDataBufferVPrintf */
static
rc_t KDataBufferPrintf ( KDataBuffer * buf, const char * url, ... )
{
    rc_t rc;

    va_list args;
    va_start ( args, url );
    rc = KDataBufferVPrintf ( buf, url, args );
    va_end ( args );

    return rc;
}
#endif
/*--------------------------------------------------------------------------
 * URLBlock
 *  RFC 3986
 */

/* Init
 *  Initialize to default values in case portions are missing
 */
void URLBlockInit ( URLBlock *self )
{
    CONST_STRING ( & self -> scheme, "http" );
    CONST_STRING ( & self -> host, "" );
    CONST_STRING ( & self -> path, "/" );
    CONST_STRING ( & self -> query, "" );
    CONST_STRING ( & self -> fragment, "" );
    self -> port = 0; /* 0 = DEFAULT 80 for http */
}

/* ParseUrl
 *  accept standard, full http URL:
 *    <scheme>://<host>[:<port>]/<path>[?<query>][#<fragment>]
 *
 *  scheme can be missing, i.e.:
 *    //<host>[:<port>]/<path>[?<query>][#<fragment>]
 *
 *  we can also accept missing path[query][fragment], i.e.:
 *    <scheme>://<host>[:<port>]
 *    //<host>[:<port>]
 *
 *  finally, we can accept path without host, i.e.:
 *    /<path>[?<query>][#<fragment>]
 *
 *  patterns to reject:
 *    <scheme>:/<path>...    # scheme followed by anything other than '//'
 *    <path>...              # no leading '/'
 */
rc_t ParseUrl ( URLBlock * b, const char * url, size_t url_size ) 
{
    rc_t rc;
    char * sep;
    const char * buf = url;
    const char * end = buf + url_size;

    bool have_host, have_scheme;

    URLBlockInit ( b );

    /* scheme default to false because url may be a path */
    have_scheme = false;

    /* check if url is empty
       scheme cannot start with a forward slash - detecting an absolute path */
    if ( buf < end && buf [ 0 ] != '/' )
    {
        /* here we identify the scheme by finding the ':' */
        sep = string_chr ( url, end - buf, ':' );
        if ( sep != NULL )
        {
            String http;
            CONST_STRING ( & http, "http" );

            /* assign scheme to the url_block */
            StringInit ( & b -> scheme, buf, sep - buf, ( uint32_t ) ( sep - buf ) );

            /* check to make sure it is 'http' */
            if ( ! StringCaseEqual ( & b -> scheme, & http ) )
            {
                rc = RC ( rcNS, rcUrl, rcEvaluating, rcName, rcIncorrect );
                PLOGERR ( klogErr ,( klogErr, rc, "Scheme is '$(scheme)'", "scheme=%S", & b -> scheme ) );
                return rc;
            }

            /* accept scheme - skip past */
            buf = sep + 1;
            have_scheme = true;
        }
    }
    
    /* discard fragment - not sending to server, but still record it */
    sep = string_rchr ( buf, end - buf,  '#' );
    if ( sep != NULL )
    {
        /* advance to first character in fragment */
        const char *frag = sep + 1;

        /* assign fragment to the url_block */
        StringInit ( & b -> fragment, frag, end - frag, ( uint32_t ) ( end - frag ) );

        /* remove fragment from URL */
        end = sep;
    }
                         
    /* detect host */
    have_host = false;
    
    /* check for '//' in the first two elements 
       will fail if no scheme was detected */
    if ( string_match ( "//", 2, buf, end - buf, 2, NULL ) == 2 )
    {
        /* skip ahead to host spec */
        buf += 2;

        /* if we ran into the end of the string, we dont have a host */
        if ( buf == end )
        {
            rc = RC ( rcNS, rcUrl, rcParsing, rcOffset, rcIncorrect );
            PLOGERR ( klogErr ,( klogErr, rc, "expected hostspec in url '$(url)'", "url=%.*s", ( uint32_t ) url_size, url ) );
            return rc;
        }

        have_host = true;
    }

    /* if there is a scheme but no host, error */
    if ( have_scheme && ! have_host )
    {
        rc = RC ( rcNS, rcUrl, rcParsing, rcName, rcNotFound );
        PLOGERR ( klogErr ,( klogErr, rc, "Host is '$(host)'", "host=%s", "NULL" ) );
        return rc;
    }
        
    /* find dividing line between host and path, which MUST start with '/' */
    sep = string_chr ( buf, end - buf, '/' );

    /* detect no path */
    if ( sep == NULL )
    {
        /* no path and no host */
        if ( ! have_host )
        {
            rc = RC ( rcNS, rcUrl, rcParsing, rcName, rcNotFound );
            PLOGERR ( klogErr ,( klogErr, rc, "Path is '$(path)'", "path=%s", "/" ) );
            return rc;
        }
        /* no path but have host 
           default value for path is already '/' */
        sep = ( char* ) end;
    }

    /* capture host ( could be empty - just given a file system path ) */
    if ( have_host )
    {
        /* assign host to url_block */
        StringInit ( & b -> host, buf, sep - buf, ( uint32_t ) ( sep - buf ) );

        /* advance to path */
        buf = sep;
    }

    /* detect relative path 
       <hostname>/<path> - OK, handled above
       /<path> - OK
    */
    if ( buf != sep )
    {
        rc = RC ( rcNS, rcPath, rcParsing, rcOffset, rcIncorrect );
        PLOGERR ( klogErr ,( klogErr, rc, "Path is '$(path)'", "path=%s", "NULL" ) );
        return rc;
    }

    /* if we dont have a host we must have a path
       if we DO have a host and the path is not empty */
    if ( ! have_host || buf != end )
    {
        /* check for query */
        sep = string_chr ( buf, end - buf,  '?' );
        if ( sep != NULL )
        {
            const char *query = sep + 1;
            /* assign query to url_block */
            StringInit ( & b -> query, query, end - query, ( uint32_t ) ( end - query ) ); 

            /* advance end to sep */
            end = sep;
        }

        /* assign path ( could also be empty ) */
        StringInit ( & b -> path, buf, end - buf, ( uint32_t ) ( end - buf ) );
    }

    /* if we have a host, split on ':' to check for a port
       OK if not found */
    if ( have_host )
    {
        buf = b -> host . addr;
        end = buf + b -> host . size;

        /* check for port */
        sep = string_chr ( buf, end - buf,  ':' );
        if ( sep != NULL )
        {
            char *term;
            const char * port = sep + 1;
            /* assign port to url block converting to 32 bit int 
             term should point to end */
            b -> port = strtou32 ( port, & term, 10 );

            /* error if 0 or term isnt at the end of the buffer */
            if ( b -> port == 0 || ( const char* ) term != end )
            {
                rc = RC ( rcNS, rcUrl, rcParsing, rcNoObj, rcIncorrect );
                PLOGERR ( klogErr ,( klogErr, rc, "Port is '$(port)'", "port=%d", b -> port ) );
                return rc;
            }

            /* assign host to url_block */
            StringInit ( & b -> host, buf, sep - buf, ( uint32_t ) ( sep - buf ) );
        }
    }
    
    return 0;
}

/*--------------------------------------------------------------------------
 * KHttpHeader
 *  node structure to place http header lines into a BSTree
 */

void CC KHttpHeaderWhack ( BSTNode *n, void *ignore )
{
    KHttpHeader * self = ( KHttpHeader* ) n;
    KDataBufferWhack ( & self -> value_storage );
    free ( self );
}

static
int CC KHttpHeaderSort ( const BSTNode *na, const BSTNode *nb )
{
    const KHttpHeader *a = ( const KHttpHeader* ) na;
    const KHttpHeader *b = ( const KHttpHeader* ) nb;

    return StringCaseCompare ( & a -> name, & b -> name );
}

static
int CC KHttpHeaderCmp ( const void *item, const BSTNode *n )
{
    const String *a = item;
    const KHttpHeader *b = ( const KHttpHeader * ) n;

    return StringCaseCompare ( a, & b -> name );
}

/*--------------------------------------------------------------------------
 * KHttp
 *  hyper text transfer protocol 
 *  structure that will act as the 'client' for networking tasks
 */
struct KHttp
{
    const KNSManager *mgr;
    KStream * sock;

    /* buffer for accumulating response data from "sock" */
    KDataBuffer block_buffer;
    size_t block_valid;         /* number of valid response bytes in buffer */
    size_t block_read;          /* number of bytes read out by line reader or stream */

    KDataBuffer line_buffer; /* data accumulates for reading headers and chunk size */
    size_t line_valid;

    KDataBuffer hostname_buffer;
    String hostname; 
    uint32_t port;

    ver_t vers;

    KRefcount refcount;

    KEndPoint ep;
    bool ep_valid;
};


#define KHttpBlockBufferIsEmpty( self ) \
    ( ( self ) -> block_read == ( self ) -> block_valid )

#define KHttpBlockBufferReset( self ) \
    ( ( void ) ( ( self ) -> block_valid = ( self ) -> block_read = 0 ) )

#define KHttpLineBufferReset( self ) \
    ( ( void ) ( ( self ) -> line_valid = 0 ) )
    
static
void KHttpClose ( KHttp *self )
{
    KStreamRelease ( self -> sock );
    self -> sock = NULL;
}


/* used to be in whack function, but we needed the ability
   to clear out the http object for redirection */
static
rc_t KHttpClear ( KHttp *self )
{
    KHttpClose ( self );

    KHttpBlockBufferReset ( self );
    KHttpLineBufferReset ( self );

    KDataBufferWhack ( & self -> hostname_buffer );

    return 0;
}

static
rc_t KHttpWhack ( KHttp * self )
{
    KHttpClear ( self );
    
    KDataBufferWhack ( & self -> block_buffer );
    KDataBufferWhack ( & self -> line_buffer );
    KNSManagerRelease ( self -> mgr );
    KRefcountWhack ( & self -> refcount, "KHttp" );
    free ( self );

    return 0;
}

static
rc_t KHttpOpen ( KHttp * self, const String * hostname, uint32_t port )
{
    rc_t rc;

    if ( ! self -> ep_valid )
    {
        rc = KNSManagerInitDNSEndpoint ( self -> mgr, & self -> ep, hostname, port );
        if ( rc != 0 )
            return rc;

        self -> ep_valid = true;
    }

    rc = KNSManagerMakeConnection ( self -> mgr, & self -> sock, NULL, & self -> ep );
    if ( rc == 0 )
    {
        self -> port = port;
        return 0;
    }

    self -> sock = NULL;
    return rc;
}

/* Initialize KHttp object */
static
rc_t KHttpInit ( KHttp * http, const KDataBuffer *hostname_buffer, KStream * conn, ver_t _vers, const String * _host, uint32_t port )
{
    rc_t rc;

    if ( port == 0 )
        port = 80;

    /* we accept a NULL connection ( from ) */
    if ( conn != NULL )
        rc = KStreamAddRef ( conn );
    else
    {
        rc = KHttpOpen ( http, _host, port );
    }

    if ( rc == 0 )
    {
        http -> sock = conn;
        http -> port = port;
        http -> vers = _vers & 0xFFFF0000; /* safety measure - limit to major.minor */


        /* YOU NEED AN assert HERE TO ENSURE _host IS WITHIN hostname_buffer */
        assert ( KDataBufferContainsString ( hostname_buffer, _host ) );

        /* initialize hostname buffer from external buffer */
        rc = KDataBufferSub ( hostname_buffer, &http -> hostname_buffer,
                              ( _host -> addr - ( const char* ) hostname_buffer -> base ),
                              _host -> size );
        if ( rc == 0 )
            /* Its safe to assign pointer because we know
               that the pointer is within the buffer */
            http -> hostname = * _host;
    }
    
    return rc;
} 


/* MakeHttp
 *  create an HTTP protocol
 *
 *  "http" [ OUT ] - return parameter for HTTP object
 *
 *  "conn" [ IN ] - previously opened stream for communications.
 *
 *  "vers" [ IN ] - http version
 *   the only legal types are 1.0 ( 0x01000000 ) and 1.1 ( 0x01010000 )
 *
 *  "host" [ IN ] - parameter to give the host dns name for the connection
 *
 *  "port" [ IN, DEFAULT ZERO ] - if zero, defaults to standard for scheme
 *   if non-zero, is taken as explicit port specification
 */
static
rc_t KNSManagerMakeHttpInt ( const KNSManager *self, KHttp **_http,
    const KDataBuffer *hostname_buffer,  KStream *conn, ver_t vers, const String *host, uint32_t port )
{
    rc_t rc;

    KHttp * http = calloc ( 1, sizeof * http );
    if ( http == NULL )
        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcNull );
    else
    {
        rc = KNSManagerAddRef ( self );
        if ( rc == 0 )
        {
            char save, *text;

            http -> mgr = self;


            /* Dont use MakeBytes because we dont need to allocate memory
               and we only need to know that the elem size is 8 bits */
            KDataBufferClear ( & http -> block_buffer );
            KDataBufferClear ( & http -> line_buffer );

            /* make sure address of bost is within hostname_buffer */
            assert ( KDataBufferContainsString ( hostname_buffer, host ) );

            /* SET TEXT TO POINT TO THE HOST NAME AND NUL TERMINATE IT FOR DEBUGGING
             Its safe to modify the const char array because we allocated the buffer*/ 
            text = ( char* ) ( host -> addr );
            save = text [ host -> size ];
            text [ host -> size ] = 0;
        
            /* initialize reference counter on object to 1 - text is now nul-terminated */
            KRefcountInit ( & http -> refcount, 1, "KHttp", "make", text );

            text [ host -> size ] = save;

            /* init the KHttp object */
            rc = KHttpInit ( http, hostname_buffer, conn, vers, host, port );
            if ( rc == 0 )
            {
                /* assign to OUT http param */
                * _http = http;
                return 0;
            }

            KNSManagerRelease ( self );
        }
                
        free ( http );
    }

    return rc;
}

LIB_EXPORT rc_t CC KNSManagerMakeHttp ( const KNSManager *self,
    KHttp **_http, KStream *conn, ver_t vers, const String *host, uint32_t port )
{
    rc_t rc;
    
    /* check return parameters */
    if ( _http == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        /* check input parameters */
        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        /* make sure we have one of the two versions supported - 1.0, 1.1 */
        else if ( vers < 0x01000000 || vers > 0x01010000 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcIncorrect );
        else if ( host == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        /* make sure there is data in the host name */
        else if ( host -> size == 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
        else
        {
            KDataBuffer hostname_buffer;

            /* create storage buffer for hostname */
            rc = KDataBufferMakeBytes ( & hostname_buffer, host -> size + 1 );
            if ( rc == 0 )
            {
                String _host;

                /* copy hostname with nul termination */
                string_copy ( hostname_buffer . base, ( size_t ) hostname_buffer . elem_count,
                              host -> addr, host -> size );

                /* create copy of host that points into new buffer */
                StringInit ( &_host, hostname_buffer . base, host -> size, host -> len );

                /* initialize http object - will create a new reference to hostname buffer */
                rc = KNSManagerMakeHttpInt ( self, _http, & hostname_buffer, conn, vers, &_host, port );

                /* release our reference to buffer */
                KDataBufferWhack ( & hostname_buffer );

                if ( rc == 0 )
                    return 0;
            }
        }

        * _http = NULL;
    }
    
    return rc;
}


/* AddRef
 * Release
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC KHttpAddRef ( const KHttp *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, "KHttp" ) )
        {
        case krefLimit:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcRange, rcExcessive );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcSelf, rcInvalid );
        default:
            break;
        }
    }
    
    return 0;
}

LIB_EXPORT rc_t CC KHttpRelease ( const KHttp *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, "KHttp" ) )
        {
        case krefWhack:
            return KHttpWhack ( ( KHttp* ) self );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcReleasing, rcRange, rcExcessive );
        default:
            break;
        }
    }
    
    return 0;
}

/* Communication Methods
 *  Read in the http response and return 1 char at a time
 */
static
rc_t KHttpGetCharFromResponse ( KHttp *self, char *ch )
{
    rc_t rc;
    char * buffer = self -> block_buffer . base;

    /* check for data in buffer */
    if ( KHttpBlockBufferIsEmpty ( self ) )
    {
        /* check to see ho many bytes are in the buffer */
        size_t bsize = KDataBufferBytes ( & self -> block_buffer );

        /* First time around, bsize will be 0 */
        if ( bsize == 0 )
        {
            bsize = 64 * 1024;
            rc = KDataBufferResize ( & self -> block_buffer, bsize );
            if ( rc != 0 )
                return rc;

            /* re-assign new base pointer */
            buffer = self -> block_buffer . base;
        }

        /* zero out offsets */
        KHttpBlockBufferReset ( self );

        /* read from the stream into the buffer, and record the bytes read
           into block_valid */
        /* NB - do NOT use KStreamReadAll or it will block with http 1.1 
           because http/1.1 uses keep alive and the read will block until the server 
           drops the connection */
        rc = KStreamRead ( self -> sock, buffer, bsize, & self -> block_valid );
        if ( rc != 0 )
            return rc;

        /* if nothing was read, we have reached the end of the stream */
        if ( self -> block_valid == 0 )
        {
            /* return nul char */
            * ch = 0;
            return 0;
        }
    }

    /* return the next char in the buffer */
    * ch = buffer [ self -> block_read ++ ];
    return 0;
}

/* Read and return entire lines ( until \r\n ) */
static
rc_t KHttpGetLine ( KHttp *self )
{
    rc_t rc;

    char * buffer = self -> line_buffer . base;
    size_t bsize = KDataBufferBytes ( & self -> line_buffer );

    /* num_valid bytes read starts at 0 */
    self -> line_valid = 0;
    while ( 1 )
    {
        char ch;

        /* get char */
        rc = KHttpGetCharFromResponse ( self, &ch );
        if ( rc != 0 )
            break;

        if ( ch == '\n' )
        {
            /* check that there are valid bytes read and the previous char is '\r' */
            if ( self -> line_valid > 0 && buffer [ self -> line_valid - 1 ] == '\r' )
            {
                /* decrement number of valid bytes to remove '\r' */
                -- self -> line_valid;
            }
            /* record end of line */
            ch = 0;
        }

        /* check if the buffer is full */
        if ( self -> line_valid == bsize )
        {
            /* I assume that the header lines will not be too large
               so only need to increment  by small chunks */
            bsize += 256;

            /* TBD - place an upper limit on resize */

            /* resize */
            rc = KDataBufferResize ( & self -> line_buffer, bsize );
            if ( rc != 0 )
                return rc;

            /* assign new base pointer */
            buffer = self -> line_buffer . base;
        }

        /* buffer is not full, insert char into the buffer */
        buffer [ self -> line_valid ] = ch;
        
        /* get out of loop if end of line */
        if ( ch == 0 )
        {
#if _DEBUGGING
            if ( KNSManagerIsVerbose ( self->mgr ) )
                KOutMsg( "RX:%s\n", buffer );
#endif
            break;
        }
        /* not end of line - increase num of valid bytes read */
        ++ self -> line_valid;
    }

    return rc;
}

/* AddHeaderString
 *  performs task of entering a header into BSTree
 *  or updating an existing node
 *
 *  Headers are always made up of a name: value pair
 */
static
rc_t KHttpAddHeaderString ( BSTree *hdrs, const String *name, const String *value )
{
    rc_t rc = 0;

    /* if there is no name - error */
    if ( name -> size == 0 )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
    else
    {
        /* test for previous existence of node by name */
        KHttpHeader * node = ( KHttpHeader * ) BSTreeFind ( hdrs, name, KHttpHeaderCmp );
        if ( node == NULL )
        {
            /* node doesnt exist - allocate memory for a new one */
            node = calloc ( 1, sizeof * node );
            if ( node == NULL )
                rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcNull );
            else
            {
                /* size of the KDataBuffer to store string data */
                size_t bsize = name -> size + value ->  size + 1;
                rc = KDataBufferMakeBytes ( & node -> value_storage, bsize );
                if ( rc == 0 )
                {
                    /* copy the string data into storage */
                    rc = string_printf ( node -> value_storage . base, bsize, NULL,
                                         "%S%S"
                                         , name
                                         , value );
                    if ( rc == 0 )
                    {
                        /* initialize the Strings to point into KHttpHeader node */
                        StringInit ( & node -> name, node -> value_storage . base, name -> size, name -> len );
                        StringInit ( & node -> value, node -> name . addr + name -> size, value -> size, value -> len );
                        
                        /* insert into tree, sorted by alphabetical order */
                        BSTreeInsert ( hdrs, & node -> dad, KHttpHeaderSort );
                        
                        return 0;
                    }
                    
                    KDataBufferWhack ( & node -> value_storage );
                }
                
                free ( node );
            }
        }
        
        /* node exists
           check that value param has data */
        else if ( value -> size != 0 )
        {
            /* find the current size of the data in the node */
            size_t cursize = node -> name . size + node -> value . size;
            /* resize databuffer to hold the additional value data + comma + nul */
            rc = KDataBufferResize ( & node -> value_storage, cursize + value -> size + 1 + 1 );
            if ( rc == 0 )
            {
                char *buffer = node -> value_storage . base;

                /* copy string data into buffer */
                rc = string_printf ( & buffer [ cursize ], value -> size + 2, NULL,
                                     ",%S"
                                     , value ); 
                if ( rc == 0 )
                {
                    /* update size and len of value in the node */
                    node -> value . size += value -> size + 1;
                    node -> value . len += value -> len + 1;
                    return 0;
                }
                
                /* In case of almost impossible error
                   restore values to what they were */
                KDataBufferResize ( & node -> value_storage, cursize + 1 );
            }
        }
    }

    return rc;
}

static
rc_t KHttpVAddHeader ( BSTree *hdrs, const char *_name, const char *_val, va_list args )
{
    rc_t rc;

    size_t bsize;
    String name, value;
    char buf [ 4096 ];

    /* initialize name string from param */
    StringInitCString ( & name, _name );

    /* copy data into buf, using va_list for value format */
    rc = string_vprintf ( buf, sizeof buf, &bsize, _val, args );
    if ( rc == 0 )
    {
        /* get length of buf */
        size_t blen = string_len ( buf, bsize );

        /* init value */
        StringInit ( & value, buf, bsize, ( uint32_t ) blen );

        rc = KHttpAddHeaderString ( hdrs, & name, & value );
    }

    return rc;
}

static
rc_t KHttpAddHeader ( BSTree *hdrs, const char *name, const char *val, ... )
{
    rc_t rc;
    va_list args;
    va_start ( args, val );
    rc = KHttpVAddHeader ( hdrs, name, val, args );
    va_end ( args );
    return rc;
}

/* Capture each header line to add to BSTree */
rc_t KHttpGetHeaderLine ( KHttp *self, BSTree *hdrs, bool *blank, bool *close_connection )
{
    /* Starting from the second line of the response */
    rc_t rc = KHttpGetLine ( self );
    if ( rc == 0 )
    {
        /* blank = empty line_buffer = separation between headers and body of response */
        if ( self -> line_valid == 0 )
            * blank = true;
        else
        {
            char * sep;
            char * buffer = self -> line_buffer . base;
            char * end = buffer + self -> line_valid;

            /* find the separation between name: value */
            sep = string_chr ( buffer, end - buffer, ':' );
            if ( sep == NULL )
                rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
            else
            {
                String name, value;
                const char * last = sep;
                
                /* trim white space around name */
                while ( buffer < last && isspace ( buffer [ 0 ] ) )
                    ++ buffer;
                while ( buffer < last && isspace ( last [ -1 ] ) )
                    -- last;

                /* assign the name data into the name string */
                StringInit ( & name, buffer, last - buffer, ( uint32_t ) ( last - buffer ) );
                
                /* move the buffer forward to value */
                buffer = sep + 1;
                last = end;
                
                /* trim white space around value */
                while ( buffer < last && isspace ( buffer [ 0 ] ) )
                    ++ buffer;
                while ( buffer < last && isspace ( last [ -1 ] ) )
                    -- last;

                /* assign the value data into the value string */
                StringInit ( & value, buffer, last - buffer, ( uint32_t ) ( last - buffer ) );
                
                /* check for Connection: close header */
                if ( name . size == sizeof "Connection" -1 && value . size == sizeof "close" - 1 )
                {
                    if ( tolower ( name . addr [ 0 ] ) == 'c' && tolower ( value . addr [ 0 ] ) == 'c' )
                    {
                        if ( strcase_cmp ( name . addr, name . size, "Connection", name . size, ( uint32_t ) name . size ) == 0 &&
                             strcase_cmp ( value . addr, value . size, "close", value . size, ( uint32_t ) value . size ) == 0 )
                        {
                            DBGMSG(DBG_VFS, DBG_FLAG(DBG_VFS),
                                ("*** seen connection close ***\n"));
                            * close_connection = true;
                        }
                    }
                }
                
                rc = KHttpAddHeaderString ( hdrs, & name, & value );
            }
        }
    }

    return rc;
}

/* Locate a KhttpHeader obj in BSTree */
static
rc_t KHttpFindHeader ( const BSTree *hdrs, const char *_name, char *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc = 0;
    String name;
    KHttpHeader * node;

    StringInitCString ( &name, _name );

    /* find the header */
    node = ( KHttpHeader * ) BSTreeFind ( hdrs, &name, KHttpHeaderCmp );
    if ( node == NULL )
    {
        rc = RC ( rcNS, rcNoTarg, rcSearching, rcName, rcNull );
    }
    else
    {
        /* make sure buffer is large enough */
        if ( bsize < node -> value . size )
        {
            /* return the amount needed */
            * num_read = node -> value . size;
            
            return RC ( rcNS, rcNoTarg, rcParsing, rcParam, rcInsufficient );
        }
        
        /* copy data and return the num_read */
        * num_read = string_copy ( buffer, bsize, node -> value . addr, node -> value . size );
    }
    return rc;
}

rc_t KHttpGetStatusLine ( KHttp *self, String *msg, uint32_t *status, ver_t *version )
{
    /* First time reading the response */
    rc_t rc = KHttpGetLine ( self );
    if ( rc == 0 )
    {
        char * sep;
        char * buffer = self -> line_buffer . base;
        char * end = buffer + self -> line_valid;

        /* Detect protocol
           expect HTTP/1.[01]<sp><digit>+<sp><msg>\r\n */
        sep = string_chr ( buffer, end - buffer, '/' );
        if ( sep == NULL )
        {
            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
        }
        else
        {
            /* make sure it is http */
            if ( strcase_cmp ( "http", 4, buffer, sep - buffer, 4 ) != 0 )
                rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcUnsupported );
            else
            {
                /* move buffer up to version */
                buffer = sep + 1;

                /* find end of version */
                sep = string_chr ( buffer, end - buffer, ' ' );
                if ( sep == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
                else
                {
                    /* must be 1.0 or 1.1 */
                    if ( ( string_cmp ( "1.0", 3, buffer, sep - buffer, 3 ) != 0 ) &&
                         ( string_cmp ( "1.1", 3, buffer, sep - buffer, 3 ) != 0 ) )
                        rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcUnsupported );
                    else
                    {
                        /* which one? */
                        * version = string_cmp ( "1.0", 3, buffer, sep - buffer, -1 ) == 0 ? 0x01000000 : 0x01010000;
                        
                        /* move up to status code */
                        buffer = sep + 1;

                        /* record status as uint32 
                         sep should point to 1 byte after end of status text */
                        * status = strtou32 ( buffer, & sep, 10 );

                        /* if at the end of buffer or sep didnt land on a space - error */
                        if ( sep == buffer || * sep != ' ' )
                            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
                        else
                        {
                            /* move up to status msg */
                            buffer = sep + 1;

                            /* initialize the msg String with the proper size and length */
                            StringInit ( msg, buffer, end - buffer, ( uint32_t ) ( end - buffer ) );
                        }
                    }
                }
            }
        }
    }
    return rc;
}

/*--------------------------------------------------------------------------
 * HttpStream
 *  structure that represents the body of the response
 */
struct KHttpStream
{
    KStream dad;
    
    /* content_length is the size of the chunk
       total_read is the number of read from the chunk */
    uint64_t content_length;
    uint64_t total_read;

    KHttp * http;

    uint8_t state; /* keeps track of state for chunked reader */
    bool size_unknown; /* for HTTP/1.0 dynamic */
};

enum 
{
    end_chunk,
    new_chunk,
    within_chunk,
    end_stream,
    error_state
};

static
rc_t CC KHttpStreamWhack ( KHttpStream *self )
{
    KHttpRelease ( self -> http );
    free ( self );
    return 0;
}

/* Read from stream - not chunked or within a chunk */
static
rc_t CC KHttpStreamRead ( const KHttpStream *cself,
    void *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc;
    KHttpStream *self = ( KHttpStream * ) cself;
    KHttp *http = self -> http;

    /* minimum of bytes requested and bytes available in stream */
    uint64_t num_to_read = self -> content_length - self -> total_read;

    /* take the minimum of bytes avail or bytes requested */
    if ( self -> size_unknown || bsize < num_to_read )
        num_to_read = bsize;

    /* Should be 0 because nothing has been read. Caller
       sets its to 0 */
    assert ( * num_read == 0 );
    /* exit if there is nothing to read */
    if ( num_to_read == 0 )
        return 0;

    /* read directly from stream 
       check if the buffer is empty */
    if ( KHttpBlockBufferIsEmpty ( http ) )
    {
        /* ReadAll blocks for 1.1. Server will drop the connection */
        rc =  KStreamRead ( http -> sock, buffer, num_to_read, num_read );
        if ( rc != 0 )
        {
            /* TBD - handle dropped connection - may want to reestablish */

            /* LOOK FOR DROPPED CONNECTION && SIZE UNKNOWN - HTTP/1.0 DYNAMIC CASE */
            if ( self -> size_unknown )
                rc = 0;
        }

        /* if nothing was read - incomplete transfer */
        else if ( ! self -> size_unknown && * num_read == 0 )
        {
            rc = RC ( rcNS, rcNoTarg, rcTransfer, rcNoObj, rcIncomplete);
        }

    }
    else
    {
        char *buf;

        /* bytes available in buffer */
        uint64_t bytes_in_buffer = http -> block_valid - http -> block_read;

        /* take the minimum of bytes avail or bytes requested */
        if ( num_to_read > bytes_in_buffer )
            num_to_read = bytes_in_buffer;

        /* capture base pointer */
        buf = http -> block_buffer . base;

        /* copy data into the user buffer from the offset of bytes not yet read */
        memcpy ( buffer, & buf [ http -> block_read ], num_to_read );

        /* update the amount read */
        http -> block_read += num_to_read;

        /* return how much was read */
        * num_read = num_to_read;

        rc = 0;
    }

    /* update the total from the stream
       keep track of total bytes read within the chunk */
    self -> total_read += * num_read;

    return rc;
}

/* Uses a state machine*/
static
rc_t CC KHttpStreamReadChunked ( const KHttpStream *cself,
    void *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc;
    char * sep;
    KHttpStream *self = ( KHttpStream * ) cself;
    KHttp * http = self -> http;

    assert ( * num_read == 0 );

    switch ( self -> state )
    {
    case end_chunk:
        rc = KHttpGetLine ( http );
        /* this should be the CRLF following chunk */
        if ( rc != 0 || http -> line_valid != 0 )
        {
            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcIncorrect);
            self -> state = error_state;
            break;
        }

        self -> state = new_chunk;

        /* NO BREAK */

        /* start */
    case new_chunk:

        /* Get chunk size */
        rc = KHttpGetLine ( http );
        if ( rc != 0 )
        {
            self -> state = error_state;
            break;
        }

        /* convert the hex number containing chunk size to uint64 
           sep should be pointing at nul byte */
        self -> content_length = strtou64 ( http -> line_buffer . base, & sep, 16 );

        /* TBD - eat spaces here? */
        /* check if there was no hex number, or sep isn't pointing to nul byte */
        if ( sep == http -> line_buffer . base || ( * sep != 0 && * sep != ';' ) )
        {
            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcIncorrect);
            self -> state = error_state;
            break;
        }

        /* check for end of stream */
        if ( self -> content_length == 0 )
        {
            self -> state = end_stream;
            return 0;
        }

        /* havent read anything - start at 0 */
        self -> total_read = 0;

        /* now within a chunk */
        self -> state = within_chunk;

        /* NO BREAK */

    case within_chunk: 
        /* start reading */
        rc = KHttpStreamRead ( self, buffer, bsize, num_read );
        if ( rc != 0 ) /* TBD - handle connection errors */
            self -> state = error_state;
        /* incomplete if nothing to read */
        else if ( * num_read == 0 )
        {
            rc = RC ( rcNS, rcNoTarg, rcTransfer, rcNoObj, rcIncomplete);
            self -> state = error_state;
        }
        /* check for end of chunk */
        else if ( self -> total_read == self -> content_length )
            self -> state = end_chunk;
        break;

    case end_stream:
        return 0;

    case error_state:
        rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcIncorrect );
        break;

    default:
        /* internal error */
        rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcError );
    }

    return rc;
}

/* cannot write - for now */
static
rc_t CC KHttpStreamWrite ( KHttpStream *self,
    const void *buffer, size_t size, size_t *num_writ )
{
    return RC ( rcNS, rcNoTarg, rcWriting, rcNoObj, rcError );
}

static KStream_vt_v1 vtKHttpStream = 
{
    1, 0,
    KHttpStreamWhack,
    KHttpStreamRead,
    KHttpStreamWrite
};

static KStream_vt_v1 vtKHttpStreamChunked =
{
    1, 0,
    KHttpStreamWhack,
    KHttpStreamReadChunked,
    KHttpStreamWrite
};

/* Make a KHttpStream object */
static
rc_t KHttpStreamMake ( KHttp *self, KStream **sp, const char *strname, size_t content_length, bool size_unknown )
{
    rc_t rc;
    KHttpStream *s = calloc ( 1, sizeof * s );
    if ( s == NULL )
        rc = RC ( rcNS, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    else
    {
        rc = KStreamInit ( & s -> dad, ( const KStream_vt * ) & vtKHttpStream, 
                           "KHttpStream", strname, true, false );
        if ( rc == 0 )
        {                                       
            rc = KHttpAddRef ( self );
            if ( rc == 0 )
            {
                s -> http = self;
                s -> content_length = content_length;
                s -> size_unknown = size_unknown;
                *sp = & s -> dad;
                return 0;
            }
        }
        free ( s );
    }
    *sp = NULL;

    return rc;
}

static
rc_t KHttpStreamMakeChunked ( KHttp *self, KStream **sp, const char *strname )
{
    rc_t rc;
    KHttpStream *s = calloc ( 1, sizeof * s );
    if ( s == NULL )
        rc = RC ( rcNS, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    else
    {
        rc = KStreamInit ( & s -> dad, ( const KStream_vt * ) & vtKHttpStreamChunked, 
                           "KHttpStreamChunked", strname, true, false );
        if ( rc == 0 )
        {
            rc = KHttpAddRef ( self );
            if ( rc == 0 )
            {
                s -> http = self;

                /* state should be new_chunk */
                s -> state = new_chunk;

                *sp = & s -> dad;
                return 0;
            }
        }
        free ( s );
    }
    *sp = NULL;

    return rc;
}

/*--------------------------------------------------------------------------
 * KHttpResult
 *  hyper text transfer protocol
 *  Holds all the headers in a BSTree
 *  Records the status msg, status code and version of the response 
 */
struct KHttpResult
{
    KHttp *http;
    
    BSTree hdrs;
    
    String msg;
    uint32_t status;
    ver_t version;

    KRefcount refcount;
    bool close_connection;
};

static
rc_t KHttpResultWhack ( KHttpResult * self )
{
    BSTreeWhack ( & self -> hdrs, KHttpHeaderWhack, NULL );
    if ( self -> close_connection )
    {
        DBGMSG(DBG_VFS, DBG_FLAG(DBG_VFS),
            ("*** closing connection ***\n"));
        KHttpClose ( self -> http );
    }
    KHttpRelease ( self -> http );
    KRefcountWhack ( & self -> refcount, "KHttpResult" );
    free ( self );
    return 0;
}


/* Sends the request and receives the response into a KHttpResult obj */
static 
rc_t KHttpSendReceiveMsg ( KHttp *self, KHttpResult **rslt,
    const char *buffer, size_t len, const KDataBuffer *body, const char *url )
{
    rc_t rc = 0;

    size_t sent;


    /* TBD - may want to assert that there is an empty line in "buffer" */
#if _DEBUGGING
    if ( KNSManagerIsVerbose ( self->mgr ) )
        KOutMsg( "TX:%.*s", len, buffer );
#endif

    /* reopen connection if NULL */
    if ( self -> sock == NULL )
        rc = KHttpOpen ( self, & self -> hostname, self -> port );

    /* ALWAYS want to use write all when sending */
    if ( rc == 0 )
        rc = KStreamWriteAll ( self -> sock, buffer, len, & sent ); 
    
    /* check the data was completely sent */
    if ( rc == 0 && sent != len )
        rc = RC ( rcNS, rcNoTarg, rcWriting, rcTransfer, rcIncomplete );
    if ( rc == 0 && body != NULL )
    {
        /* "body" contains bytes plus trailing NUL */
        size_t to_send = ( size_t ) body -> elem_count - 1;
        rc = KStreamWriteAll ( self -> sock, body -> base, to_send, & sent );
        if ( rc == 0 && sent != to_send )
            rc = RC ( rcNS, rcNoTarg, rcWriting, rcTransfer, rcIncomplete );
    }
    if ( rc == 0 )
    {
        String msg;
        ver_t version;
        uint32_t status;

        /* we have now received a response 
           start reading the header lines */
        rc = KHttpGetStatusLine ( self, & msg, & status, & version );
        if ( rc == 0 )
        {         
            /* create a result object with enough space for msg string + nul */
            KHttpResult *result = malloc ( sizeof * result + msg . size + 1 );
            if ( result == NULL )
                rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcExhausted );
            else
            {
                /* zero out */
                memset ( result, 0, sizeof * result );
                
                rc = KHttpAddRef ( self );
                if ( rc == 0 )
                {
                    bool blank;

                    /* treat excess allocation memory as text space */
                    char *text = ( char* ) ( result + 1 );

                    /* copy in the data to the text space */
                    string_copy ( text, msg . size + 1, msg . addr, msg . size );

                    /* initialize the result members
                       "hdrs" is initialized via "memset" above
                     */
                    result -> http = self;
                    result -> status = status;
                    result -> version = version;

                    /* correlate msg string in result to the text space */
                    StringInit ( & result -> msg, text, msg . size, msg . len );

                    /* TBD - pass in URL as instance identifier */
                    KRefcountInit ( & result -> refcount, 1, "KHttpResult", "sending-msg", url );

                    /* receive and parse all header lines 
                       blank = end of headers */
                    for ( blank = false; ! blank && rc == 0; )
                        rc = KHttpGetHeaderLine ( self, & result -> hdrs, & blank, & result -> close_connection );

                    if ( rc == 0 )
                    {
                        /* assign to OUT result obj */
                        * rslt = result;
                        return 0; 
                    }

                    BSTreeWhack ( & result -> hdrs, KHttpHeaderWhack, NULL );
                }

                KHttpRelease ( self );
            }

            free ( result );
        }
    }
    return rc;
}

/* AddRef
 * Release
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC KHttpResultAddRef ( const KHttpResult *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, "KHttpResult" ) )
        {
        case krefLimit:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcRange, rcExcessive );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcSelf, rcInvalid );
        default:
            break;
        }
    }

    return 0;
}

LIB_EXPORT rc_t CC KHttpResultRelease ( const KHttpResult *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, "KHttpResult" ) )
        {
        case krefWhack:
            return KHttpResultWhack ( ( KHttpResult* ) self );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcReleasing, rcRange, rcExcessive );
        default:
            break;
        }
    }

    return 0;
}


/* Status
 *  access the response status code
 *  and optionally the message
 *
 *  "code" [ OUT ] - return parameter for status code
 *
 *  "msg_buff" [ IN, NULL OKAY ] and "buff_size" [ IN, ZERO OKAY ] -
 *   buffer for capturing returned message. if "msg_buff" is not
 *   NULL and "buff_size" is insufficient for copying status message,
 *   the message returns rcBuffer, rcInsufficient.
 *
 *  "msg_size" [ OUT, NULL OKAY ] - size of returned message in bytes.
 *   if not NULL, returns the size of status message. if "msg_buff" is
 *   NULL, returns rcBuffer, rcNull.
 */
LIB_EXPORT rc_t CC KHttpResultStatus ( const KHttpResult *self, uint32_t *code,
    char *msg_buff, size_t buff_size, size_t *msg_size )
{
    rc_t rc;

    /* check OUT parameters */
    if ( code == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        /* IN parameters */
        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
        else
        {
            /* capture the status code to OUT param */
            * code = self -> status;
            rc = 0;

            /* if asking about msg size */
            if ( msg_size != NULL )
            {
                /* capture the msg size */
                * msg_size = self -> msg . size;

                /* catch NULL buffer pointer */
                if ( msg_buff == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcValidating, rcBuffer, rcNull );
            }

            /* if they apparently want the message */
            if ( msg_buff != NULL )
            {
                /* check for an insufficient buffer size */
                if ( buff_size < self -> msg . size )
                    rc = RC ( rcNS, rcNoTarg, rcValidating, rcBuffer, rcInsufficient );
                else
                    /* copy out the message */
                    string_copy ( msg_buff, buff_size, self -> msg . addr, self -> msg . size );
            }

            return rc;
        }

        * code = 0;
    }

    return rc;
}


/* KeepAlive
 *  retrieves keep-alive property of response
 */
LIB_EXPORT bool CC KHttpResultKeepAlive ( const KHttpResult *self )
{
    rc_t rc;

    if ( self != NULL )
    {
        /* we're requiring version 1.1 -
           some 1.0 servers also supported it... */
        if ( self -> version == 0x01010000 )
        {
            size_t num_writ;
            char buffer [ 1024 ];
            size_t bsize = sizeof buffer;

            /* retreive the node that has the keep-alive property */
            rc = KHttpResultGetHeader ( self, "Connection", buffer, bsize, & num_writ );
            if ( rc == 0 )
            {
                String keep_alive, compare;

                /* init strings */
                StringInitCString ( & keep_alive, buffer );
                CONST_STRING ( & compare, "keep-alive" );

                /* compare strings for property value */
                if ( StringCaseCompare ( & keep_alive, & compare ) == 0 )
                    return true;
            }
        }
    }
    return false;
}


/* Range
 *  retrieves position and partial size for partial requests
 *
 *  "pos" [ OUT ] - offset to beginning portion of response
 *
 *  "bytes" [ OUT ] - size of range
 *
 *  HERE WE NEED TO HAVE PASSED THE RANGE REQUEST TO THE RESULT ON CREATION,
 *  AND WE WILL RESPOND TO THE HTTP "PARTIAL RESULT" OR WHATEVER RETURN CODE,
 *  AND BASICALLY UPDATE WHAT THE RANGE WAS.
 */
static
rc_t KHttpResultHandleContentRange ( const KHttpResult *self, uint64_t *pos, size_t *bytes )
{
    rc_t rc;
    size_t num_read;
    char buffer [ 1024 ];
    const size_t bsize = sizeof buffer;

    /* get Content-Range
     *  expect: "bytes <first-position>-<last-position>/<total-size>"
     */
    rc = KHttpResultGetHeader ( self, "Content-Range", buffer, bsize, & num_read );
    if ( rc == 0 )
    {
        char * sep;
        char *buf = buffer;
        const char *end = & buffer [ num_read ];

        /* look for separation of 'bytes' and first position */
        sep = string_chr ( buf, end - buf, ' ' );
        if ( sep == NULL )
            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
        else
        {
            uint64_t start_pos;
                        
            /* buf now points to value */
            buf = sep + 1;

            /* capture starting position 
               sep should land on '-' */
            start_pos = strtou64 ( buf, & sep, 10 );

            /* check if we didnt read anything or sep didnt land on '-' */
            if ( sep == buf || * sep != '-' )
                rc =  RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
            else
            {
                uint64_t end_pos;

                buf = sep + 1;
                end_pos = strtou64 ( buf, & sep, 10 );
                if ( sep == buf || * sep != '/' )
                    rc =  RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );                 
                else
                {
                    uint64_t total;

                    buf = sep +1;
                    total = strtou64 ( buf, &sep, 10 );
                    if ( sep == buf || * sep != 0 )
                        rc =  RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );                 
                    else
                    {
                        /* check variables */
                        if ( total == 0 ||
                             start_pos > total ||
                             end_pos < start_pos ||
                             end_pos > total )
                        {
                            rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
                        }
                        else
                        {
                            uint64_t length;
                                        
                            /* get content-length to confirm bytes sent */
                            rc = KHttpResultGetHeader ( self, "Content-Length", buffer, bsize, & num_read );
                            if ( rc != 0 )
                            {
                                            
                                /* remember that we can have chunked encoding,
                                   so "Content-Length" may not exist. */
                                * pos = start_pos;
                                * bytes = end_pos - start_pos; 
                                            
                                return 0;
                            }

                            buf = buffer;
                            end = & buffer [ num_read ];
                                            
                            /* capture the length */
                            length  = strtou64 ( buf, & sep, 10 );
                            if ( sep == buf || * sep != 0 )
                                rc =  RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
                            else 
                            {
                                /* finally check all the acquired information */
                                if ( ( length != ( ( end_pos - start_pos ) + 1 ) ) ||
                                     ( length > total ) )
                                    rc = RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
                                else
                                {
                                    /* assign to OUT params */
                                    * pos = start_pos;
                                    * bytes = length; 
                                                    
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return rc;
}

LIB_EXPORT rc_t CC KHttpResultRange ( const KHttpResult *self, uint64_t *pos, size_t *bytes )
{
    rc_t rc;

    if ( pos ==  NULL || bytes == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
    else
    {
        switch ( self -> status )
        {
        case 206:
            /* partial content */
            rc = KHttpResultHandleContentRange ( self, pos, bytes );
            if ( rc == 0 )
                return 0;

        case 416:
            /* unsatisfiable range */
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcError, rcIncorrect );
            break;

        default:
            /* codes not handling right now */
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcError, rcUnsupported );
        }
    }

    if ( pos != NULL )
        * pos = 0;
    if ( bytes != NULL )
        * bytes = 0;

    return rc;
}


/* Size
 *  retrieves overall size of entity, if known
 *
 *  "size" [ OUT ] - size in bytes of response
 *   this is the number of bytes that may be expected from the input stream
 *
 */
LIB_EXPORT bool CC KHttpResultSize ( const KHttpResult *self, uint64_t *size )
{
    if ( size != NULL && self != NULL )
    {
        rc_t rc;
        size_t num_read;
        char buffer [ 1024 ];
        const size_t bsize = sizeof buffer;
        
        /* check for content-length */
        rc = KHttpResultGetHeader ( self, "Content-Length", buffer, bsize, & num_read );
        if ( rc == 0 )
        {
            char * sep;
            
            /* capture length as uint64 */
            uint64_t length = strtou64 ( buffer, & sep, 10 );
            if ( sep == buffer || * sep != 0 )
                rc =  RC ( rcNS, rcNoTarg, rcParsing, rcNoObj, rcNotFound );
            else
            {
                /* assign to OUT param */
                * size = length;
                return true;
            }
        }
    }
    return false;
}

/* AddHeader
 *  allow addition of an arbitrary HTTP header to RESPONSE
 *  this can be used to repair or normalize odd server behavior
 *
 */
LIB_EXPORT rc_t CC KHttpResultAddHeader ( KHttpResult *self,
    const char *name, const char *val, ... )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
    else if ( name == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    /* have to test for empty name */
    else if ( name [ 0 ] == 0 )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
    else if ( val == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    /* same for empty value fmt string */
    else if ( val [ 0 ] == 0 )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
    else
    {
        va_list args;
        va_start ( args, val );
        
        rc = KHttpVAddHeader ( & self -> hdrs, name, val, args );
        
        va_end ( args );
    }
    return rc;
}


/* GetHeader
 *  retrieve named header if present
 *  this cand potentially return a comma separated value list
 */
LIB_EXPORT rc_t CC KHttpResultGetHeader ( const KHttpResult *self, const char *name,
    char *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc = 0;

    if ( num_read == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        * num_read = 0;

        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
        else if ( name == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        else if ( buffer == NULL && bsize != 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        else
        {
            rc = KHttpFindHeader ( & self -> hdrs, name, buffer, bsize, num_read );
        }
    }

    return rc;
}

#if _DEBUGGING
static
void PrintHeaders ( BSTNode *n, void *ignore )
{
    KHttpHeader *node = ( KHttpHeader * ) n;

    KOutMsg ( "%S: %S\n",
              & node -> name,
              & node -> value );
}
#endif

/* GetInputStream
 *  access the body of response as a stream
 *  only reads are supported
 *
 *  "s" [ OUT ] - return parameter for input stream reference
 *   must be released via KStreamRelease
 */
LIB_EXPORT rc_t CC KHttpResultGetInputStream ( KHttpResult *self, KStream ** s )
{
    rc_t rc;
    
    if ( s == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
        else
        {
            char buffer [ 512 ];
            size_t num_read = 0;
            uint64_t content_length = 0;

            /* find header to check for type of data being received 
               assign bytes read from value to num_read */
            rc = KHttpResultGetHeader ( self, "Transfer-Encoding", buffer, sizeof buffer, & num_read );
            if ( rc == 0 && num_read > 0 )
            {
                /* check if chunked encoding */
                if ( strcase_cmp ( "chunked", sizeof "chunked" - 1,
                    buffer, num_read, sizeof "chunked" - 1 ) == 0 )
                {
                    return KHttpStreamMakeChunked ( self -> http, s, "KHttpStreamChunked" );
                }
                /* TBD - print actual value */
                LOGERR ( klogSys, rc, "Transfer-Encoding does not provide a value" );

            }
            /* get the content length of the entire stream if known */
            if ( KHttpResultSize ( self, & content_length ) )
                return KHttpStreamMake ( self -> http, s, "KHttpStream", content_length, false );

            /* detect pre-HTTP/1.1 dynamic content */
            if ( self -> version < 0x01010000 )
                return KHttpStreamMake ( self -> http, s, "KHttpStream", 0, true );

#if _DEBUGGING
            BSTreeForEach ( & self -> hdrs, false, PrintHeaders, NULL );
#endif            

            rc = RC ( rcNS, rcNoTarg, rcValidating, rcMessage, rcUnsupported );
            LOGERR ( klogInt, rc, "HTTP response does not give content length" ); 

        }
    }
    
    * s = NULL;
    
    return rc;
}



/*--------------------------------------------------------------------------
 * KHttpRequest
 *  hyper text transfer protocol
 */

struct KHttpRequest
{
    KHttp * http;

    URLBlock url_block;
    KDataBuffer url_buffer;

    KDataBuffer body;
    
    BSTree hdrs;

    KRefcount refcount;
};

static
rc_t KHttpRequestClear ( KHttpRequest *self )
{
    KDataBufferWhack ( & self -> url_buffer );

    return 0;
}

static
rc_t KHttpRequestWhack ( KHttpRequest * self )
{
    KHttpRequestClear ( self );

    KHttpRelease ( self -> http );
    KDataBufferWhack ( & self -> body );
    
    BSTreeWhack  ( & self -> hdrs, KHttpHeaderWhack, NULL );
    KRefcountWhack ( & self -> refcount, "KHttpRequest" );
    free ( self );
    return 0;
}

static 
rc_t KHttpRequestInit ( KHttpRequest * req,
    const URLBlock *b, const KDataBuffer *buf )
{
    rc_t rc = KDataBufferSub ( buf, & req -> url_buffer, 0, UINT64_MAX );
    if ( rc == 0 )
    {
        /* assign url_block */
        req -> url_block = * b;
    }
    return rc;
}
        

/* MakeRequestInt[ernal]
 */
static
rc_t KHttpMakeRequestInt ( const KHttp *self,
    KHttpRequest **_req, const URLBlock *block, const KDataBuffer *buf )
{
    rc_t rc;

    /* create the object with empty buffer */
    KHttpRequest * req = calloc ( 1, sizeof * req );
    if ( req == NULL )
        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcNull );
    else
    {
        rc = KHttpAddRef ( self );
        if ( rc == 0 )
        {
            /* assign http */
            req -> http = ( KHttp* ) self; 

            /* initialize body to zero size */
            KDataBufferClear ( & req -> body );
                
            KRefcountInit ( & req -> refcount, 1, "KHttpRequest", "make", buf -> base ); 

            /* fill out url_buffer with URL */
            rc = KHttpRequestInit ( req, block, buf );
            if ( rc == 0 )
            {
                * _req = req;
                return 0;
            }

            KHttpRelease ( self );
        }
    }
    
    free ( req );

    return rc;
}

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
LIB_EXPORT rc_t CC KHttpVMakeRequest ( const KHttp *self,
    KHttpRequest **_req, const char *url, va_list args )
{
    rc_t rc;
    
    if ( _req == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        * _req = NULL;

        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
        else if ( url ==  NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        else if ( url [ 0 ] == 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
        else
        {
            KDataBuffer buf;

            /* make a KDataBuffer and copy in url with the va_lis */
            /* rc = KDataBufferMakeBytes ( & buf, 4096 );*/
            KDataBufferClear ( &buf );
            
            rc = KDataBufferVPrintf ( &buf, url, args );
            if ( rc == 0 )
            {
                /* parse the URL */
                URLBlock block;
                rc = ParseUrl ( & block, buf . base, buf . elem_count - 1 );
                if ( rc == 0 )
                    rc = KHttpMakeRequestInt ( self, _req, & block, & buf );
            }

            KDataBufferWhack ( & buf );
        }
    }

    return rc;
}

/* MakeRequest
 *  create a request that can be used to contact HTTP server
 *
 *  "req" [ OUT ] - return parameter for HTTP request object
 *
 *  "url" [ IN ] - full resource identifier. if "conn" is NULL,
 *   the url is parsed for remote endpoint and is opened by mgr.
 */
LIB_EXPORT rc_t CC KHttpMakeRequest ( const KHttp *self,
    KHttpRequest **_req, const char *url, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, url );
    rc = KHttpVMakeRequest ( self, _req, url, args );
    va_end ( args );

    return rc;
}

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
LIB_EXPORT rc_t CC KNSManagerMakeRequest ( const KNSManager *self,
    KHttpRequest **req, ver_t vers, KStream *conn, const char *url, ... )
{
    rc_t rc;

    if ( req == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {
        * req = NULL;

        if ( self == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
        else if ( vers < 0x01000000 || vers > 0x01010000 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcIncorrect );
        else if ( url == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcString, rcNull );
        else if ( url [ 0 ] == 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcString, rcEmpty );
        else
        {
            va_list args;
            KDataBuffer buf;
            
            KDataBufferClear ( &buf );
                /* convert var-arg "url" to a full string */
            va_start ( args, url );
            rc = KDataBufferVPrintf ( & buf, url, args );
            va_end ( args );
            if ( rc == 0 )
            {
                /* parse the URL */
                URLBlock block;
                rc = ParseUrl ( & block, buf . base, buf . elem_count - 1 );
                if ( rc == 0 )
                {
                    KHttp * http;
                    
                    rc = KNSManagerMakeHttpInt ( self, & http, & buf, conn, vers, & block . host, block . port );
                    if ( rc == 0 )
                    {
                        rc = KHttpMakeRequestInt ( http, req, & block, & buf );
                        KHttpRelease ( http );
                    }
                }
            }
            KDataBufferWhack ( & buf );
        }
    }
    return rc;
}


/* AddRef
 * Release
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC KHttpRequestAddRef ( const KHttpRequest *self )
{
        if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, "KHttpRequest" ) )
        {
        case krefLimit:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcRange, rcExcessive );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcAttaching, rcSelf, rcInvalid );
        default:
            break;
        }
    }

    return 0;
}

LIB_EXPORT rc_t CC KHttpRequestRelease ( const KHttpRequest *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, "KHttpRequest" ) )
        {
        case krefWhack:
            return KHttpRequestWhack ( ( KHttpRequest* ) self );
        case krefNegative:
            return RC ( rcNS, rcNoTarg, rcReleasing, rcRange, rcExcessive );
        default:
            break;
        }
    }

    return 0;
}


/* Connection
 *  sets connection management headers
 *
 *  "close" [ IN ] - if "true", inform the server to close the connection
 *   after its response ( default for version 1.0 ). when "false" ( default
 *   for version 1.1 ), ask the server to keep the connection open.
 *
 * NB - the server is not required to honor the request
 */
LIB_EXPORT rc_t CC KHttpRequestConnection ( KHttpRequest *self, bool close )
{
    rc_t rc = 0;

    if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
    else
    {
        String name, value;
        
        CONST_STRING ( & name, "Connection" );
        /* if version is 1.1 and close is true, add 'close' to Connection header value. */
        /* if version if 1.1 default is false - no action needed */
        if ( self -> http -> vers == 0x01010000 && close == true )
            CONST_STRING ( & value, "close" );
        else if ( self -> http -> vers == 0x01000000 && close == false )
            CONST_STRING ( & value, "keep-alive" );
        else
            return 0;

        rc = KHttpRequestAddHeader ( self,  name . addr, value . addr );
            
    }
    return rc;
}


/* ByteRange
 *  set requested byte range of response
 *
 *  "pos" [ IN ] - beginning offset within remote entity
 *
 *  "bytes" [ IN ] - the number of bytes being requested
 */
LIB_EXPORT rc_t CC KHttpRequestByteRange ( KHttpRequest *self, uint64_t pos, size_t bytes )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull);
    else
    {
        char  range [ 256 ];
        size_t num_writ;
        String name, value;
        
        CONST_STRING ( & name, "Range" );
        rc = string_printf ( range, sizeof range, & num_writ, "bytes=%u-%u"
                             , pos
                             , pos + bytes - 1);
        if ( rc == 0 )
        {
            StringInitCString ( & value, range );

            rc = KHttpRequestAddHeader ( self, name . addr, value . addr );
        }
    }
    return rc;
}


/* AddHeader
 *  allow addition of an arbitrary HTTP header to message
 */
LIB_EXPORT rc_t CC KHttpRequestAddHeader ( KHttpRequest *self,
                                           const char *name, const char *val, ... )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull);
    else
    {
        if ( name == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        /* have to test for empty name, too */
        else if ( name [ 0 ] == 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
        else if ( val == NULL )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
        /* same for empty value fmt string */
        else if ( val [ 0 ] == 0 )
            rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInsufficient );
        else
        {
            size_t name_size;

            va_list args;
            va_start ( args, val );

            /* disallow setting of "Host" and other headers */
            name_size = string_size ( name );

            if ( strcase_cmp ( name, name_size, "Host", sizeof "Host", sizeof "Host" - 1 ) == 0 )
                rc = RC ( rcNS, rcNoTarg, rcComparing, rcParam, rcUnsupported );
            if ( strcase_cmp ( name, name_size, "Content-Length", sizeof "Content-Length", sizeof "Content-Length" - 1 ) == 0 )
                rc = RC ( rcNS, rcNoTarg, rcComparing, rcParam, rcUnsupported );
            else
                rc = KHttpVAddHeader ( & self -> hdrs, name, val, args );

            va_end ( args );
        }
    }
    return rc;
}

/* AddPostParam
 *  adds a parameter for POST
 */
LIB_EXPORT rc_t CC KHttpRequestVAddPostParam ( KHttpRequest *self, const char *fmt, va_list args )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull );
    else if ( fmt == NULL )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else if ( fmt [ 0 ] == 0 )
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull );
    else
    {

        /* TBD - reject embedded newlines */
        /* TBD - URL-encoding or at least detect need for it */

        /* first param */
        if ( self -> body . elem_count == 0 )
            rc = KDataBufferVPrintf ( & self -> body, fmt, args );
        else
        {
            /* additional param - add separator */
            rc = KDataBufferPrintf ( & self -> body, "&" );
            if ( rc == 0 )
                rc = KDataBufferVPrintf ( & self -> body, fmt, args );
        }
    }

    return rc;
}

LIB_EXPORT rc_t CC KHttpRequestAddPostParam ( KHttpRequest *self, const char *fmt, ... )
{
    rc_t rc;

    va_list args;
    va_start ( args, fmt );
    rc = KHttpRequestVAddPostParam ( self, fmt, args );
    va_end ( args );

    return rc;
}


static
rc_t KHttpRequestFormatMsg ( const KHttpRequest *self,
    char *buffer, size_t bsize, const char *method, size_t *len )
{
    rc_t rc;
    size_t total;
    const KHttpHeader *node;
    
    KHttp *http = self -> http;

    /* determine if there is a query */
    const char *has_query = ( self -> url_block . query . size == 0 ) ? "" : "?";

    /* there are 2 places where the can be a host name stored
       we give preference to the one attached to the url_block, because
       it is the most recently determined.
       If that one is empty, we look at the http object for its
       host name.
       Error if both are empty */
    String hostname = self -> url_block . host;
    if ( hostname . size == 0 )
    {
        hostname = http -> hostname;
        if ( hostname . size == 0 )
            return RC ( rcNS, rcNoTarg, rcValidating, rcName, rcEmpty );
    }

    /* start building the buffer that will be sent 
       We are inlining the host:port, instead of
       sending it in its own header */

    /* TBD - should we include the port with the host name? */
    #if 0
    rc = string_printf ( buffer, bsize, len, 
                         "%s %S://%S%S%s%S HTTP/%.2V\r\n"
                         , method
                         , & self -> url_block . scheme
                         , & hostname
                         , & self -> url_block . path
                         , has_query
                         , & self -> url_block . query
                         , http -> vers );
     #else
    rc = string_printf ( buffer, bsize, len, 
                         "%s %S%s%S HTTP/%.2V\r\nHost: %S\r\nAccept: */*\r\n"
                         , method
                         , & self -> url_block . path
                         , has_query
                         , & self -> url_block . query
                         , http -> vers
                         , & hostname );
     #endif
    

    /* print all headers remaining into buffer */
    total = * len;
    for ( node = ( const KHttpHeader* ) BSTreeFirst ( & self -> hdrs );
          rc == 0 && node != NULL;
          node = ( const KHttpHeader* ) BSTNodeNext ( & node -> dad ) )
    {
        /* add header line */
        rc = string_printf ( & buffer [ total ], bsize - total, len,
                             "%S: %S\r\n"
                             , & node -> name
                             , & node -> value );
        total += * len;
    }

    /* add terminating empty header line */
    if ( rc == 0 )
    {
        rc = string_printf ( & buffer [ total ], bsize - total, len, "\r\n" );
        * len += total;
    }
    
    return rc;
}

static
rc_t KHttpRequestHandleRedirection ( KHttpRequest *self, KHttpResult *rslt )
{
    rc_t rc = 0;
    String Location;
    KHttpHeader *loc;

    /* find relocation URI */
    CONST_STRING ( & Location, "Location" );
    loc = ( KHttpHeader* ) BSTreeFind ( & rslt -> hdrs, & Location, KHttpHeaderCmp );
    if ( loc == NULL )
    {
        LOGERR ( klogSys, rc, "Location header not found on relocate msg" );
        return RC ( rcNS, rcNoTarg, rcValidating, rcNode, rcNull );
    }

    /* capture the new URI in loc -> value_storage */
    if ( loc -> value . size == 0 )
    {
        LOGERR ( klogSys, rc, "Location does not provide a value" );
        rc = RC ( rcNS, rcNoTarg, rcValidating, rcNode, rcIncorrect );
    }
    else
    {
        URLBlock b;
        KDataBuffer uri;
        /* pull out uri */
        rc = KDataBufferSub ( &loc -> value_storage, &uri, loc -> name . size, loc -> value . size + 1 );
        if ( rc == 0 )
        {
            /* parse the URI into local url_block */
            rc = ParseUrl ( &b, uri . base, uri . elem_count - 1 );
            if ( rc == 0 )
            {
                KHttp *http = self -> http;

                /* close the open http connection and clear out all data except for the manager */
                KHttpClear ( http );

                /* clear the previous endpoint */
                http -> ep_valid = false;

                /* reinitialize the http from uri */
                rc = KHttpInit ( http, &uri, NULL, http -> vers , &b . host, b . port );
                if ( rc == 0 )
                {
                    KHttpRequestClear ( self );
                    rc = KHttpRequestInit ( self, &b, &uri );
                    if ( rc == 0 )
                        KHttpResultRelease ( rslt );
                }
            }

            KDataBufferWhack ( & uri );
        }
        
    } 

    return rc;
}

static
rc_t KHttpRequestSendReceiveNoBody ( KHttpRequest *self, KHttpResult **_rslt, const char *method )
{
    rc_t rc = 0;

    KHttpResult *rslt;

    uint32_t i;
    const uint32_t max_redirect = 5;

    /* TBD - may want to prevent a Content-Type or other headers here */

    if ( self -> body . elem_count != 0 )
        return RC ( rcNS, rcNoTarg, rcValidating, rcNoObj, rcIncorrect );

    for ( i = 0; i < max_redirect; ++ i )
    {
        size_t len;
        char buffer [ 4096 ];

        /* create message */
        rc = KHttpRequestFormatMsg ( self, buffer, sizeof buffer, method, & len );
        if ( rc != 0 )
            break;

        /* send the message and create a response */
        rc = KHttpSendReceiveMsg ( self -> http, _rslt, buffer, len, NULL, self -> url_buffer . base );
        if ( rc != 0 )
            break;

        /* look at status code */
        rslt = * _rslt;
        switch ( rslt -> status )
        {
        case 100:
            /* Continue
               The client SHOULD continue with its request. This interim response is used
               to inform the client that the initial part of the request has been received
               and has not yet been rejected by the server. The client SHOULD continue by
               sending the remainder of the request or, if the request has already been completed,
               ignore this response. The server MUST send a final response after the request
               has been completed. See section 8.2.3 for detailed discussion of the use and
               handling of this status code. */

            /* TBD - should not see this, but needs to be handled */
            return 0;

            /* TBD - need to include RFC rule for handling codes for HEAD and GET */
        case 301: /* "moved permanently" */
        case 302: /* "found" - okay to reissue for HEAD and GET, but not for POST */
        case 307: /* "moved temporarily" */
            break;

        case 505: /* HTTP Version Not Supported */
            if ( self -> http -> vers > 0x01000000 )
            {
                /* downgrade version requested */
                self -> http -> vers -= 0x00010000;
                /* TBD - remove any HTTP/1.1 specific headers */
                continue;
            }

            /* NO BREAK */

        default:

            /* TBD - should all status codes be interpreted as rc ? */
            return 0;
        }

        /* reset connection, reset request */
        KOutMsg ( "\nRedirected!!!\n\n" );
        rc = KHttpRequestHandleRedirection ( self, rslt );
        if ( rc != 0 )
            break;
    }
    return rc;
}

/* HEAD
 *  send HEAD message
 */
LIB_EXPORT rc_t CC KHttpRequestHEAD ( KHttpRequest *self, KHttpResult **rslt )
{
    return KHttpRequestSendReceiveNoBody ( self, rslt, "HEAD" );
} 

/* GET
 *  send GET message
 *  all query AND post parameters are combined in URL
 */
LIB_EXPORT rc_t CC KHttpRequestGET ( KHttpRequest *self, KHttpResult **rslt )
{
    return KHttpRequestSendReceiveNoBody ( self, rslt, "GET" );
}

/* POST
 *  send POST message
 *  query parameters are sent in URL
 *  post parameters are sent in body
 */
LIB_EXPORT rc_t CC KHttpRequestPOST ( KHttpRequest *self, KHttpResult **_rslt )
{
    rc_t rc = 0;

    KHttpResult *rslt;

    uint32_t i;
    const uint32_t max_redirect = 5;

    /* TBD comment - add debugging test to ensure "Content-Length" header not present */

    /* fix headers for POST params */
    if ( self -> body . elem_count > 1 )
    {
        /* "body" contains data plus NUL byte */
        rc = KHttpAddHeader ( & self -> hdrs, "Content-Length", "%lu", self -> body . elem_count - 1 );
        if ( rc == 0 )
        {
            String Content_Type;
            const KHttpHeader *node;

            CONST_STRING ( & Content_Type, "Content-Type" );

            node = ( const KHttpHeader* ) BSTreeFind ( & self -> hdrs, & Content_Type, KHttpHeaderCmp );
            if ( node == NULL )
            {
                /* add content type for form parameters */
                /* TBD - before general application, need to perform URL-encoding! */
                rc = KHttpAddHeader ( & self -> hdrs, "Content-Type", "application/x-www-form-urlencoded" );
            }
        }
    }

    for ( i = 0; i < max_redirect; ++ i )
    {
        size_t len;
        char buffer [ 4096 ];

        /* create message */
        rc = KHttpRequestFormatMsg ( self, buffer, sizeof buffer, "POST", & len );
        if ( rc != 0 )
            break;

        /* send the message and create a response */
        rc = KHttpSendReceiveMsg ( self -> http, _rslt, buffer, len, & self -> body, self -> url_buffer . base );
        if ( rc != 0 )
            break;

        /* look at status code */
        rslt = * _rslt;
        switch ( rslt -> status )
        {
        case 100:
            /* Continue
               The client SHOULD continue with its request. This interim response is used
               to inform the client that the initial part of the request has been received
               and has not yet been rejected by the server. The client SHOULD continue by
               sending the remainder of the request or, if the request has already been completed,
               ignore this response. The server MUST send a final response after the request
               has been completed. See section 8.2.3 for detailed discussion of the use and
               handling of this status code. */

            /* TBD - should not see this, but needs to be handled */
            return 0;

            /* TBD - Add RFC rules about POST */
        case 301: /* "moved permanently" */
        case 307: /* "moved temporarily" */
            break;

        case 505: /* HTTP Version Not Supported */
            if ( self -> http -> vers > 0x01000000 )
            {
                /* downgrade version requested */
                self -> http -> vers -= 0x00010000;
                /* TBD - remove any HTTP/1.1 specific headers */
                continue;
            }

            /* NO BREAK */

        default:

            /* TBD - should all status codes be interpreted as rc ? */
            return 0;
        }

        /* reset connection, reset request */
        rc = KHttpRequestHandleRedirection ( self, rslt );
        if ( rc != 0 )
            break;
    }
    return rc;
}

/*******************************************************************************************
 * KHttpFile
 */


struct KHttpFile
{
    KFile dad;
    
    uint64_t file_size;

    KHttp *http;

    String url;
    KDataBuffer url_buffer;
};

static
rc_t KHttpFileDestroy ( KHttpFile *self )
{
    KHttpRelease ( self -> http );
    KDataBufferWhack ( & self -> url_buffer );
    free ( self );

    return 0;
}

static
struct KSysFile* KHttpFileGetSysFile ( const KHttpFile *self, uint64_t *offset )
{
    *offset = 0;
    return NULL;
}

static
rc_t KHttpFileRandomAccess ( const KHttpFile *self )
{
    return 0;
}

/* KHttpFile must have a file size to be created
   impossible for this funciton to fail */
static
rc_t KHttpFileSize ( const KHttpFile *self, uint64_t *size )
{
    *size = self -> file_size;
    return 0;
}

static
rc_t KHttpFileSetSize ( KHttpFile *self, uint64_t size )
{
    return RC ( rcNS, rcFile, rcUpdating, rcFile, rcReadonly );
}

static
rc_t KHttpFileRead ( const KHttpFile *cself, uint64_t pos,
     void *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc;
    KHttpFile *self = ( KHttpFile * ) cself;
    KHttp *http = self -> http;

    /* starting position was beyond EOF */
    if ( pos >= self -> file_size )
    {
        *num_read = 0;
        return 0;
    }
    /* starting position was within file but the range fell beyond EOF */
    else 
    {
        KHttpRequest *req;

        if ( pos + bsize > self -> file_size )
            bsize = self -> file_size - pos;
        
        rc = KHttpMakeRequest ( http, &req, self -> url_buffer . base );
        if ( rc == 0 )
        {
            rc = KHttpRequestByteRange ( req, pos, bsize );
            if ( rc == 0 )
            {
                KHttpResult *rslt;
                
                rc = KHttpRequestGET ( req, &rslt );
                if ( rc == 0 )
                {
                    uint32_t code;
                    
                    /* dont need to know what the response message was */
                    rc = KHttpResultStatus ( rslt, &code, NULL, 0, NULL );
                    if ( rc == 0 )
                    {
                        switch ( code )
                        {
                        case 206:
                        {
                            uint64_t start_pos;
                            size_t result_size;

                            rc = KHttpResultRange ( rslt, &start_pos, &result_size );
                            if ( rc == 0 && 
                                 start_pos == pos &&
                                 result_size == bsize )
                            {
                                KStream *response;
                                
                                rc = KHttpResultGetInputStream ( rslt, &response );
                                if ( rc == 0 )
                                {
                                    rc = KStreamReadAll ( response, buffer, bsize, num_read );
                                    if ( rc != 0 || num_read == 0 )
                                        return rc;
                                    
                                    KStreamRelease ( response );
                                }
                            }
                            break;
                        }
                        case 416:
                        default:
                            rc = RC ( rcNS, rcFile, rcReading, rcFileDesc, rcInvalid );
                        }
                    }
                    KHttpResultRelease ( rslt );
                }
            }
            KHttpRequestRelease ( req );
        }
    }

    return rc;
}

static
rc_t KHttpFileWrite ( KHttpFile *self, uint64_t pos, 
                      const void *buffer, size_t size, size_t *num_writ )
{
    return RC ( rcNS, rcFile, rcUpdating, rcInterface, rcUnsupported );
}

static KFile_vt_v1 vtKHttpFile = 
{
    1, 0,

    KHttpFileDestroy,
    KHttpFileGetSysFile,
    KHttpFileRandomAccess,
    KHttpFileSize,
    KHttpFileSetSize,
    KHttpFileRead,
    KHttpFileWrite
};

LIB_EXPORT rc_t CC KNSManagerVMakeHttpFile ( const KNSManager *self,
    const KFile **file, KStream *conn, ver_t vers, const char *url, va_list args )
{
    rc_t rc;

    if ( file == NULL )
        rc = RC ( rcNS, rcFile, rcConstructing, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC( rcNS, rcNoTarg, rcConstructing, rcParam, rcNull );
        else if ( url == NULL )
            rc = RC ( rcNS, rcFile, rcConstructing, rcPath, rcNull );
        else if ( url [ 0 ] == 0 )
            rc = RC ( rcNS, rcFile, rcConstructing, rcPath, rcInvalid );
        else
        {
            KHttpFile *f;

            f = calloc ( 1, sizeof *f );
            if ( f == NULL )
                rc = RC ( rcNS, rcFile, rcConstructing, rcMemory, rcExhausted );
            else
            {
                rc = KFileInit ( &f -> dad, ( const KFile_vt * ) &vtKHttpFile, "KHttpFile", url, true, false );
                if ( rc == 0 )
                {
                    KDataBuffer *buf = & f -> url_buffer;
                    buf -> elem_bits = 8;
                    rc = KDataBufferVPrintf ( buf, url, args );
                    if ( rc == 0 )
                    {
                        URLBlock block;
                        rc = ParseUrl ( &block, buf -> base, buf -> elem_count - 1 );
                        if ( rc == 0 )
                        {
                            KHttp *http;
                          
                            rc = KNSManagerMakeHttpInt ( self, &http, buf, conn, vers, &block . host, block . port );
                            if ( rc == 0 )
                            {
                                KHttpRequest *req;

                                rc = KHttpMakeRequestInt ( http, &req, &block, buf );
                                if ( rc == 0 )
                                {
                                    KHttpResult *rslt;
                                  
                                    rc = KHttpRequestHEAD ( req, &rslt );
                                    KHttpRequestRelease ( req );

                                    if ( rc == 0 )
                                    {
                                        uint64_t size;

                                        bool have_size = KHttpResultSize ( rslt, &size );
                                        KHttpResultRelease ( rslt );

                                        if ( ! have_size )
                                            rc = RC ( rcNS, rcFile, rcValidating, rcNoObj, rcError );
                                        else
                                        {
                                            f -> file_size = size;
                                            f -> http = http;

                                            * file = & f -> dad;

                                            return 0;
                                        }
                                    }
                                }

                                KHttpRelease ( http );
                            }
                        }
                    }
                    KDataBufferWhack ( buf );
                }
                free ( f );
            }
        }

        * file = NULL;
    }

    return rc;
}

LIB_EXPORT rc_t CC KNSManagerMakeHttpFile ( const KNSManager *self,
    const KFile **file, KStream *conn, ver_t vers, const char *url, ... )
{
    rc_t rc;

    va_list args;
    va_start ( args, url );
    rc = KNSManagerVMakeHttpFile ( self, file, conn, vers, url, args );
    va_end ( args );

    return rc;
}

