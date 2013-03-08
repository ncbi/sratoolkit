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
#include "sam-unaligned.h"
#include <kapp/main.h>
#include <sysalloc.h>
#include <ctype.h>

#define COL_READ "(INSDC:dna:text)READ"
#define COL_REF_NAME "(ascii)REF_NAME"
#define COL_REF_SEQ_ID "(ascii)REF_SEQ_ID"
#define COL_REF_POS "(INSDC:coord:zero)REF_POS"

typedef struct prim_table_ctx
{
    const VCursor * cursor;

    uint32_t ref_name_idx;
    uint32_t ref_seq_id_idx;
    uint32_t ref_pos_idx;
} prim_table_ctx;


static rc_t prepare_prim_table_ctx( samdump_opts * opts, input_table * itab, prim_table_ctx * ptx )
{
    rc_t rc;

    if ( opts->cursor_cache_size == 0 )
        rc = VTableCreateCursorRead( itab->tab, &ptx->cursor );
    else
        rc = VTableCreateCachedCursorRead( itab->tab, &ptx->cursor, opts->cursor_cache_size );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VTableCreateCursorRead( PRIMARY_ALIGNMENT ) for $(tn) failed", "tn=%s", itab->path ) );
    }
    else
    {
        rc = add_column( ptx->cursor, COL_REF_NAME, &ptx->ref_name_idx );
        if ( rc == 0 )
            rc = add_column( ptx->cursor, COL_REF_SEQ_ID, &ptx->ref_seq_id_idx );
        if ( rc == 0 )
            rc = add_column( ptx->cursor, COL_REF_POS, &ptx->ref_pos_idx );
        if ( rc == 0 )
        {
            rc = VCursorOpen( ptx->cursor );
            if ( rc != 0 )
            {
                (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorOpen( PRIMARY_ALIGNMENT ) for $(tn) failed", "tn=%s", itab->path ) );
            }
        }
    }
    return rc;
}


#define COL_ALIGN_COUNT "(U8)ALIGNMENT_COUNT"
#define COL_SPOT_ID "(INSDC:SRA:spotid_t)SPOT_ID"
#define COL_PRIM_AL_ID "(I64)PRIMARY_ALIGNMENT_ID"
#define COL_READ_TYPE "(INSDC:SRA:xread_type)READ_TYPE"
#define COL_READ_FILTER "(INSDC:SRA:read_filter)READ_FILTER"
#define COL_READ_LEN "(INSDC:coord:len)READ_LEN"
#define COL_READ_START "(INSDC:coord:zero)READ_START"
/* #define COL_QUALITY "(INSDC:quality:text:phred_33)QUALITY" */
#define COL_QUALITY "(INSDC:quality:phred)QUALITY"
#define COL_SPOT_GROUP "(ascii)SPOT_GROUP"
#define COL_NAME "(ascii)NAME"

#define INVALID_COLUMN 0xFFFFFFFF

typedef struct seq_table_ctx
{
    const VCursor * cursor;

    uint32_t align_count_idx;
    uint32_t prim_al_id_idx;
    uint32_t read_type_idx;
    uint32_t read_filter_idx;
    uint32_t read_len_idx;
    uint32_t read_start_idx;
    uint32_t read_idx;
    uint32_t quality_idx;
    uint32_t spot_group_idx;
    uint32_t name_idx;
} seq_table_ctx;


static rc_t prepare_seq_table_ctx( samdump_opts * opts, input_table * itab, seq_table_ctx * stx, bool legacy )
{
    rc_t rc;

    if ( opts->cursor_cache_size == 0 )
        rc = VTableCreateCursorRead( itab->tab, &stx->cursor );
    else
        rc = VTableCreateCachedCursorRead( itab->tab, &stx->cursor, opts->cursor_cache_size );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VTableCreateCursorRead( SEQUENCE ) for $(tn) failed", "tn=%s", itab->path ) );
    }
    else
    {
        if ( legacy )
            stx->align_count_idx = INVALID_COLUMN;
        else
            rc = add_column( stx->cursor, COL_ALIGN_COUNT, &stx->align_count_idx );

        if ( rc == 0 )
        {
            if ( legacy )
                stx->prim_al_id_idx = INVALID_COLUMN;
            else
                rc = add_column( stx->cursor, COL_PRIM_AL_ID, &stx->prim_al_id_idx );
        }

        if ( rc == 0 )
        {
            if ( legacy )
                rc = add_column( stx->cursor, COL_NAME, &stx->name_idx );
            else
                stx->name_idx = INVALID_COLUMN;
        }

        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_READ_TYPE, &stx->read_type_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_READ_FILTER, &stx->read_filter_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_READ_LEN, &stx->read_len_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_READ_START, &stx->read_start_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_READ, &stx->read_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_QUALITY, &stx->quality_idx );
        if ( rc == 0 )
            rc = add_column( stx->cursor, COL_SPOT_GROUP, &stx->spot_group_idx );
    }
    return rc;
}


typedef struct seq_row
{
    uint32_t nreads;

    bool fully_unaligned;
    bool partly_unaligned;
    bool filtered_out;
} seq_row;


static rc_t complain_size_diff( int64_t row_id, const char * txt )
{
    rc_t rc = RC( rcExe, rcNoTarg, rcConstructing, rcSize, rcInvalid );
    (void)PLOGERR( klogInt, ( klogInt, rc, "in row $(rn) of SEQUENCE.$(tx)", "rn=%ld,tx=%s", row_id, txt ) );
    return rc;
}


static rc_t read_seq_row( samdump_opts * opts, seq_table_ctx * stx, seq_row * row, int64_t row_id )
{
    rc_t rc = 0;

    if ( stx->align_count_idx == INVALID_COLUMN )
    {
        const INSDC_read_type *ptr;

        row->fully_unaligned = true;
        row->partly_unaligned = false;
        row->filtered_out = false;
        rc = read_INSDC_read_type_ptr( row_id, stx->cursor, stx->read_type_idx, &ptr, &row->nreads );
    }
    else
    {
        const uint8_t * u8ptr;
        size_t u8ptr_len;

        /* ALIGNMENT_COUNT ... only to detect if reads are unaligned, could be done with PRIMARY_ALINGMENT too,
           but this one is only 8 bit instead of 64 bit, that means faster */
        rc = read_u8_ptr_and_size( row_id, stx->cursor, stx->align_count_idx, &u8ptr, &u8ptr_len );
        if ( rc == 0 )
        {
            uint32_t i, n;

            row->nreads = u8ptr_len;
            for ( i = 0, n = 0; i < u8ptr_len; ++i )
                if ( u8ptr[ i ] != 0 )
                    n++;
            row->fully_unaligned = ( n == 0 );
            row->partly_unaligned = ( n < u8ptr_len && n > 0 );

            if ( row->partly_unaligned )
                row->filtered_out = !opts->print_half_unaligned_reads;
            else if ( row->fully_unaligned )
                row->filtered_out = !opts->print_fully_unaligned_reads;
            else
                row->filtered_out = true;
        }
    }
    return rc;
}


