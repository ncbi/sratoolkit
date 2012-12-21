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

#include <kdb/extern.h>
#include "colidx1-priv.h"
#include <kfs/file.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <byteswap.h>


/*--------------------------------------------------------------------------
 * KColumnIdx1Node
 *  a binary search tree node
 */
typedef struct KColumnIdx1Node KColumnIdx1Node;
struct KColumnIdx1Node
{
    BSTNode n;
    KColBlockLoc loc;
};

/* Find
 */
static
int CC KColumnIdx1NodeFind ( const void *item, const BSTNode *n )
{
#define a ( * ( const int64_t* ) item )
#define b ( ( const KColumnIdx1Node* ) n )

    if ( a < b -> loc . start_id )
        return -1;
    return a >= ( b -> loc . start_id + b -> loc . id_range );

#undef a
#undef b
}

/* Sort
 */
static
int CC KColumnIdx1NodeSort ( const BSTNode *item, const BSTNode *n )
{
#define a ( ( const KColumnIdx1Node* ) item )
#define b ( ( const KColumnIdx1Node* ) n )

    if ( ( a -> loc . start_id + a -> loc . id_range ) <= b -> loc . start_id )
        return -1;
    return a -> loc . start_id >= ( b -> loc . start_id + b -> loc . id_range );

#undef a
#undef b
}

/* Whack
 */
static
void CC KColumnIdx1NodeWhack ( BSTNode *n, void *ignore )
{
    free ( n );
}


/*--------------------------------------------------------------------------
 * KColumnIdx1
 *  level 1 index
 */

/* Init
 */
static
rc_t KColumnIdx1Inflate ( KColumnIdx1 *self,
    const KColBlockLoc *buffer, uint32_t count )
{
    uint32_t i;
    for ( i = 0; i < count; ++ i )
    {
        KColumnIdx1Node *exist, *n = malloc ( sizeof * n );
        if ( n == NULL )
            return RC ( rcDB, rcColumn, rcConstructing, rcMemory, rcExhausted );

        n -> loc = buffer [ i ];
        if ( BSTreeInsertUnique ( & self -> bst,
             & n -> n, ( BSTNode** ) & exist, KColumnIdx1NodeSort ) )
        {
            free ( n );
            return RC ( rcDB, rcColumn, rcConstructing, rcIndex, rcCorrupt );
        }

        ++ self -> count;
    }

    return 0;
}

static
void KColumnIdx1Swap ( KColBlockLoc *buffer, uint32_t count )
{
    uint32_t i;
    for ( i = 0; i < count; ++ i )
    {
        buffer [ i ] . pg = bswap_64 ( buffer [ i ] . pg );
        buffer [ i ] . u . gen = bswap_32 ( buffer [ i ] . u . gen );
        buffer [ i ] . id_range = bswap_32 ( buffer [ i ] . id_range );
        buffer [ i ] . start_id = bswap_64 ( buffer [ i ] . start_id );
    }
}

static
rc_t KColumnIdx1Init ( KColumnIdx1 *self, uint32_t off, uint32_t count )
{
    rc_t rc;
    KColBlockLoc *buffer = malloc ( 2048 * sizeof * buffer );
    if ( buffer == NULL )
        rc = RC ( rcDB, rcColumn, rcConstructing, rcMemory, rcExhausted );
    else
    {
        size_t num_read;
        uint32_t i, cnt;
        for ( rc = 0, i = 0; i < count; off += ( uint32_t ) num_read, i += cnt )
        {
            cnt = count - i;
            if ( cnt > 2048 )
                cnt = 2048;

            rc = KFileReadAll ( self -> f, off,
                buffer, cnt * sizeof * buffer, & num_read );
            if ( rc != 0 )
                break;
            if ( num_read == 0 )
                break;
            if ( ( num_read % sizeof * buffer ) != 0 )
            {
                rc = RC ( rcDB, rcColumn, rcConstructing, rcIndex, rcCorrupt );
                break;
            }

            if ( self -> bswap )
                KColumnIdx1Swap ( buffer, cnt );

            rc = KColumnIdx1Inflate ( self, buffer, cnt );
            if ( rc != 0 )
                break;
        }

        free ( buffer );
    }
    return rc;
}

/* Open
 */
