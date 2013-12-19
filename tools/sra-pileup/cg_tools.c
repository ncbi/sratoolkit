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
#include <stdlib.h>
#include <string.h>
#include <assert.h>


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


int32_t ExplodeCIGAR( CigOps dst[], uint32_t len, char const cigar[], uint32_t ciglen )
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
    tmp->CG_adjust = 0;
    for ( i = 0; i < input->p_cigar.len; ++ops )
    {
        char opChar = 0;
        int opLen = 0;
        int n;

        for ( n = 0; ( ( n + i ) < input->p_cigar.len ) && ( isdigit( input->p_cigar.ptr[ n + i ] ) ); n++ )
        {
            opLen = opLen * 10 + input->p_cigar.ptr[ n + i ] - '0';
        }

        if ( ( n + i ) < input->p_cigar.len )
        {
            opChar = input->p_cigar.ptr[ n + i ];
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
        size_t sz;
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
        memmove( &output->cigar[ 0 ], &input->p_cigar.ptr[ 0 ], input->p_cigar.len );
        output->cigar_len = input->p_cigar.len;
        output->cigar[ output->cigar_len ] = 0;
        output->edit_dist = input->edit_dist;
        output->p_cigar.ptr = output->cigar;
        output->p_cigar.len = output->cigar_len;
        rc = 0;
    }
    else if ( rc == 0 )
    {
        if ( tmp.CG_adjust == 0 )
        {
            if ( tmp.gap[ 0 ] < tmp.opCnt )
                tmp.CG_adjust = tmp.cigOp[ tmp.gap[ 0 ] ].length;

            if ( tmp.gap[ 1 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 1 ] ].length;

            if ( tmp.gap[ 2 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 2 ] ].length;
        }

        rc = adjust_cigar( input, &tmp, output );
    }
    return rc;
}


static char merge_M_type_ops( char a, char b )
{ /*MX=*/
    char c = 0;
    switch( b )
    {
        case 'X' :  switch( a )
                    {
                        case '=' : c = 'X'; break;
                        case 'X' : c = 'M'; break; /**we don't know - 2X may create '=' **/
                        case 'M' : c = 'M'; break;
                    }
                    break;
        case 'M' :  c = 'M'; break;
        case '=' :  c = a; break;
    }
    assert( c != 0 );
    return c;
}


static size_t fmt_cigar_elem( char * dst, uint32_t cig_oplen, char cig_op )
{
    size_t num_writ;
    rc_t rc = string_printf ( dst, 11, &num_writ, "%d%c", cig_oplen, cig_op );
    assert ( rc == 0 && num_writ > 1 );
    return num_writ;
}

