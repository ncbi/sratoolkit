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


#include <vfs/extern.h>
#include "resolver-priv.h"

#include <vfs/manager.h>
#include <vfs/path.h>
#include <kns/kns_mgr.h>
#include <kns/curl-file.h>
#include <kns/KCurlRequest.h>
#include <kfs/file.h>
#include <kfs/directory.h>
#include <kfg/repository.h>
#include <kfg/config.h>
#include <klib/text.h>
#include <klib/vector.h>
#include <klib/refcount.h>
#include <klib/namelist.h>
#include <klib/printf.h>
#include <klib/data-buffer.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <vfs/path-priv.h>
#include "path-priv.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* to turn of CGI name resolution for
   any refseq accessions */
#define NO_REFSEQ_CGI 1

/* to turn of CGI name resolution for
   legacy WGS packages used by refseq */
#define NO_LEGACY_WGS_REFSEQ_CGI NO_REFSEQ_CGI


/*--------------------------------------------------------------------------
 * String
 */
static
void CC string_whack ( void *obj, void *ignore )
{
    StringWhack ( ( String* ) obj );
}



/*--------------------------------------------------------------------------
 * VResolverAccToken
 *  breaks up an accession into parts
 *
 *  "acc" is entire accession as given
 *
 *  the remainder is divided like so:
 *
 *    [<prefix>_]<alpha><digits>[.<ext1>[.<ext2>]]
 *
 *  prefix is optional
 *  alpha can be zero length iff prefix is not zero length
 *  digits must be non-zero length
 *  ext1 and ext2 are optional
 */
typedef struct VResolverAccToken VResolverAccToken;
struct VResolverAccToken
{
    String acc;
    String prefix;
    String alpha;
    String digits;
    String ext1;
    String ext2;
};

/*--------------------------------------------------------------------------
 * VResolverAlg
 *  represents a set of zero or more volumes
 *  each of which is addressed using a particular expansion algorithm
 */
typedef enum
{
    appUnknown,
    appAny,
    appREFSEQ,
    appSRA,
    appWGS,
    appCount
} VResolverAppID;

typedef enum
{
    algCGI,
    algSRAFlat,
    algSRA1024,
    algSRA1000,
    algFUSE1000,
    algREFSEQ,
    algWGSFlat,
    algWGS,
    algFuseWGS,
    algSRA_NCBI,
    algSRA_EBI,

    /* leave as last value */
    algUnknown

} VResolverAlgID;

typedef enum
{
    cacheDisallow,
    cacheAllow
} VResolverCacheAllow;

typedef struct VResolverAlg VResolverAlg;
struct VResolverAlg
{
    /* volume paths - stored as String* */
    Vector vols;

    /* root path - borrowed reference */
    const String *root;

    /* download ticket - borrowed reference
       non-NULL means that the root is a
       resolver CGI. also, don't rely on
       presence of any volumes... */
    const String *ticket;

    /* app_id helps to filter out volumes by app */
    VResolverAppID app_id;

    /* how to expand an accession */
    VResolverAlgID alg_id;

    /* a property of the repository */
    bool protected;

    /* whether the volumes are cache-capable
       in particularl, enabled if cache forced */
    bool cache_capable;

    /* whether the volumes are cache-enabled */
    bool cache_enabled;

    /* whether the volume is disabled in config */
    bool disabled;
};


/* Whack
 */
static
void CC VResolverAlgWhack ( void *item, void *ignore )
{
    VResolverAlg *self = item;

    /* drop any volumes */
    VectorWhack ( & self -> vols, string_whack, NULL );

    /* everything else is a borrowed reference */

    free ( self );
}

/* Make
 */
static
rc_t VResolverAlgMake ( VResolverAlg **algp, const String *root,
     VResolverAppID app_id, VResolverAlgID alg_id, bool protected, bool disabled )
{
    VResolverAlg *alg = calloc ( 1, sizeof * alg );
    if ( alg == NULL )
        return RC ( rcVFS, rcMgr, rcConstructing, rcMemory, rcExhausted );
    VectorInit ( & alg -> vols, 0, 8 );
    alg -> root = root;
    alg -> app_id = app_id;
    alg -> alg_id = alg_id;
    alg -> protected = protected;
    alg -> disabled = disabled;

    * algp = alg;
    return 0;
}

/* MakeeLocalWGSRefseqURI
 *  create a special URI that tells KDB how to open this
 *  obscured table, hidden away within a KAR file
 */
static
rc_t VResolverAlgMakeLocalWGSRefseqURI ( const VResolverAlg *self,
    const String *vol, const String *exp, const String *acc, const VPath ** path )
{
    if ( self -> root == NULL )
        return VPathMakeFmt ( ( VPath** ) path, NCBI_FILE_SCHEME ":%S/%S#tbl/%S", vol, exp, acc );
    return VPathMakeFmt ( ( VPath** ) path, NCBI_FILE_SCHEME ":%S/%S/%S#tbl/%S", self -> root, vol, exp, acc );
}

/* MakeeRemoteWGSRefseqURI
 *  create a special URI that tells KDB how to open this
 *  obscured table, hidden away within a KAR file
 */
static
rc_t VResolverAlgMakeRemoteWGSRefseqURI ( const VResolverAlg *self,
    const char *url, const String *acc, const VPath ** path )
{
    return VPathMakeFmt ( ( VPath** ) path, "%s#tbl/%S", url, acc );
}

/* MakeRemotePath
 *  the path is known to exist in the remote file system
 *  turn it into a VPath
 */
static
rc_t VResolverAlgMakeRemotePath ( const VResolverAlg *self,
    const char *url, const VPath ** path )
{
    return VPathMakeFmt ( ( VPath** ) path, url );
}

/* MakeLocalPath
 *  the path is known to exist in the local file system
 *  turn it into a VPath
 */
static
rc_t VResolverAlgMakeLocalPath ( const VResolverAlg *self,
    const String *vol, const String *exp, const VPath ** path )
{
    if ( self -> root == NULL )
        return VPathMakeFmt ( ( VPath** ) path, "%S/%S", vol, exp );
    return VPathMakeFmt ( ( VPath** ) path, "%S/%S/%S", self -> root, vol, exp );
}

/* expand_accession
 *  expand accession according to algorithm
 */
static
rc_t expand_algorithm ( const VResolverAlg *self, const VResolverAccToken *tok,
    char *expanded, size_t bsize, size_t *size, bool legacy_wgs_refseq )
{
    rc_t rc;
    uint32_t num;

   switch ( self -> alg_id )
    {
    case algCGI:
        return RC ( rcVFS, rcResolver, rcResolving, rcType, rcIncorrect );
    case algSRAFlat:
        rc = string_printf ( expanded, bsize, size,
            "%S%S.sra", & tok -> alpha, & tok -> digits );
        break;
    case algSRA1024:
        num = ( uint32_t ) strtoul ( tok -> digits . addr, NULL, 10 );
        rc = string_printf ( expanded, bsize, size,
            "%S/%06u/%S%S.sra", & tok -> alpha, num >> 10, & tok -> alpha, & tok -> digits );
        break;
    case algSRA1000:
        num = ( uint32_t ) ( tok -> alpha . size + tok -> digits . size - 3 );
        rc = string_printf ( expanded, bsize, size,
            "%S/%.*S/%S%S.sra", & tok -> alpha, num, & tok -> acc, & tok -> alpha, & tok -> digits );
        break;
    case algFUSE1000:
        num = ( uint32_t ) ( tok -> alpha . size + tok -> digits . size - 3 );
        rc = string_printf ( expanded, bsize, size,
            "%S/%.*S/%S%S/%S%S.sra", & tok -> alpha, num, & tok -> acc, 
            & tok -> alpha, & tok -> digits, & tok -> alpha, & tok -> digits );
        break;
    case algREFSEQ:
        if ( ! legacy_wgs_refseq )
            rc = string_printf ( expanded, bsize, size, "%S", & tok -> acc );
        else
            rc = string_printf ( expanded, bsize, size, "%S%.2S", & tok -> alpha, & tok -> digits );
        break;
    case algWGSFlat:
        num = ( uint32_t ) ( tok -> alpha . size + 2 );
        if ( tok -> prefix . size != 0 )
            num += tok -> prefix . size + 1;
        rc = string_printf ( expanded, bsize, size,
            "%.*S", num, & tok -> acc );
        break;
    case algWGS:
        num = ( uint32_t ) ( tok -> alpha . size + 2 );
        if ( tok -> prefix . size != 0 )
            num += tok -> prefix . size + 1;
        rc = string_printf ( expanded, bsize, size,
            "WGS/%.2s/%.2s/%.*S", tok -> alpha . addr, tok -> alpha . addr + 2, num, & tok -> acc );
        break;
    case algFuseWGS:
        num = ( uint32_t ) ( tok -> alpha . size + 2 );
        if ( tok -> prefix . size != 0 )
            num += tok -> prefix . size + 1;
        rc = string_printf ( expanded, bsize, size,
            "%.2s/%.2s/%.*S", tok -> alpha . addr, tok -> alpha . addr + 2, num, & tok -> acc );
        break;
    case algSRA_NCBI:
        num = ( uint32_t ) strtoul ( tok -> digits . addr, NULL, 10 );
        rc = string_printf ( expanded, bsize, size,
            "%S/%06u/%S%S", & tok -> alpha, num >> 10, & tok -> alpha, & tok -> digits );
        break;
    case algSRA_EBI:
        num = ( uint32_t ) ( tok -> alpha . size + tok -> digits . size - 3 );
        rc = string_printf ( expanded, bsize, size,
            "%S/%.*S/%S%S", & tok -> alpha, num, & tok -> acc, & tok -> alpha, & tok -> digits );
        break;
    default:
        return RC ( rcVFS, rcResolver, rcResolving, rcType, rcUnrecognized );
    }

   return rc;
}