/**********************************************************************************

    0x001   template having multiple fragments in sequencing
            (row->nreads > 1 )

    0x002   each fragment properly aligned according to the aligner
            ( never the case in an unaligned read )

    0x004   fragment unmapped
            ( always true in an unaligned read )

    0x008   next fragment in the template unmapped
            ( is the mate aligned? next: read_idx+1, if read_idx==row->nreads-1 then read_idx-1 )

    0x010   SEQ being reverse complemented
            ( column 'READ_TYPE' has bit READ_TYPE_REVERSE set )

    0x020   SEQ of the next fragment in the template being reversed
            ( is the mate reversed? )

    0x040   the first fragment in the template
            ( read_idx == 0 )

    0x080   the last fragment in the template
            ( read_idx == ( row->nreads - 1 ) )

    0x100   secondary alignment
            ( never the case in an unaligned read )

    0x200   not passing quality controls
            ( column 'READ_FILTER' has bit READ_FILTER_REJECT set )

    0x400   PCR or optical duplicate
            ( column 'READ_FILTER' has bit READ_FILTER_CRITERIA set )

**********************************************************************************/
static uint32_t calculate_unaligned_sam_flags( uint32_t nreads, uint32_t read_idx,
                                               uint32_t mate_idx, int64_t mate_id,
                                               const INSDC_read_type * read_type,
                                               const INSDC_read_filter * read_filter )
{
    uint32_t res = 0x04;

    if ( nreads > 1 )
    {
        res |= 0x01;
        /* the following test only make sense if there is a mate */

        if ( mate_id == 0 )
            res |= 0x08;
        if ( ( read_type[ mate_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE )
            res |= 0x020;
        if ( read_idx == 0 )
            res |= 0x040;
        if ( read_idx == ( nreads - 1 ) )
            res |= 0x080;
    }

    if ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE )
        res |= 0x010;
    if ( read_filter[ read_idx ] == READ_FILTER_REJECT )
        res |= 0x200;
    if ( read_filter[ read_idx ] == READ_FILTER_CRITERIA )
        res |= 0x400;
    return res;
}


const char * ref_name_star = "*";

static rc_t get_mate_info( prim_table_ctx * ptx, int64_t row_id, int64_t mate_id,
                           const char ** mate_ref_name, uint32_t * mate_ref_name_len,
                           INSDC_coord_zero *mate_ref_pos, uint32_t nreads,
                           matecache * mc, input_database * ids )
{
    rc_t rc = 0;

    *mate_ref_name = ref_name_star;
    *mate_ref_name_len = 1;
    *mate_ref_pos = 0;
    if ( nreads < 2 )
        return rc;

    if ( mate_id != 0 )
    {
        uint32_t ref_idx;
        int64_t seq_spot_id;
        rc = matecache_lookup_unaligned( mc, ids->db_idx, mate_id, mate_ref_pos, &ref_idx, &seq_spot_id );
        if ( rc == 0 )
        {
            const ReferenceObj* refobj;
            *mate_ref_pos += 1;
            /* now we have to lookup the reference-name based on it's index into the ids->reflist */
            rc = ReferenceList_Get( ids->reflist, &refobj, ref_idx );
            if ( rc != 0 )
            {
                (void)PLOGERR( klogInt, ( klogInt, rc, "in row $(rn) of SEQUENCE.$(rx)", "rn=%ld,rx=%ld", mate_id, row_id ) );
            }
            else
            {
                rc = ReferenceObj_Name( refobj, mate_ref_name );
                if ( rc != 0 )
                {
                    (void)PLOGERR( klogInt, ( klogInt, rc, "in row $(rn) of SEQUENCE.$(rx)", "rn=%ld,rx=%ld", mate_id, row_id ) );
                }
                else
                {
                    *mate_ref_name_len = strlen( *mate_ref_name );
                }
            }
        }
        else if ( GetRCState( rc ) == rcNotFound )
        {
            /* we have a mate and it is aligned! ---> look it up in the PRIMARY_ALIGNMENT - table*/
            const char * ptr;
            size_t len;

            rc = read_char_ptr_and_size( mate_id, ptx->cursor, ptx->ref_name_idx, &ptr, &len );
            if ( rc == 0 && len > 0 )
            {
                *mate_ref_name = ptr;
                *mate_ref_name_len = len;
            }

            if ( rc == 0 )
            {
                INSDC_coord_zero pos;
                rc = read_INSDC_coord_zero( mate_id, ptx->cursor, ptx->ref_pos_idx, &pos, &len );
                if ( rc == 0 && len > 0 )
                {
                    *mate_ref_pos = ( pos + 1 );
                }
            }
        }
    }
    return rc;
}


static rc_t print_sliced_read( const INSDC_dna_text * read, uint32_t read_idx,
                               bool reverse, const INSDC_coord_zero * read_start, const INSDC_coord_len * read_len )
{
    rc_t rc = 0;
    const INSDC_dna_text * ptr = read + read_start[ read_idx ];
    if ( !reverse )
    {
        rc = KOutMsg( "%.*s", read_len[ read_idx ], ptr );
    }
    else
    {
        const char cmp_tbl [] =
        {
            'T', 'V', 'G', 'H', 'E', 'F', 'C', 'D',
            'I', 'J', 'M', 'L', 'K', 'N', 'O', 'P',
            'Q', 'Y', 'S', 'A', 'U', 'B', 'W', 'X',
            'R', 'Z'
        };

        int32_t i = ( read_len[ read_idx ] - 1 );
        while( i >= 0 && rc == 0 )
        {
            int c = ptr [ i ];
            if ( isalpha ( c ) )
            {
                if ( islower ( c )  )
                     c = tolower ( cmp_tbl [ toupper ( c ) - 'A' ] );
                else
                     c = cmp_tbl [ c - 'A' ];
            }

            rc = KOutMsg( "%c", ( char ) c );
            i--;
        }
    }
    return rc;
}


static rc_t print_sliced_quality( samdump_opts * opts, const char * quality, uint32_t read_idx,
                                  bool reverse, const INSDC_coord_zero * read_start, const INSDC_coord_len * read_len )
{
    const char * ptr = quality + read_start[ read_idx ];
    return dump_quality( opts, ptr, read_len[ read_idx ], reverse ); /* sam-dump-opts.c */
}


static rc_t dump_the_other_read( int64_t row_id, seq_table_ctx * stx, prim_table_ctx * ptx, uint32_t mate_idx )
{
    uint32_t row_len;
    int64_t *prim_al_id_ptr;

    /* read from the SEQUENCE-table the value of the colum "PRIMARY_ALIGNMENT_ID"[ mate_idx ] */
    rc_t rc = read_int64_t_ptr( row_id, stx->cursor, stx->prim_al_id_idx, &prim_al_id_ptr, &row_len );
    if ( rc == 0 )
    {
        if ( row_len == 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "rowlen zero in row $(rn) of SEQUENCE.PRIMARY_ALIGNMENT_ID", "rn=%ld", row_id ) );
        }
        else if ( mate_idx >= row_len )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "mate_idx invalid in row $(rn) of SEQUENCE.PRIMARY_ALIGNMENT_ID", "rn=%ld", row_id ) );
        }
        else
        {
            /* read from the PRIMARY_ALIGNMENT_TABE the value of the columns "REF_NAME" and "REF_POS" */
            int64_t a_row_id = prim_al_id_ptr[ mate_idx ];
            if ( a_row_id == 0 )
            {
                rc = KOutMsg( "*\t0\t" );
            }
            else
            {
                const char * ref_name;
                size_t ref_name_len;
                rc = read_char_ptr_and_size( a_row_id, ptx->cursor, ptx->ref_name_idx, &ref_name, &ref_name_len );
                if ( rc == 0 )
                {
                    if ( ref_name_len == 0 )
                    {
                        (void)PLOGERR( klogInt, ( klogInt, rc, "rowlen zero in row $(rn) of PRIM.REF_NAME", "rn=%ld", a_row_id ) );
                    }
                    else
                    {
                        const INSDC_coord_zero *ref_pos;
                        rc = read_INSDC_coord_zero_ptr( a_row_id, ptx->cursor, ptx->ref_pos_idx, &ref_pos, &row_len );
                        if ( rc == 0 )
                        {
                            rc = KOutMsg( "%.*s\t%i\t", ref_name_len, ref_name, ref_pos[ 0 ] + 1 );
                        }
                    }
                }
            }
        }
    }
    return rc;
}


