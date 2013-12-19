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

#ifndef _h_ref_regions_
#define _h_ref_regions_

#include <klib/container.h>

#ifdef __cplusplus
extern "C" {
#endif

rc_t parse_and_add_region( BSTree * regions, const char * s );

rc_t add_region( BSTree * regions, const char * name, const uint64_t start, const uint64_t end );

void check_ref_regions( BSTree * regions );

void free_ref_regions( BSTree * regions );

uint32_t count_ref_regions( BSTree * regions );

rc_t foreach_ref_region( BSTree * regions,
    rc_t ( CC * on_region ) ( const char * name, uint32_t start, uint32_t end, void *data ), 
    void *data );


struct reference_region;

const struct reference_region * get_first_ref_node( const BSTree * regions );

const struct reference_region * get_next_ref_node( const struct reference_region * node );
    
const char * get_ref_node_name( const struct reference_region * node );

uint32_t get_ref_node_range_count( const struct reference_region * node );



struct reference_range;

const struct reference_range * get_ref_range( const struct reference_region * node, uint32_t idx );

uint64_t get_ref_range_start( const struct reference_range * range );

uint64_t get_ref_range_end( const struct reference_range * range );


#ifdef __cplusplus
}
#endif

#endif /*  _h_ref_regions_ */
