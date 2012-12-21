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

#include <klib/extern.h>
#include <klib/checksum.h>
#include <sysalloc.h>

/*--------------------------------------------------------------------------
 * CRC32
 */
static
uint32_t sCRC32_tbl [ 256 ];

/* CRC32Init
 *  initializes table
 *  IDEMPOTENT
 */
LIB_EXPORT void CC CRC32Init ( void )
{
    static int beenHere = 0;
    if ( ! beenHere )
    {
        int i, j;
        int32_t kPoly32 = 0x04C11DB7;
        
        for ( i = 0; i < 256; ++ i )
        {
            int32_t byteCRC = i << 24;
            for ( j = 0; j < 8; ++ j )
            {
                if ( byteCRC < 0 )
                    byteCRC = ( byteCRC << 1 ) ^ kPoly32;
                else
                    byteCRC <<= 1;
            }
            sCRC32_tbl [ i ] = byteCRC;
        }

        beenHere = 1;
    }
}

/* CRC32
 *  runs checksum on arbitrary data, returning result
 *  initial checksum to be passed in is 0
 *  subsequent checksums should be return from prior invocation
 */
LIB_EXPORT uint32_t CC CRC32 ( uint32_t checksum, const void *data, size_t size )
{
    size_t j;

#define str ( ( const unsigned char* ) data )

    if ( sCRC32_tbl [ 0 ] == sCRC32_tbl [ 1 ] )
        CRC32Init();

    for ( j = 0; j < size; ++ j )
    {
        uint32_t i = ( checksum >> 24 ) ^ str [ j ];
        checksum <<= 8;
        checksum ^= sCRC32_tbl [ i ];
    }
    return checksum;
    
#undef str
}