static uint32_t calc_mate_idx( uint32_t n_reads, uint32_t read_idx )
{
    if ( read_idx == ( n_reads - 1 ) )
        return ( read_idx - 1 );
    else
        return ( read_idx + 1 );
}


static rc_t read_quality( seq_table_ctx * stx, int64_t row_id, const char **quality, uint32_t read_len )
{
    size_t quality_len;
    rc_t rc = read_char_ptr_and_size( row_id, stx->cursor, stx->quality_idx, quality, &quality_len );
    if ( rc == 0 && read_len != quality_len )
        rc = complain_size_diff( row_id, "QUALITY" );
    return rc;
}


static rc_t read_read_type( seq_table_ctx * stx, int64_t row_id, const INSDC_read_type **read_type, uint32_t read_len )
{
    uint32_t read_type_len;
    rc_t rc = read_INSDC_read_type_ptr( row_id, stx->cursor, stx->read_type_idx, read_type, &read_type_len );
    if ( rc == 0 && read_type_len != read_len )
        rc = complain_size_diff( row_id, "READ_TYPE" );
    return rc;
}


static rc_t read_read_filter( seq_table_ctx * stx, int64_t row_id, const INSDC_read_filter **read_filter, uint32_t read_len )
{
    uint32_t read_filter_len;
    rc_t rc = read_INSDC_read_filter_ptr( row_id, stx->cursor, stx->read_filter_idx, read_filter, &read_filter_len );
    if ( rc == 0 && read_filter_len != read_len )
        rc = complain_size_diff( row_id, "READ_FILTER" );
    return rc;
}


static rc_t read_read_start( seq_table_ctx * stx, int64_t row_id, const INSDC_coord_zero **read_start, uint32_t nreads )
{
    uint32_t read_start_len;
    rc_t rc = read_INSDC_coord_zero_ptr( row_id, stx->cursor, stx->read_start_idx, read_start, &read_start_len );
    if ( rc == 0 && nreads != read_start_len )
        rc = complain_size_diff( row_id, "READ_START" );
    return rc;
}


static rc_t read_read_len( seq_table_ctx * stx, int64_t row_id, const INSDC_coord_len **read_len, uint32_t nreads )
{
    uint32_t read_len_len;
    rc_t rc = read_INSDC_coord_len_ptr( row_id, stx->cursor, stx->read_len_idx, read_len, &read_len_len );
    if ( rc == 0 && nreads != read_len_len )
        rc = complain_size_diff( row_id, "READ_LEN" );
    return rc;
}