uint32_t CombineCIGAR( char dst[], CigOps const seqOp[], uint32_t seq_len,
                       uint32_t refPos, CigOps const refOp[], uint32_t ref_len )
{
    bool done = false;
    uint32_t ciglen = 0, last_ciglen = 0, last_cig_oplen = 0;
    int32_t si = 0, ri = 0;
    char last_cig_op = '0'; /* never used operation, forces a mismatch in MACRO_BUILD_CIGAR */
    CigOps seq_cop = { 0, 0, 0, 0 }, ref_cop = { 0, 0, 0, 0 };
    int32_t seq_pos = 0;        /** seq_pos is tracked roughly - with every extraction from seqOp **/
    int32_t ref_pos = 0;        /** ref_pos is tracked precisely - with every delta and consumption in cigar **/
    int32_t delta = refPos;     /*** delta in relative positions of seq and ref **/
                                /*** when delta < 0 - rewind or extend the reference ***/
                                /*** wher delta > 0 - skip reference  ***/
#define MACRO_BUILD_CIGAR(OP,OPLEN) \
	if( last_cig_oplen > 0 && last_cig_op == OP){							\
                last_cig_oplen += OPLEN;								\
                ciglen = last_ciglen + fmt_cigar_elem( dst + last_ciglen, last_cig_oplen,last_cig_op );	\
        } else {											\
                last_ciglen = ciglen;									\
                last_cig_oplen = OPLEN;									\
                last_cig_op    = OP;									\
                ciglen = ciglen      + fmt_cigar_elem( dst + last_ciglen, last_cig_oplen, last_cig_op );	\
        }
    while( !done )
    {
        while ( delta < 0 )
        { 
            ref_pos += delta; /** we will make it to back up this way **/
            if( ri > 0 )
            { /** backing up on ref if possible ***/
                int avail_oplen = refOp[ ri - 1 ].oplen - ref_cop.oplen;
                if ( avail_oplen > 0 )
                {
                    if ( ( -delta ) <= avail_oplen * ref_cop.ref_sign )
                    { /*** rewind within last operation **/
                        ref_cop.oplen -= delta;
                        delta -= delta * ref_cop.ref_sign;
                    }
                    else
                    { /*** rewind the whole ***/
                        ref_cop.oplen += avail_oplen;
                        delta += avail_oplen * ref_cop.ref_sign;
                    }
                }
                else
                {
                    ri--;
                    /** pick the previous as used up **/
                    ref_cop = refOp[ri-1];
                    ref_cop.oplen =0; 
                }
            }
            else
            { /** extending the reference **/
                SetCigOp( &ref_cop, '=', ref_cop.oplen - delta );
                delta = 0;
            }
            ref_pos -= delta; 
        }
        if ( ref_cop.oplen == 0 )
        { /*** advance the reference ***/
            ref_cop = refOp[ri++];
            if ( ref_cop.oplen == 0 )
            { /** extending beyond the reference **/
                SetCigOp( &ref_cop,'=', 1000 );
            }
            assert( ref_cop.oplen > 0 );
        }
        if ( delta > 0 )
        { /***  skip refOps ***/
            ref_pos += delta; /** may need to back up **/
            if ( delta >=  ref_cop.oplen )
            { /** full **/
                delta -= ref_cop.oplen * ref_cop.ref_sign;
                ref_cop.oplen = 0;
            }
            else
            { /** partial **/
                ref_cop.oplen -= delta;
                delta -= delta * ref_cop.ref_sign;
            }
            ref_pos -= delta; /** if something left - restore ***/
            continue;
        }

        /*** seq and ref should be synchronized here **/
        assert( delta == 0 );
        if ( seq_cop.oplen == 0 )
        { /*** advance sequence ***/
            if ( seq_pos < seq_len )
            {
                seq_cop = seqOp[ si++ ];
                assert( seq_cop.oplen > 0 );
                seq_pos += seq_cop.oplen * seq_cop.seq_sign;
            }
            else
            {
                done=true;
            }
        }

        if( !done )
        {
            int seq_seq_step = seq_cop.oplen * seq_cop.seq_sign; /** sequence movement**/
            int seq_ref_step = seq_cop.oplen * seq_cop.ref_sign; /** influence of sequence movement on intermediate reference **/
            int ref_seq_step = ref_cop.oplen * ref_cop.seq_sign; /** movement of the intermediate reference ***/
            int ref_ref_step = ref_cop.oplen * ref_cop.ref_sign; /** influence of the intermediate reference movement on final reference ***/
            assert( ref_ref_step >= 0 ); /** no B in the reference **/
            if ( seq_ref_step <= 0 )
            { /** BSIPH in the sequence against anything ***/
                MACRO_BUILD_CIGAR( seq_cop.op, seq_cop.oplen );
                seq_cop.oplen = 0;
                delta = seq_ref_step; /** if negative - will force rewind next cycle **/
            }
            else if ( ref_ref_step <= 0 )
            { /** MX=DN against SIPH in the reference***/
                if ( ref_seq_step == 0 )
                { /** MX=DN against PH **/
                    MACRO_BUILD_CIGAR( ref_cop.op,ref_cop.oplen);
                    ref_cop.oplen = 0;
                }
                else
                {
                    int min_len = ( seq_cop.oplen < ref_cop.oplen ) ? seq_cop.oplen : ref_cop.oplen;
                    if( seq_seq_step == 0 )
                    { /** DN agains SI **/
                        MACRO_BUILD_CIGAR( 'P', min_len );
                    }
                    else
                    { /** MX= agains SI ***/
                        MACRO_BUILD_CIGAR( ref_cop.op,min_len );
                    }
                    seq_cop.oplen -= min_len;
                    ref_cop.oplen -= min_len;
                }
            }
            else
            {
                /*MX=DN  against MX=DN*/
                int min_len = ( seq_cop.oplen < ref_cop.oplen ) ? seq_cop.oplen : ref_cop.oplen;
                if ( seq_seq_step == 0 )
                { /* DN against MX=DN */
                    if ( ref_seq_step == 0 )
                    { /** padding DN against DN **/
                        MACRO_BUILD_CIGAR( 'P', min_len );
                        ref_cop.oplen -= min_len;
                        seq_cop.oplen -= min_len;
                    }
                    else
                    { /* DN against MX= **/
                        MACRO_BUILD_CIGAR( seq_cop.op, min_len );
                        seq_cop.oplen -= min_len;
                    }
                }
                else if ( ref_cop.seq_sign == 0 )
                { /* MX= against DN - always wins */
                    MACRO_BUILD_CIGAR( ref_cop.op, min_len );
                    ref_cop.oplen -= min_len;
                }
                else
                { /** MX= against MX= ***/
                    char curr_op = merge_M_type_ops( seq_cop.op, ref_cop.op );
                    /* or otherwise merge_M_type_ops() will be called twice by the macro! */
                    MACRO_BUILD_CIGAR( curr_op, min_len );
                    ref_cop.oplen -= min_len;
                    seq_cop.oplen -= min_len;
                }
                ref_pos += min_len;
            }
        }
    }
    return ciglen;
}


