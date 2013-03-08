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

#include <align/manager.h>
#include <align/iterator.h>
#include <sysalloc.h>

#include "read_fkt.h"
#include "cg_tools.h"
#include "sam-aligned.h"

rc_t CC Quitting( void );

/* -------------------------------------------------------------------------------------------
    column                      PRIM    SEC     EV_INT      EV_ALIGN ( outside of iterator )

    SEQ_SPOT_ID                 X       X       -           X
    SAM_FLAGS                   X       X       -           -
    CIGAR_LONG                  X       X       X           X 
    CIGAR_SHORT                 X       X       X           X
    CIGAR_LONG_LEN              X       X       X           X 
    CIGAR_SHORT_LEN             X       X       X           X
    MATE_ALIGN_ID               X       X       -           -
    MATE_REF_NAME               X       X       -           -
    MATE_REF_POS                X       X       -           -
    TEMPLATE_LEN                X       X       -           -
    READ                        X       X       X           X
    READ_LEN                    X       X       X           X
    MISMATCH_READ               X       X       X           X
    SAM_QUALITY                 X       X       X           X
    REF_ORIENTATION             X       X       X           X
    EDIT_DISTANCE               X       X       X           X
    SEQ_SPOT_GROUP              X       X       -           X
    SEQ_READ_ID                 X       X       -           X
    RAW_READ                    X       X       X           X
    EVIDENCE_ALIGNMENT_IDS      -       -       X           -
    REF_POS                                                 o
    REF_PLOIDY                                              o
   -------------------------------------------------------------------------------------------*/


#define COL_NOT_AVAILABLE 0xFFFFFFFF

#define COL_SEQ_SPOT_ID "(I64)SEQ_SPOT_ID"
#define COL_SAM_FLAGS "(U32)SAM_FLAGS"
#define COL_LONG_CIGAR "(ascii)CIGAR_LONG"
#define COL_SHORT_CIGAR "(ascii)CIGAR_SHORT"
#define COL_MATE_ALIGN_ID "(I64)MATE_ALIGN_ID"
#define COL_MATE_REF_NAME "(ascii)MATE_REF_NAME"
#define COL_MATE_REF_POS "(INSDC:coord:zero)MATE_REF_POS"
#define COL_TEMPLATE_LEN "(I32)TEMPLATE_LEN"
#define COL_MISMATCH_READ "(ascii)MISMATCH_READ"
#define COL_SAM_QUALITY "(INSDC:quality:text:phred_33)SAM_QUALITY"
#define COL_REF_ORIENTATION "(bool)REF_ORIENTATION"
#define COL_EDIT_DIST "(U32)EDIT_DISTANCE"
#define COL_SEQ_SPOT_GROUP "(ascii)SEQ_SPOT_GROUP"
#define COL_SEQ_READ_ID "(INSDC:coord:one)SEQ_READ_ID"
#define COL_RAW_READ "(INSDC:dna:text)RAW_READ"
#define COL_PLOIDY "(NCBI:align:ploidy)PLOIDY"
#define COL_CIGAR_LONG_LEN "(INSDC:coord:len)CIGAR_LONG_LEN"
#define COL_CIGAR_SHORT_LEN "(INSDC:coord:len)CIGAR_SHORT_LEN"
#define COL_READ_LEN "(INSDC:coord:len)READ_LEN"
#define COL_EV_ALIGNMENTS "(I64)EVIDENCE_ALIGNMENT_IDS"
#define COL_REF_POS "(INSDC:coord:zero)REF_POS"
#define COL_REF_PLOIDY "(U32)REF_PLOIDY"


enum align_table_type
{
    att_primary = 0,
    att_secondary,
    att_evidence
};


/* the part common to prim/sec/ev-alignment */
typedef struct align_cmn_context
{
    const VCursor * cursor;

    uint32_t seq_spot_id_idx;
    uint32_t cigar_idx;
    uint32_t cigar_len_idx;
    uint32_t read_idx;
    uint32_t read_len_idx;
    uint32_t edit_dist_idx;
    uint32_t seq_spot_group_idx;
    uint32_t seq_read_id_idx;
    uint32_t raw_read_idx;
    uint32_t sam_quality_idx;
    uint32_t ref_orientation_idx;
} align_cmn_context;


typedef struct align_table_context
{
    /* which Reference-Obj in the ReferenceList we are aligning against... */
    const ReferenceObj* ref_obj;
    uint32_t ref_idx;

    /* which index into the input-files object / needed to distinguish cache entries with the same number
       but comming from different input-files */
    uint32_t db_idx;

    /* objects of which table are we aligning PRIM/SEC/EV ? */
    enum align_table_type align_table_type;

    /* the part common to prim/sec/ev-alignment */
    align_cmn_context cmn;

    /* this is specific to pim/sec */
    uint32_t sam_flags_idx;
    uint32_t mate_align_id_idx;
    uint32_t mate_ref_name_idx;
    uint32_t mate_ref_pos_idx;
    uint32_t tlen_idx;

    /* this is specific to ev-interval/ev-alignmnet */
    uint32_t ploidy_idx;
    uint32_t ev_alignments_idx;
    uint32_t ref_pos_idx;
    uint32_t ref_ploidy_idx;

    /* the common part repeats for evidence-alignment */
    align_cmn_context eval;
} align_table_context;


static void invalidate_all_cmn_column_idx( align_cmn_context * actx )
{
    actx->seq_spot_id_idx       = COL_NOT_AVAILABLE;
    actx->cigar_idx             = COL_NOT_AVAILABLE;    
    actx->cigar_len_idx         = COL_NOT_AVAILABLE;
    actx->read_idx              = COL_NOT_AVAILABLE;
    actx->read_len_idx          = COL_NOT_AVAILABLE;    
    actx->edit_dist_idx         = COL_NOT_AVAILABLE;
    actx->seq_spot_group_idx    = COL_NOT_AVAILABLE;
    actx->seq_read_id_idx       = COL_NOT_AVAILABLE;
    actx->raw_read_idx          = COL_NOT_AVAILABLE;
    actx->sam_quality_idx       = COL_NOT_AVAILABLE;
    actx->ref_orientation_idx   = COL_NOT_AVAILABLE;
}

static void invalidate_all_column_idx( align_table_context * atx )
{
    atx->sam_flags_idx      = COL_NOT_AVAILABLE;
    atx->mate_align_id_idx  = COL_NOT_AVAILABLE;
    atx->mate_ref_name_idx  = COL_NOT_AVAILABLE;
    atx->mate_ref_pos_idx   = COL_NOT_AVAILABLE;
    atx->tlen_idx           = COL_NOT_AVAILABLE;
    atx->ploidy_idx         = COL_NOT_AVAILABLE;
    atx->ev_alignments_idx  = COL_NOT_AVAILABLE;
    atx->ref_pos_idx        = COL_NOT_AVAILABLE;
    atx->ref_ploidy_idx     = COL_NOT_AVAILABLE;
    invalidate_all_cmn_column_idx( &atx->cmn );
    invalidate_all_cmn_column_idx( &atx->eval );
}


