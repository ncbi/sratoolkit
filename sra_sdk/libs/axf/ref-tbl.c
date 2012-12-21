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
#include <vdb/database.h>
#include <vdb/table.h>
#include <kdb/meta.h>
#include <klib/rc.h>

#include "ref-tbl.h"
#include <string.h>

rc_t AlignRefTable(const VTable* table, const VTable** ref_table)
{
    rc_t rc = 0;
    const VDatabase *db = NULL;

    if( (rc = VTableOpenParentRead(table, &db)) != 0 ) {
        rc = ResetRCContext(rc, rcXF, GetRCTarget(rc), GetRCContext(rc));
    } else if( db == NULL ) {
        rc = RC(rcXF, rcDatabase, rcAccessing, rcItem, rcNull);
    } else {
        char ref_tbl_name[512];
        const KMetadata* meta;
        if( (rc = VTableOpenMetadataRead(table, &meta)) == 0 ) {
            const KMDataNode* node;
            if( (rc = KMetadataOpenNodeRead(meta, &node, "CONFIG/REF_TABLE")) == 0 ) {
                size_t sz;
                rc = KMDataNodeReadCString(node, ref_tbl_name, sizeof(ref_tbl_name), &sz);
                ref_tbl_name[sz] = '\0';
                KMDataNodeRelease(node);
            } 
            KMetadataRelease(meta);
        }
        if( rc != 0 ) {
            strcpy(ref_tbl_name, "REFERENCE");
        }
        rc = VDatabaseOpenTableRead(db, ref_table, ref_tbl_name);
    }
    VDatabaseRelease(db);
    return rc;
}