static rc_t dump_seq_row_sam_filtered( samdump_opts * opts, seq_table_ctx * stx, prim_table_ctx * ptx,
                                       int64_t row_id, uint32_t nreads, uint32_t * printed, matecache * mc, input_database * ids )
{
    uint32_t read_idx, rd_len, prim_align_ids_len;
    int64_t * prim_align_ids = NULL;
    const char * spot_group = NULL;
    const char * quality = NULL;
    const INSDC_dna_text * read = NULL;
    const INSDC_read_type * read_type = NULL;
    const INSDC_read_filter * read_filter = NULL;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t spot_group_len;

    rc_t rc = read_int64_t_ptr( row_id, stx->cursor, stx->prim_al_id_idx, &prim_align_ids, &prim_align_ids_len );
    if ( rc == 0 && nreads != prim_align_ids_len )
        rc = complain_size_diff( row_id, "PRIMARY_ALIGNMENT_ID" );
    if ( rc == 0 )
        rc = read_read_len( stx, row_id, &read_len, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        int64_t align_id = prim_align_ids[ read_idx ];
        if ( align_id == 0 &&              /* read is NOT aligned! */
             read_len[ read_idx ] > 0 )    /* and has a length! */
        {
            uint32_t mate_idx = calc_mate_idx( nreads, read_idx );

            /*  here we have to find out if the read is actually requested: 
                -----------------------------------------------------------
                read from the SEQ-table the ID of the mate in PRIM-table
                look if we have the requested ID in the mate-cache, if not here it ends...
            */
            align_id = prim_align_ids[ mate_idx ];
            if ( align_id != 0 )
            {
                uint32_t ref_idx;
                INSDC_coord_zero mate_ref_pos;
                int64_t seq_spot_id;
                rc = matecache_lookup_unaligned( mc, ids->db_idx, align_id, &mate_ref_pos, &ref_idx, &seq_spot_id );
                if ( rc == 0 )
                {
                    const ReferenceObj* refobj;
                    /* now we have to lookup the reference-name based on it's index into the ids->reflist */
                    rc = ReferenceList_Get( ids->reflist, &refobj, ref_idx );
                    if ( rc != 0 )
                    {
                        (void)PLOGERR( klogInt, ( klogInt, rc, "in row $(rn) of SEQUENCE.$(rx)", "rn=%ld,rx=%ld", mate_idx, row_id ) );
                    }
                    else
                    {
                        const char * mate_ref_name;
                        rc = ReferenceObj_Name( refobj, &mate_ref_name );
                        if ( rc != 0 )
                        {
                            (void)PLOGERR( klogInt, ( klogInt, rc, "in row $(rn) of SEQUENCE.$(rx)", "rn=%ld,rx=%ld", mate_idx, row_id ) );
                        }
                        else
                        {
                            bool reverse = opts -> reverse_unaligned_reads;

                            /* SAM-FIELD: QNAME     SRA-column: SPOT_ID ( int64 ) */
                            if ( rc == 0 )
                                rc = KOutMsg( "%ld\t", seq_spot_id );

                            if ( rc == 0 && read_type == NULL )
                                rc = read_read_type( stx, row_id, &read_type, nreads );
                            if ( rc == 0 )
                                reverse &= ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
                            if ( rc == 0 && read_filter == NULL )
                                rc = read_read_filter( stx, row_id, &read_filter, nreads );

                            /* SAM-FIELD: FLAG      SRA-column: calculated from READ_TYPE, READ_FILTER etc. */
                            if ( rc == 0 )
                            {
                                uint32_t sam_flags = calculate_unaligned_sam_flags( nreads, read_idx, mate_idx, 
                                                                                    align_id, read_type, read_filter );
                                rc = KOutMsg( "%u\t", sam_flags );
                            }

                            /* SAM-FIELD: RNAME     SRA-column: none, fix '*' */
                            /* SAM-FIELD: POS       SRA-column: none, fix '0' */
                            /* SAM-FIELD: MAPQ      SRA-column: none, fix '0' */
                            /* SAM-FIELD: CIGAR     SRA-column: none, fix '*' */
                            if ( rc == 0 )
                                rc = KOutMsg( "*\t0\t0\t*\t" );

                            /* SAM-FIELD: RNEXT     SRA-column: found in cache */
                            /* SAM-FIELD: POS       SRA-column: found in cache */
                            if ( rc == 0 )
                                rc = KOutMsg( "%s\t%li\t", mate_ref_name, mate_ref_pos + 1 );

                            /* SAM-FIELD: TLEN      SRA-column: none, fix '0' */
                            if ( rc == 0 )
                                rc = KOutMsg( "0\t" );

                            if ( rc == 0 && read == NULL )
                                rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
                            if ( rc == 0 && quality == NULL )
                                rc = read_quality( stx, row_id, &quality, rd_len );
                            if ( rc == 0 && read_start == NULL )
                                rc = read_read_start( stx, row_id, &read_start, nreads );

                            /* SAM-FIELD: SEQ       SRA-column: READ, sliced by READ_START/READ_LEN */
                            if ( rc == 0 )
                                rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
                            if ( rc == 0 )
                                rc = KOutMsg( "\t" );

                            /* SAM-FIELD: QUAL      SRA-column: QUALITY, sliced by READ_START/READ_LEN */
                            if ( rc == 0 )
                                rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );

                            /* OPT SAM-FIIELD:      SRA-column: ALIGN_ID */
                            if ( rc == 0 && opts->print_alignment_id_in_column_xi )
                                rc = KOutMsg( "\tXI:i:%u", row_id );

                            /* OPT SAM-FIIELD:      SRA-column: SPOT_GROUP */
                            if ( rc == 0 && spot_group == NULL )
                                rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );
                            if ( rc == 0 )
                                rc = KOutMsg( "\tRG:Z:%.*s", spot_group_len, spot_group );

                            if ( rc == 0 )
                                rc = KOutMsg( "\n" );

                            if ( rc == 0 )
                                (*printed)++;

                        }
                    }
                }
                else
                {
                    rc = 0;     /* it is OK if an alignment is not found in the cache, do not terminate! */
                }
            }
        }
    }
    return rc;
}