static rc_t prepare_cmn_table_rows( samdump_opts * opts, align_cmn_context * cmn, bool prim_sec )
{
    const VCursor * cursor = cmn->cursor;
    rc_t rc = 0;

    if ( prim_sec )
        rc = add_column( cursor, COL_SEQ_SPOT_ID, &cmn->seq_spot_id_idx );

    if ( rc == 0 )
    {
        if ( opts->use_long_cigar )
        {
            rc = add_column( cursor, COL_LONG_CIGAR, &cmn->cigar_idx );
            if ( rc == 0 && !prim_sec )
                rc = add_column( cursor, COL_CIGAR_LONG_LEN, &cmn->cigar_len_idx );
        }
        else
        {
            rc = add_column( cursor, COL_SHORT_CIGAR, &cmn->cigar_idx );
            if ( rc == 0 && !prim_sec )
                rc = add_column( cursor, COL_CIGAR_SHORT_LEN, &cmn->cigar_len_idx );
        }
    }

    if ( rc == 0 )
    {
        if ( opts->print_matches_as_equal_sign )
            rc = add_column( cursor, COL_MISMATCH_READ, &cmn->read_idx );
        else
            rc = add_column( cursor, COL_READ, &cmn->read_idx );
    }

    if ( rc == 0 )
        rc = add_column( cursor, COL_READ_LEN, &cmn->read_len_idx );

    if ( rc == 0 )
        rc = add_column( cursor, COL_SAM_QUALITY, &cmn->sam_quality_idx );

    if ( rc == 0 )
        rc = add_column( cursor, COL_REF_ORIENTATION, &cmn->ref_orientation_idx );

    if ( rc == 0 )
        rc = add_column( cursor, COL_EDIT_DIST, &cmn->edit_dist_idx );

    if ( prim_sec && rc == 0 )
        rc = add_column( cursor, COL_SEQ_SPOT_GROUP, &cmn->seq_spot_group_idx );

    if ( prim_sec && rc == 0 )
        rc = add_column( cursor, COL_SEQ_READ_ID, &cmn->seq_read_id_idx );

    if ( rc == 0 )
        rc = add_column( cursor, COL_RAW_READ, &cmn->raw_read_idx );

    return rc;
}


static rc_t prepare_prim_sec_table_cursor( samdump_opts * opts, const VDatabase *db,
                                           const char * table_name, align_table_context * atx )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead( db, &tbl, table_name );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VDatabaseOpenTableRead( $(tn) ) failed", "tn=%s", table_name ) );
    }
    else
    {
        if ( opts->cursor_cache_size == 0 )
            rc = VTableCreateCursorRead( tbl, &atx->cmn.cursor );
        else
            rc = VTableCreateCachedCursorRead( tbl, &atx->cmn.cursor, opts->cursor_cache_size );
        if ( rc != 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "VTableCreateCursorRead( $(tn) ) failed", "tn=%s", table_name ) );
        }
        else
        {
            const VCursor * cursor = atx->cmn.cursor;
            
            rc = prepare_cmn_table_rows( opts, &atx->cmn, true );

            if ( rc == 0 )
                rc = add_column( cursor, COL_SAM_FLAGS, &atx->sam_flags_idx );

            /*  i don't have to add REF_NAME or REF_SEQ_ID, because i have it from the ref_obj later
                i don't have to add REF_POS, because i have it from the iterator later
                i don't have to add MAPQ, it is in the PlacementRecord later
                    ... when walking the iterator ...
            */
            if ( rc == 0 )
                rc = add_column( cursor, COL_MATE_ALIGN_ID, &atx->mate_align_id_idx );
            if ( rc == 0 )
                rc = add_column( cursor, COL_MATE_REF_NAME, &atx->mate_ref_name_idx );
            if ( rc == 0 )
                rc = add_column( cursor, COL_MATE_REF_POS, &atx->mate_ref_pos_idx );
            if ( rc == 0 )
                rc = add_column( cursor, COL_TEMPLATE_LEN, &atx->tlen_idx );

            if ( rc != 0 )
                VCursorRelease( cursor );
        }
        VTableRelease ( tbl ); /* the cursor keeps the table alive */
    }
    return rc;
}


static rc_t prepare_evidence_table_cursor( samdump_opts * opts, const VDatabase *db,
                                           const char * table_name, align_table_context * atx )
{
    const VTable *evidence_interval_tbl;
    rc_t rc = VDatabaseOpenTableRead( db, &evidence_interval_tbl, table_name );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VDatabaseOpenTableRead( $(tn) ) failed", "tn=%s", table_name ) );
    }
    else
    {
        if ( opts->cursor_cache_size == 0 )
            rc = VTableCreateCursorRead( evidence_interval_tbl, &atx->cmn.cursor );
        else
            rc = VTableCreateCachedCursorRead( evidence_interval_tbl, &atx->cmn.cursor, opts->cursor_cache_size );
        if ( rc != 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "VTableCreateCursorRead( $(tn) ) failed", "tn=%s", table_name ) );
        }
        else
        {
            rc = prepare_cmn_table_rows( opts, &atx->cmn, false );
            if ( rc == 0 )
                rc = add_column( atx->cmn.cursor, COL_PLOIDY, &atx->ploidy_idx );            

            if ( rc == 0 && opts->dump_cg_sam )
                rc = add_column( atx->cmn.cursor, COL_EV_ALIGNMENTS, &atx->ev_alignments_idx );

            if ( rc == 0 && opts->dump_cg_sam )
            {
                const VTable *evidence_alignment_tbl;
                rc = VDatabaseOpenTableRead( db, &evidence_alignment_tbl, "EVIDENCE_ALIGNMENT" );
                if ( rc != 0 )
                {
                    (void)PLOGERR( klogInt, ( klogInt, rc, "VDatabaseOpenTableRead( $(tn) ) failed", "tn=%s", "EVIDENCE_ALIGNMENT" ) );
                }
                else
                {
                    if ( opts->cursor_cache_size == 0 )
                        rc = VTableCreateCursorRead( evidence_alignment_tbl, &atx->eval.cursor );
                    else
                        rc = VTableCreateCachedCursorRead( evidence_alignment_tbl, &atx->eval.cursor, opts->cursor_cache_size );
                    if ( rc != 0 )
                    {
                        (void)PLOGERR( klogInt, ( klogInt, rc, "VTableCreateCursorRead( $(tn) ) failed", "tn=%s", "EVIDENCE_ALIGNMENT" ) );
                    }
                    else
                    {
                        rc = prepare_cmn_table_rows( opts, &atx->eval, false ); /* common to prim/sec/ev-align */
                        if ( rc == 0 )
                        {
                            /* special to ev-align */
                            rc = add_column( atx->eval.cursor, COL_REF_POS, &atx->ref_pos_idx );
                            if ( rc == 0 )
                                rc = add_column( atx->eval.cursor, COL_REF_PLOIDY, &atx->ref_ploidy_idx );
                        }
                        rc = VCursorOpen( atx->eval.cursor );
                        if ( rc != 0 )
                        {
                            (void)PLOGERR( klogInt, ( klogInt, rc, "VCursorOpen( $(tn) ) failed", "tn=%s", "EVIDENCE_ALIGNMENT" ) );
                        }
                    }
                }
                VTableRelease ( evidence_alignment_tbl ); /* the cursor keeps the table alive */
            }
            if ( rc != 0 )
                VCursorRelease( atx->cmn.cursor );
        }
        VTableRelease ( evidence_interval_tbl ); /* the cursor keeps the table alive */
    }
    return rc;
}