/* LocalResolve
 *  resolve an accession into a VPath or not found
 *
 *  1. expand accession according to algorithm
 *  2. search all volumes for accession
 *  3. return not found or new VPath
 */
static
rc_t VResolverAlgLocalResolve ( const VResolverAlg *self,
    const KDirectory *wd, const VResolverAccToken *tok,
    const VPath ** path, bool legacy_wgs_refseq, bool for_cache )
{
    KPathType kpt;
    uint32_t i, count;

    /* expanded accession */
    String exp;
    size_t size;
    char expanded [ 256 ];

    /* in some cases, "root" is NULL */
    const String *vol, *root = self -> root;

    /* expand the accession */
    rc_t rc = expand_algorithm ( self, tok, expanded, sizeof expanded, & size, legacy_wgs_refseq );

    /* should never have a problem here... */
    if ( rc != 0 )
        return rc;

    /* if this is to detect a cache file, append extension */
    if ( for_cache )
    {
        size += string_copy ( & expanded [ size ], sizeof expanded - size, ".cache", sizeof ".cache" - 1 );
        if ( size == sizeof expanded )
            return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
    }

    /* turn the expanded portion into a String
       we know that size is also length due to
       accession content rules */
    StringInit ( & exp, expanded, size, ( uint32_t ) size );

    /* remove the cache extension */
    if ( for_cache )
    {
        exp . len -= sizeof ".cache" - 1;
        exp . size -= sizeof ".cache" - 1;
    }

    /* now search all volumes */
    count = VectorLength ( & self -> vols );
    if ( root == NULL )
    {
        for ( i = 0; i < count; ++ i )
        {
            vol = VectorGet ( & self -> vols, i );
            kpt = KDirectoryPathType ( wd, "%.*s/%.*s"
                , ( int ) vol -> size, vol -> addr
                , ( int ) size, expanded );
            switch ( kpt & ~ kptAlias )
            {
            case kptFile:
            case kptDir:
                if ( legacy_wgs_refseq )
                    return VResolverAlgMakeLocalWGSRefseqURI ( self, vol, & exp, & tok -> acc, path );
                return VResolverAlgMakeLocalPath ( self, vol, & exp, path );
            default:
                break;
            }
        }
    }
    else
    {
        for ( i = 0; i < count; ++ i )
        {
            vol = VectorGet ( & self -> vols, i );
            kpt = KDirectoryPathType ( wd, "%.*s/%.*s/%.*s"
                , ( int ) root -> size, root -> addr
                , ( int ) vol -> size, vol -> addr
                , ( int ) size, expanded );
            switch ( kpt & ~ kptAlias )
            {
            case kptFile:
            case kptDir:
                if ( legacy_wgs_refseq )
                    return VResolverAlgMakeLocalWGSRefseqURI ( self, vol, & exp, & tok -> acc, path );
                return VResolverAlgMakeLocalPath ( self, vol, & exp, path );
            default:
                break;
            }
        }
    }
    
    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}


/* ParseResolverCGIResponse_1_0
 *  expect single row table, with this structure:
 *
 *  <accession>|<download-ticket>|<url>|<result-code>|<message>
 */
static
rc_t VResolverAlgParseResolverCGIResponse_1_0 ( const char *start, size_t size, const VPath ** path )
{
    uint32_t result_code;
    const char *url_start, *url_end;
    const char *ticket_start, *ticket_end;

    /* skip over accession */
    const char *end = start + size;
    const char *sep = string_chr ( start, size, '|' );
    if ( sep == NULL )
        return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );

    /* get the download-ticket */
    start = sep + 1;
    sep = string_chr ( start, end - start, '|' );
    if ( sep == NULL )
        return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );

    /* capture ticket */
    ticket_start = start;
    ticket_end = sep;

    /* get the url */
    start = sep + 1;
    sep = string_chr ( start, end - start, '|' );
    if ( sep == NULL )
        return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );

    /* capture url */
    url_start = start;
    url_end = sep;

    /* get the result code */
    start = sep + 1;
    sep = string_chr ( start, end - start, '|' );
    if ( sep == NULL )
        return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
    result_code = strtoul ( start, NULL, 10 );

    /* get the message */
    start = sep + 1;
    sep = string_chr ( start, end - start, '\n' );
    if ( sep == NULL )
    {
        sep = string_chr ( start, end - start, '\r' );
        if ( sep == NULL )
            sep = end;
    }
    else if ( sep > start && sep [ -1 ] == '\r' )
    {
        -- sep;
    }

    switch ( result_code )
    {
    case 200:
        /* detect protected response */
        if ( ticket_end > ticket_start )
        {
            return VPathMakeFmt ( ( VPath** ) path, "%.*s?tic=%.*s"
                , ( uint32_t ) ( url_end - url_start ), url_start
                , ( uint32_t ) ( ticket_end - ticket_start ), ticket_start
            );
        }

        /* normal public response */
        return VPathMakeFmt ( ( VPath** ) path, "%.*s", ( uint32_t ) ( url_end - url_start ), url_start );
    }

    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}


/* ParseResolverCGIResponse
 *  the response should be NUL terminated
 *  but should also be close to the size of result
 */
static
rc_t VResolverAlgParseResolverCGIResponse ( const KDataBuffer *result, const VPath ** path )
{
    /* the textual response */
    const char *start = ( const void* ) result -> base;
    size_t i, size = KDataBufferBytes ( result );

    /* peel back buffer to significant bytes */
    while ( size > 0 && start [ size - 1 ] == 0 )
        -- size;

    /* skip over blanks */
    for ( i = 0; i < size; ++ i )
    {
        if ( ! isspace ( start [ i ] ) )
            break;
    }

    /* at this point, we expect only version 1.0 */
    if ( string_cmp ( & start [ i ], size - i, "#1.0", sizeof "#1.0" - 1, sizeof "#1.0" - 1 ) == 0 )
    {
        do
        {
            /* accept version line */
            i += sizeof "#1.0" - 1;

            /* must be followed by eoln */
            if ( start [ i ] == '\r' && start [ i + 1 ] == '\n' )
                i += 2;
            else if ( start [ i ] == '\n' )
                i += 1;
            else
                break;

            /* parse 1.0 response table */
            return VResolverAlgParseResolverCGIResponse_1_0 ( & start [ i ], size - i, path );
        }
        while ( false );
    }

    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}


/* RemoteProtectedResolve
 *  use NCBI CGI to resolve accession into URL
 */
static
rc_t VResolverAlgRemoteProtectedResolve ( const VResolverAlg *self,
    const KNSManager *kns, const String *acc,
    const VPath ** path, bool legacy_wgs_refseq )
{
    KCurlRequest *req;
    rc_t rc = KNSManagerMakeRequest ( kns, & req, self -> root -> addr, false );
    if ( rc == 0 )
    {
        String name, val;

        /* build up POST information: */
        CONST_STRING ( & name, "version" );
        CONST_STRING (& val, "1.0" );
        rc = KCurlRequestAddSField ( req, & name, & val );
        if ( rc == 0 )
        {
            CONST_STRING ( & name, "acc" );
            rc = KCurlRequestAddSField ( req, & name, acc );
        }
        if ( rc == 0 && legacy_wgs_refseq )
        {
            CONST_STRING ( & name, "ctx" );
            CONST_STRING (& val, "refseq" );
            rc = KCurlRequestAddSField ( req, & name, & val );
        }
        if ( rc == 0 && self -> ticket != NULL )
        {
            CONST_STRING ( & name, "tic" );
            rc = KCurlRequestAddSField ( req, & name, self -> ticket );
        }

        /* execute post */
        if ( rc == 0 )
        {
            KDataBuffer result;
            memset ( & result, 0, sizeof result );
            rc = KCurlRequestPerform ( req, & result );
            if ( rc == 0 )
            {
                /* expect a table as a NUL-terminated string, but
                   having close to the number of bytes in results */
                rc = VResolverAlgParseResolverCGIResponse ( & result, path );
                KDataBufferWhack ( & result );
            }
        }

        KCurlRequestRelease ( req );
    }

    return rc == 0 ? 0 : RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}

/* RemoteResolve
 *  resolve an accession into a VPath or not found
 *
 *  1. expand accession according to algorithm
 *  2. search all volumes for accession
 *  3. return not found or new VPath
 */