rc_t KColumnIdx1OpenRead ( KColumnIdx1 *self, const KDirectory *dir,
    uint64_t *data_eof, uint32_t *idx0_count, uint64_t *idx2_eof,
    size_t *pgsize, int32_t *checksum )
{
    rc_t rc;

    BSTreeInit ( & self -> bst );
    self -> count = 0;
    self -> vers = 0;
    self -> bswap = false;
    self -> fidx = NULL;
            
    rc = KDirectoryVOpenFileRead ( dir, & self -> f, "idx1", NULL );
    if ( rc == 0 )
    {
        KColumnHdr hdr;
        size_t num_bytes;
        rc = KFileReadAll ( self -> f, 0, & hdr, sizeof hdr, & num_bytes );
        if ( rc == 0 )
        {
            if ( num_bytes == 0 )
                rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcNotFound );
            else if ( num_bytes < KColumnHdrMinSize ( hdr ) )
                rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcCorrupt );
            else
            {
                rc = KDBHdrValidate ( & hdr . dad, num_bytes, 1, KCOL_CURRENT_VERSION );
                if ( GetRCState ( rc ) == rcIncorrect && GetRCObject ( rc ) == rcByteOrder )
                {
                    self -> bswap = true;
                    hdr . dad . endian = bswap_32 ( hdr . dad . endian );
                    hdr . dad . version = bswap_32 ( hdr . dad . version );
                    rc = KDBHdrValidate ( & hdr . dad, num_bytes, 1, KCOL_CURRENT_VERSION );
                }
                if ( rc == 0 )
                {
                    uint32_t off, count;

                    switch ( hdr . dad . version )
                    {
                    case 1:
                        if ( num_bytes < KColumnHdrOffset ( hdr, v1 ) )
                            rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcCorrupt );
                        else
                        {
                            if ( self -> bswap )
                            {
                                hdr . u . v1 . data_eof = bswap_64 ( hdr . u . v1 . data_eof );
                                hdr . u . v1 . idx2_eof = bswap_64 ( hdr . u . v1 . idx2_eof );
                                hdr . u . v1 . num_blocks = bswap_32 ( hdr . u . v1 . num_blocks );
                                hdr . u . v1 . page_size = bswap_32 ( hdr . u . v1 . page_size );
                            }

                            * data_eof = hdr . u . v1 . data_eof;
                            * idx2_eof = hdr . u . v1 . idx2_eof;
                            * pgsize = hdr . u . v1 . page_size;
                            * checksum = hdr . u . v1 . checksum;
                            count = hdr . u . v1 . num_blocks;
                            off = KColumnHdrOffset ( hdr, v1 );

                            /* not supported in v1 */
                            * idx0_count = 0;
                        }
                        break;

                    default:
                        rc = KDirectoryVOpenFileRead ( dir, ( const KFile** ) & self -> fidx, "idx", NULL );
                        if ( rc == 0 )
                        {
                            off = sizeof hdr . dad;
                            rc = KFileReadAll ( self -> fidx, 0, & hdr, sizeof hdr, & num_bytes );
                            if ( rc == 0 )
                            {
                                if ( num_bytes < KColumnHdrOffset ( hdr, v2 ) )
                                    rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcCorrupt );
                                else
                                {
                                    rc = KDBHdrValidate ( & hdr . dad,
                                        num_bytes, 2, KCOL_CURRENT_VERSION );
                                    if ( GetRCState ( rc ) == rcIncorrect && GetRCObject ( rc ) == rcByteOrder )
                                    {
                                        if ( ! self->bswap ) /* catch mis-matched endianess */
                                            rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcCorrupt );
                                        else
                                        {
                                            hdr . dad . endian = bswap_32 ( hdr . dad . endian );
                                            hdr . dad . version = bswap_32 ( hdr . dad . version );
                                            rc = KDBHdrValidate ( & hdr . dad, num_bytes, 1, KCOL_CURRENT_VERSION );
                                        }
                                    }
                                    else if ( self -> bswap ) /* catch mis-matched endianess */
                                        rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcCorrupt );

                                    if ( rc == 0 ) switch ( hdr . dad . version )
                                    {
                                    case 2:
                                        if ( self -> bswap )
                                        {
                                            hdr. u . v2 . data_eof = bswap_64 ( hdr. u . v2 . data_eof );
                                            hdr. u . v2 . idx2_eof = bswap_64 ( hdr. u . v2 . idx2_eof );
                                            hdr. u . v2 . num_blocks = bswap_32 ( hdr. u . v2 . num_blocks );
                                            hdr. u . v2 . page_size = bswap_32 ( hdr. u . v2 . page_size );
                                        }

                                        * data_eof = hdr . u . v2 . data_eof;
                                        * idx2_eof = hdr . u . v2 . idx2_eof;
                                        * pgsize = hdr . u . v2 . page_size;
                                        * checksum = hdr . u . v2 . checksum;
                                        count = hdr . u . v2 . num_blocks;
                                        * idx0_count = 0;
                                        break;

                                    case 3:
                                        if ( self -> bswap )
                                        {
                                            hdr. u . v3 . data_eof = bswap_64 ( hdr. u . v3 . data_eof );
                                            hdr. u . v3 . idx2_eof = bswap_64 ( hdr. u . v3 . idx2_eof );
                                            hdr. u . v3 . idx0_count = bswap_32 ( hdr. u . v3 . idx0_count );
                                            hdr. u . v3 . num_blocks = bswap_32 ( hdr. u . v3 . num_blocks );
                                            hdr. u . v3 . page_size = bswap_32 ( hdr. u . v3 . page_size );
                                        }

                                        * data_eof = hdr . u . v3 . data_eof;
                                        * idx2_eof = hdr . u . v3 . idx2_eof;
                                        * idx0_count = hdr . u . v3 . idx0_count;
                                        * pgsize = hdr . u . v3 . page_size;
                                        * checksum = hdr . u . v3 . checksum;
                                        count = hdr . u . v3 . num_blocks;
                                        break;

                                    default:
                                        rc = RC ( rcDB, rcColumn, rcOpening, rcIndex, rcBadVersion );
                                    }
                                }
                            }
                        }
                    }

                    if ( rc == 0 )
                    {
                        self -> vers = hdr . dad . version;
                        rc = KColumnIdx1Init ( self, off, count );
                        if ( rc == 0 )
                            return rc;
                    }

                    KFileRelease ( self -> fidx );
                    self -> fidx = NULL;
                }
            }
        }

        KFileRelease ( self -> f );
        self -> f = NULL;
    }

    return rc;
}