static rc_t add_table_pl_iter( samdump_opts * opts, PlacementSetIterator * set_iter, const ReferenceObj* ref_obj,
    input_database * ids, INSDC_coord_zero ref_pos, INSDC_coord_len ref_len,
    const char * spot_group, const char * table_name, align_id_src id_src_selector, Vector * context_list )
{
    rc_t rc = 0;
    align_table_context * atx;
    PlacementRecordExtendFuncs ext_0; /* ReferenceObj_MakePlacementIterator makes copies of the elements */

    memset( &ext_0, 0, sizeof ext_0 );
    atx = calloc( sizeof *atx, 1 );
    if ( atx == NULL )
    {
        rc = RC( rcExe, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
        (void)PLOGERR( klogInt, ( klogInt, rc, "align-context-allocation for $(tn) failed", "tn=%s", table_name ) );
    }
    else
    {
        atx->db_idx = ids->db_idx;
        atx->ref_obj = ref_obj;
        invalidate_all_column_idx( atx );
        rc = ReferenceObj_Idx( ref_obj, &atx->ref_idx );
        if ( rc != 0 )
        {
            (void)PLOGERR( klogInt, ( klogInt, rc, "failed to detect ref-idx for $(tn) failed", "tn=%s", table_name ) );
        }
        else
        {
            switch( id_src_selector )
            {
                case primary_align_ids   :  atx->align_table_type = att_primary;
                                            rc = prepare_prim_sec_table_cursor( opts, ids->db, table_name, atx );
                                            break;

                case secondary_align_ids :  atx->align_table_type = att_secondary;
                                            rc = prepare_prim_sec_table_cursor( opts, ids->db, table_name, atx );
                                            break;

                case evidence_align_ids  :  atx->align_table_type = att_evidence;
                                            rc = prepare_evidence_table_cursor( opts, ids->db, table_name, atx );
                                            break;
            }
        }
        if ( rc == 0 )
        {
            ext_0.data = atx;
            /* we must put the atx-ptr into a global list, in order to close everything later at the end... */
        }
        else
            free( atx );
    }

    if ( rc == 0 )
    {
        int32_t min_mapq = 0;
        PlacementIterator *pl_iter;

        if ( opts->use_min_mapq )
            min_mapq = opts->min_mapq;

        rc = ReferenceObj_MakePlacementIterator( ref_obj, /* the reference-obj it is made from */
            &pl_iter,           /* the placement-iterator we want to make */
            ref_pos,            /* where it starts on the reference */
            ref_len,            /* the whole length of this reference/chromosome */
            min_mapq,           /* no minimal mapping-quality to filter out */
            NULL,               /* no special reference-cursor */
            atx->cmn.cursor,    /* a cursor into the PRIMARY/SECONDARY/EVIDENCE-table */
            id_src_selector,    /* what ID-source to select from REFERENCE-table (ref_obj) */
            &ext_0,             /* placement-record extensions #0 with data-ptr pointing to cursor/index-struct */
            NULL,               /* no placement-record extensions #1 */
            spot_group );       /* no spotgroup re-grouping (yet) */
        if ( rc == 0 )
        {
            rc = PlacementSetIteratorAddPlacementIterator ( set_iter, pl_iter );
            if ( GetRCState( rc ) == rcDone ) { rc = 0; }
        }
    }

    if ( rc == 0 )
        rc = VectorAppend ( context_list, NULL, atx );
    return rc;
}


static rc_t add_pl_iter( samdump_opts * opts, PlacementSetIterator * set_iter, const ReferenceObj* ref_obj,
    input_database * ids, INSDC_coord_zero ref_pos, INSDC_coord_len ref_len,
    const char * spot_group, Vector * context_list )
{
    KNamelist *tables;
    rc_t rc = VDatabaseListTbl( ids->db, &tables );
    if ( rc != 0 )
    {
        (void)PLOGERR( klogInt, ( klogInt, rc, "VDatabaseListTbl( $(tn) ) failed", "tn=%s", ids->path ) );
    }
    else
    {
        if ( opts->dump_primary_alignments && namelist_contains( tables, "PRIMARY_ALIGNMENT" ) )
        {
            rc = add_table_pl_iter( opts, set_iter, ref_obj, ids, ref_pos, ref_len, spot_group, 
                                    "PRIMARY_ALIGNMENT", primary_align_ids, context_list );
        }

        if ( rc == 0 && opts->dump_secondary_alignments && namelist_contains( tables, "SECONDARY_ALIGNMENT" ) )
        {
            rc = add_table_pl_iter( opts, set_iter, ref_obj, ids, ref_pos, ref_len, spot_group, 
                                    "SECONDARY_ALIGNMENT", secondary_align_ids, context_list );
        }

        if ( rc == 0 && ( opts->dump_cg_evidence ) && namelist_contains( tables, "EVIDENCE_INTERVAL" ) )
        {
            rc = add_table_pl_iter( opts, set_iter, ref_obj, ids, ref_pos, ref_len, spot_group, 
                                    "EVIDENCE_INTERVAL", evidence_align_ids, context_list );
        }
        KNamelistRelease( tables );
    }
    return rc;
}


/* the user did not specify ranges on the reference, that means the whole file has to be dumped...
   the reflist is iterated over all ref-objects it contains ... */
static rc_t prepare_whole_files( samdump_opts * opts, input_files * ifs,
                                 PlacementSetIterator * set_iter, Vector * context_list )
{
    rc_t rc = 0;
    uint32_t db_idx;
    /* we now loop through all input-databases... */
    for ( db_idx = 0; db_idx < ifs->database_count && rc == 0; ++db_idx )
    {
        input_database * ids = VectorGet( &ifs->dbs, db_idx );
        if ( ids != NULL )
        {
            uint32_t refobj_count;
            rc = ReferenceList_Count( ids->reflist, &refobj_count );
            if ( rc == 0 && refobj_count > 0 )
            {
                uint32_t ref_idx;
                for ( ref_idx = 0; ref_idx < refobj_count && rc == 0; ++ref_idx )
                {
                    const ReferenceObj* ref_obj;
                    rc = ReferenceList_Get( ids->reflist, &ref_obj, ref_idx );
                    if ( rc == 0 && ref_obj != NULL )
                    {
                        INSDC_coord_len ref_len;
                        rc = ReferenceObj_SeqLength( ref_obj, &ref_len );
                        if ( rc == 0 )
                            rc = add_pl_iter( opts,
                                set_iter,
                                ref_obj,
                                ids,
                                0,                  /* where it starts on the reference */
                                ref_len,            /* the whole length of this reference/chromosome */
                                NULL,               /* no spotgroup re-grouping (yet) */
                                context_list
                                );
                        ReferenceObj_Release( ref_obj );
                    }
                }
            }
        }
    }
    return rc;
}


typedef struct on_region_ctx
{
    rc_t rc;
    samdump_opts * opts;
    input_database * ids;
    PlacementSetIterator * set_iter;
    Vector *context_list;
} on_region_ctx;


static void CC on_region( BSTNode *n, void *data )
{
    on_region_ctx * rctx = data;
    if ( rctx->rc == 0 )
    {
        reference_region * ref_rgn = ( reference_region * )n;
        const ReferenceObj * ref_obj;
        rctx->rc = ReferenceList_Find( rctx->ids->reflist, &ref_obj, ref_rgn->name, string_size( ref_rgn->name ) );
        if ( rctx->rc == 0 )
        {
            uint32_t range_idx, range_count = VectorLength( &ref_rgn->ranges );
            for ( range_idx = 0; range_idx < range_count && rctx->rc == 0; ++range_idx )
            {
                range * r = VectorGet( &ref_rgn->ranges, range_idx );
                if ( r != NULL )
                {
                    INSDC_coord_len len;
                    if ( r->start == 0 && r->end == 0 )
                    {
                        r->start = 1;
                        rctx->rc = ReferenceObj_SeqLength( ref_obj, &len );
                        if ( rctx->rc == 0 )
                            r->end = ( r->start + len );
                    }
                    else
                    {
                        len = ( r->end - r->start + 1 );
                    }
                    if ( rctx->rc == 0 )
                    {
                        rctx->rc = add_pl_iter( rctx->opts, rctx->set_iter, ref_obj, rctx->ids,
                            r->start,           /* where the range starts on the reference */
                            len,                /* the length of this range */
                            NULL,               /* no spotgroup re-grouping (yet) */
                            rctx->context_list
                            );
                    }
                }
            }
            ReferenceObj_Release( ref_obj );
        }
        else
        {
            if ( GetRCState( rctx->rc ) == rcNotFound ) rctx->rc = 0;
        }
    }
}


static rc_t prepare_regions( samdump_opts * opts, input_files * ifs,
                             PlacementSetIterator * set_iter, Vector *context_list )
{
    uint32_t db_idx;
    on_region_ctx rctx;

    rctx.rc = 0;
    rctx.opts = opts;
    rctx.set_iter = set_iter;
    rctx.context_list = context_list;
    /* we now loop through all input-databases... */
    for ( db_idx = 0; db_idx < ifs->database_count && rctx.rc == 0; ++db_idx )
    {
        rctx.ids = VectorGet( &ifs->dbs, db_idx );
        if ( rctx.ids != NULL )
            BSTreeForEach( &opts->regions, false, on_region, &rctx );
    }
    return rctx.rc;
}


static uint32_t calc_mate_flags( uint32_t flags )
{
    uint32_t res = ( flags & 0x1 ) |
                   ( flags & 0x2 ) |
                   ( ( flags & 0x8 ) >> 1 ) |
                   ( ( flags & 0x4 ) << 1 ) |
                   ( ( flags & 0x20 ) >> 1 ) |
                   ( ( flags & 0x10 ) << 1 ) |
                   ( ( flags & 0x40 ) ? 0x80 : 0x40 ) |
                   ( flags & 0x700 );
    return res;
}


static const char *equal_sign = "=";


static rc_t print_slice( const char * source, uint32_t source_str_len, uint32_t *source_offset, 
                         uint32_t *source_len_vector, uint32_t source_len_vector_len,
                         uint32_t slice_nr )
{
    rc_t rc = 0;
    if ( *source_offset > source_str_len || slice_nr >= source_len_vector_len )
        rc = RC( rcExe, rcNoTarg, rcReading, rcParam, rcInvalid );
    else
    {
        uint32_t len = source_len_vector[ slice_nr ];
        if ( len > 0 )
        {
            const char * ptr = &source[ *source_offset ];
            rc = KOutMsg( "%.*s\t", len, ptr );
            *source_offset += len;
        }
        else
            rc = KOutMsg( "*\t" );
    }
    return rc;
}


static rc_t print_qslice( samdump_opts * opts, bool reverse, const char * source, uint32_t source_str_len,
                          uint32_t *source_offset, uint32_t *source_len_vector,
                          uint32_t source_len_vector_len, uint32_t slice_nr )
{
    rc_t rc = 0;
    if ( *source_offset > source_str_len || slice_nr >= source_len_vector_len )
        rc = RC( rcExe, rcNoTarg, rcReading, rcParam, rcInvalid );
    else
    {
        uint32_t len = source_len_vector[ slice_nr ];
        if ( len > 0 )
        {
            const char * ptr = &source[ *source_offset ];
            rc = dump_quality_33( opts, ptr, len, reverse ); /* sam-dump-opts.c */
            if ( rc == 0 )
            {
                rc = KOutMsg( "\t" );
                if ( rc == 0 )
                    *source_offset += len;
            }
        }
        else
            rc = KOutMsg( "*\t" );
    }
    return rc;
}


static rc_t print_allel_name( samdump_opts * opts, const PlacementRecord *rec, uint32_t ploidy_idx )
{
    rc_t rc = KOutMsg( "ALLELE_%li.%u\t", rec->id, ploidy_idx + 1 );
    return rc;
}

static rc_t print_char_ptr( int64_t row_id, const VCursor * cursor, uint32_t col_idx )
{
    const char *ptr;
    size_t len;
    rc_t rc = read_char_ptr_and_size( row_id, cursor, col_idx, &ptr, &len );
    if ( rc == 0 )
    {
        if ( len > 0 )
            rc = KOutMsg( "%.*s\t", len, ptr );
        else
            rc = KOutMsg( "*\t" );
    }
    return rc;
}


static rc_t modify_and_print_cigar( int64_t row_id, const VCursor * cursor, uint32_t col_idx )
{
    cg_cigar_input input;
    rc_t rc = read_char_ptr_and_size( row_id, cursor, col_idx, &input.cigar, &input.cigar_len );
    if ( rc == 0 )
    {
        if ( input.cigar_len > 0 )
        {
            cg_cigar_output output;

            input.orientation = false;
            input.seq_req_id = 0;
            input.edit_dist_available = false;
            input.edit_dist = 0;
            rc = make_cg_cigar( &input, &output ); /* cg_tools.c */
            if ( rc == 0 )
                rc = KOutMsg( "%.*s\t", output.cigar_len, output.cigar );
        }
        else
            rc = KOutMsg( "*\t" );
    }
    return rc;
}


static rc_t print_evidence_alignment( samdump_opts * opts, const PlacementRecord *rec,
                                      align_table_context * atx, int64_t align_id, uint32_t ploidy_idx )
{
    uint32_t ref_ploidy;
    const VCursor * cursor = atx->eval.cursor;
    rc_t rc = read_uint32_t( align_id, cursor, atx->ref_ploidy_idx, &ref_ploidy, 0 );
    if ( rc == 0 && ( ref_ploidy == ploidy_idx ) )
    {
        uint32_t sam_flags = 0x40;
        INSDC_coord_zero ref_pos;
        size_t ref_pos_len;

        /* SAM-FIELD: QNAME     constructed from allel-id/sub-id */
        rc = KOutMsg( "%li\t", align_id );

        if ( rc == 0 )
            rc = read_INSDC_coord_zero( align_id, cursor, atx->ref_pos_idx, &ref_pos, &ref_pos_len );

        /* SAM-FIELD: FLAG      SRA-column: SAM_FLAGS ( uint32 ) */
        /* SAM-FIELD: RNAME     SRA-column: ALLEL-NAME.ploidy_idx */
        /* SAM-FIELD: POS       SRA-column: REF_POS + 1 */
        /* SAM-FIELD: MAPQ      SRA-column: MAPQ */
        if ( rc == 0 )
            rc = KOutMsg( "%u\tALLELE_%li.%u\t%i\t%d\t", sam_flags, rec->id, ploidy_idx, ref_pos + 1, rec->mapq );

        /* SAM-FIELD: CIGAR     SRA-column: CIGAR_SHORT / with special treatment */
        if ( rc == 0 )
            rc = modify_and_print_cigar( align_id, cursor, atx->eval.cigar_idx );


        /* SAM-FIELD: RNEXT     SRA-column: MATE_REF_NAME '*' no mates! */
        /* SAM-FIELD: PNEXT     SRA-column: MATE_REF_POS + 1 '0' no mates */
        /* SAM-FIELD: TLEN      SRA-column: TEMPLATE_LEN '0' not in table */
        if ( rc == 0 )
            rc = KOutMsg( "*\t0\t0\t" );

        /* SAM-FIELD: SEQ       SRA-column: READ  */
        if ( rc == 0 )
            rc = print_char_ptr( align_id, cursor, atx->eval.read_idx );

        /* SAM-FIELD: QUAL      SRA-column: SAM_QUALITY */
        if ( rc == 0 )
        {
            const char * quality;
            size_t quality_str_len;

            rc = read_char_ptr_and_size( rec->id, cursor, atx->eval.sam_quality_idx, &quality, &quality_str_len );
            if ( rc == 0 )
                rc = dump_quality_33( opts, quality, quality_str_len, false );
        }

        /* OPT SAM-FIELD: XI     SRA-column: ALIGN_ID */
        if ( rc == 0 && opts->print_alignment_id_in_column_xi )
            rc = KOutMsg( "\tXI:i:%u", align_id );

        if ( rc == 0 )
            rc = KOutMsg( "\n" );

    }
    return rc;
}


static rc_t print_evidence_alignments( samdump_opts * opts, const PlacementRecord *rec,
                                    align_table_context * atx, uint32_t ploidy_idx )
{
    int64_t *ev_al_ids;
    uint32_t ev_al_ids_count;
    rc_t rc = read_int64_t_ptr( rec->id, atx->cmn.cursor, atx->ev_alignments_idx, &ev_al_ids, &ev_al_ids_count );
    if ( rc == 0 && ev_al_ids_count > 0 )
    {
        uint32_t idx;
        for ( idx = 0; idx < ev_al_ids_count && rc == 0; ++idx )
        {
            rc = print_evidence_alignment( opts, rec, atx, ev_al_ids[ idx ], ploidy_idx );
        }
    }
    return rc;
}


/* print minimal one alignment from the EVIDENCE-INTERVAL / EVIDENCE-ALIGNMENT - table(s) */
static rc_t print_alignment_sam_ev( samdump_opts * opts, const char * ref_name,
                                    INSDC_coord_zero pos,
                                    const PlacementRecord *rec, align_table_context * atx )
{
    uint32_t ploidy;
    const VCursor * cursor = atx->cmn.cursor;
    rc_t rc = read_uint32_t( rec->id, cursor, atx->ploidy_idx, &ploidy, 0 );
    if ( rc == 0 && ploidy > 0 )
    {
        uint32_t ploidy_idx, cigar_len_vector_len, read_len_vector_len, edit_dist_vector_len;
        size_t cigar_str_len=0, read_str_len, quality_str_len;
        uint32_t *cigar_len_vector;
        uint32_t *read_len_vector;
        uint32_t *edit_dist_vector;
        uint32_t cigar_offset = 0;
        uint32_t read_offset = 0;
        uint32_t quality_offset = 0;
        uint32_t sam_flags = 0x40;
        const char * cigar = NULL;
        const char * read;
        const char * quality;

        rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.cigar_idx, &cigar, &cigar_str_len );
        if ( rc == 0 )
            rc = read_uint32_t_ptr( rec->id, cursor, atx->cmn.cigar_len_idx, &cigar_len_vector, &cigar_len_vector_len );
        if ( rc == 0 )
            rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.read_idx, &read, &read_str_len );
        if ( rc == 0 )
            rc = read_uint32_t_ptr( rec->id, cursor, atx->cmn.read_len_idx, &read_len_vector, &read_len_vector_len );
        if ( rc == 0 )
            rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.sam_quality_idx, &quality, &quality_str_len );
        if ( rc == 0 )
            rc = read_uint32_t_ptr( rec->id, cursor, atx->cmn.edit_dist_idx, &edit_dist_vector, &edit_dist_vector_len );

        for ( ploidy_idx = 0; ploidy_idx < ploidy && rc == 0; ++ploidy_idx )
        {
            /* SAM-FIELD: QNAME     SRA-column: eventually prefixed row-id into EVIDENCE_INTERVAL - table */
            rc = print_allel_name( opts, rec, ploidy_idx );

            /* SAM-FIELD: FLAG      SRA-column: SAM_FLAGS ( uint32 ) */
            /* SAM-FIELD: RNAME     SRA-column: REF_NAME / REF_SEQ_ID ( char * ) */
            /* SAM-FIELD: POS       SRA-column: REF_POS + 1 */
            /* SAM-FIELD: MAPQ      SRA-column: MAPQ */
            if ( rc == 0 )
                rc = KOutMsg( "%u\t%s\t%u\t%d\t", sam_flags, ref_name, pos + 1, rec->mapq );

            /* SAM-FIELD: CIGAR     SRA-column: CIGAR_SHORT / CIGAR_LONG sliced!!! */
            if ( rc == 0 )
                rc = print_slice( cigar, cigar_str_len, &cigar_offset, cigar_len_vector, cigar_len_vector_len, ploidy_idx );

            /* SAM-FIELD: RNEXT     SRA-column: MATE_REF_NAME ( !!! row_len can be zero !!! ) */
            /* SAM-FIELD: PNEXT     SRA-column: MATE_REF_POS + 1 ( !!! row_len can be zero !!! ) */
            /* SAM-FIELD: TLEN      SRA-column: TEMPLATE_LEN ( !!! row_len can be zero !!! ) */
            if ( rc == 0 )
                rc = KOutMsg( "*\t%u\t%d\t", 0, 0 );

            /* SAM-FIELD: SEQ       SRA-column: READ sliced!!! */
            if ( rc == 0 )
                rc = print_slice( read, read_str_len, &read_offset, read_len_vector, read_len_vector_len, ploidy_idx );

            /* SAM-FIELD: QUAL      SRA-column: SAM_QUALITY sliced!!! */
            if ( rc == 0 )
                rc = print_qslice( opts, false, quality, quality_str_len, &quality_offset, read_len_vector, read_len_vector_len, ploidy_idx );

            /* OPT SAM-FIELD: XI     SRA-column: ALIGN_ID */
            if ( rc == 0 && opts->print_alignment_id_in_column_xi )
                rc = KOutMsg( "\tXI:i:%u", rec->id );

            /* OPT SAM-FIELD: NM     SRA-column: EDIT_DISTANCE sliced!!! */
            if ( rc == 0 )
            {
                if ( ploidy_idx < edit_dist_vector_len )
                    rc = KOutMsg( "\tNM:i:%u", edit_dist_vector[ ploidy_idx ] );
            }

            if ( rc == 0 )
                rc = KOutMsg( "\n" );

            opts->rows_so_far++;

            /* we do that here per ALLEL-READ, not at the end per ALLEL, because we have to test which alignments
               fit the ploidy_idx */
            if ( rc == 0 && opts->dump_cg_sam )
                rc = print_evidence_alignments( opts, rec, atx, ploidy_idx + 1 );
        }
    }
    return rc;
}