static
rc_t VResolverAlgRemoteResolve ( const VResolverAlg *self,
    const KNSManager *kns, const VResolverAccToken *tok,
    const VPath ** path, const KFile ** opt_file_rtn,
    bool legacy_wgs_refseq )
{
    rc_t rc;
    uint32_t i, count;

    /* expanded accession */
    String exp;
    size_t size;
    char expanded [ 256 ];

    const String *root;

    /* check for download ticket */
    if ( self -> alg_id == algCGI
#if NO_LEGACY_WGS_REFSEQ_CGI
         && ! legacy_wgs_refseq
#endif
        )
    {
        return VResolverAlgRemoteProtectedResolve ( self, kns, & tok -> acc, path, legacy_wgs_refseq );
    }

    /* for remote, root can never be NULL */
    root = self -> root;

    /* expand the accession */
    rc = expand_algorithm ( self, tok, expanded, sizeof expanded, & size, legacy_wgs_refseq );

    /* should never have a problem here... */
    if ( rc != 0 )
        return rc;

    /* turn the expanded portion into a String
       we know that size is also length due to
       accession content rules */
    StringInit ( & exp, expanded, size, ( uint32_t ) size );

    /* now search all remote volumes */
    count = VectorLength ( & self -> vols );
    for ( i = 0; i < count; ++ i )
    {
        char url [ 8192 ];
        const String *vol = VectorGet ( & self -> vols, i );
        rc = string_printf ( url, sizeof url, NULL, "%S/%S/%S", root, vol, & exp );
        if ( rc == 0 )
        {
            const KFile *f;
            rc = KCurlFileMake ( & f, url, false );
            if ( rc == 0 )
            {
                if ( opt_file_rtn != NULL )
                    * opt_file_rtn = f;
                else
                    KFileRelease ( f );

                if ( legacy_wgs_refseq )
                    return VResolverAlgMakeRemoteWGSRefseqURI ( self, url, & tok -> acc, path );
                return VResolverAlgMakeRemotePath ( self, url, path );
            }
        }
    }
    
    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}


/* CacheResolve
 *  try to resolve accession for currently cached file
 */
static
rc_t VResolverAlgCacheResolve ( const VResolverAlg *self,
    const KDirectory *wd, const VResolverAccToken *tok,
    const VPath ** path, bool legacy_wgs_refseq )
{
    /* see if the cache file already exists */
    const bool for_cache = true;
    rc_t rc = VResolverAlgLocalResolve ( self, wd, tok, path, legacy_wgs_refseq, for_cache );
    if ( rc == 0 )
        return 0;

    /* TBD - see if any of these volumes is a good candidate for creating a file */

    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}



/* MakeCachePath
 *  we have an accession that matches this volume
 *  create a path for it
 */
static
rc_t VResolverAlgMakeCachePath ( const VResolverAlg *self,
    const VResolverAccToken *tok, const VPath ** path, bool legacy_wgs_refseq )
{
    uint32_t i, count;

    /* expanded accession */
    String exp;
    size_t size;
    char expanded [ 256 ];

    const String *vol;

    /* expand the accession */
    rc_t rc = expand_algorithm ( self, tok, expanded, sizeof expanded, & size, legacy_wgs_refseq );

    /* should never have a problem here... */
    if ( rc != 0 )
        return rc;

    /* turn the expanded portion into a String
       we know that size is also length due to
       accession content rules */
    StringInit ( & exp, expanded, size, ( uint32_t ) size );

    /* now search all volumes */
    count = VectorLength ( & self -> vols );
    for ( i = 0; i < count; ++ i )
    {
        vol = VectorGet ( & self -> vols, i );
        return VResolverAlgMakeLocalPath ( self, vol, & exp, path );
    }
    
    return RC ( rcVFS, rcResolver, rcResolving, rcPath, rcNotFound );
}



/*--------------------------------------------------------------------------
 * VResolver
 */
struct VResolver
{
    /* root paths - stored as String* */
    Vector roots;

    /* volume algorithms - stored as VResolverAlg* */
    Vector local;
    Vector remote;

    /* working directory for testing local paths */
    const KDirectory *wd;

    /* if there is a protected remote resolver,
       we will need a KNS manager */
    const KNSManager *kns;

    /* if there is a working protected repository,
       store the download ticket here */
    const String *ticket;

    KRefcount refcount;

    /* counters for various app volumes */
    uint32_t num_app_vols [ appCount ];
};


/* "process" global settings
 *  actually, they are library-closure global
 */
static atomic32_t enable_local, enable_remote, enable_cache;


/* Whack
 */
static
rc_t VResolverWhack ( VResolver *self )
{
    KRefcountWhack ( & self -> refcount, "VResolver" );

    /* drop all remote volume sets */
    VectorWhack ( & self -> remote, VResolverAlgWhack, NULL );

    /* drop local volume sets */
    VectorWhack ( & self -> local, VResolverAlgWhack, NULL );

    /* drop download ticket */
    if ( self -> ticket != NULL )
        StringWhack ( ( String* ) self -> ticket );

    /* drop root paths */
    VectorWhack ( & self -> roots, string_whack, NULL );

    /* release kns */
    if ( self -> kns != NULL )
        KNSManagerRelease ( self -> kns );

    /* release directory onto local file system */
    KDirectoryRelease ( self -> wd );

    free ( self );
    return 0;
}


/* AddRef
 * Release
 */
LIB_EXPORT
rc_t CC VResolverAddRef ( const VResolver * self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, "VResolver" ) )
        {
        case krefOkay:
            break;
        case krefZero:
            return RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcIncorrect );
        case krefLimit:
            return RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcExhausted );
        case krefNegative:
            return RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcInvalid );
        default:
            return RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcUnknown );
        }
    }
    return 0;
}

LIB_EXPORT
rc_t CC VResolverRelease ( const VResolver * self )
{
    rc_t rc = 0;
    if ( self != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, "VResolver" ) )
        {
        case krefOkay:
        case krefZero:
            break;
        case krefWhack:
            VResolverWhack ( ( VResolver* ) self );
            break;
        case krefNegative:
            return RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcInvalid );
        default:
            rc = RC ( rcVFS, rcResolver, rcAttaching, rcRefcount, rcUnknown );
            break;            
        }
    }
    return rc;
}


/* get_accession_code
 */
static
uint32_t get_accession_code ( const String * accession, VResolverAccToken *tok )
{
#if USE_VPATH_OPTIONS_STRINGS
#error this thing is wrong
#else
    uint32_t code;

    const char *acc = accession -> addr;
    size_t i, size = accession -> size;

    /* capture the whole accession */
    tok -> acc = * accession;

    /* scan prefix or alpha */
    for ( i = 0; i < size; ++ i )
    {
        if ( ! isalpha ( acc [ i ] ) )
            break;
    }

    /* terrible situation - unrecognizable */
    if ( i == size || i == 0 || i >= 16 )
    {
        StringInit ( & tok -> prefix, acc, 0, 0 );
        StringInit ( & tok -> alpha, acc, i, i );
        StringInit ( & tok -> digits, & acc [ i ], 0, 0 );
        tok -> ext1 = tok -> ext2 = tok -> digits;
        return 0;
    }

    /* if stopped on '_', we have a prefix */
    if ( acc [ i ] == '_' )
    {
        /* prefix
           store only its presence, not length */
        code = 1 << 4 * 4;
        StringInit ( & tok -> prefix, acc, i, i );

        /* remove prefix */
        acc += ++ i;
        size -= i;

        /* scan for alpha */
        for ( i = 0; i < size; ++ i )
        {
            if ( ! isalpha ( acc [ i ] ) )
                break;
        }

        if ( i == size || i >= 16 )
        {
            StringInit ( & tok -> alpha, acc, i, i );
            StringInit ( & tok -> digits, & acc [ i ], 0, 0 );
            tok -> ext1 = tok -> ext2 = tok -> digits;
            return 0;
        }

        code |= i << 4 * 3;
        StringInit ( & tok -> alpha, acc, i, i );
    }
    else if ( ! isdigit ( acc [ i ] ) )
    {
        StringInit ( & tok -> prefix, acc, 0, 0 );
        StringInit ( & tok -> alpha, acc, i, i );
        StringInit ( & tok -> digits, & acc [ i ], 0, 0 );
        tok -> ext1 = tok -> ext2 = tok -> digits;
        return 0;
    }
    else
    {
        /* alpha */
        code = i << 4 * 3;
        StringInit ( & tok -> prefix, acc, 0, 0 );
        StringInit ( & tok -> alpha, acc, i, i );
    }

    /* remove alpha */
    acc += i;
    size -= i;

    /* scan digits */
    for ( i = 0; i < size; ++ i )
    {
        if ( ! isdigit ( acc [ i ] ) )
            break;
    }

    /* record digits */
    StringInit ( & tok -> digits, acc, i, i );
    StringInit ( & tok -> ext1, & acc [ i ], 0, 0 );
    tok -> ext2 = tok -> ext1;

    if ( i == 0 || i >= 16 )
        return 0;

    code |= i << 4 * 2;

    /* normal return point for SRA */
    if ( i == size )
        return code;

    /* check for extension */
    if ( acc [ i ] != '.' )
        return 0;

    /* remove digit */
    acc += ++ i;
    size -= i;

    /* scan numeric extension */
    for ( i = 0; i < size; ++ i )
    {
        if ( ! isdigit ( acc [ i ] ) )
            break;
    }

    if ( i == 0 || i >= 16 )
        return 0;

    /* record the actual extension */
    StringInit ( & tok -> ext1, acc, i, i );
    /* codify the extension simply as present, not by its length */
    code |= 1 << 4 * 1;

    if ( i == size )
        return code;

    if ( acc [ i ] != '.' )
        return 0;


    /* remove first extension */
    acc += ++ i;
    size -= i;

    /* scan 2nd numeric extension */
    for ( i = 0; i < size; ++ i )
    {
        if ( ! isdigit ( acc [ i ] ) )
            break;
    }

    if ( i == 0 || i >= 16 )
        return 0;

    StringInit ( & tok -> ext2, acc, i, i );
    code |= 1 << 4 * 0;

    if ( i == size )
        return code;

    /* no other patterns are accepted */
    return 0;
#endif
}


/* get_accession_app
 */
