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
#ifndef _h_read_fkt_
#define _h_read_fkt_

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#include <klib/vector.h>
#include <klib/out.h>
#include <klib/text.h>
#include <klib/rc.h>
#include <klib/log.h>

#include <vdb/cursor.h>
#include <insdc/sra.h>

rc_t read_bool( int64_t row_id, const VCursor * cursor, uint32_t idx, bool *res, bool dflt );
rc_t read_int64( int64_t row_id, const VCursor * cursor, uint32_t idx, int64_t *res, int64_t dflt );
rc_t read_char_ptr_and_size( int64_t row_id, const VCursor * cursor, uint32_t idx, const char **res, size_t *res_len );
rc_t read_u8_ptr_and_size( int64_t row_id, const VCursor * cursor, uint32_t idx, const uint8_t **res, size_t *res_len );
rc_t read_INSDC_coord_zero( int64_t row_id, const VCursor * cursor, uint32_t idx, INSDC_coord_zero *res, size_t *res_len );
rc_t read_INSDC_coord_len( int64_t row_id, const VCursor * cursor, uint32_t idx, INSDC_coord_len *res );
rc_t read_uint32_t( int64_t row_id, const VCursor * cursor, uint32_t idx, uint32_t *res, uint32_t dflt );
rc_t read_uint32_t_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, uint32_t **res, uint32_t *len );
rc_t read_int64_t_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, int64_t **res, uint32_t *len );
rc_t read_INSDC_read_type_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_read_type **res, uint32_t *len );
rc_t read_INSDC_read_filter_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_read_filter **res, uint32_t *len );
rc_t read_INSDC_coord_len_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_coord_len **res, uint32_t *len );
rc_t read_INSDC_coord_zero_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_coord_zero **res, uint32_t *len );
rc_t read_INSDC_dna_text_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_dna_text **res, uint32_t *len );

rc_t add_column( const VCursor * cursor, const char *colname, uint32_t * idx );
bool namelist_contains( const KNamelist *tables, const char * table_name );

#endif