static rc_t print_alignment_sam_ps( samdump_opts * opts, const char * ref_name,
                                    INSDC_coord_zero pos, matecache * mc,
                                    const PlacementRecord *rec, align_table_context * atx )
{
    uint32_t sam_flags = 0;
    INSDC_coord_zero mate_ref_pos = 0;
    INSDC_coord_len tlen = 0;
    int64_t mate_align_id = 0;
    const char * mate_ref_name = ref_name;
    size_t mate_ref_name_len = string_size( ref_name );
    size_t mate_ref_pos_len = 0;
    const VCursor * cursor = atx->cmn.cursor;
    int64_t *seq_spot_id;
    uint32_t seq_spot_id_len;

    /* SAM-FIELD: NONE      SRA-column: MATE_ALIGN_ID ( int64 ) ... for cache lookup's */
    rc_t rc = read_int64( rec->id, cursor, atx->mate_align_id_idx, &mate_align_id, 0 );

    /* pre-read seq-spot-id, needed for unaligned cache and SAM-field QNAME */
    if ( rc == 0 )
        rc = read_int64_t_ptr( rec->id, cursor, atx->cmn.seq_spot_id_idx, &seq_spot_id, &seq_spot_id_len );

    /* try to find the info about the mate in the CACHE... */
    if ( rc == 0 )
    {
        if ( mate_align_id != 0 )
        {
            rc = matecache_lookup_same_ref( mc, atx->db_idx, mate_align_id, &mate_ref_pos, &sam_flags, &tlen );
            if ( rc == 0 )
            {
                /* cache entry-found! (on the same reference) -> that means we have now mate_ref_pos, flags and tlen */
                rc = matecache_remove_same_ref( mc, atx->db_idx, mate_align_id );
                mate_ref_name = equal_sign;
                mate_ref_name_len = 1;
                mate_ref_pos_len = 1;
            }
        }

        if ( ( mate_align_id != 0 && GetRCState( rc ) == rcNotFound )||( mate_align_id == 0 ) )
        {
            /* no cache entry-found ... */

            /* that means we have to read it from the table... */
            rc = read_char_ptr_and_size( rec->id, cursor, atx->mate_ref_name_idx, &mate_ref_name, &mate_ref_name_len );

            if ( rc == 0 )
                rc = read_INSDC_coord_zero( rec->id, cursor, atx->mate_ref_pos_idx, &mate_ref_pos, &mate_ref_pos_len );
            if ( rc == 0 )
                rc = read_INSDC_coord_len( rec->id, cursor, atx->tlen_idx, &tlen );
            if ( rc == 0 )
                rc = read_uint32_t( rec->id, cursor, atx->sam_flags_idx, &sam_flags, 0 );

            if ( rc == 0 )
            {
                if ( mate_align_id != 0 )
                {
                    /* now that we have the data, store it in sam-ref-cache it the mate is on the same ref. */
                    if ( mate_ref_name_len > 0 )
                    {
                        int cmp = string_cmp( mate_ref_name, mate_ref_name_len, ref_name, string_size( ref_name ), mate_ref_name_len );
                        if ( cmp == 0 )
                        {
                            rc = matecache_insert_same_ref( mc, atx->db_idx, rec->id, pos/* + 1 */, 
                                                            calc_mate_flags( sam_flags ), -tlen );
                            mate_ref_name = equal_sign;
                            mate_ref_name_len = 1;
                        }
                    }
                }
                else if ( opts->print_half_unaligned_reads )
                {
                    rc = matecache_insert_unaligned( mc, atx->db_idx, rec->id, pos, atx->ref_idx, *seq_spot_id );
                }
            }
        }
    }

    if ( rc == 0 && opts->use_matepair_filter )
    {
        if ( !filter_by_matepair_dist( opts, tlen ) )
            return 0;
    }

    opts->rows_so_far++;

    /* SAM-FIELD: QNAME     SRA-column: SEQ_SPOT_ID ( int64 ) */
    if ( rc == 0 )
    {
        if ( seq_spot_id_len > 0 )
        {
            if ( opts->print_spot_group_in_name | opts->print_cg_names )
            {
                const char * spot_group;
                size_t spot_group_len;
                rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.seq_spot_group_idx, &spot_group, &spot_group_len );
                if ( rc == 0 )
                    rc = dump_name( opts, *seq_spot_id, spot_group, spot_group_len );
            }
            else
                rc = dump_name( opts, *seq_spot_id, NULL, 0 );
        }
        else
            rc = KOutMsg( "*" );
    }

    if ( rc == 0 )
        rc = KOutMsg( "\t" );


    /* massage the sam-flag if we are not dumping unaligned reads... */
    if ( !opts->dump_unaligned_reads    /** not going to dump unaligned **/
         && ( sam_flags & 0x1 )         /** but we have sequenced multiple fragments **/
         && ( sam_flags & 0x8 ) )       /** and not all of them align **/
    {
        /*** remove flags talking about multiple reads **/
        /* turn off 0x001 0x008 0x040 0x080 */
        sam_flags &= ~0xC9;
    }

    /* SAM-FIELD: FLAG      SRA-column: SAM_FLAGS ( uint32 ) */
    /* SAM-FIELD: RNAME     SRA-column: REF_NAME / REF_SEQ_ID ( char * ) */
    /* SAM-FIELD: POS       SRA-column: REF_POS + 1 */
    /* SAM-FIELD: MAPQ      SRA-column: MAPQ */
    if ( rc == 0 )
        rc = KOutMsg( "%u\t%s\t%u\t%d\t", sam_flags, ref_name, pos + 1, rec->mapq );

    /* SAM-FIELD: CIGAR     SRA-column: CIGAR_SHORT / CIGAR_LONG */
    if ( rc == 0 )
        rc = print_char_ptr( rec->id, cursor, atx->cmn.cigar_idx );

    /* SAM-FIELD: RNEXT     SRA-column: MATE_REF_NAME ( !!! row_len can be zero !!! ) */
    /* SAM-FIELD: PNEXT     SRA-column: MATE_REF_POS + 1 ( !!! row_len can be zero !!! ) */
    /* SAM-FIELD: TLEN      SRA-column: TEMPLATE_LEN ( !!! row_len can be zero !!! ) */
    if ( rc == 0 )
    {
        if ( mate_ref_name_len > 0 )
        {
            rc = KOutMsg( "%.*s\t%u\t%d\t", mate_ref_name_len, mate_ref_name, mate_ref_pos + 1, tlen );
        }
        else
        {
            if ( mate_ref_pos_len == 0 )
                rc = KOutMsg( "*\t0\t%d\t", tlen );
            else
                rc = KOutMsg( "*\t%u\t%d\t", mate_ref_pos + 1, tlen );
        }
    }

    /* SAM-FIELD: SEQ       SRA-column: READ */
    if ( rc == 0 )
        rc = print_char_ptr( rec->id, cursor, atx->cmn.read_idx );

    /* SAM-FIELD: QUAL      SRA-column: SAM_QUALITY */
    if ( rc == 0 )
    {
        const char * quality;
        size_t quality_len;
        rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.sam_quality_idx, &quality, &quality_len );
        if ( rc == 0 )
        {
            if ( quality_len > 0 )
                rc = dump_quality_33( opts, quality, quality_len, false );
            else
                rc = KOutMsg( "*" );
        }
    }

    /* OPT SAM-FIELD: RG     SRA-column: SPOT_GROUP */
    if ( rc == 0 )
    {
        if ( atx->cmn.seq_spot_group_idx != COL_NOT_AVAILABLE )
        {
            const char * spot_grp;
            size_t spot_grp_len;
            rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.seq_spot_group_idx, &spot_grp, &spot_grp_len );
            if ( rc == 0 && spot_grp_len > 0 )
                rc = KOutMsg( "\tRG:Z:%.*s", spot_grp_len, spot_grp );
        }
    }

    /* OPT SAM-FIELD: XI     SRA-column: ALIGN_ID */
    if ( rc == 0 && opts->print_alignment_id_in_column_xi )
        rc = KOutMsg( "\tXI:i:%u", rec->id );

    /* OPT SAM-FIELD: NM     SRA-column: EDIT_DISTANCE */
    if ( rc == 0 )
    {
        uint32_t *edit_dist;
        uint32_t edit_dist_len;
        rc = read_uint32_t_ptr( rec->id, cursor, atx->cmn.edit_dist_idx, &edit_dist, &edit_dist_len );
        if ( rc == 0 && edit_dist_len > 0 )
            rc = KOutMsg( "\tNM:i:%u", *edit_dist );
    }

    if ( rc == 0 )
        rc = KOutMsg( "\n" );
    return rc;
}