static rc_t dump_seq_prim_row_sam( samdump_opts * opts, seq_table_ctx * stx, prim_table_ctx * ptx,
                                   int64_t row_id, uint32_t nreads, uint32_t * printed, matecache * mc, input_database * ids )
{
    uint32_t read_idx, rd_len, prim_align_ids_len;
    int64_t * prim_align_ids = NULL;
    const char * spot_group = NULL;
    const char * quality = NULL;
    const INSDC_dna_text * read = NULL;
    const INSDC_read_type * read_type = NULL;
    const INSDC_read_filter * read_filter = NULL;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t spot_group_len;

    rc_t rc = read_int64_t_ptr( row_id, stx->cursor, stx->prim_al_id_idx, &prim_align_ids, &prim_align_ids_len );
    if ( rc == 0 && nreads != prim_align_ids_len )
        rc = complain_size_diff( row_id, "PRIMARY_ALIGNMENT_ID" );
    if ( rc == 0 )
        rc = read_read_len( stx, row_id, &read_len, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        if ( prim_align_ids[ read_idx ] == 0 &&     /* read is NOT aligned! */
             read_len[ read_idx ] > 0 )             /* and has a length! */
        {
            bool reverse;
            uint32_t mate_idx;

            if ( read_idx == ( nreads - 1 ) )
                mate_idx = read_idx - 1;
            else
                mate_idx = read_idx + 1;

            if ( rc == 0 && read_type == NULL )
                rc = read_read_type( stx, row_id, &read_type, nreads );
            if ( rc == 0 )
                reverse = ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
            if ( rc == 0 && read_filter == NULL )
                rc = read_read_filter( stx, row_id, &read_filter, nreads );

            /* SAM-FIELD: QNAME     SRA-column: SPOT_ID ( int64 ) */
            if ( rc == 0 )
                rc = KOutMsg( "%ld\t", row_id );

            /* SAM-FIELD: FLAG      SRA-column: calculated from READ_TYPE, READ_FILTER etc. */
            if ( rc == 0 )
            {
                uint32_t sam_flags = calculate_unaligned_sam_flags( nreads, read_idx, mate_idx, 
                                            prim_align_ids[ mate_idx ], read_type, read_filter );
                rc = KOutMsg( "%u\t", sam_flags );
            }

            /* SAM-FIELD: RNAME     SRA-column: none, fix '*' */
            /* SAM-FIELD: POS       SRA-column: none, fix '0' */
            /* SAM-FIELD: MAPQ      SRA-column: none, fix '0' */
            /* SAM-FIELD: CIGAR     SRA-column: none, fix '*' */
            if ( rc == 0 )
                rc = KOutMsg( "*\t0\t0\t*\t" );

            /* SAM-FIELD: RNEXT     SRA-column: look up in cache, or none */
            /* SAM-FIELD: POS       SRA-column: look up in cache, or none */
            if ( rc == 0 )
            {
                if ( ptx != NULL && mc != NULL && ids != NULL )
                {
                    const char * mate_ref_name;
                    uint32_t mate_ref_name_len;
                    INSDC_coord_zero mate_ref_pos;
                    int64_t mate_id = prim_align_ids[ mate_idx ];
                    rc = get_mate_info( ptx, row_id, mate_id, &mate_ref_name, &mate_ref_name_len, &mate_ref_pos, nreads, mc, ids );
                    if ( rc == 0 )
                        rc = KOutMsg( "%.*s\t%li\t", mate_ref_name_len, mate_ref_name, mate_ref_pos );
                }
                else
                {
                    /* print the mate info */
                    rc = dump_the_other_read( row_id, stx, ptx, mate_idx );
                }
            }


            /* SAM-FIELD: TLEN      SRA-column: none, fix '0' */
            if ( rc == 0 )
                rc = KOutMsg( "0\t" );

            if ( rc == 0 && read == NULL )
                rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
            if ( rc == 0 && read_start == NULL )
                rc = read_read_start( stx, row_id, &read_start, nreads );

            /* SAM-FIELD: SEQ       SRA-column: READ, sliced by READ_START/READ_LEN */
            if ( rc == 0 )
                rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
            if ( rc == 0 )
                rc = KOutMsg( "\t" );

            if ( rc == 0 && quality == NULL )
                rc = read_quality( stx, row_id, &quality, rd_len );

            /* SAM-FIELD: QUAL      SRA-column: QUALITY, sliced by READ_START/READ_LEN */
            if ( rc == 0 )
                rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );

            /* OPT SAM-FIIELD:      SRA-column: ALIGN_ID */
            if ( rc == 0 && opts->print_alignment_id_in_column_xi )
                rc = KOutMsg( "\tXI:i:%u", row_id );

            /* OPT SAM-FIIELD:      SRA-column: SPOT_GROUP */
            if ( rc == 0 && spot_group == NULL )
                rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );
            if ( rc == 0 )
                rc = KOutMsg( "\tRG:Z:%.*s", spot_group_len, spot_group );

            if ( rc == 0 )
                rc = KOutMsg( "\n" );

            if ( rc == 0 )
                (*printed)++;
        }
    }
    return rc;
}


static rc_t dump_seq_row_sam( samdump_opts * opts, seq_table_ctx * stx, int64_t row_id, uint32_t nreads, uint32_t * printed )
{
    uint32_t read_idx, rd_len;
    const char * spot_group = NULL;
    const char * quality = NULL;
    const char * name;
    const INSDC_dna_text * read = NULL;
    const INSDC_read_type * read_type;
    const INSDC_read_filter * read_filter = NULL;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t spot_group_len, name_len;

    rc_t rc = read_read_len( stx, row_id, &read_len, nreads );
    if ( rc == 0 )
        rc = read_char_ptr_and_size( row_id, stx->cursor, stx->name_idx, &name, &name_len );
    if ( rc == 0 )
        rc = read_read_type( stx, row_id, &read_type, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        if ( ( read_len[ read_idx ] > 0 ) &&             /* has a length! */
             ( ( read_type[ read_idx ] & READ_TYPE_BIOLOGICAL ) == READ_TYPE_BIOLOGICAL ) )
        {
            bool reverse;
            uint32_t mate_idx;

            if ( read_idx == ( nreads - 1 ) )
                mate_idx = read_idx - 1;
            else
                mate_idx = read_idx + 1;

            if ( rc == 0 ) /* types in interfaces/insdc/insdc.h */
                reverse = ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
            if ( rc == 0 && read_filter == NULL )
                rc = read_read_filter( stx, row_id, &read_filter, nreads );

            /* SAM-FIELD: QNAME     SRA-column: SPOT_ID ( int64 ) */
            if ( rc == 0 )
                rc = KOutMsg( "%.*s\t", name_len, name );

            /* SAM-FIELD: FLAG      SRA-column: calculated from READ_TYPE, READ_FILTER etc. */
            if ( rc == 0 )
            {
                uint32_t sam_flags = calculate_unaligned_sam_flags( nreads, read_idx, mate_idx, 0, read_type, read_filter );
                rc = KOutMsg( "%u\t", sam_flags );
            }

            /* SAM-FIELD: RNAME     SRA-column: none, fix '*' */
            /* SAM-FIELD: POS       SRA-column: none, fix '0' */
            /* SAM-FIELD: MAPQ      SRA-column: none, fix '0' */
            /* SAM-FIELD: CIGAR     SRA-column: none, fix '*' */
            /* SAM-FIELD: RNEXT     SRA-column: none, fix '*' */
            /* SAM-FIELD: POS       SRA-column: none, fix '0' */
            /* SAM-FIELD: TLEN      SRA-column: none, fix '0' */

            if ( rc == 0 )
                rc = KOutMsg( "*\t0\t0\t*\t*\t0\t0\t" );

            if ( rc == 0 && read == NULL )
                rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
            if ( rc == 0 && read_start == NULL )
                rc = read_read_start( stx, row_id, &read_start, nreads );

            /* SAM-FIELD: SEQ       SRA-column: READ, sliced by READ_START/READ_LEN */
            if ( rc == 0 )
                rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
            if ( rc == 0 )
                rc = KOutMsg( "\t" );

            if ( rc == 0 && quality == NULL )
                rc = read_quality( stx, row_id, &quality, rd_len );

            /* SAM-FIELD: QUAL      SRA-column: QUALITY, sliced by READ_START/READ_LEN */
            if ( rc == 0 )
                rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );

            /* OPT SAM-FIIELD:      SRA-column: ALIGN_ID */
            if ( rc == 0 && opts->print_alignment_id_in_column_xi )
                rc = KOutMsg( "\tXI:i:%u", row_id );

            /* OPT SAM-FIIELD:      SRA-column: SPOT_GROUP */
            if ( rc == 0 && spot_group == NULL )
                rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );
            if ( rc == 0 && ( spot_group != NULL ) && ( spot_group_len > 0 ) )
                rc = KOutMsg( "\tRG:Z:%.*s", spot_group_len, spot_group );

            if ( rc == 0 )
                rc = KOutMsg( "\n" );

            if ( rc == 0 )
                (*printed)++;
        }
    }
    return rc;
}