typedef struct cg_merger
{
    char newSeq[ MAX_READ_LEN ];
    char newQual[ MAX_READ_LEN ];
    char tags[ MAX_CG_CIGAR_LEN * 2 ];
} cg_merger;


rc_t merge_cg_cigar( const cg_cigar_input * input, cg_cigar_output * output )
{
    rc_t rc;
    cg_cigar_temp tmp;
    memset( &tmp, 0, sizeof tmp );

    rc = CIGAR_to_CG_Ops( input, &tmp );
    if ( GetRCState( rc ) == rcNotFound && GetRCObject( rc ) == rcFormat )
    {
        memmove( &output->cigar[ 0 ], &input->p_cigar.ptr[ 0 ], input->p_cigar.len );
        output->cigar_len = input->p_cigar.len;
        output->cigar[ output->cigar_len ] = 0;
        output->edit_dist = input->edit_dist;
        rc = 0;
    }
    else if ( rc == 0 )
    {

        if ( tmp.CG_adjust == 0 )
        {
            if ( tmp.gap[ 0 ] < tmp.opCnt )
                tmp.CG_adjust = tmp.cigOp[ tmp.gap[ 0 ] ].length;

            if ( tmp.gap[ 1 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 1 ] ].length;

            if ( tmp.gap[ 2 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 2 ] ].length;
        }

        rc = adjust_cigar( input, &tmp, output );

    }
    return rc;
}