static rc_t print_alignment_fastx( samdump_opts * opts, const char * ref_name,
                                   INSDC_coord_zero pos, matecache * mc,
                                   const PlacementRecord *rec, align_table_context * atx )
{
    bool orientation;
    const VCursor *cursor = atx->cmn.cursor;
    int64_t mate_align_id;
    int64_t * seq_spot_id;
    uint32_t seq_spot_id_len;

    rc_t rc = read_int64_t_ptr( rec->id, cursor, atx->cmn.seq_spot_id_idx, &seq_spot_id, &seq_spot_id_len );

    /* this is here to detect if the mate is aligned, if NOT, we want to put it into the unaligned-cache! */
    if ( rc == 0 && opts->print_half_unaligned_reads )
    {
        rc = read_int64( rec->id, cursor, atx->mate_align_id_idx, &mate_align_id, 0 );
        if ( rc == 0 && mate_align_id == 0 )
        {
            uint32_t sam_flags;
            rc = read_uint32_t( rec->id, cursor, atx->sam_flags_idx, &sam_flags, 0 );
            if ( rc == 0 )
                rc = matecache_insert_unaligned( mc, atx->db_idx, rec->id, pos, atx->ref_idx, *seq_spot_id );
        }
    }

    opts->rows_so_far++;

    if ( opts->output_format == of_fastq )
        rc = KOutMsg( "@" );
    else
        rc = KOutMsg( ">" );

    /* SAM-FIELD: QNAME     1.row: name */
    if ( rc == 0 )
    {
        if ( seq_spot_id_len > 0 )
        {
            if ( opts->print_spot_group_in_name )
            {
                const char * spot_grp;
                size_t spot_grp_len;
                rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.seq_spot_group_idx, &spot_grp, &spot_grp_len );
                if ( rc == 0 )
                    rc = dump_name( opts, *seq_spot_id, spot_grp, spot_grp_len );
            }
            else
                rc = dump_name( opts, *seq_spot_id, NULL, 0 );
        }
        else
            rc = KOutMsg( "*" );

        if ( rc == 0 )
        {
            uint32_t * seq_read_id;
            uint32_t seq_read_id_len;
            rc = read_uint32_t_ptr( rec->id, cursor, atx->cmn.seq_read_id_idx, &seq_read_id, &seq_read_id_len );
            if ( rc == 0 && seq_read_id_len > 0 )
                rc = KOutMsg( "/%u", *seq_read_id );
        }
    }

    /* SRA-column: REF_ORIENTATION ( bool ) ... needed for quality */
    if ( rc == 0 )
        rc = read_bool( rec->id, cursor, atx->cmn.ref_orientation_idx, &orientation, false );

    /* source of the alignment: primary/secondary/evidence */
    if ( rc == 0 )
    {
        switch( atx->align_table_type )
        {
        case att_primary    :   rc = KOutMsg( " primary" ); break;
        case att_secondary  :   rc = KOutMsg( " secondary" ); break;
        case att_evidence   :   rc = KOutMsg( " evidence" ); break;
        }
    }

    /* against what reference aligned, at what position, with what mapping-quality */
    if ( rc == 0 )
        rc = KOutMsg( " ref=%s pos=%u mapq=%i\n", ref_name, pos + 1, rec->mapq );

    /* READ at a new line */
    if ( rc == 0 )
    {
        const char * read;
        size_t read_size;
        rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.raw_read_idx, &read, &read_size );
        if ( rc == 0 )
        {
            if ( read_size > 0 )
                rc = KOutMsg( "%.*s\n", read_size, read );
            else
                rc = KOutMsg( "*\n" );
        }
    }

    /* QUALITY on a new line if in fastq-mode */
    if ( rc == 0 && opts->output_format == of_fastq )
    {
        rc = KOutMsg( "+\n" );
        if ( rc == 0 )
        {
            const char * quality;
            size_t quality_size;
            rc = read_char_ptr_and_size( rec->id, cursor, atx->cmn.sam_quality_idx, &quality, &quality_size );
            if ( rc == 0 )
            {
                if ( quality_size > 0 )
                    rc = dump_quality_33( opts, quality, quality_size, false );
                else
                    rc = KOutMsg( "*" );
            }
            if ( rc == 0 )
                rc = KOutMsg( "\n" );
        }
    }

    return rc;
}