static rc_t dump_seq_row_fastx_filtered( samdump_opts * opts, seq_table_ctx * stx,
                                         int64_t row_id, uint32_t nreads, uint32_t * printed,
                                         matecache * mc, input_database * ids )
{
    uint32_t read_idx, rd_len, prim_align_ids_len;
    int64_t * prim_align_ids = NULL;
    const char * quality = NULL;
    const INSDC_dna_text * read = NULL;
    const char * spot_group = NULL;
    const INSDC_read_type * read_type = NULL;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t spot_group_len = 0;

    rc_t rc = read_int64_t_ptr( row_id, stx->cursor, stx->prim_al_id_idx, &prim_align_ids, &prim_align_ids_len );
    if ( rc == 0 && nreads != prim_align_ids_len )
        rc = complain_size_diff( row_id, "PRIMARY_ALIGNMENT_ID" );
    if ( rc == 0 )
        rc = read_read_len( stx, row_id, &read_len, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        if ( prim_align_ids[ read_idx ] == 0 &&    /* read is NOT aligned! */
             read_len[ read_idx ] > 0 )            /* and has a length! */
        {
            uint32_t mate_idx = calc_mate_idx( nreads, read_idx );
            int64_t align_id = prim_align_ids[ mate_idx ];
            if ( align_id != 0 )
            {
                uint32_t ref_idx;
                INSDC_coord_zero mate_ref_pos;
                int64_t seq_spot_id;
                rc = matecache_lookup_unaligned( mc, ids->db_idx, align_id, &mate_ref_pos, &ref_idx, &seq_spot_id );
                if ( rc == 0 )
                {
                    bool reverse;

                    /* the NAME */
                    if ( opts->output_format == of_fastq )
                        rc = KOutMsg( "@" );
                    else
                        rc = KOutMsg( ">" );

                    if ( rc == 0 )
                    {
                        if ( opts->print_spot_group_in_name && spot_group == NULL )
                            rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );
                        if ( rc == 0 )
                            rc = dump_name( opts, seq_spot_id, spot_group, spot_group_len ); /* sam-dump-opts.c */
                        if ( rc == 0 )
                            rc = KOutMsg( "/%u unaligned\n", read_idx + 1 );
                    }

                    if ( rc == 0 && read == NULL )
                        rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
                    if ( rc == 0 && quality == NULL )
                        rc = read_quality( stx, row_id, &quality, rd_len );
                    if ( rc == 0 && read_type == NULL )
                        rc = read_read_type( stx, row_id, &read_type, nreads );
                    if ( rc == 0 )
                        reverse = ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
                    if ( rc == 0 && read_start == NULL )
                        rc = read_read_start( stx, row_id, &read_start, nreads );

                    /* the READ */
                    if ( rc == 0 )
                        rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
                    if ( rc == 0 )
                        rc = KOutMsg( "\n" );

                    /* in case of fastq : the QUALITY-line */
                    if ( rc == 0 && opts->output_format == of_fastq )
                    {
                        rc = KOutMsg( "+\n" );
                        if ( rc == 0 )
                            rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );
                        if ( rc == 0 )
                            rc = KOutMsg( "\n" );
                    }
                    (*printed)++;
                }
                else
                {
                    rc = 0;
                }
            }
        }
    }
    return rc;
}


static rc_t dump_seq_row_fastx( samdump_opts * opts, seq_table_ctx * stx, int64_t row_id, uint32_t nreads, uint32_t * printed )
{
    uint32_t read_idx, rd_len, prim_align_ids_len;
    int64_t * prim_align_ids = NULL;
    const char * quality = NULL;
    const INSDC_dna_text * read = NULL ;
    const char * spot_group = NULL;
    const INSDC_read_type * read_type = NULL;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t spot_group_len = 0;

    rc_t rc = read_int64_t_ptr( row_id, stx->cursor, stx->prim_al_id_idx, &prim_align_ids, &prim_align_ids_len );
    if ( rc == 0 && nreads != prim_align_ids_len )
        rc = complain_size_diff( row_id, "PRIMARY_ALIGNMENT_ID" );
    if ( rc == 0 )
        rc = read_read_len( stx, row_id, &read_len, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        if ( prim_align_ids[ read_idx ] == 0 &&    /* read is NOT aligned! */
             read_len[ read_idx ] > 0 )            /* and has a length! */
        {
            bool reverse;

            /* the NAME */
            if ( opts->output_format == of_fastq )
                rc = KOutMsg( "@" );
            else
                rc = KOutMsg( ">" );
            if ( rc == 0 )
            {
                if ( opts->print_spot_group_in_name && spot_group == NULL )
                    rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );
                if ( rc == 0 )
                    rc = dump_name( opts, row_id, spot_group, spot_group_len ); /* sam-dump-opts.c */
                if ( rc == 0 )
                    rc = KOutMsg( "/%u unaligned\n", read_idx + 1 );
            }

            if ( rc == 0 && read == NULL )
                rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
            if ( rc == 0 && quality == NULL )
                rc = read_quality( stx, row_id, &quality, rd_len );
            if ( rc == 0 && read_type == NULL )
                rc = read_read_type( stx, row_id, &read_type, nreads );
            if ( rc == 0 )
                reverse = ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
            if ( rc == 0 && read_start == NULL )
                rc = read_read_start( stx, row_id, &read_start, nreads );

            /* the READ */
            if ( rc == 0 )
                rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
            if ( rc == 0 )
                rc = KOutMsg( "\n" );

            /* in case of fastq : the QUALITY-line */
            if ( rc == 0 && opts->output_format == of_fastq )
            {
                rc = KOutMsg( "+\n" );
                if ( rc == 0 )
                    rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );
                if ( rc == 0 )
                    rc = KOutMsg( "\n" );
            }
            (*printed)++;
        }
    }
    return rc;
}


