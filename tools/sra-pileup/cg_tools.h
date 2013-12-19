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

#ifndef _h_cgtools_
#define _h_cgtools_

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#include <klib/rc.h>
#include <insdc/sra.h>

#define MAX_CG_CIGAR_LEN ( ( 11 * 35 ) + 1 )
#define MAX_GC_LEN ( ( 11 * 3 ) + 1 )
#define MAX_READ_LEN ( 35 )


typedef struct ptr_len
{
    const char * ptr;
    uint32_t len;
} ptr_len;

typedef struct cg_cigar_input
{
    ptr_len p_cigar;
    ptr_len p_read;
    ptr_len p_quality;

    bool orientation;
    INSDC_coord_one seq_req_id;
    bool edit_dist_available;
    int32_t edit_dist;
} cg_cigar_input;


typedef struct cg_cigar_output
{
    char cigar[ MAX_CG_CIGAR_LEN ];
    uint32_t cigar_len;

    ptr_len p_cigar;
    ptr_len p_read;
    ptr_len p_quality;
    ptr_len p_tags;

    char newSeq[ MAX_READ_LEN ];
    char newQual[ MAX_READ_LEN ];
    char tags[ MAX_CG_CIGAR_LEN * 2 ];

    int32_t edit_dist;
} cg_cigar_output;


rc_t make_cg_cigar( const cg_cigar_input * input, cg_cigar_output * output );

rc_t make_cg_merge( const cg_cigar_input * input, cg_cigar_output * output );


typedef struct CigOps
{
    char op;
    int8_t   ref_sign; /* 0;+1;-1; ref_offset = ref_sign * offset */
    int8_t   seq_sign; /* 0;+1;-1; seq_offset = seq_sign * offset */
    uint32_t oplen;
} CigOps;


int32_t ExplodeCIGAR( CigOps dst[], uint32_t len, char const cigar[], uint32_t ciglen );

uint32_t CombineCIGAR( char dst[], CigOps const seqOp[], uint32_t seq_len,
                       uint32_t refPos, CigOps const refOp[], uint32_t ref_len );


typedef struct rna_splice_candidate
{
    uint32_t offset;
    uint32_t len;
    uint32_t op_idx;
    uint32_t matched;
} rna_splice_candidate;


#define MAX_RNA_SPLICE_CANDIDATES 10

typedef struct rna_splice_candidates
{
    rna_splice_candidate candidates[ MAX_RNA_SPLICE_CANDIDATES ];
    uint32_t count, fwd_matched, rev_matched;
} rna_splice_candidates;


rc_t discover_rna_splicing_candidates( uint32_t cigar_len, const char * cigar, uint32_t min_len, rna_splice_candidates * candidates );

rc_t change_rna_splicing_cigar( uint32_t cigar_len, char * cigar, rna_splice_candidates * candidates );

#endif