static
VResolverAppID get_accession_app ( const String * accession, bool refseq_ctx,
    VResolverAccToken *tok, bool *legacy_wgs_refseq )
{
    VResolverAppID app;
    uint32_t code = get_accession_code ( accession, tok );

    /* disregard extensions at this point */
    switch ( code >> 4 * 2 )
    {
    case 0x015: /* e.g. "J01415" or "J01415.2"     */
    case 0x026: /* e.g. "CM000071" or "CM000039.2" */
    case 0x126: /* e.g. "NZ_DS995509.1"            */
        app = appREFSEQ;
        break;

    case 0x036: /* e.g. "SRR012345"    */
    case 0x037: /* e.g. "SRR0123456"   */
    case 0x038: /* e.g. "SRR01234567"  */
    case 0x039: /* e.g. "SRR012345678" */
        app = appSRA;
        break;

    case 0x106: /* e.g. "NC_000012.10"                      */
    case 0x109: /* e.g. "NW_003315935.1", "GPC_000000393.1" */
        app = appREFSEQ;
        break;

    case 0x042: /* e.g. "AAAB01" is WGS package name */
    case 0x048: /* e.g. "AAAA01000001"               */
    case 0x049: /* contig can be 6 or 7 digits       */
    case 0x142: /* e.g. "NZ_AAEW01"                  */
    case 0x148: /* e.g. "NZ_AAEW01000001"            */
    case 0x149:
        app = appWGS;
        break;

    default:
        /* TBD - people appear to be able to throw anything into refseq,
           so anything unrecognized we may as well test out there...
           but this should not stay the case */
        app = appREFSEQ;
    }

    if ( app == appWGS )
    {
        /* if we know this is for refseq, clobber it here */
        if ( refseq_ctx )
        {
            app = appREFSEQ;
            * legacy_wgs_refseq = true;
        }
    }

    return app;
}


/* LocalResolve
 *  resolve an accession into a VPath or not found
 *
 *  1. determine the type of accession we have, i.e. its "app"
 *  2. search all local algorithms of app type for accession
 *  3. return not found or new VPath
 */
static
rc_t VResolverLocalResolve ( const VResolver *self,
    const String * accession, const VPath ** path, bool refseq_ctx )
{
    rc_t rc;
    uint32_t i, count;

    VResolverAccToken tok;
    bool legacy_wgs_refseq = false;
    VResolverAppID app = get_accession_app ( accession, refseq_ctx, & tok, & legacy_wgs_refseq );

    /* search all local volumes by app and accession algorithm expansion */
    count = VectorLength ( & self -> local );
    for ( i = 0; i < count; ++ i )
    {
        const VResolverAlg *alg = VectorGet ( & self -> local, i );
        if ( alg -> app_id == app )
        {
            const bool for_cache = false;
            rc = VResolverAlgLocalResolve ( alg, self -> wd, & tok, path, legacy_wgs_refseq, for_cache );
            if ( rc == 0 )
                return 0;
        }
    }

    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}

static
bool VPathHasRefseqContext ( const VPath * accession )
{
    size_t num_read;
    char option [ 64 ];
    rc_t rc = VPathOption ( accession, vpopt_vdb_ctx, option, sizeof option, & num_read );
    if ( rc != 0 )
        return false;
    return ( num_read == sizeof "refseq" - 1 &&
             strcase_cmp ( "refseq", sizeof "refseq" - 1,
                           option, num_read, num_read ) == 0 );
}


/* Local
 *  Find an existing local file/directory that is named by the accession.
 *  rcState of rcNotFound means it does not exist.
 *
 *  other rc code for failure are possible.
 *
 *  Accession must be an ncbi-acc scheme or a simple name with no 
 *  directory paths.
 */
LIB_EXPORT
rc_t CC VResolverLocal ( const VResolver * self,
    const VPath * accession, const VPath ** path )
{
    rc_t rc;

    if ( path == NULL )
        rc = RC ( rcVFS, rcResolver, rcResolving, rcParam, rcNull );
    else
    {
        * path = NULL;

        if ( self == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcSelf, rcNull );
        else if ( accession == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcPath, rcNull );
        else
        {
            bool refseq_ctx = false;
            VPUri_t uri_type = VPathGetUri_t ( accession );
            switch ( uri_type )
            {
            case vpuri_none:
                /* this is a simple spec -
                   check for not being a POSIX path */
#if USE_VPATH_OPTIONS_STRINGS
#error this is wrong
#else
                if ( string_chr ( accession -> path . addr, accession -> path . size, '/' ) == NULL )
                    return VResolverLocalResolve ( self, & accession -> path, path, refseq_ctx );
#endif

                /* no break */
            case vpuri_not_supported:
            case vpuri_ncbi_vfs:
#if SUPPORT_FILE_URL
            case vpuri_file:
#endif
            case vpuri_http:
            case vpuri_ftp:
            case vpuri_ncbi_legrefseq:
                rc = RC ( rcVFS, rcResolver, rcResolving, rcUri, rcIncorrect );
                break;

            case vpuri_ncbi_acc:
            {
                refseq_ctx = VPathHasRefseqContext ( accession );
#if USE_VPATH_OPTIONS_STRINGS
#error this is wrong
#else
                return VResolverLocalResolve ( self, & accession -> path, path, refseq_ctx );
#endif
            }

            default:
                rc = RC ( rcVFS, rcResolver, rcResolving, rcUri, rcInvalid );
            }
        }
    }

    return rc;
}


/* LocalEnable
 *  modify settings for using local repositories,
 *  meaning site, user-public and user-protected.
 *
 *  "enable" [ IN ] - enable or disable local access,
 *  or follow settings in KConfig
 *
 *  returns the previous state of "remote-enabled" property
 *
 * NB - in VDB-2, the state is associated with library code
 *  shared libraries in separate closures will have separate
 *  state. this can only occur if dynamic ( manual ) loading of
 *  shared libraries is used, and will not occur with normal
 *  usage. in VDB-3 the state will be stored with the process,
 *  not the library.
 */
LIB_EXPORT VResolverEnableState CC VResolverLocalEnable ( const VResolver * self, VResolverEnableState enable )
{
    int32_t val, cur, prior;

    if ( self == NULL )
        return false;

    /* convert "VResolverEnableState" to 32-bit signed integer for atomic operation */
    val = ( int32_t ) enable;

    /* before performing atomic swap, get the current setting,
       and return right away if it is already set correctly */
    prior = atomic32_read ( & enable_local );
    if ( prior != val ) do
    {
        cur = prior;
        prior = atomic32_test_and_set ( & enable_local, val, prior );
    }
    while ( prior != cur );

    return prior;
}


/* RemoteEnable
 *  apply or remove a process-wide enabling of remote access
 *  regardless of configuration settings
 *
 *  "enable" [ IN ] - if "true", enable all remote access
 *  if false, use settings from configuration.
 *
 *  returns the previous state of "remote-enabled" property
 *
 * NB - in VDB-2, the state is associated with library code
 *  shared libraries in separate closures will have separate
 *  state. this can only occur if dynamic ( manual ) loading of
 *  shared libraries is used, and will not occur with normal
 *  usage. in VDB-3 the state will be stored with the process,
 *  not the library.
 */
LIB_EXPORT VResolverEnableState CC VResolverRemoteEnable ( const VResolver * self, VResolverEnableState enable )
{
    int32_t val, cur, prior;

    if ( self == NULL )
        return false;

    /* convert "VResolverEnableState" to 32-bit signed integer for atomic operation */
    val = ( int32_t ) enable;

    /* before performing atomic swap, get the current setting,
       and return right away if it is already set correctly */
    prior = atomic32_read ( & enable_remote );
    if ( prior != val ) do
    {
        cur = prior;
        prior = atomic32_test_and_set ( & enable_remote, val, prior );
    }
    while ( prior != cur );

    return prior;
}


/* CacheEnable
 *  modify settings for caching files in user repositories
 *
 *  "enable" [ IN ] - enable or disable user repository cache,
 *  or follow settings in KConfig
 *
 *  returns the previous state of "cache-enabled" property
 *
 * NB - in VDB-2, the state is associated with library code
 *  shared libraries in separate closures will have separate
 *  state. this can only occur if dynamic ( manual ) loading of
 *  shared libraries is used, and will not occur with normal
 *  usage. in VDB-3 the state will be stored with the process,
 *  not the library.
 */
LIB_EXPORT VResolverEnableState CC VResolverCacheEnable ( const VResolver * self, VResolverEnableState enable )
{
    int32_t val, cur, prior;

    if ( self == NULL )
        return false;

    /* convert "VResolverEnableState" to 32-bit signed integer for atomic operation */
    val = ( int32_t ) enable;

    /* before performing atomic swap, get the current setting,
       and return right away if it is already set correctly */
    prior = atomic32_read ( & enable_cache );
    if ( prior != val ) do
    {
        cur = prior;
        prior = atomic32_test_and_set ( & enable_cache, val, prior );
    }
    while ( prior != cur );

    return prior;
}


/* RemoteResolve
 *  resolve an accession into a remote VPath or not found
 *  may optionally open a KFile to the object in the process
 *  of finding it
 *
 *  2. determine the type of accession we have, i.e. its "app"
 *  3. search all local algorithms of app type for accession
 *  4. return not found or new VPath
 */