rc_t make_cg_merge( const cg_cigar_input * input, cg_cigar_output * output )
{
    rc_t rc;
    cg_cigar_temp tmp;
    memset( &tmp, 0, sizeof tmp );

    rc = CIGAR_to_CG_Ops( input, &tmp );
    if ( GetRCState( rc ) == rcNotFound && GetRCObject( rc ) == rcFormat )
    {
        memmove( &output->cigar[ 0 ], &input->p_cigar.ptr[ 0 ], input->p_cigar.len );
        output->cigar_len = input->p_cigar.len;
        output->cigar[ output->cigar_len ] = 0;
        output->edit_dist = input->edit_dist;
        rc = 0;
    }
    else if ( rc == 0 )
    {

        if ( tmp.CG_adjust == 0 )
        {
            if ( tmp.gap[ 0 ] < tmp.opCnt )
                tmp.CG_adjust = tmp.cigOp[ tmp.gap[ 0 ] ].length;

            if ( tmp.gap[ 1 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 1 ] ].length;

            if ( tmp.gap[ 2 ] < tmp.opCnt )
                tmp.CG_adjust += tmp.cigOp[ tmp.gap[ 2 ] ].length;
        }
    }

    if ( rc == 0 )
    {
        uint32_t const B_len = tmp.cigOp[ tmp.gap[ 0 ] ].length;
        uint32_t const B_at = tmp.gap[ 0 ] < tmp.gap[ 2 ] ? 5 : 30;
            
        if ( 0 < B_len && B_len < 5 )
        {
            memcpy( output->newSeq,  input->p_read.ptr, MAX_READ_LEN );
            memcpy( output->newQual, input->p_quality.ptr, MAX_READ_LEN );
            
            output->p_read.ptr = output->newSeq;
            output->p_read.len = ( MAX_READ_LEN - B_len );
            
            output->p_quality.ptr = output->newQual;
            output->p_quality.len = ( MAX_READ_LEN - B_len );
            
            output->p_tags.ptr = output->tags;
            output->p_tags.len = 0;

            /* nBnM -> nB0M */
            tmp.cigOp[ tmp.gap[ 0 ] + 1 ].length -= B_len;
            if ( tmp.gap[ 0 ] < tmp.gap[ 2 ] )
            {
                size_t written;
                rc = string_printf( output->tags, sizeof( output->tags ), &written,
                                    "GC:Z:%uS%uG%uS\tGS:Z:%.*s\tGQ:Z:%.*s",
                                    5 - B_len, B_len, 30 - B_len, 2 * B_len, &output->newSeq[ 5 - B_len ], 2 * B_len, &output->newQual[ 5 - B_len ] );
                if ( rc == 0 )
                    output->p_tags.len = written;
                memmove( &tmp.cigOp[ tmp.gap[ 0 ] ],
                         &tmp.cigOp[ tmp.gap[ 0 ] + 1 ],
                         ( tmp.opCnt - ( tmp.gap[ 0 ] + 1 ) ) * sizeof( tmp.cigOp[ 0 ] ) );
                --tmp.opCnt;
            }
            else
            {
                size_t written;
                rc = string_printf( output->tags, sizeof( output->tags ), &written,
                                    "GC:Z:%uS%uG%uS\tGS:Z:%.*s\tGQ:Z:%.*s",
                                    30 - B_len, B_len, 5 - B_len, 2 * B_len, &output->newSeq[ 30 - B_len ], 2 * B_len, &output->newQual[ 30 - B_len ] );
                if ( rc == 0 )
                    output->p_tags.len = written;
                memmove( &tmp.cigOp[ tmp.gap[ 0 ] ],
                         &tmp.cigOp[ tmp.gap[ 0 ] + 1 ],
                         ( tmp.opCnt - ( tmp.gap[ 0 ] + 1 ) ) * sizeof( tmp.cigOp[ 0 ] ) );
                --tmp.opCnt;
            }
            if ( rc == 0 )
            {
                uint32_t i;
                for ( i = B_at; i < B_at + B_len; ++i )
                {
                    uint32_t const Lq = output->newQual[ i - B_len ];
                    uint32_t const Rq = output->newQual[ i ];

                    if ( Lq <= Rq )
                    {
                        output->newSeq[ i - B_len ] = output->newSeq[ i ];
                        output->newQual[ i - B_len ] = Rq;
                    }
                    else
                    {
                        output->newSeq[ i ] = output->newSeq[ i - B_len ];
                        output->newQual[ i ] = Lq;
                    }
                }
                memmove( &output->newSeq [ B_at ], &output->newSeq [ B_at + B_len ], MAX_READ_LEN - B_at - B_len );
                memmove( &output->newQual[ B_at ], &output->newQual[ B_at + B_len ], MAX_READ_LEN - B_at - B_len );
            }
        }
        else
        {
            uint32_t i, len = tmp.cigOp[ tmp.gap[ 0 ] ].length;
            
            tmp.cigOp[ tmp.gap[ 0 ] ].code = 'I';
            for ( i = tmp.gap[ 0 ] + 1; i < tmp.opCnt && len > 0; ++i )
            {
                if ( tmp.cigOp[ i ].length <= len )
                {
                    len -= tmp.cigOp[ i ].length;
                    tmp.cigOp[ i ].length = 0;
                }
                else
                {
                    tmp.cigOp[ i ].length -= len;
                    len = 0;
                }
            }
            tmp.CG_adjust -= tmp.cigOp[ tmp.gap[ 0 ] ].length;
        }
    }

    if ( rc == 0 )
        rc = adjust_cigar( input, &tmp, output );

    return rc;
}

