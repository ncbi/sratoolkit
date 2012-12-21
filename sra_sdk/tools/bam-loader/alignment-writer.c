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

#include <klib/rc.h>
#include <klib/log.h>
#include <sysalloc.h>
#include <klib/out.h>

#include <vdb/vdb-priv.h>

#include "alignment-writer.h"
#include "Globals.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

enum e_tables {
    tblPrimary,
    tblSecondary,
    tblN
};

struct s_alignment {
    VDatabase *db;
    TableWriterAlgn const *tbl[tblN];
    int64_t rowId;
    int st;
};

Alignment *AlignmentMake(VDatabase *db) {
    Alignment *self = calloc(1, sizeof(*self));
    
    if (self) {
        self->db = db;
        VDatabaseAddRef(self->db);
    }
    return self;
}

static rc_t SetColumnDefaults(TableWriterAlgn const *tbl)
{
    return 0;
}

static rc_t WritePrimaryRecord(Alignment *const self, AlignmentRecord *const data)
{
    if (self->tbl[tblPrimary] == NULL) {
        rc_t rc = TableWriterAlgn_Make(&self->tbl[tblPrimary], self->db,
                                       ewalgn_tabletype_PrimaryAlignment,
                                       ewalgn_co_TMP_KEY_ID + 
                                       (G.expectUnsorted ? ewalgn_co_unsorted : 0));
        if (rc)
            return rc;
        rc = SetColumnDefaults(self->tbl[tblPrimary]);
        if (rc)
            return rc;
    }
    return TableWriterAlgn_Write(self->tbl[tblPrimary], &data->data, &data->alignId);
}

static rc_t WriteSecondaryRecord(Alignment *const self, AlignmentRecord *const data)
{
    if (self->tbl[tblSecondary] == NULL) {
        rc_t rc = TableWriterAlgn_Make(&self->tbl[tblSecondary], self->db,
                                       ewalgn_tabletype_SecondaryAlignment,
                                       ewalgn_co_TMP_KEY_ID + 
                                       (G.expectUnsorted ? ewalgn_co_unsorted : 0));
        if (rc)
            return rc;
        rc = SetColumnDefaults(self->tbl[tblSecondary]);
        if (rc)
            return rc;
    }
#if 1
    /* try to make consistent with cg-load */
    if (data->mate_ref_pos == 0) {
        data->data.mate_ref_orientation.elements = 0;
    }
#endif
    return TableWriterAlgn_Write(self->tbl[tblSecondary], &data->data, &data->alignId);
}

rc_t AlignmentWriteRecord(Alignment *const self, AlignmentRecord *const data)
{
    return data->isPrimary ? WritePrimaryRecord(self, data) : WriteSecondaryRecord(self, data);
}

rc_t AlignmentStartUpdatingSpotIds(Alignment *const self)
{
    return 0;
}

rc_t AlignmentGetSpotKey(Alignment *const self, uint64_t * keyId)
{
    rc_t rc;
    
    switch (self->st) {
    case 0:
        rc = TableWriterAlgn_TmpKeyStart(self->tbl[tblPrimary]);
        if (rc)
            break;
        self->rowId = 0;
        ++self->st;
    case 1:
        rc = TableWriterAlgn_TmpKey(self->tbl[tblPrimary], ++self->rowId, keyId);
        if (rc == 0)
            break;
        ++self->st;
        if (GetRCState(rc) != rcNotFound || GetRCObject(rc) != rcRow || self->tbl[tblSecondary] == NULL)
            break;
    case 2:
        rc = TableWriterAlgn_TmpKeyStart(self->tbl[tblSecondary]);
        if (rc)
            break;
        self->rowId = 0;
        ++self->st;
    case 3:
        rc = TableWriterAlgn_TmpKey(self->tbl[tblSecondary], ++self->rowId, keyId);
        if (rc == 0)
            break;
        if (GetRCState(rc) != rcNotFound || GetRCObject(rc) != rcRow)
            break;
        ++self->st;
        break;
    default:
        rc = RC(rcAlign, rcTable, rcUpdating, rcError, rcIgnored);
        break;
    }
    return rc;
}

rc_t AlignmentWriteSpotId(Alignment * const self, int64_t const spotId)
{
    switch (self->st) {
    case 1:
        return TableWriterAlgn_Write_SpotId(self->tbl[tblPrimary], self->rowId, spotId);
    case 3:
        return TableWriterAlgn_Write_SpotId(self->tbl[tblSecondary], self->rowId, spotId);
    default:
        return RC(rcAlign, rcTable, rcUpdating, rcSelf, rcInconsistent);
    }
}

rc_t AlignmentWhack(Alignment * const self, bool const commit) 
{
    rc_t const rc = TableWriterAlgn_Whack(self->tbl[tblPrimary], commit, NULL);
    rc_t const rc2 = self->tbl[tblSecondary] ? TableWriterAlgn_Whack(self->tbl[tblSecondary], commit | (rc == 0), NULL) : 0;

    VDatabaseRelease(self->db);
    free(self);
    return rc ? rc : rc2;
}
