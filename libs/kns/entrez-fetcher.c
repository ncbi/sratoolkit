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

#include <klib/rc.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/refcount.h>
#include <kns/entrez-fetcher.h>
#include <kns/url-fetcher.h>
#include <sysalloc.h>

#include <stdlib.h>
#include <string.h>

#define URI_PARAM_LEN 70
#define ENTREZ_FIRST_LINE_LEN 120
#define ENTREZ_NEWLINE_DIV 80

struct KEntrezFetcher
{
    KRefcount refcount;

    KUrlFetcher * url_fetcher;
    char * uri;
};

static const char classname[] = "KSraFetcher";

rc_t KEntrezFetcherAddRef ( const KEntrezFetcher *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, classname ) )
        {
        case krefLimit:
            return RC ( rcApp, rcFunction, rcAttaching, rcRange, rcExcessive );
        }
    }
    return 0;
}


rc_t KEntrezFetcherRelease ( const KEntrezFetcher *cself )
{
    KEntrezFetcher *self = ( KEntrezFetcher* ) cself;
    if ( cself != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, classname ) )
        {
        case krefWhack:
            {
                KUrlFetcherRelease ( self -> url_fetcher );
                if ( self -> uri ) free( self -> uri );
                free( self );
                return 0;
            }
        case krefLimit:
            return RC ( rcApp, rcFunction, rcReleasing, rcRange, rcExcessive );
        }
    }
    return 0;
}


rc_t KEntrezFetcherSetupUri ( KEntrezFetcher *self, const char * uri )
{
    if ( uri == NULL || uri[0] == 0 )
        return RC ( rcApp, rcFunction, rcAccessing, rcParam, rcNull );
    if ( self == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcSelf, rcNull );

    if ( self -> uri ) free( self -> uri );
    self -> uri = string_dup_measure ( uri, NULL );
    return 0;
}


/* composes the internal uri from the parameters and returns an estimated buffersize */
rc_t KEntrezFetcherSetup ( KEntrezFetcher *self,
    const char * server, const char * seq_id, 
    const size_t max_seq_len, const uint64_t row_id, const size_t row_count,
    size_t * buffsize )
{
    rc_t rc;
    size_t uri_len;

    if ( server == NULL || seq_id == NULL || buffsize == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcParam, rcNull );
    if ( self == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcSelf, rcNull );

    if ( self -> uri ) free( self -> uri );
    uri_len = string_size( server ) + string_size( seq_id ) + URI_PARAM_LEN;
    self -> uri = malloc( uri_len );
    if ( self -> uri != NULL )
    {
        size_t num_written;
        uint64_t seq_start, seq_stop;
        
        seq_start = max_seq_len * ( row_id - 1 ) + 1;
        seq_stop = max_seq_len * ( row_id + row_count - 1 );
        
        if ( seq_stop <= seq_start )
            return RC ( rcApp, rcFunction, rcAccessing, rcSelf, rcNull );
            
        *buffsize = ( seq_stop - seq_start );
        *buffsize += ( *buffsize / ENTREZ_NEWLINE_DIV );
        *buffsize += ENTREZ_FIRST_LINE_LEN;

        rc = string_printf ( self->uri, uri_len, &num_written, 
            "%s?db=nucleotide&id=%s&rettype=fasta&seq_start=%lu&seq_stop=%lu", 
            server, seq_id, seq_start, seq_stop );
    }
    else
        rc = RC( rcExe, rcString, rcAllocating, rcMemory, rcExhausted );
    
    return rc;
}


/* remove the first line, including it's line-feed */
static void remove_first_line( char * s, size_t * len )
{
    char * p = string_chr ( s, *len, '\n' );
    if ( p )
    {
        *len -= ( p - s ) + 1;
        memmove( s, p + 1, *len );
    }
}


/* filters out all char's c by using a filtered temp-buffer... */
static void filter_char( char * s, size_t * len, const char c )
{
    size_t src, dst;

    if ( s == NULL || len == NULL || *len == 0 )
        return;

    dst = 0;
    for ( src = 0; src < *len; ++src )
    {
        char ch = s[ src ];
        if ( ch != c )
        {
            if ( src != dst )
                s[ dst ] = ch;
            ++dst;
        }
    }
    *len = dst;
}


rc_t KEntrezFetcherRead ( KEntrezFetcher *self,
                          void *dst, size_t dst_size, size_t *num_read )
{
    rc_t rc;
    
    if ( dst == NULL || dst_size == 0 || num_read == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcParam, rcNull );
    if ( self == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcSelf, rcNull );
    if ( self -> url_fetcher == NULL || self -> uri == NULL )
        return RC ( rcApp, rcFunction, rcAccessing, rcParam, rcNull );

    rc = KUrlFetcherRead( self -> url_fetcher, self -> uri, dst, dst_size, num_read );
    if ( rc == 0 )
    {
        remove_first_line( dst, num_read );
        filter_char( dst, num_read, '\n' );
    }
    return rc;
}


rc_t KEntrezFetcherMake ( KEntrezFetcher **fetcher, KUrlFetcher * url_fetcher )
{
    rc_t rc;

    if ( fetcher == NULL || url_fetcher == NULL )
        rc = RC ( rcApp, rcFunction, rcConstructing, rcParam, rcNull );
    else
    {
        *fetcher = malloc( sizeof( *fetcher ) );
        if ( *fetcher != NULL )
        {
            (*fetcher) -> url_fetcher = url_fetcher;
            KUrlFetcherAddRef ( url_fetcher );
            (*fetcher) -> uri = NULL;
            rc = 0;
        }
        else
            rc = RC( rcApp, rcFunction, rcListing, rcParam, rcNull );
    }
    return rc;
}