#define MAX_RNA_SPLICE_CIGOPS 200

rc_t discover_rna_splicing_candidates( uint32_t cigar_len, const char * cigar, uint32_t min_len, rna_splice_candidates * candidates )
{
    rc_t rc = 0;
    uint32_t cigops_len = cigar_len / 2 + 1;
    CigOps * cigops = malloc( ( sizeof * cigops ) * cigops_len );
    if ( cigops == NULL )
        rc = RC( rcExe, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    else
    {
        int32_t op_idx;
        uint32_t offset = 0;
        int32_t n_cigops = ExplodeCIGAR( cigops, cigops_len, cigar, cigar_len );
        candidates->count = 0;
        for ( op_idx = 0; op_idx < ( n_cigops - 1 ); op_idx++ )
        {
            char op_code = cigops[ op_idx ].op;
            uint32_t op_len = cigops[ op_idx ].oplen;
            if ( op_code == 'D' && op_len >= min_len && candidates->count < MAX_RNA_SPLICE_CANDIDATES )
            {
                rna_splice_candidate * rsc = &candidates->candidates[ candidates->count++ ];
                rsc->offset = offset;
                rsc->len = op_len;
                rsc->op_idx = op_idx;
                rsc->matched = 0;
            }
            if ( op_code == 'M' || op_code == 'X' || op_code == '=' || op_code == 'D' || op_code == 'N' )
                offset += op_len;
        }
        free( cigops );
    }
    return rc;
}


rc_t change_rna_splicing_cigar( uint32_t cigar_len, char * cigar, rna_splice_candidates * candidates )
{
    rc_t rc = 0;
    uint32_t cigops_len = cigar_len / 2;
    CigOps * cigops = malloc( ( sizeof * cigops ) * cigops_len );
    if ( cigops == NULL )
        rc = RC( rcExe, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    else
    {
        int32_t idx, dst;
        int32_t n_cigops = ExplodeCIGAR( cigops, MAX_RNA_SPLICE_CIGOPS, cigar, cigar_len );
        for ( idx = 0; idx < candidates->count; ++idx )    
        {
            rna_splice_candidate * rsc = &candidates->candidates[ idx ];
            if ( rsc->matched != 0 && cigops[ rsc->op_idx ].op == 'D' )
                cigops[ rsc->op_idx ].op = 'N';
        }

        for ( idx = 0, dst = 0; idx < ( n_cigops - 1 ) && rc == 0; ++idx )
        {
            size_t sz;
            rc = string_printf( &cigar[ dst ], cigar_len + 1 - dst, &sz, "%u%c", cigops[ idx ].oplen, cigops[ idx ].op );
            dst += sz;
        }
        free( cigops );
    }
    return rc;
}