static
rc_t VResolverRemoteResolve ( const VResolver *self,
    const String * accession, const VPath ** path,
    const KFile ** opt_file_rtn, bool refseq_ctx )
{
    rc_t rc;
    uint32_t i, count;

    VResolverAccToken tok;
    VResolverAppID app, wildCard;
    bool legacy_wgs_refseq = false;

    /* here, determine whether remote access is globally disabled */
    VResolverEnableState remote_state = atomic32_read ( & enable_remote );
    if ( remote_state == vrAlwaysDisable )
        return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );

    /* subject the accession to pattern recognition */
    app = get_accession_app ( accession, refseq_ctx, & tok, & legacy_wgs_refseq );

    /* search all remote volumes by app and accession algorithm expansion */
    count = VectorLength ( & self -> remote );

    /* allow matching wildcard app */
    wildCard = appAny;
#if NO_REFSEQ_CGI
    if ( app == appREFSEQ )
        wildCard = -1;
#endif

    /* test for forced enable, which applies only to main guys
       TBD - limit to main sub-category */
    if ( remote_state == vrAlwaysEnable )
    {
        for ( i = 0; i < count; ++ i )
        {
            const VResolverAlg *alg = VectorGet ( & self -> remote, i );
            if ( alg -> app_id == app || alg -> app_id == wildCard )
            {
                rc = VResolverAlgRemoteResolve ( alg, self -> kns, & tok, path, opt_file_rtn, legacy_wgs_refseq );
                if ( rc == 0 )
                    return 0;
            }
        }
    }
    else
    {
        for ( i = 0; i < count; ++ i )
        {
            const VResolverAlg *alg = VectorGet ( & self -> remote, i );
            if ( ( alg -> app_id == app || alg -> app_id == wildCard ) && ! alg -> disabled )
            {
                rc = VResolverAlgRemoteResolve ( alg, self -> kns, & tok, path, opt_file_rtn, legacy_wgs_refseq );
                if ( rc == 0 )
                    return 0;
            }
        }
    }

    return RC ( rcVFS, rcResolver, rcResolving, rcName, rcNotFound );
}


/* Remote
 *  Find an existing remote file that is named by the accession.
 *
 *  rcState of rcNotFound means it did not exist and can not be 
 *  downloaded. Probably a bad accession name.
 *
 *  Need a specific rc for no network configured.
 *  Need a specific rc for network access permitted.
 *
 *  Other rc code for failure are possible.
 *
 *  Accession must be an ncbi-acc scheme or a simple name with no 
 *  directory paths.
 *
 *  "opt_file_rtn" [ OUT, NULL OKAY ] - optional return parameter for
 *   any KFile that may be opened as a result of resolution. This can
 *   happen if resolving an accession involves opening a URL to a
 *   remote server, for example, in which case the KFile can be returned.
 */
LIB_EXPORT
rc_t CC VResolverRemote ( const VResolver * self,
    const VPath * accession, const VPath ** path,
    const KFile ** opt_file_rtn )
{
    rc_t rc;

    if ( path == NULL )
        rc = RC ( rcVFS, rcResolver, rcResolving, rcParam, rcNull );
    else
    {
        * path = NULL;

        if ( self == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcSelf, rcNull );
        else if ( accession == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcPath, rcNull );
        else
        {
            bool refseq_ctx = false;
            VPUri_t uri_type = VPathGetUri_t ( accession );
            switch ( uri_type )
            {
            case vpuri_none:
                /* this is a simple spec -
                   check for not being a POSIX path */
#if USE_VPATH_OPTIONS_STRINGS
#error this is wrong
#else
                if ( string_chr ( accession -> path . addr, accession -> path . size, '/' ) == NULL )
                    return VResolverRemoteResolve ( self, & accession -> path, path, opt_file_rtn, refseq_ctx );
#endif

                /* no break */
            case vpuri_not_supported:
            case vpuri_ncbi_vfs:
#if SUPPORT_FILE_URL
            case vpuri_file:
#endif
            case vpuri_http:
            case vpuri_ftp:
            case vpuri_ncbi_legrefseq:
                rc = RC ( rcVFS, rcResolver, rcResolving, rcUri, rcIncorrect );
                break;

            case vpuri_ncbi_acc:
            {
                refseq_ctx = VPathHasRefseqContext ( accession );
#if USE_VPATH_OPTIONS_STRINGS
#error this is wrong
#else
                return VResolverRemoteResolve ( self, & accession -> path, path, opt_file_rtn, refseq_ctx );
#endif
            }

            default:
                rc = RC ( rcVFS, rcResolver, rcResolving, rcUri, rcInvalid );
            }
        }
    }

    return rc;
}


/* ExtractURLAccessionApp
 *  examine a URL for accession portion,
 *  and try to recognize what app it belongs to
 */
static
VResolverAppID VResolverExtractURLAccessionApp ( const VResolver *self, const VPath * url,
    String * accession, VResolverAccToken * tok, bool *legacy_wgs_refseq )
{
    const char *p;
    bool refseq_ctx = false;

    /* have the difficulty here of determining whether there is
       a table fragment, which would indicate that the download
       is appREFSEQ even if the accession indicates appWGS. */

#if USE_VPATH_OPTIONS_STRINGS
#error this is wrong
#else
    * accession = url -> path;
    if ( url -> fragment != NULL && url -> fragment [ 0 ] != 0 )
        refseq_ctx = true;
#endif
    p = string_rchr ( accession -> addr, accession -> size, '/' );
    if ( p != NULL )
    {
        accession -> size -= ++ p - accession -> addr;
        accession -> addr = p;
        accession -> len = string_len ( p, accession -> size );
    }
    p = string_rchr ( accession -> addr, accession -> size, '.' );
    if ( p != NULL )
    {
        if ( strcase_cmp ( p, accession -> size - ( p - accession -> addr ),
                           ".sra", sizeof ".sra" - 1, -1 ) == 0 )
        {
            accession -> size -= sizeof ".sra" - 1;
            accession -> len -= sizeof ".sra" - 1;
        }
        else if ( strcase_cmp ( p, accession -> size - ( p - accession -> addr ),
                                ".wgs", sizeof ".wgs" - 1, -1 ) == 0 )
        {
            accession -> size -= sizeof ".wgs" - 1;
            accession -> len -= sizeof ".wgs" - 1;
        }
    }

    /* should have something looking like an accession.
       determine its app to see if we were successful */
    return get_accession_app ( accession, refseq_ctx, tok, legacy_wgs_refseq );
}

static
bool VPathHasDownloadTicket ( const VPath * url )
{
    size_t num_read;
    char option [ 64 ];
    rc_t rc = VPathOption ( url, vpopt_gap_ticket, option, sizeof option, & num_read );
    return rc == 0;
}


/* Cache
 *  Find a cache directory that might or might not contain a partially
 *  downloaded file.
 *
 *  Accession must be an ncbi-acc scheme, an http url or a simple name with no 
 *  directory paths. All three should return the same directory URL as a VPath. (?)
 *  Or should it be a directory or a file url depending upon finding a partial
 *  download? This would require co-ordination with all download mechanisms that
 *  we permit.
 *
 *  With refseq holding wgs objects we have a case were the downloaded file is not
 *  named the same as the original accession as the file archive you want is a
 *  container for other files.
 *
 *  Find local will give a path that has a special scheme in these cases. 
 *  Find remote will give the url for the container that contains the accession
 *  so using the returned VPath from resolve remote is better than the original
 *  accession in this one case.  I think...
 */
LIB_EXPORT
rc_t CC VResolverCache ( const VResolver * self,
    const VPath * url, const VPath ** path, uint64_t file_size )
{
    rc_t rc;

    VResolverEnableState cache_state = atomic32_read ( & enable_cache );

    if ( path == NULL )
        rc = RC ( rcVFS, rcResolver, rcResolving, rcParam, rcNull );
    else
    {
        * path = NULL;

        if ( self == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcSelf, rcNull );
        else if ( url == NULL )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcParam, rcNull );
        else if ( cache_state == vrAlwaysDisable )
            rc = RC ( rcVFS, rcResolver, rcResolving, rcPath, rcNotFound );
        else
        {
            VPUri_t uri_type = VPathGetUri_t ( url );
            switch ( uri_type )
            {
            case vpuri_http:
            {
                String accession;
                VResolverAccToken tok;
                bool legacy_wgs_refseq = false;
                VResolverAppID app = VResolverExtractURLAccessionApp ( self,
                    url, & accession, & tok, & legacy_wgs_refseq );

                /* going to walk the local volumes, and remember
                   which one was best. actually, we have no algorithm
                   for determining it, so it's just the comment for TBD */
                const VResolverAlg *alg, *best = NULL;

                /* search the volumes for a cache-enabled place */
                uint32_t i, count = VectorLength ( & self -> local );

                /* check for protected status by presence of a download ticket */
                bool protected = VPathHasDownloadTicket ( url );

                /* check for cache-enable override */
                if ( cache_state == vrAlwaysEnable )
                {
                    for ( i = 0; i < count; ++ i )
                    {
                        alg = VectorGet ( & self -> local, i );
                        if ( alg -> cache_capable && alg -> protected == protected &&
                             ( alg -> app_id == app || alg -> app_id == appAny ) )
                        {
                            /* try to find an existing cache file
                               NB - race condition exists unless
                               we do something with lock files */
                            rc = VResolverAlgCacheResolve ( alg, self -> wd, & tok, path, legacy_wgs_refseq );
                            if ( rc == 0 )
                                return 0;

                            /* just remember the first as best for now */
                            if ( best == NULL )
                                best = alg;
                        }
                    }
                }
                else
                {
                    for ( i = 0; i < count; ++ i )
                    {
                        alg = VectorGet ( & self -> local, i );
                        if ( alg -> cache_enabled && alg -> protected == protected &&
                             ( alg -> app_id == app || alg -> app_id == appAny ) )
                        {
                            /* try to find an existing cache file
                               NB - race condition exists unless
                               we do something with lock files */
                            rc = VResolverAlgCacheResolve ( alg, self -> wd, & tok, path, legacy_wgs_refseq );
                            if ( rc == 0 )
                                return 0;

                            /* just remember the first as best for now */
                            if ( best == NULL )
                                best = alg;
                        }
                    }
                }

                /* no existing cache file was found,
                   so create a new one using the best
                   TBD - this should remember a volume path */
                if ( best == NULL )
                    rc = RC ( rcVFS, rcResolver, rcResolving, rcPath, rcNotFound );
                else
                    rc = VResolverAlgMakeCachePath ( best, & tok, path, legacy_wgs_refseq );
                break;
            }
            default:
                rc = RC ( rcVFS, rcResolver, rcResolving, rcUri, rcInvalid );
            }
        }
    }

    return rc;
}


