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

#include "cg_tools.h"
#include "debug.h"

#include <klib/printf.h>
#include <sysalloc.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

typedef struct CigOps
{
    char op;
    int8_t   ref_sign; /* 0;+1;-1; ref_offset = ref_sign * offset */
    int8_t   seq_sign; /* 0;+1;-1; seq_offset = seq_sign * offset */
    uint32_t oplen;
} CigOps;


static void SetCigOp( CigOps * dst, char op, uint32_t oplen )
{
    dst->op    = op;
    dst->oplen = oplen;
    switch( op ) 
    { /*MX= DN B IS PH*/

    case 'M' :
    case 'X' :
    case '=' :  dst->ref_sign = +1;
                dst->seq_sign = +1;
                break;

    case 'D' :
    case 'N' :  dst->ref_sign = +1;
                dst->seq_sign = 0;
                break;

    case 'B' :  dst->ref_sign = -1;
                dst->seq_sign = 0;
                break;

    case 'S' :
    case 'I' :  dst->ref_sign = 0;
                dst->seq_sign = +1;
                break;

    case 'P' :
    case 'H' :
    case 0   :  dst->ref_sign= 0;   /* terminating op */
                dst->seq_sign= 0;
                break;

    default :   assert( 0 );
                break;
    }
}


static int32_t ExplodeCIGAR( CigOps dst[], uint32_t len, char const cigar[], uint32_t ciglen )
{
    uint32_t i;
    uint32_t j;
    int32_t opLen;
    
    for ( i = j = opLen = 0; i < ciglen; ++i )
    {
        if ( isdigit( cigar[ i ] ) )
        {
            opLen = opLen * 10 + cigar[ i ] - '0';
        }
        else
        {
            assert( isalpha( cigar[ i ] ) );
            SetCigOp( dst + j, cigar[ i ], opLen );
            opLen = 0;
            j++;
        }
    }
    SetCigOp( dst + j, 0, 0 );
    j++;
    return j;
}


typedef struct cgOp_s
{
    uint16_t length;
    uint8_t type; /* 0: match, 1: insert, 2: delete */
    char code;
} cgOp;


static void print_CG_cigar( int line, cgOp const op[], unsigned const ops, unsigned const gap[ 3 ] )
{
#if _DEBUGGING
    unsigned i;
    
    SAM_DUMP_DBG( 3, ( "%5i: ", line ) );
    for ( i = 0; i < ops; ++i )
    {
        if ( gap && ( i == gap[ 0 ] || i == gap[ 1 ] || i == gap[ 2 ] ) )
        {
            SAM_DUMP_DBG( 3, ( "%u%c", op[ i ].length, tolower( op[ i ].code ) ) );
        }
        else
        {
            SAM_DUMP_DBG( 3, ( "%u%c", op[ i ].length, toupper( op[ i ].code ) ) );
        }
    }
    SAM_DUMP_DBG( 3, ( "\n" ) );
#endif
}


/* gap contains the indices of the wobbles in op
 * gap[0] is between read 1 and 2; it is the 'B'
 * gap[1] is between read 2 and 3; it is an 'N'
 * gap[2] is between read 3 and 4; it is an 'N'
 */

typedef struct cg_cigar_temp
{
    unsigned gap[ 3 ];
    cgOp cigOp[ MAX_READ_LEN ];
    unsigned opCnt;
    unsigned S_adjust;
    unsigned CG_adjust;
} cg_cigar_temp;


