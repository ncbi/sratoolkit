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

#define MAX_CG_CIGAR_LEN ( ( 11 * 35 ) + 1 )
#define MAX_GC_LEN ( ( 11 * 3 ) + 1 )
#define MAX_READ_LEN ( 35 )

typedef struct cg_cigar_input
{
    const char *cigar;
    size_t cigar_len;
    bool orientation;
    uint32_t seq_req_id;
    bool edit_dist_available;
    int32_t edit_dist;
} cg_cigar_input;


typedef struct cg_cigar_output
{
    char cigar[ MAX_CG_CIGAR_LEN ];
    size_t cigar_len;
    int32_t edit_dist;
} cg_cigar_output;


rc_t make_cg_cigar( const cg_cigar_input * input, cg_cigar_output * output );

#endif