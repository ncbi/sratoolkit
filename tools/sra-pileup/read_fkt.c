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

#include "read_fkt.h"
#include <sysalloc.h>

rc_t read_bool( int64_t row_id, const VCursor * cursor, uint32_t idx, bool *res, bool dflt )
{
    const bool * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) bool failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        *res = row_len > 0 ? *value : dflt;
    }
    return rc;
}


rc_t read_int64( int64_t row_id, const VCursor * cursor, uint32_t idx, int64_t *res, int64_t dflt )
{
    const int64_t *value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) int64 failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        *res = row_len > 0 ? *value : dflt;
    }
    return rc;
}


rc_t read_char_ptr_and_size( int64_t row_id, const VCursor * cursor, uint32_t idx, const char **res, size_t *res_len )
{
    const char * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) char_ptr failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        if ( res_len != NULL )
            *res_len = ( size_t )row_len;
    }
    return rc;
}


rc_t read_u8_ptr_and_size( int64_t row_id, const VCursor * cursor, uint32_t idx, const uint8_t **res, size_t *res_len )
{
    const uint8_t * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) char_ptr failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        if ( res_len != NULL )
            *res_len = ( size_t )row_len;
    }
    return rc;
}


rc_t read_INSDC_coord_zero( int64_t row_id, const VCursor * cursor, uint32_t idx, INSDC_coord_zero *res, size_t *res_len )
{
    INSDC_coord_zero * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_coord_zero failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = *value;
        if ( res_len != NULL )
            *res_len = ( size_t )row_len;
    }
    return rc;
}


rc_t read_INSDC_coord_len( int64_t row_id, const VCursor * cursor, uint32_t idx, INSDC_coord_len *res )
{
    INSDC_coord_len * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_coord_len failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else if ( row_len > 0 )
        *res = *value;
    return rc;
}


rc_t read_uint32_t( int64_t row_id, const VCursor * cursor, uint32_t idx, uint32_t *res, uint32_t dflt )
{
    uint32_t * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) uint32_t failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        *res = ( row_len > 0 ) ? *value : dflt;
    }
    return rc;
}


rc_t read_uint32_t_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, uint32_t **res, uint32_t *len )
{
    uint32_t * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) uint32_t (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_int64_t_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, int64_t **res, uint32_t *len )
{
    int64_t * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) uint64_t (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_INSDC_read_type_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_read_type **res, uint32_t *len )
{
    const INSDC_read_type * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_read_type (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_INSDC_read_filter_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_read_filter **res, uint32_t *len )
{
    const INSDC_read_filter * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_read_filter (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_INSDC_coord_len_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_coord_len **res, uint32_t *len )
{
    const INSDC_coord_len * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_coord_len (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_INSDC_coord_zero_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_coord_zero **res, uint32_t *len )
{
    const INSDC_coord_zero * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_coord_zero (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t read_INSDC_dna_text_ptr( int64_t row_id, const VCursor * cursor, uint32_t idx, const INSDC_dna_text **res, uint32_t *len )
{
    const INSDC_dna_text * value;
    uint32_t elem_bits, boff, row_len;
    rc_t rc = VCursorCellDataDirect( cursor, row_id, idx, &elem_bits, (const void**)&value, &boff, &row_len );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorCellDataDirect( $(tr).$(ti) ) INSDC_dna_text (ptr) failed", 
            "tr=%li,ti=%u", row_id, idx ) );
    }
    else
    {
        if ( row_len > 0 )
            *res = value;
        *len = row_len;
    }
    return rc;
}


rc_t add_column( const VCursor * cursor, const char *colname, uint32_t * idx )
{
    rc_t rc = VCursorAddColumn( cursor, idx, colname );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorAddColumn( $(cn) ) failed", "cn=%s", colname ) );
    }
    return rc;
}


bool namelist_contains( const KNamelist *tables, const char * table_name )
{
    bool res = false;
    uint32_t count;
    rc_t rc = KNamelistCount( tables, &count );
    if ( rc == 0 && count > 0 )
    {
        uint32_t idx;
        size_t table_name_len = string_size( table_name );
        for ( idx = 0; idx < count && rc == 0 && !res; ++idx )
        {
            const char * name;
            rc = KNamelistGet( tables, idx, &name );
            if ( rc == 0 && name != NULL )
            {
                size_t name_len = string_size( name );
                size_t max_len = table_name_len > name_len ? table_name_len : name_len;
                int cmp = string_cmp( table_name, table_name_len, name, name_len, max_len );
                if ( cmp == 0 )
                    res = true;
            }
        }
    }
    return res;
}
