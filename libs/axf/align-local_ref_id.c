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
#include <vdb/extern.h>

#include <vdb/xform.h>
#include <vdb/database.h>
#include <vdb/table.h>
#include <vdb/cursor.h>
#include <vdb/vdb-priv.h>
#include <insdc/insdc.h>
#include <klib/data-buffer.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <bitstr.h>

#include "ref-tbl.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct LocalRefID LocalRefID;
struct LocalRefID
{
    uint32_t maxSeqLen;
};

static
void CC LocalRefIDWhack ( void *obj )
{
    LocalRefID * self = obj;
    if ( self != NULL )
    {
        free ( self );
    }
}

static
rc_t LocalRefIDMake ( LocalRefID **objp, const VTable *tbl )
{
    rc_t rc;
    const VCursor *curs;
    uint32_t colIdx;

    /* create the object */
    LocalRefID *obj = malloc ( sizeof * obj );
    if ( obj == NULL ) {
        rc = RC ( rcXF, rcFunction, rcConstructing, rcMemory, rcExhausted );
    } else {
        /* open the reference table */
        const VTable *reftbl;
        if( (rc = AlignRefTable(tbl, &reftbl)) == 0 ) {
            /* create a cursor */
            rc = VTableCreateCursorRead(reftbl, & curs);
            VTableRelease ( reftbl );
            if( rc == 0 ) {
                /* add columns to cursor */
                if( (rc = VCursorAddColumn(curs, &colIdx, "(U32)MAX_SEQ_LEN")) == 0 ) {
                    rc = VCursorOpen(curs);
                }
                if (rc == 0) {
                    const void *base;
                    uint32_t row_len;
                    
                    rc = VCursorCellDataDirect(curs, 1, colIdx, NULL, &base, NULL, &row_len);
                    if (rc == 0) {
                        memcpy(&obj->maxSeqLen, base, 4);
                    }
                }
                VCursorRelease(curs);
                if( rc == 0 ) {
                    *objp = obj;
                    return 0;
                }
            }
        }
        free ( obj );
    }
    return rc;
}

/*
 function I64 NCBI:align:local_ref_id ( U64 global_ref_start )
 */
static
rc_t CC align_local_ref_id ( void *data, const VXformInfo *info, void *Dst, const void *Src, uint64_t elem_count )
{
    LocalRefID const *self = (void const *)data;
    int64_t *dst=(int64_t*)Dst;
    uint64_t const *global_ref_start = Src; 
    unsigned i;

    for (i = 0; i != elem_count; ++i) {
        dst[i] = global_ref_start[i] / self->maxSeqLen + 1;
    }
    return 0;
}

VTRANSFACT_IMPL ( NCBI_align_local_ref_id, 1, 0, 0 ) ( const void *self, const VXfactInfo *info,
    VFuncDesc *rslt, const VFactoryParams *cp, const VFunctionParams *dp )
{
    LocalRefID *fself;
    rc_t rc = LocalRefIDMake ( & fself, info -> tbl );
    if ( rc == 0 )
    {
        rslt -> self = fself;
        rslt -> whack = LocalRefIDWhack;
        rslt -> u . af = align_local_ref_id;
        rslt -> variant = vftArray;
    }

    return rc;
}