/* print one record of alignment-information in SAM-format */
static rc_t walk_position( samdump_opts * opts, PlacementSetIterator * set_iter,
                           const char * ref_name, INSDC_coord_zero pos, matecache * mc )
{
    rc_t rc = 0;
    while ( rc == 0 && !test_limit_reached( opts ) )
    {
        const PlacementRecord *rec;
        rc = PlacementSetIteratorNextRecordAt( set_iter, pos, &rec );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) != rcDone )
            {
                LOGERR( klogInt, rc, "PlacementSetIteratorNextRecordAt() failed" );
            }
        }
        else
        {
            align_table_context * atx = PlacementRecord_get_ext_data_ptr( rec, placementRecordExtension0 );
            if ( atx == NULL )
            {
                rc = RC( rcExe, rcNoTarg, rcReading, rcParam, rcNull );
                LOGERR( klogInt, rc, "no placement-record-context available" );
            }
            else
            {
                if ( opts->output_format == of_sam )
                {
                    if ( atx->align_table_type == att_evidence )
                        rc = print_alignment_sam_ev( opts, ref_name, pos, rec, atx );
                    else
                        rc = print_alignment_sam_ps( opts, ref_name, pos, mc, rec, atx );
                }
                else
                    rc = print_alignment_fastx( opts, ref_name, pos, mc, rec, atx );
            }
        }
    }
    if ( GetRCState( rc ) == rcDone ) rc = 0;
    return rc;
}