/* Whack
 */
rc_t KColumnIdx1Whack ( KColumnIdx1 *self )
{
    rc_t rc = KFileRelease ( self -> fidx );
    if ( rc == 0 )
    {
        self -> fidx = NULL;
        rc = KFileRelease ( self -> f );
        if ( rc == 0 )
        {
            self -> f = NULL;
            BSTreeWhack ( & self -> bst, KColumnIdx1NodeWhack, NULL );
            BSTreeInit ( & self -> bst );
        }
    }
    return rc;
}

/* Version
 */
#ifndef KColumnIdx1Version
rc_t KColumnIdx1Version ( const KColumnIdx1 *self, uint32_t *version )
{
    * version = ( uint32_t ) self -> vers;
    return 0;
}
#endif

/* IdRange
 *  returns range of ids contained within
 */
bool KColumnIdx1IdRange ( const KColumnIdx1 *self,
    int64_t *first, int64_t *upper )
{
    const KColumnIdx1Node *a, *z;

    assert ( self != NULL );
    assert ( first != NULL );
    assert ( upper != NULL );

    a = ( const KColumnIdx1Node* ) BSTreeFirst ( & self -> bst );
    if ( a == NULL )
        return false;

    z = ( const KColumnIdx1Node* ) BSTreeLast ( & self -> bst );
    assert ( z != NULL );

    * first = a -> loc . start_id;
    * upper = z -> loc . start_id + z -> loc . id_range;
    assert ( * first < * upper );

    return true;
}

/* LocateBlock
 *  locates an idx2 block by range
 *  return values:
 */
rc_t KColumnIdx1LocateBlock ( const KColumnIdx1 *self,
    KColBlockLoc *bloc, int64_t first, int64_t upper )
{
    const KColumnIdx1Node *n;

    assert ( self != NULL );
    assert ( bloc != NULL );
    assert ( first < upper );

    n = ( const KColumnIdx1Node* )
        BSTreeFind ( & self -> bst, & first, KColumnIdx1NodeFind );

    if ( n == NULL )
        return RC ( rcDB, rcColumn, rcSelecting, rcBlob, rcNotFound );

    assert ( first >= n -> loc . start_id );
    assert ( first < ( n -> loc . start_id + n -> loc . id_range ) );

    if ( upper > ( n -> loc . start_id + n -> loc . id_range ) )
        return RC ( rcDB, rcColumn, rcSelecting, rcRange, rcInvalid );

    * bloc = n -> loc;
    return 0;
}