/* LoadVolume
 *  capture volume path and other information
 */
static
rc_t VResolverAlgLoadVolume ( VResolverAlg *self, uint32_t *num_vols, const char *start, size_t size )
{
    rc_t rc = 0;

#if 0
    /* trim volume whitespace */
    while ( size != 0 && isspace ( start [ 0 ] ) )
    {
        ++ start;
        -- size;
    }
    while ( size != 0 && isspace ( start [ size - 1 ] ) )
        -- size;
#endif

    /* trim trailing slashes */
    while ( size != 0 && start [ size - 1 ] == '/' )
        -- size;

    /* now see if the string survives */
    if ( size != 0 )
    {
        String loc_vol_str;
        const String *vol_str;
        StringInit ( & loc_vol_str, start, size, string_len ( start, size ) );
        rc = StringCopy ( & vol_str, & loc_vol_str );
        if ( rc == 0 )
        {
            rc = VectorAppend ( & self -> vols, NULL, vol_str );
            if ( rc == 0 )
            {
                * num_vols += 1;
                return 0;
            }

            StringWhack ( vol_str );
        }
    }

    return rc;
}

/* LoadVolumes
 *
 *    path-list
 *        = PATH
 *        | <path-list> ':' PATH ;
 */
static
rc_t VResolverAlgLoadVolumes ( VResolverAlg *self, uint32_t *num_vols, const String *vol_list )
{
    const char *start = vol_list -> addr;
    const char *end = & vol_list -> addr [ vol_list -> size ];
    const char *sep = string_chr ( start, end - start, ':' );
    while ( sep != NULL )
    {
        rc_t rc = VResolverAlgLoadVolume ( self, num_vols, start, sep - start );
        if ( rc != 0 )
            return rc;
        start = sep + 1;
        sep = string_chr ( start, end - start, ':' );
    }
    return VResolverAlgLoadVolume ( self, num_vols, start, end - start );
}

/* LoadAlgVolumes
 *
 *    volumes
 *        = <path-list> ;
 */
static
rc_t VResolverLoadAlgVolumes ( Vector *algs, const String *root, const String *ticket,
    VResolverCacheAllow allow_cache, VResolverAppID app_id, VResolverAlgID alg_id,
     uint32_t *num_vols, const String *vol_list, bool protected, bool disabled, bool caching )
{
    VResolverAlg *alg;
    rc_t rc = VResolverAlgMake ( & alg, root, app_id, alg_id, protected, disabled );
    if ( rc == 0 )
    {
        alg -> ticket = ticket;
        alg -> cache_capable = allow_cache == cacheAllow;
        alg -> cache_enabled = caching;

        if ( ticket != NULL )
            alg -> alg_id = algCGI;

        rc = VResolverAlgLoadVolumes ( alg, num_vols, vol_list );
        if ( rc == 0 && VectorLength ( & alg -> vols ) != 0 )
        {
            rc = VectorAppend ( algs, NULL, alg );
            if ( rc == 0 )
                return 0;
        }

        VResolverAlgWhack ( alg, NULL );
    }

    return rc;
}

/* LoadApp
 *
 *    alg-block
 *        = <alg-type> <volumes> ;
 *
 *    alg-type
 *        = "flat" | "sraFlat" | "sra1024" | "sra1000" | "fuse1000"
 *        | "refseq" | "wgs" | "fuseWGS" | "ncbi" | "ddbj" | "ebi" ;
 */
static
rc_t VResolverLoadVolumes ( Vector *algs, const String *root, const String *ticket,
    VResolverCacheAllow allow_cache, VResolverAppID app_id, uint32_t *num_vols,
    const KConfigNode *vols, bool resolver_cgi, bool protected, bool disabled, bool caching )
{
    KNamelist *algnames;
    rc_t rc = KConfigNodeListChildren ( vols, & algnames );
    if ( rc == 0 )
    {
        uint32_t i, count;
        rc = KNamelistCount ( algnames, & count );
        for ( i = 0; i < count && rc == 0; ++ i )
        {
            const char *algname;
            rc = KNamelistGet ( algnames, i, & algname );
            if ( rc == 0 )
            {
                const KConfigNode *alg;
                rc = KConfigNodeOpenNodeRead ( vols, & alg, algname );
                if ( rc == 0 )
                {
                    VResolverAlgID alg_id = algUnknown;

                    /* if using CGI for resolution */
                    if ( resolver_cgi || strcmp ( algname, "cgi" ) == 0 )
                        alg_id = algCGI;
                    /* stored in a flat directory with ".sra" extension */
                    else if ( strcmp ( algname, "sraFlat" ) == 0 )
                        alg_id = algSRAFlat;
                    /* stored in a three-level directory with 1K banks and ".sra" extension */
                    else if ( strcmp ( algname, "sra1024" ) == 0 )
                        alg_id = algSRA1024;
                    /* stored in a three-level directory with 1000 banks and ".sra" extension */
                    else if ( strcmp ( algname, "sra1000" ) == 0 )
                        alg_id = algSRA1000;
                    /* stored in a four-level directory with 1000 banks and ".sra" extension */
                    else if ( strcmp ( algname, "fuse1000" ) == 0 )
                        alg_id = algFUSE1000;
                    /* stored in a flat directory with no extension */
                    else if ( strcmp ( algname, "refseq" ) == 0 )
                        alg_id = algREFSEQ;
                    /* stored in a flat directory with no extension */
                    else if ( strcmp ( algname, "wgsFlat" ) == 0 )
                        alg_id = algWGSFlat;
                    /* stored in a multi-level directory with no extension */
                    else if ( strcmp ( algname, "wgs" ) == 0 )
                        alg_id = algWGS;
                    else if ( strcmp ( algname, "fuseWGS" ) == 0 )
                        alg_id = algFuseWGS;
                    /* stored in a three-level directory with 1K banks and no extension */
                    else if ( strcmp ( algname, "ncbi" ) == 0 ||
                              strcmp ( algname, "ddbj" ) == 0 )
                        alg_id = algSRA_NCBI;
                    /* stored in a three-level directory with 1000 banks and no extension */
                    else if ( strcmp ( algname, "ebi" ) == 0 )
                        alg_id = algSRA_EBI;

                    if ( alg_id != algUnknown )
                    {
                        String *vol_list;
                        rc = KConfigNodeReadString ( alg, & vol_list );
                        if ( rc == 0 )
                        {
                            if ( StringLength ( vol_list ) != 0 )
                            {
                                rc = VResolverLoadAlgVolumes ( algs, root, ticket, allow_cache,
                                    app_id, alg_id, num_vols, vol_list, protected, disabled, caching );
                            }
                            StringWhack ( vol_list );
                        }
                    }

                    KConfigNodeRelease ( alg );
                }
            }
        }

        KNamelistRelease ( algnames );
    }
    return rc;
}

/* LoadApp
 *
 *    app
 *        = [ <disabled> ] [ <cache-enabled> ] <vol-group> ;
 *
 *    disabled
 *        = "disabled" '=' ( "true" | "false" ) ;
 *
 *    cache-enabled
 *        = "cache-enabled" '=' ( "true" | "false" ) ;
 *
 *    vol-group
 *        = "volumes" <alg-block>* ;
 */
static
rc_t VResolverLoadApp ( VResolver *self, Vector *algs, const String *root, const String *ticket,
    VResolverCacheAllow allow_cache, VResolverAppID app_id, uint32_t *num_vols,
    const KConfigNode *app, bool resolver_cgi, bool protected, bool disabled, bool caching )
{
    const KConfigNode *node;

    /* test for disabled app - it is entirely possible */
    rc_t rc = KConfigNodeOpenNodeRead ( app, & node, "disabled" );
    if ( rc == 0 )
    {
        bool app_disabled = false;
        rc = KConfigNodeReadBool ( node, & app_disabled );
        KConfigNodeRelease ( node );
        if ( rc == 0 && app_disabled && algs == & self -> local )
            return 0;
        disabled |= app_disabled;
    }

    /* test again for cache enabled */
    if ( allow_cache == cacheAllow )
    {
        rc = KConfigNodeOpenNodeRead ( app, & node, "cache-enabled" );
        if ( rc == 0 )
        {
            /* allow this node to override current value */
            bool cache;
            rc = KConfigNodeReadBool ( node, & cache );
            KConfigNodeRelease ( node );
            if ( rc == 0 )
                caching = cache;
        }
    }

    /* get volumes */
    rc = KConfigNodeOpenNodeRead ( app, & node, "volumes" );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        rc = VResolverLoadVolumes ( algs, root, ticket, allow_cache,
            app_id, num_vols, node, resolver_cgi, protected, disabled, caching );
        KConfigNodeRelease ( node );
    }

    return rc;
}