static rc_t walk_window( samdump_opts * opts, PlacementSetIterator * set_iter,
                         const char * ref_name, matecache * mc )
{
    rc_t rc = 0;
    while ( rc == 0 && !test_limit_reached( opts ) )
    {
        INSDC_coord_zero pos;
        rc = PlacementSetIteratorNextAvailPos( set_iter, &pos, NULL );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) != rcDone )
            {
                LOGERR( klogInt, rc, "PlacementSetIteratorNextAvailPos() failed" );
            }
        }
        else
        {
            rc = walk_position( opts, set_iter, ref_name, pos, mc );
        }
    }
    if ( GetRCState( rc ) == rcDone ) rc = 0;
    return rc;
}


static rc_t walk_reference( samdump_opts * opts, PlacementSetIterator * set_iter,
                            struct ReferenceObj const * ref_obj, const char * ref_name, matecache * mc )
{
    rc_t rc = 0;
    while ( rc == 0 && !test_limit_reached( opts ) )
    {
        rc = Quitting ();
        if ( rc == 0 )
        {
            INSDC_coord_zero first_pos;
            INSDC_coord_len len;
            rc = PlacementSetIteratorNextWindow( set_iter, &first_pos, &len );
            if ( rc != 0 )
            {
                if ( GetRCState( rc ) != rcDone )
                {
                    LOGERR( klogInt, rc, "PlacementSetIteratorNextWindow() failed" );
                }
            }
            else
                rc = walk_window( opts, set_iter, ref_name, mc );
        }
    }
    if ( GetRCState( rc ) == rcDone ) rc = 0;
    if ( rc == 0 )
        rc = matecache_clear_same_ref( mc );

    return rc;
}