static rc_t dump_seq_tab_row_fastx( samdump_opts * opts, seq_table_ctx * stx, int64_t row_id, uint32_t nreads, uint32_t * printed )
{
    uint32_t read_idx, rd_len;
    const char * quality = NULL;
    const char * name;
    const INSDC_dna_text * read = NULL ;
    const char * spot_group = NULL;
    const INSDC_read_type * read_type;
    const INSDC_coord_zero * read_start = NULL;
    const INSDC_coord_len * read_len;
    size_t name_len, spot_group_len = 0;

    rc_t rc = read_read_len( stx, row_id, &read_len, nreads );
    if ( rc == 0 )
        rc = read_char_ptr_and_size( row_id, stx->cursor, stx->name_idx, &name, &name_len );
    if ( rc == 0 )
        rc = read_read_type( stx, row_id, &read_type, nreads );

    for ( read_idx = 0; ( read_idx < nreads ) && ( rc == 0 ); ++read_idx )
    {
        if ( ( read_len[ read_idx ] > 0 ) &&            /* has a length! */
             ( ( read_type[ read_idx ] & READ_TYPE_BIOLOGICAL ) == READ_TYPE_BIOLOGICAL ) )
        {
            bool reverse;

            /* the NAME */
            if ( opts->output_format == of_fastq )
                rc = KOutMsg( "@" );
            else
                rc = KOutMsg( ">" );
            if ( rc == 0 )
            {
                if ( opts->print_spot_group_in_name && spot_group == NULL )
                    rc = read_char_ptr_and_size( row_id, stx->cursor, stx->spot_group_idx, &spot_group, &spot_group_len );

                if ( rc == 0 )
                    rc = dump_name_legacy( opts, name, name_len, spot_group, spot_group_len ); /* sam-dump-opts.c */

                if ( rc == 0 )
                    rc = KOutMsg( "/%u unaligned\n", read_idx + 1 );
            }

            if ( rc == 0 && read == NULL )
                rc = read_INSDC_dna_text_ptr( row_id, stx->cursor, stx->read_idx, &read, &rd_len );
            if ( rc == 0 && quality == NULL )
                rc = read_quality( stx, row_id, &quality, rd_len );
            if ( rc == 0 )
                reverse = ( ( read_type[ read_idx ] & READ_TYPE_REVERSE ) == READ_TYPE_REVERSE );
            if ( rc == 0 && read_start == NULL )
                rc = read_read_start( stx, row_id, &read_start, nreads );

            /* the READ */
            if ( rc == 0 )
                rc = print_sliced_read( read, read_idx, reverse, read_start, read_len );
            if ( rc == 0 )
                rc = KOutMsg( "\n" );

            /* in case of fastq : the QUALITY-line */
            if ( rc == 0 && opts->output_format == of_fastq )
            {
                if ( quality == NULL )
                    rc = read_quality( stx, row_id, &quality, rd_len );
                if ( rc == 0 )
                    rc = KOutMsg( "+\n" );
                if ( rc == 0 )
                    rc = print_sliced_quality( opts, quality, read_idx, reverse, read_start, read_len );
                if ( rc == 0 )
                    rc = KOutMsg( "\n" );
            }
            (*printed)++;
        }
    }
    return rc;
}


static rc_t print_unaligned_filtered( samdump_opts * opts, input_table * seq,
                                      input_table * prim, matecache * mc, input_database * ids )
{
    seq_table_ctx stx;
    rc_t rc = prepare_seq_table_ctx( opts, seq, &stx, ( prim == NULL ) );
    if ( rc == 0 )
    {
        rc = VCursorOpen( stx.cursor );
        if ( rc != 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorOpen( SEQUENCE ) for $(tn) failed", "tn=%s", seq->path ) );
        }
        else
        {
            int64_t first_row;
            uint64_t row_count;
            rc = VCursorIdRange( stx.cursor, stx.prim_al_id_idx, &first_row, &row_count );
            if ( rc != 0 )
            {
                (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorIdRange( SEQUENCE ) for $(tn) failed", "tn=%s", seq->path ) );
            }
            else
            {
                prim_table_ctx ptx;
                rc = prepare_prim_table_ctx( opts, prim, &ptx );
                if ( rc == 0 )
                {
                    int64_t row_id;
                    seq_row row;

                    opts->rows_so_far = 0;
                    for ( row_id = first_row; ( ( row_id - first_row ) < row_count ) && rc == 0 && !test_limit_reached( opts ); ++row_id )
                    {
                        rc = Quitting();
                        if ( rc == 0 )
                        {
                            rc = read_seq_row( opts, &stx, &row, row_id );
                            if ( rc == 0 && !row.filtered_out )
                            {
                                uint32_t printed = 0;
                                switch( opts->output_format )
                                {
                                    case of_sam   : rc = dump_seq_row_sam_filtered( opts, &stx, &ptx, row_id, row.nreads, &printed, mc, ids ); break;
                                    case of_fasta : /* fall through intended ! */
                                    case of_fastq : rc = dump_seq_row_fastx_filtered( opts, &stx, row_id, row.nreads, &printed, mc, ids ); break;
                                }
                                opts->rows_so_far += printed;
                            }
                        }
                    }
                    VCursorRelease( ptx.cursor );
                }
            }
        }
        VCursorRelease( stx.cursor );
    }
    return rc;
}


static rc_t print_unaligned_full_loop( samdump_opts * opts, input_table * seq,
                                       seq_table_ctx * stx, prim_table_ctx * ptx,
                                       matecache * mc, input_database * ids )
{
    int64_t first_row, row_id;
    uint64_t row_count;
    rc_t rc = VCursorIdRange( stx->cursor, stx->read_type_idx, &first_row, &row_count );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorIdRange( SEQUENCE ) for $(tn) failed", "tn=%s", seq->path ) );
    }
    else
    {
        seq_row row;
        opts->rows_so_far = 0;
        for ( row_id = first_row; ( ( row_id - first_row ) < row_count ) && rc == 0 && !test_limit_reached( opts ); ++row_id )
        {
            rc = Quitting();
            if ( rc == 0 )
            {
                rc = read_seq_row( opts, stx, &row, row_id );
                if ( rc == 0 && !row.filtered_out )
                {
                    uint32_t printed = 0;
                    switch( opts->output_format )
                    {
                        case of_sam   : rc = dump_seq_prim_row_sam( opts, stx, ptx, row_id, row.nreads, &printed, mc, ids ); break;
                        case of_fasta : /* fall through intended ! */
                        case of_fastq : rc = dump_seq_row_fastx( opts, stx, row_id, row.nreads, &printed ); break;
                    }
                    opts->rows_so_far += printed;
                }
            }
        }
    }
    return rc;
}


