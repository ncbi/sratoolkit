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

#ifndef _h_ref_walker_
#define _h_ref_walker_

#include <klib/container.h>
#include <insdc/sra.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ref_walker;


/* create the ref-walker ( not ref-counted ) */
rc_t ref_walker_create( struct ref_walker ** self );


typedef struct ref_walker_data
{
    /* for the reference - level */
    const char * ref_name;
    uint64_t ref_start;
    uint64_t ref_end;

    /* for the position - level */
    INSDC_coord_zero pos;
    uint32_t depth;
    INSDC_4na_bin bin_ref_base;
    char ascii_ref_base;

    /* for the spot-group - level */
    const char * spot_group;
    size_t spot_group_len;

    /* for the alignment - level */
    int32_t state, mapq;
    INSDC_4na_bin bin_alignment_base;
    char ascii_alignment_base;
    char quality;
    INSDC_coord_zero seq_pos;
    bool reverse, first, last, skip, match, valid;

    void * data;
} ref_walker_data;


typedef rc_t ( CC * ref_walker_callback )( ref_walker_data * rwd );


typedef struct ref_walker_callbacks
{
    ref_walker_callback on_enter_ref;
    ref_walker_callback on_exit_ref;

    ref_walker_callback on_enter_ref_window;
    ref_walker_callback on_exit_ref_window;

    ref_walker_callback on_enter_ref_pos;
    ref_walker_callback on_exit_ref_pos;

    ref_walker_callback on_enter_spot_group;
    ref_walker_callback on_exit_spot_group;

    ref_walker_callback on_alignment;
} ref_walker_callbacks;


/* set boolean / numeric parameters */
rc_t ref_walker_set_min_mapq( struct ref_walker * self, int32_t min_mapq );
rc_t ref_walker_set_omit_quality( struct ref_walker * self, bool omit_quality );
rc_t ref_walker_set_read_tlen( struct ref_walker * self, bool read_tlen );
rc_t ref_walker_set_process_dups( struct ref_walker * self, bool process_dups );
rc_t ref_walker_set_use_seq_name( struct ref_walker * self, bool use_seq_name );
rc_t ref_walker_set_no_skip( struct ref_walker * self, bool no_skip );
rc_t ref_walker_set_primary_alignments( struct ref_walker * self, bool enabled );
rc_t ref_walker_set_secondary_alignments( struct ref_walker * self, bool enabled );
rc_t ref_walker_set_evidence_alignments( struct ref_walker * self, bool enabled );
rc_t ref_walker_set_spot_group( struct ref_walker * self, const char * spot_group );

/* set callbacks */
rc_t ref_walker_set_callbacks( struct ref_walker * self, ref_walker_callbacks * callbacks );

/* add_sources and ranges */
rc_t ref_walker_add_source( struct ref_walker * self, const char * src );

rc_t ref_walker_parse_and_add_range( struct ref_walker * self, const char * range );

rc_t ref_walker_add_range( struct ref_walker * self, const char * name, const uint64_t start, const uint64_t end );


/* walk the sources/ranges by calling the supplied call-backs, passing data to the callbacks */
rc_t ref_walker_walk( struct ref_walker * self, void * data );


/* destroy the ref-walker */
rc_t ref_walker_destroy( struct ref_walker * self );
    
#ifdef __cplusplus
}
#endif

#endif /*  _h_ref_walker_ */