/* LoadApps
 *
 *    app-block
 *        = <app-name> <app> ;
 *
 *    app-name
 *        = "refseq" | "sra" | "wgs" ;
 */
static
rc_t VResolverLoadApps ( VResolver *self, Vector *algs, const String *root,
    const String *ticket, VResolverCacheAllow allow_cache, const KConfigNode *apps,
    bool resolver_cgi, bool protected, bool disabled, bool caching )
{
    KNamelist *appnames;
    rc_t rc = KConfigNodeListChildren ( apps, & appnames );
    if ( rc == 0 )
    {
        uint32_t i, count;
        rc = KNamelistCount ( appnames, & count );
        if ( resolver_cgi && rc == 0 && count == 0 )
        {
            VResolverAlg *cgi;
            rc = VResolverAlgMake ( & cgi, root, appAny, algCGI, protected, disabled );
            if ( rc == 0 )
            {
                rc = VectorAppend ( algs, NULL, cgi );
                if ( rc == 0 )
                {
                    ++ self -> num_app_vols [ appAny ];
                    return 0;
                }
            }
        }
        else for ( i = 0; i < count && rc == 0; ++ i )
        {
            const char *appname;
            rc = KNamelistGet ( appnames, i, & appname );
            if ( rc == 0 )
            {
                const KConfigNode *app;
                rc = KConfigNodeOpenNodeRead ( apps, & app, appname );
                if ( rc == 0 )
                {
                    VResolverAppID app_id = appUnknown;
                    if ( strcmp ( appname, "refseq" ) == 0 )
                        app_id = appREFSEQ;
                    else if ( strcmp ( appname, "sra" ) == 0 )
                        app_id = appSRA;
                    else if ( strcmp ( appname, "wgs" ) == 0 )
                        app_id = appWGS;

                    rc = VResolverLoadApp ( self, algs, root, ticket, allow_cache, app_id,
                        & self -> num_app_vols [ app_id ], app, resolver_cgi, protected, disabled, caching );

                    KConfigNodeRelease ( app );
                }
            }
        }
        KNamelistRelease ( appnames );
    }
    return rc;
}

/* LoadRepo
 *
 *    repository
 *        = [ <disabled> ] [ <cache-enabled> ] <root> <app-group> ;
 *
 *    disabled
 *        = "disabled" '=' ( "true" | "false" ) ;
 *
 *    cache-enabled
 *        = "cache-enabled" '=' ( "true" | "false" ) ;
 *
 *    root
 *        = "root" '=' PATH ;
 *
 *    app-group
 *        = "apps" <app-block>* ;
 */
static
rc_t VResolverLoadRepo ( VResolver *self, Vector *algs, const KConfigNode *repo,
    const String *ticket, VResolverCacheAllow allow_cache, bool protected )
{
    const KConfigNode *node;
    bool caching, resolver_cgi;

    /* test for disabled repository */
    bool disabled = false;
    rc_t rc = KConfigNodeOpenNodeRead ( repo, & node, "disabled" );
    if ( rc == 0 )
    {
        rc = KConfigNodeReadBool ( node, & disabled );
        KConfigNodeRelease ( node );

        /* don't bother recording local, disabled repositories */
        if ( rc == 0 && disabled && algs == & self -> local )
            return 0;
    }

    /* check for caching */
    caching = allow_cache;
    if ( allow_cache )
    {
        rc = KConfigNodeOpenNodeRead ( repo, & node, "cache-enabled" );
        if ( rc == 0 )
        {
            rc = KConfigNodeReadBool ( node, & caching );
            KConfigNodeRelease ( node );
            if ( rc != 0 )
                caching = false;
        }
    }

    /* cache-capable repositories cannot be remote resolvers */
    resolver_cgi = false;
    if ( allow_cache )
        rc = KConfigNodeOpenNodeRead ( repo, & node, "root" );
    else
    {
        /* check for specific resolver CGI */
        rc = KConfigNodeOpenNodeRead ( repo, & node, "resolver-cgi" );
        if ( rc == 0 )
            resolver_cgi = true;
        /* or get the repository root */
        else if ( GetRCState ( rc ) == rcNotFound )
        {
            rc = KConfigNodeOpenNodeRead ( repo, & node, "root" );
        }
    }
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        /* read root as String */
        String *root;
        rc = KConfigNodeReadString ( node, & root );
        KConfigNodeRelease ( node );
        if ( GetRCState ( rc ) == rcNotFound )
            rc = 0;
        else if ( rc == 0 )
        {
            /* perform a bit of cleanup on root */
            while ( root -> size != 0 && root -> addr [ root -> size - 1 ] == '/' )
            {
                /* this is terribly nasty, but known to be safe */
                -- root -> len;
                -- root -> size;
            }

            /* store string on VResolver for management purposes,
               pass the loaned reference to sub-structures */
            rc = VectorAppend ( & self -> roots, NULL, root );
            if ( rc != 0 )
                StringWhack ( root );
            else
            {
                /* open the "apps" sub-node */
                rc = KConfigNodeOpenNodeRead ( repo, & node, "apps" );
                if ( rc == 0 )
                {
                    rc = VResolverLoadApps ( self, algs, root, ticket,
                        allow_cache, node, resolver_cgi, protected, disabled, caching );
                    KConfigNodeRelease ( node );
                }
                else if ( GetRCState ( rc ) == rcNotFound )
                {
                    rc = 0;
                    if ( resolver_cgi )
                    {
                        VResolverAlg *cgi;
                        rc = VResolverAlgMake ( & cgi, root, appAny, algCGI, protected, disabled );
                        if ( rc == 0 )
                        {
                            cgi -> ticket = ticket;

                            rc = VectorAppend ( algs, NULL, cgi );
                            if ( rc == 0 )
                            {
                                ++ self -> num_app_vols [ appAny ];
                                return 0;
                            }
                        }

                        VResolverAlgWhack ( cgi, NULL );
                    }
                }
            }
        }
    }

    return rc;
}


/* LoadNamedRepo
 *
 *    repository-block
 *        = ID <repository> ;
 */
static
rc_t VResolverLoadNamedRepo ( VResolver *self, Vector *algs, const KConfigNode *sub,
    const String *ticket, const char *name, VResolverCacheAllow allow_cache, bool protected )
{
    const KConfigNode *repo;
    rc_t rc = KConfigNodeOpenNodeRead ( sub, & repo, name );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        rc = VResolverLoadRepo ( self, algs, repo, ticket, allow_cache, protected );
        KConfigNodeRelease ( repo );
    }
    return rc;
}

/* LoadSubCategory
 *
 *    sub-category-block
 *        = <sub-category> <repository-block>* ;
 *
 *    sub-category
 *        = "main" | "aux" | "protected"
 *
 *    repository-block
 *        = ID <repository> ;
 */
static
rc_t VResolverLoadSubCategory ( VResolver *self, Vector *algs, const KConfigNode *kfg,
    const String *ticket, const char *sub_path, VResolverCacheAllow allow_cache, bool protected )
{
    const KConfigNode *sub;
    rc_t rc = KConfigNodeOpenNodeRead ( kfg, & sub, sub_path );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        KNamelist *children;
        rc = KConfigNodeListChildren ( sub, & children );
        if ( rc == 0 )
        {
            uint32_t i, count;
            rc = KNamelistCount ( children, & count );
            for ( i = 0; i < count && rc == 0; ++ i )
            {
                const char *name;
                rc = KNamelistGet ( children, i, & name );
                if ( rc == 0 )
                    rc = VResolverLoadNamedRepo ( self, algs, sub, ticket, name, allow_cache, protected );
            }

            KNamelistRelease ( children );
        }
        KConfigNodeRelease ( sub );
    }
    return rc;
}

/* LoadProtected
 *  special function to handle single, active protected workspace
 */
static
rc_t VResolverLoadProtected ( VResolver *self, const KConfigNode *kfg, const char *rep_name )
{
    const KConfigNode *repo;
    rc_t rc = KConfigNodeOpenNodeRead ( kfg, & repo, "user/protected/%s", rep_name );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        rc = VResolverLoadRepo ( self, & self -> local, repo, NULL, cacheAllow, true );
        KConfigNodeRelease ( repo );
    }
    return rc;
}

/* LoadLegacyRefseq
 *  load refseq information from KConfig
 *
 *  there are two legacy versions being supported
 *
 *    legacy-refseq
 *        = "refseq" <legacy-vol-or-repo> ;
 *
 *    legacy-vol-or-repo
 *        = "volumes" '=' <path-list>
 *        | <legacy-refseq-repo> <legacy-refseq-vols>
 *        ;
 */
static
rc_t VResolverLoadLegacyRefseq ( VResolver *self, const KConfig *cfg )
{
    const KConfigNode *vols;
    rc_t rc = KConfigOpenNodeRead ( cfg, & vols, "/refseq/paths" );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        String *vol_list;
        rc = KConfigNodeReadString ( vols, & vol_list );
        if ( rc == 0 )
        {
            const bool protected = false;
            const bool disabled = false;
            const bool caching = true;
            rc = VResolverLoadAlgVolumes ( & self -> local, NULL, NULL, cacheAllow,
                appREFSEQ, algREFSEQ,  & self -> num_app_vols [ appREFSEQ ],
                vol_list, protected, disabled, caching );
            StringWhack ( vol_list );
        }
        KConfigNodeRelease ( vols );
    }

    return rc;
}


/* ForceRemoteRefseq
 *  makes sure there is a remote source of refseq
 *  or else adds a hard-coded URL to NCBI
 */