static rc_t print_seqtable_full_loop( samdump_opts * opts, input_table * seq,  seq_table_ctx * stx )
{
    int64_t first_row, row_id;
    uint64_t row_count;
    rc_t rc = VCursorIdRange( stx->cursor, stx->read_type_idx, &first_row, &row_count );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorIdRange( SEQUENCE ) for $(tn) failed", "tn=%s", seq->path ) );
    }
    else
    {
        seq_row row;
        opts->rows_so_far = 0;
        for ( row_id = first_row; ( ( row_id - first_row ) < row_count ) && rc == 0 && !test_limit_reached( opts ); ++row_id )
        {
            rc = Quitting();
            if ( rc == 0 )
            {
                rc = read_seq_row( opts, stx, &row, row_id );
                if ( rc == 0 && !row.filtered_out )
                {
                    uint32_t printed = 0;
                    switch( opts->output_format )
                    {
                        case of_sam   : rc = dump_seq_row_sam( opts, stx, row_id, row.nreads, &printed ); break;
                        case of_fasta : /* fall through intended ! */
                        case of_fastq : rc = dump_seq_tab_row_fastx( opts, stx, row_id, row.nreads, &printed ); break;
                    }
                    opts->rows_so_far += printed;
                }
            }
        }
    }
    return rc;
}


static rc_t print_unaligned_full( samdump_opts * opts, input_table * seq,
                                  input_table * prim, matecache * mc, input_database * ids )
{
    seq_table_ctx stx;
    rc_t rc = prepare_seq_table_ctx( opts, seq, &stx, ( prim == NULL ) );
    if ( rc == 0 )
    {
        rc = VCursorOpen( stx.cursor );
        if ( rc != 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorOpen( SEQUENCE ) for $(tn) failed", "tn=%s", seq->path ) );
        }
        else
        {
            if ( prim != NULL )
            {
                /* we are printing from a sra-database ... */
                prim_table_ctx ptx;
                rc = prepare_prim_table_ctx( opts, prim, &ptx );
                if ( rc == 0 )
                {
                    rc = print_unaligned_full_loop( opts, seq, &stx, &ptx, mc, ids );
                    VCursorRelease( ptx.cursor );
                }
            }
            else
            {
                /* we are printing a legacy table ... */
                rc = print_seqtable_full_loop( opts, seq, &stx );
            }
        }
        VCursorRelease( stx.cursor );
    }
    return rc;
}


typedef struct ua_ctx
{
    samdump_opts * opts;
    input_database * ids;
    input_table * seq;
} ua_ctx;


rc_t print_unaligned_spots( samdump_opts * opts, input_files * ifs, matecache *mc )
{
    rc_t rc = 0;
    if ( ( ifs->database_count > 0 ) && ( opts->dump_unaligned_reads || opts->dump_unaligned_only ) )
    {
        uint32_t db_idx;
        for ( db_idx = 0; db_idx < ifs->database_count && rc == 0; ++db_idx )
        {
            input_database * ids = VectorGet( &ifs->dbs, db_idx );
            if ( ids != NULL )
            {
                input_table seq;

                seq.path = ids->path;
                rc = VDatabaseOpenTableRead( ids->db, &seq.tab, "SEQUENCE" );
                if ( rc != 0 )
                {
                    (void)PLOGERR( klogInt, ( klogInt, rc, "cannot open table SEQUENCE for $(tn)", "tn=%s", ids->path ) );
                }
                else
                {
                    input_table prim;
                    prim.path = ids->path;
                    rc = VDatabaseOpenTableRead( ids->db, &prim.tab, "PRIMARY_ALIGNMENT" );
                    if ( rc != 0 )
                    {
                        (void)PLOGERR( klogInt, ( klogInt, rc, "cannot open table PRIMARY_ALIGNMENT $(tn)", "tn=%s", ids->path ) );
                    }
                    else
                    {
                        if ( opts->region_count > 0 )
                        {
                            rc = print_unaligned_filtered( opts, &seq, &prim, mc, ids );
                        }
                        else
                        {
                            rc = print_unaligned_full( opts, &seq, &prim, mc, ids );
                        }
                        VTableRelease( prim.tab );
                    }
                    VTableRelease( seq.tab );
                }
            }
        }
    }

    if ( rc == 0 && ifs->table_count > 0 )
    {
        uint32_t tab_idx;
        for ( tab_idx = 0; tab_idx < ifs->table_count && rc == 0; ++tab_idx )
        {
            input_table * itab = VectorGet( &ifs->tabs, tab_idx );
            if ( itab != NULL )
                rc = print_unaligned_full( opts, itab, NULL, NULL, NULL );
        }
    }
    return rc;
}