static rc_t CIGAR_to_CG_Ops( const cg_cigar_input * input,
                             cg_cigar_temp * tmp )
{
    unsigned i;
    unsigned ops = 0;
    unsigned gapno;

    tmp->opCnt = 0;
    tmp->S_adjust = 0;
    for ( i = 0; i < input->cigar_len; ++ops )
    {
        char opChar = 0;
        int opLen = 0;
        int n;

        for ( n = 0; ( ( n + i ) < input->cigar_len ) && ( isdigit( input->cigar[ n + i ] ) ); n++ )
        {
            opLen = opLen * 10 + input->cigar[ n + i ] - '0';
        }

        if ( ( n + i ) < input->cigar_len )
        {
            opChar = input->cigar[ n + i ];
            n++;
        }

        if ( ( ops + 1 ) >= MAX_READ_LEN )
            return RC( rcExe, rcData, rcReading, rcBuffer, rcInsufficient );

        i += n;
        
        tmp->cigOp[ ops ].length = opLen;
        tmp->cigOp[ ops ].code = opChar;
        switch ( opChar )
        {
            case 'M' :
            case '=' :
            case 'X' :  tmp->cigOp[ ops ].type = 0;
                        break;

            case 'S' :  tmp->S_adjust += opLen;
            case 'I' :  tmp->cigOp[ ops ].type = 1;
                        tmp->cigOp[ ops ].code = 'I';
                        break;

            case 'D':   tmp->cigOp[ ops ].type = 2;
                        break;

            default :   return RC( rcExe, rcData, rcReading, rcConstraint, rcViolated );
        }
    }

    tmp->opCnt = ops;
    tmp->gap[ 0 ] = tmp->gap[ 1 ] = tmp->gap[ 2 ] = ops;
    print_CG_cigar( __LINE__, tmp->cigOp, ops, NULL );

    if ( ops < 3 )
        return RC( rcExe, rcData, rcReading, rcFormat, rcNotFound ); /* CG pattern not found */

    {
        unsigned fwd = 0; /* 5 10 10 10 */
        unsigned rev = 0; /* 10 10 10 5 */
        unsigned acc; /* accumulated length */
        
        if ( ( input->seq_req_id == 1 && !input->orientation ) ||
             ( input->seq_req_id == 2 && input->orientation ) )
        {
            for ( i = 0, acc = 0; i < ops  && acc <= 5; ++i )
            {
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc == 5 && tmp->cigOp[ i + 1 ].type == 1 )
                    {
                        fwd = i + 1;
                        break;
                    }
                    else if ( acc > 5 )
                    {
                        unsigned const right = acc - 5;
                        unsigned const left = tmp->cigOp[ i ].length - right;
                        
                        memmove( &tmp->cigOp[ i + 2 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                        ops += 2;
                        tmp->cigOp[ i ].length = left;
                        tmp->cigOp[ i + 2 ].length = right;
                        
                        tmp->cigOp[ i + 1 ].type = 1;
                        tmp->cigOp[ i + 1 ].code = 'B';
                        tmp->cigOp[ i + 1 ].length = 0;
                        
                        fwd = i + 1;
                        break;
                    }
                }
            }
        }
        else if ( ( input->seq_req_id == 2 && !input->orientation ) ||
                  ( input->seq_req_id == 1 && input->orientation ) )
        {
            for ( i = ops, acc = 0; i > 0 && acc <= 5; )
            {
                --i;
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc == 5 && tmp->cigOp[ i ].type == 1 )
                    {
                        rev = i;
                        break;
                    }
                    else if ( acc > 5 )
                    {
                        unsigned const left = acc - 5;
                        unsigned const right = tmp->cigOp[ i ].length - left;
                        
                        memmove( &tmp->cigOp[ i + 2 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                        ops += 2;
                        tmp->cigOp[ i ].length = left;
                        tmp->cigOp[ i + 2 ].length = right;
                        
                        tmp->cigOp[ i + 1 ].type = 1;
                        tmp->cigOp[ i + 1 ].code = 'B';
                        tmp->cigOp[ i + 1 ].length = 0;
                         
                        rev = i + 1;
                        break;
                    }
                }
            }
        }
        else
        {
            /* fprintf(stderr, "guessing layout\n"); */
            for ( i = 0, acc = 0; i < ops  && acc <= 5; ++i )
            {
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc == 5 && tmp->cigOp[ i + 1 ].type == 1 )
                    {
                        fwd = i + 1;
                    }
                }
            }
            for ( i = ops, acc = 0; i > 0 && acc <= 5; )
            {
                --i;
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc == 5 && tmp->cigOp[i].type == 1 )
                    {
                        rev = i;
                    }
                }
            }
            if ( ( fwd == 0 && rev == 0 ) || ( fwd != 0 && rev != 0 ) )
            {
                for ( i = 0; i < ops; ++i )
                {
                    if ( tmp->cigOp[ i ].type == 2 )
                    {
                        tmp->cigOp[ i ].code = 'N';
                        tmp->CG_adjust += tmp->cigOp[ i ].length;
                    }
                }
                return RC( rcExe, rcData, rcReading, rcFormat, rcNotFound ); /* CG pattern not found */
            }
        }
        if ( fwd && tmp->cigOp[ fwd ].type == 1 )
        {
            for ( i = ops, acc = 0; i > fwd + 1; )
            {
                --i;
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc >= 10 )
                    {
                        if ( acc > 10 )
                        {
                            unsigned const r = 10 + tmp->cigOp[ i ].length - acc;
                            unsigned const l = tmp->cigOp[ i ].length - r;
                            
                            if ( ops + 2 >= MAX_READ_LEN )
                                return RC( rcExe, rcData, rcReading, rcBuffer, rcInsufficient );
                            memmove( &tmp->cigOp[ i + 2 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                            ops += 2;
                            tmp->cigOp[ i + 2 ].length = r;
                            tmp->cigOp[ i ].length = l;
                            
                            tmp->cigOp[ i + 1 ].length = 0;
                            tmp->cigOp[ i + 1 ].type = 2;
                            tmp->cigOp[ i + 1 ].code = 'N';
                            i += 2;
                        }
                        else if ( i - 1 > fwd )
                        {
                            if ( tmp->cigOp[ i - 1 ].type == 2 )
                                 tmp->cigOp[ i - 1 ].code = 'N';
                            else
                            {
                                if ( ops + 1 >= MAX_READ_LEN )
                                    return RC( rcExe, rcData, rcReading, rcBuffer, rcInsufficient );
                                memmove( &tmp->cigOp[ i + 1 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                                ops += 1;
                                tmp->cigOp[ i ].length = 0;
                                tmp->cigOp[ i ].type = 2;
                                tmp->cigOp[ i ].code = 'N';
                                i += 1;
                            }
                        }
                        acc = 0;
                    }
                }
            }
            /** change I to B+M **/
            tmp->cigOp[ fwd ].code = 'B';
            memmove( &tmp->cigOp[ fwd + 1 ], &tmp->cigOp[ fwd ], ( ops - fwd ) * sizeof( tmp->cigOp[ 0 ] ) );
            ops += 1;
            tmp->cigOp[ fwd + 1 ].code = 'M';
            tmp->opCnt = ops;
            /** set the gaps now **/
            for ( gapno = 3, i = ops; gapno > 1 && i > 0; )
            {
                --i;
                if ( tmp->cigOp[ i ].code == 'N' )
                    tmp->gap[ --gapno ] = i;
            }
            tmp->gap[ 0 ] = fwd;
            print_CG_cigar( __LINE__, tmp->cigOp, ops, tmp->gap );
            return 0;
        }
        if ( rev && tmp->cigOp[ rev ].type == 1 )
        {
            for ( acc = i = 0; i < rev; ++i )
            {
                if ( tmp->cigOp[ i ].type != 2 )
                {
                    acc += tmp->cigOp[ i ].length;
                    if ( acc >= 10 )
                    {
                        if ( acc > 10 )
                        {
                            unsigned const l = 10 + tmp->cigOp[ i ].length - acc;
                            unsigned const r = tmp->cigOp[ i ].length - l;
                            
                            if ( ops + 2 >= MAX_READ_LEN )
                                return RC( rcExe, rcData, rcReading, rcBuffer, rcInsufficient );
                            memmove( &tmp->cigOp[ i + 2 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                            ops += 2;
                            tmp->cigOp[ i + 2 ].length = r;
                            tmp->cigOp[ i ].length = l;
                            
                            tmp->cigOp[ i + 1 ].length = 0;
                            tmp->cigOp[ i + 1 ].type = 2;
                            tmp->cigOp[ i + 1 ].code = 'N';
                            rev += 2;
                            i += 2;
                        }
                        else if ( i + 1 < rev )
                        {
                            if ( tmp->cigOp[ i + 1 ].type == 2 )
                                 tmp->cigOp[ i + 1 ].code = 'N';
                            else
                            {
                                if ( ops + 1 >= MAX_READ_LEN )
                                    return RC( rcExe, rcData, rcReading, rcBuffer, rcInsufficient );
                                memmove( &tmp->cigOp[ i + 1 ], &tmp->cigOp[ i ], ( ops - i ) * sizeof( tmp->cigOp[ 0 ] ) );
                                ops += 1;
                                tmp->cigOp[ i + 1 ].length = 0;
                                tmp->cigOp[ i + 1 ].type = 2;
                                tmp->cigOp[ i + 1 ].code = 'N';
                                rev += 1;
                                i += 1;
                            }
                        }
                        acc = 0;
                    }
                }
            }
            for ( gapno = 3, i = 0; gapno > 1 && i < ops; ++i )
            {
                if ( tmp->cigOp[ i ].code == 'N' )
                    tmp->gap[ --gapno ] = i;
            }
            tmp->gap[ 0 ] = rev;
            tmp->cigOp[ rev ].code = 'B';
            memmove( &tmp->cigOp[ rev + 1 ], &tmp->cigOp[ rev ], ( ops - rev ) * sizeof( tmp->cigOp[ 0 ] ) );
            ops += 1;
            tmp->cigOp[ rev + 1 ].code = 'M';
            tmp->opCnt = ops;
            print_CG_cigar( __LINE__, tmp->cigOp, ops, tmp->gap );
            return 0;
        }
    }
    return RC( rcExe, rcData, rcReading, rcFormat, rcNotFound ); /* CG pattern not found */
}


static rc_t adjust_cigar( const cg_cigar_input * input, cg_cigar_temp * tmp, cg_cigar_output * output )
{
    rc_t rc = 0;
    unsigned i, j;
    size_t sz;

    print_CG_cigar( __LINE__, tmp->cigOp, tmp->opCnt, NULL );

    /* remove zero length ops */
    for ( j = i = 0; i < tmp->opCnt; )
    {
        if ( tmp->cigOp[ j ].length == 0 )
        {
            ++j;
            --(tmp->opCnt);
            continue;
        }
        tmp->cigOp[ i++ ] = tmp->cigOp[ j++ ];
    }

    print_CG_cigar( __LINE__, tmp->cigOp, tmp->opCnt, NULL );

    if ( input->edit_dist_available )
    {
        int const adjusted = input->edit_dist + tmp->S_adjust - tmp->CG_adjust;
        output->edit_dist = adjusted > 0 ? adjusted : 0;
        SAM_DUMP_DBG( 4, ( "NM: before: %u, after: %u(+%u-%u)\n", input->edit_dist, output->edit_dist, tmp->S_adjust, tmp->CG_adjust ) );
    }
    else
    {
        output->edit_dist = input->edit_dist;
    }

    /* merge adjacent ops */
    for ( i = tmp->opCnt; i > 1; )
    {
        --i;
        if ( tmp->cigOp[ i - 1 ].code == tmp->cigOp[ i ].code )
        {
            tmp->cigOp[ i - 1 ].length += tmp->cigOp[ i ].length;
            memmove( &tmp->cigOp[ i ], &tmp->cigOp[ i + 1 ], ( tmp->opCnt - 1 - i ) * sizeof( tmp->cigOp[ 0 ] ) );
            --(tmp->opCnt);
        }
    }
    print_CG_cigar( __LINE__, tmp->cigOp, tmp->opCnt, NULL );
    for ( i = j = 0; i < tmp->opCnt && rc == 0; ++i )
    {
        rc = string_printf( &output->cigar[ j ], sizeof( output->cigar ) - j, &sz, "%u%c", tmp->cigOp[ i ].length, tmp->cigOp[ i ].code );
        j += sz;
    }
    output->cigar_len = j;
    return rc;
}


rc_t make_cg_cigar( const cg_cigar_input * input, cg_cigar_output * output )
{
    rc_t rc;
    cg_cigar_temp tmp;
    memset( &tmp, 0, sizeof tmp );

    rc = CIGAR_to_CG_Ops( input, &tmp );
    if ( GetRCState( rc ) == rcNotFound && GetRCObject( rc ) == rcFormat )
    {
        memmove( &output->cigar[ 0 ], &input->cigar[ 0 ], input->cigar_len );
        output->cigar_len = input->cigar_len;
        output->cigar[ output->cigar_len ] = 0;
        output->edit_dist = input->edit_dist;
        rc = 0;
    }
    else if ( rc == 0 )
    {
        rc = adjust_cigar( input, &tmp, output );
    }
    return rc;
}