static rc_t walk_placements( samdump_opts * opts, PlacementSetIterator * set_iter, matecache * mc )
{
    rc_t rc = 0;
    while ( rc == 0 && !test_limit_reached( opts ) )
    {
        struct ReferenceObj const * ref_obj;

        rc = PlacementSetIteratorNextReference( set_iter, NULL, NULL, &ref_obj );
        if ( rc == 0 )
        {
            if ( ref_obj != NULL )
            {
                const char * ref_name = NULL;
                if ( opts->use_seqid_as_refname )
                    rc = ReferenceObj_SeqId( ref_obj, &ref_name );
                else
                    rc = ReferenceObj_Name( ref_obj, &ref_name );

                if ( rc == 0 )
                    rc = walk_reference( opts, set_iter, ref_obj, ref_name, mc );
                else
                {
                    if ( opts->use_seqid_as_refname )
                    {
                        (void)LOGERR( klogInt, rc, "ReferenceObj_SeqId() failed" );
                    }
                    else
                    {
                        (void)LOGERR( klogInt, rc, "ReferenceObj_Name() failed" );
                    }
                }
            }
        }
        else if ( GetRCState( rc ) != rcDone )
        {
            (void)LOGERR( klogInt, rc, "ReferenceIteratorNextReference() failed" );
        }
    }

    if ( GetRCState( rc ) == rcDone )
        rc = 0;
    return rc;
}


static void CC destroy_align_table_context( void *item, void *data )
{
    align_table_context * atx = item;
    VCursorRelease( atx->cmn.cursor );
    VCursorRelease( atx->eval.cursor );
    free( atx );
}


static rc_t print_all_aligned_spots_of_this_reference( samdump_opts * opts, input_database * ids, matecache * mc,
                                                       const AlignMgr * a_mgr, const ReferenceObj* ref_obj )
{
    PlacementSetIterator * set_iter;
    /* the we ask the alignment-manager to produce a placement-set-iterator... */
    rc_t rc = AlignMgrMakePlacementSetIterator( a_mgr, &set_iter );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot create PlacementSetIterator" );
    }
    else
    {
        /* here we need a vector to passed along into the creation of the iterators */
        Vector context_list;
        INSDC_coord_len ref_len;

        VectorInit ( &context_list, 0, 5 );

        rc = ReferenceObj_SeqLength( ref_obj, &ref_len );
        if ( rc == 0 )
        {
            rc = add_pl_iter( opts, set_iter, ref_obj, ids,
                0,                  /* where it starts on the reference */
                ref_len,            /* the whole length of this reference/chromosome */
                NULL,               /* no spotgroup re-grouping (yet) */
                &context_list
                );
            if ( rc == 0 )
                rc = walk_placements( opts, set_iter, mc );
        }

        /* walk the context_list to free the align_table_context records, close/free the cursors... */
        VectorWhack ( &context_list, destroy_align_table_context, NULL );
        PlacementSetIteratorRelease( set_iter );
    }
    return rc;
}


/*
   the user did not specify regions, print all alignments from all input-files
   this is strategy #1 to do this, create a ref_iter for every reference each
    + ... less cursors will be open at the same time, more resource efficient
    - ... if more than one input-file, the output will be sorted only within each reference
*/
static rc_t print_all_aligned_spots_0( samdump_opts * opts, input_files * ifs, matecache * mc, const AlignMgr * a_mgr )
{
    rc_t rc = 0;
    uint32_t db_idx;
    /* we now loop through all input-databases... */
    for ( db_idx = 0; db_idx < ifs->database_count && rc == 0; ++db_idx )
    {
        input_database * ids = VectorGet( &ifs->dbs, db_idx );
        if ( ids != NULL )
        {
            uint32_t refobj_count;
            rc = ReferenceList_Count( ids->reflist, &refobj_count );
            if ( rc == 0 && refobj_count > 0 )
            {
                uint32_t ref_idx;
                for ( ref_idx = 0; ref_idx < refobj_count && rc == 0; ++ref_idx )
                {
                    const ReferenceObj* ref_obj;
                    rc = ReferenceList_Get( ids->reflist, &ref_obj, ref_idx );
                    if ( rc == 0 && ref_obj != NULL )
                    {
                        rc = print_all_aligned_spots_of_this_reference( opts, ids, mc, a_mgr, ref_obj );
                        ReferenceObj_Release( ref_obj );
                    }
                }
            }
        }
    }
    return rc;
}


/*
   the user did not specify regions, print all alignments from all input-files
   this is strategy #2 to do this, throw all iterators for all input-files and all there references
   into one set_iter.
    + ... if more than one input-file, everything will be sorted
    - ... creates a large number of open cursors ( because of sub-cursors due to schema-functions )
          this can result in not beeing able to perform the functions at all because of running out of resources
    - ... a long delay at start up, before the 1st alignment is printed ( all the cursors have to be opened )
*/
static rc_t print_all_aligned_spots_1( samdump_opts * opts, input_files * ifs, matecache * mc, const AlignMgr * a_mgr )
{
    PlacementSetIterator * set_iter;
    /* the we ask the alignment-manager to produce a placement-set-iterator... */
    rc_t rc = AlignMgrMakePlacementSetIterator( a_mgr, &set_iter );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot create PlacementSetIterator" );
    }
    else
    {
        /* here we need a vector to passed along into the creation of the iterators */
        Vector context_list;
        VectorInit ( &context_list, 0, 5 );

        rc = prepare_whole_files( opts, ifs, set_iter, &context_list );

        if ( rc == 0 )
            rc = walk_placements( opts, set_iter, mc );

        /* walk the context_list to free the align_table_context records, close/free the cursors... */
        VectorWhack ( &context_list, destroy_align_table_context, NULL );
        PlacementSetIteratorRelease( set_iter );
    }
    return rc;
}


/*
   the user has specified certain regions on the references,
   print only alignments, that start in these regions
*/
static rc_t print_selected_aligned_spots( samdump_opts * opts, input_files * ifs, matecache * mc, const AlignMgr * a_mgr )
{
    PlacementSetIterator * set_iter;
    /* the we ask the alignment-manager to produce a placement-set-iterator... */
    rc_t rc = AlignMgrMakePlacementSetIterator( a_mgr, &set_iter );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot create PlacementSetIterator" );
    }
    else
    {
        /* here we need a vector to passed along into the creation of the iterators */
        Vector context_list;
        VectorInit ( &context_list, 0, 5 );

        rc = prepare_regions( opts, ifs, set_iter, &context_list );

        if ( rc == 0 )
            rc = walk_placements( opts, set_iter, mc );

        /* walk the context_list to free the align_table_context records, close/free the cursors... */
        VectorWhack ( &context_list, destroy_align_table_context, NULL );

        PlacementSetIteratorRelease( set_iter );
    }
    return rc;
}


/*
   this is called from sam-dump3.c, it prepares the iterators and then walks them
   ---> only entry into this module <--- 
*/
rc_t print_aligned_spots( samdump_opts * opts, input_files * ifs, matecache * mc )
{
    const AlignMgr * a_mgr;
    /* first we make an alignment-manager */
    rc_t rc = AlignMgrMakeRead( &a_mgr );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot create alignment-manager" );
    }
    else
    {
        if ( opts->region_count == 0 )
        {
            /* the user did not specify regions to be printed ==> print all alignments */
            switch( opts->dump_mode )
            {
                case dm_one_ref_at_a_time : rc = print_all_aligned_spots_0( opts, ifs, mc, a_mgr ); break;
                case dm_prepare_all_refs  : rc = print_all_aligned_spots_1( opts, ifs, mc, a_mgr ); break;
            }
        }
        else
        {
            /* the user did specify regions to be printed ==> print only the alignments in these regions */
            rc = print_selected_aligned_spots( opts, ifs, mc, a_mgr );
        }
        AlignMgrRelease( a_mgr );
    }
    return rc;
}