static
rc_t VResolverForceRemoteRefseq ( VResolver *self )
{
    rc_t rc;
    bool found;
    String local_root;
    const String *root;

    uint32_t i, count = VectorLength ( & self -> remote );
    for ( found = false, i = 0; i < count; ++ i )
    {
        VResolverAlg *alg = ( VResolverAlg* ) VectorGet ( & self -> remote, i );
        if ( alg -> app_id == appREFSEQ )
        {
            found = true;
            if ( alg -> disabled )
                alg -> disabled = false;
        }
    }

    if ( found )
        return 0;

    if ( self -> num_app_vols [ appAny ] != 0 )
    {
        for ( i = 0; i < count; ++ i )
        {
            VResolverAlg *alg = ( VResolverAlg* ) VectorGet ( & self -> remote, i );
            if ( alg -> app_id == appAny )
            {
                found = true;
                if ( alg -> disabled )
                    alg -> disabled = false;
            }
        }
    }

    if ( found )
        return 0;

    /* create one from hard-coded constants */
    StringInitCString ( & local_root, "http://ftp-trace.ncbi.nlm.nih.gov/sra" );
    rc = StringCopy ( & root, & local_root );    
    if ( rc == 0 )
    {
        rc = VectorAppend ( & self -> roots, NULL, root );
        if ( rc != 0 )
            StringWhack ( root );
        else
        {
            String vol_list;
            const bool protected = false;
            const bool disabled = false;
            const bool caching = false;
            StringInitCString ( & vol_list, "refseq" );
            rc = VResolverLoadAlgVolumes ( & self -> remote, root, NULL, cacheDisallow,
                appREFSEQ, algREFSEQ, & self -> num_app_vols [ appREFSEQ ],
                & vol_list, protected, disabled, caching );
        }
    }

    return rc;
}


/* GetDownloadTicket
 *  if we are within a working environment that has a download ticket,
 *  capture it here and add that local repository into the mix
 */
static
const String *VResolverGetDownloadTicket ( const VResolver *self, const KConfig *cfg,
    char *buffer, size_t bsize )
{
    const String *ticket = NULL;
    const KRepositoryMgr *rmgr;
    rc_t rc = KConfigMakeRepositoryMgrRead ( cfg, & rmgr );
    if ( rc == 0 )
    {
        const KRepository *protected;
        rc = KRepositoryMgrCurrentProtectedRepository ( rmgr, & protected );
        if ( rc == 0 )
        {
            rc = KRepositoryName ( protected, buffer, bsize, NULL );
            if ( rc == 0 )
            {
                size_t ticsz;
                char ticbuf [ 256 ];
                rc = KRepositoryDownloadTicket ( protected, ticbuf, sizeof ticbuf, & ticsz );
                if ( rc == 0 )
                {
                    String tic;
                    StringInit ( & tic, ticbuf, ticsz, ( uint32_t ) ticsz );
                    rc = StringCopy ( & ticket, & tic );
                }
            }

            KRepositoryRelease ( protected );
        }

        KRepositoryMgrRelease ( rmgr );
    }
    return ticket;
}


/* ForceRemoteProtected
 *  makes sure there is a remote CGI
 */
static
rc_t VResolverForceRemoteProtected ( VResolver *self )
{
    rc_t rc;
    const String *root;

    /* create one from hard-coded constants */
    String cgi_root;
    StringInitCString ( & cgi_root, "http://www.ncbi.nlm.nih.gov/Traces/names/names.cgi" );
    rc = StringCopy ( & root, & cgi_root );    
    if ( rc == 0 )
    {
        rc = VectorAppend ( & self -> roots, NULL, root );
        if ( rc != 0 )
            StringWhack ( root );
        else
        {
            const bool protected = true;
            const bool disabled = false;

            VResolverAlg *cgi;
            rc = VResolverAlgMake ( & cgi, root, appAny, algCGI, protected, disabled );
            if ( rc == 0 )
            {
                cgi -> ticket = self -> ticket;

                rc = VectorAppend ( & self -> remote, NULL, cgi );
                if ( rc == 0 )
                {
                    ++ self -> num_app_vols [ appAny ];
                    return 0;
                }
            }

            VResolverAlgWhack ( cgi, NULL );
        }
    }

    return rc;
}


/* Load
 *  load the respository from ( current ) KConfig
 *
 *  using pseudo BNF, it looks like this:
 *
 *    repositories
 *        = "repository" <category-block>* ;
 *
 *    category-block
 *        = <category> <sub-category-block>* ;
 *
 *    category
 *        = "remote" | "site" | "user" ;
 *
 *    sub-category-block
 *        = <sub-category> <repository-block>* ;
 *
 *    sub-category
 *        = "main" | "aux" | "protected"
 */
static
rc_t VResolverLoad ( VResolver *self, const KConfig *cfg )
{
    bool have_remote_protected = false;

    const KConfigNode *kfg;
    rc_t rc = KConfigOpenNodeRead ( cfg, & kfg, "repository" );
    if ( GetRCState ( rc ) == rcNotFound )
        rc = 0;
    else if ( rc == 0 )
    {
        /* if the user is inside of a protected workspace, load it first */
        char buffer [ 256 ];
        self -> ticket = VResolverGetDownloadTicket ( self, cfg, buffer, sizeof buffer );
        if ( self -> ticket != NULL )
            rc = VResolverLoadProtected ( self, kfg, buffer );

        /* now load user public repositories */
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> local, kfg, NULL, "user/main", cacheAllow, false );
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> local, kfg, NULL, "user/aux", cacheAllow, false );

        /* load any site repositories */
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> local, kfg, NULL, "site/main", cacheDisallow, false );
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> local, kfg, NULL, "site/aux", cacheDisallow, false );

        /* if within a protected workspace, load protected remote repositories */
        if ( rc == 0 && self -> ticket != NULL )
        {
            rc = KNSManagerMake ( ( KNSManager** ) & self -> kns );
            if ( rc == 0 )
            {
                uint32_t entry_vols = VectorLength ( & self -> remote );
                rc = VResolverLoadSubCategory ( self, & self -> remote, kfg,
                    self -> ticket, "remote/protected", cacheDisallow, true );
                have_remote_protected = VectorLength ( & self -> remote ) > entry_vols;
            }
        }

        /* load any remote repositories */
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> remote, kfg, NULL, "remote/main", cacheDisallow, false );
        if ( rc == 0 )
            rc = VResolverLoadSubCategory ( self, & self -> remote, kfg, NULL, "remote/aux", cacheDisallow, false );

        KConfigNodeRelease ( kfg );

        /* recover from public remote repositories using resolver CGI */
        if ( self -> kns == NULL && self -> num_app_vols [ appAny ] != 0 )
            rc = KNSManagerMake ( ( KNSManager** ) & self -> kns );
    }

    if ( rc == 0 && self -> num_app_vols [ appAny ] == 0 )
    {
        bool has_current_refseq = true;

        /* AT THIS POINT, a current configuration will have something.
           But, older out-of-date configurations may exist and need special handling. */
        if ( self -> num_app_vols [ appREFSEQ ] == 0 )
        {
            has_current_refseq = false;
            rc = VResolverLoadLegacyRefseq ( self, cfg );
        }

        /* now, one more special case - for external users
           who had legacy refseq configuration but nothing for SRA,
           force existence of a remote refseq access */
        if ( rc == 0
             && ! has_current_refseq
             && self -> num_app_vols [ appREFSEQ ] != 0
             && self -> num_app_vols [ appSRA ] == 0 )
        {
            rc = VResolverForceRemoteRefseq ( self );
        }
    }

    if ( rc == 0 && self -> ticket != NULL && ! have_remote_protected )
        rc = VResolverForceRemoteProtected ( self );

    return rc;
}


/* Make
 *  internal factory function
 */
static
rc_t VResolverMake ( VResolver ** objp, const KDirectory *wd, const KConfig *kfg )
{
    rc_t rc;

    VResolver *obj = calloc ( 1, sizeof * obj );
    if ( obj == NULL )
        rc = RC ( rcVFS, rcMgr, rcCreating, rcMemory, rcExhausted );
    else
    {
        VectorInit ( & obj -> roots, 0, 8 );
        VectorInit ( & obj -> local, 0, 8 );
        VectorInit ( & obj -> remote, 0, 8 );
        obj -> wd = wd;

        KRefcountInit ( & obj -> refcount, 1, "VResolver", "make", "resolver" );

        rc = VResolverLoad ( obj, kfg );
        if ( rc == 0 )
        {
            * objp = obj;
            return 0;
        }

        VResolverWhack ( obj );
    }

    return rc;
}

/* Make
 *  ask the VFS manager to make a resolver
 */
LIB_EXPORT
rc_t CC VFSManagerMakeResolver ( const VFSManager * self,
    VResolver ** new_resolver, const KConfig * cfg )
{
    rc_t rc;

    if ( new_resolver == NULL )
        rc = RC ( rcVFS, rcMgr, rcCreating, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcVFS, rcMgr, rcCreating, rcSelf, rcNull );
        else if ( cfg == NULL )
            rc = RC ( rcVFS, rcMgr, rcCreating, rcParam, rcNull );
        else
        {
            KDirectory *wd;
            rc = VFSManagerGetCWD ( self, & wd );
            if ( rc == 0 )
            {
                rc = VResolverMake ( new_resolver, wd, cfg );
                if ( rc == 0 )
                    return 0;

                KDirectoryRelease ( wd );
            }
        }

        *new_resolver = NULL;
    }

    return rc;
}
