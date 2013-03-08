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

#include "reref.h"

#include <kfs/file.h>
#include <vfs/manager.h>
#include <vfs/path.h>
#include <align/reference.h>

#include <sysalloc.h>
#include <stdlib.h>

typedef struct report_row_ctx
{
    uint32_t prim_idx;
    uint32_t sec_idx;
    int64_t row_id;
} report_row_ctx;


static rc_t report_ref_row( const VCursor *cur, report_row_ctx * row_ctx )
{
    rc_t rc = 0;
    uint32_t elem_bits, boff, prim_count, sec_count;
    const void *base;
    rc = VCursorCellDataDirect ( cur, row_ctx->row_id, row_ctx->prim_idx, &elem_bits, &base, &boff, &prim_count );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot read colum >PRIMARY_ALIGNMENT_IDS<" );
    }
    else
    {
        rc = VCursorCellDataDirect ( cur, row_ctx->row_id, row_ctx->sec_idx, &elem_bits, &base, &boff, &sec_count );
        if ( rc != 0 )
        {
            (void)LOGERR( klogErr, rc, "cannot read colum >SECONDARY_ALIGNMENT_IDS<" );
        }
        else if ( prim_count > 0 || sec_count > 0 )
        {
            rc = KOutMsg( "ROW[ %,lu ]: PRIM:%,u SEC:%,u\n", row_ctx->row_id, prim_count, sec_count );
        }
    }
    return rc;
}


static rc_t report_ref_cursor( const VCursor *cur, int64_t start, int64_t stop )
{
    report_row_ctx row_ctx;
    rc_t rc = VCursorAddColumn ( cur, &row_ctx.prim_idx, "PRIMARY_ALIGNMENT_IDS" );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot add column >PRIMARY_ALIGNMENT_IDS<" );
    }
    else
    {
        rc = VCursorAddColumn ( cur, &row_ctx.sec_idx, "SECONDARY_ALIGNMENT_IDS" );
        if ( rc != 0 )
        {
            (void)LOGERR( klogErr, rc, "cannot add column >SECONDARY_ALIGNMENT_IDS<" );
        }
        else
        {
            rc = VCursorOpen ( cur );
            if ( rc != 0 )
            {
                (void)LOGERR( klogErr, rc, "cannot open REFERENCE-CURSOR" );
            }
            else
            {
                for ( row_ctx.row_id = start; rc == 0 && row_ctx.row_id <= stop; ++row_ctx.row_id )
                {
                    rc = report_ref_row( cur, &row_ctx );
                }
            }
        }
    }
    return rc;
}


static rc_t report_ref_table( const VDBManager *vdb_mgr, const char * path, int64_t start, int64_t stop )
{
    const VDatabase* db;
    rc_t rc = VDBManagerOpenDBRead ( vdb_mgr, &db, NULL, "%s", path );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot open vdb-database" );
    }
    else
    {
        const VTable* tb;
        rc = VDatabaseOpenTableRead ( db, &tb, "REFERENCE" );
        if ( rc != 0 )
        {
            (void)LOGERR( klogErr, rc, "cannot open REFERENCE-table" );
        }
        else
        {
            const VCursor *cur;
            rc = VTableCreateCursorRead ( tb, &cur );
            if ( rc != 0 )
            {
                (void)LOGERR( klogErr, rc, "cannot open REFERENCE-cursor" );
            }
            else
            {
                rc = report_ref_cursor( cur, start, stop );
                VCursorRelease( cur );
            }
            VTableRelease ( tb );
        }
        VDatabaseRelease ( db );
    }
    return rc;
}


static rc_t report_ref_table2( const ReferenceObj* ref_obj, int64_t start, int64_t stop )
{
    rc_t rc = 0;
    int64_t row_id;
    for ( row_id = start; rc == 0 && row_id <= stop; ++row_id )
    {
        uint32_t count[ 3 ];
        rc = ReferenceObj_GetIdCount( ref_obj, row_id, count );
        if ( rc == 0 && ( count[ 0 ] > 0 || count[ 1 ] > 0 || count[ 2 ] > 0 ) )
            rc = KOutMsg( "#%,lu:\t%,u PRI\t%,u SEC\t%,u EV\n",
                          row_id, count[ 0 ], count[ 1 ], count[ 2 ] );
    }
    return rc;
}


static rc_t report_ref_obj( const VDBManager *vdb_mgr, const char * path, uint32_t idx,
                            const ReferenceObj* ref_obj, bool short_report )
{
    const char * s;
    INSDC_coord_len len;
    bool circular, external;
    int64_t start, stop;

    rc_t rc = ReferenceObj_Name( ref_obj, &s );
    if ( rc == 0 )
        rc = KOutMsg( "\nREF[%u].Name     = '%s'\n", idx, s );

    if ( rc == 0 )
        rc = ReferenceObj_SeqId( ref_obj, &s );
    if ( rc == 0 )
        rc = KOutMsg( "REF[%u].SeqId    = '%s'\n", idx, s );

    if ( rc == 0 )
        rc = ReferenceObj_SeqLength( ref_obj, &len );
    if ( rc == 0 )
        rc = KOutMsg( "REF[%u].Length   = %,u\n", idx, len );

    if ( rc == 0 )
        rc = ReferenceObj_Circular( ref_obj, &circular );
    if ( rc == 0 )
        rc = KOutMsg( "REF[%u].Circular = %s\n", idx, circular ? "yes" : "no" );

    if ( rc == 0 )
        rc = ReferenceObj_IdRange( ref_obj, &start, &stop );
    if ( rc == 0 )
        rc = KOutMsg( "REF[%u].IdRange  = [%,lu]...[%,lu]\n", idx, start, stop );

    if ( rc == 0 )
        rc = ReferenceObj_External( ref_obj, &external, NULL );
    if ( rc == 0 )
        rc = KOutMsg( "REF[%u].Extern   = %s\n", idx, external ? "yes" : "no" );

    if ( rc == 0 && !short_report )
        rc = report_ref_table2( ref_obj, start, stop );


/*
    if ( rc == 0 )
        rc = report_ref_table( vdb_mgr, path, start, stop );
*/
    return rc;
}


static rc_t report_ref_database( const VDBManager *vdb_mgr, const char * path, bool short_report )
{
    const ReferenceList* reflist;
    uint32_t options = ( ereferencelist_usePrimaryIds | ereferencelist_useSecondaryIds | ereferencelist_useEvidenceIds );
    rc_t rc = ReferenceList_MakePath( &reflist, vdb_mgr, path, options, 0, NULL, 0 );
    if ( rc != 0 )
    {
        (void)LOGERR( klogErr, rc, "cannot create ReferenceList" );
    }
    else
    {
        uint32_t count;
        rc = ReferenceList_Count( reflist, &count );
        if ( rc != 0 )
        {
            (void)LOGERR( klogErr, rc, "ReferenceList_Count() failed" );
        }
        else
        {
            rc = KOutMsg( "this object uses %u references\n", count );
            if ( rc == 0 )
            {
                uint32_t idx;
                for ( idx = 0; idx < count && rc == 0; ++idx )
                {
                    const ReferenceObj* ref_obj;
                    rc = ReferenceList_Get( reflist, &ref_obj, idx );
                    if ( rc != 0 )
                    {
                        (void)LOGERR( klogErr, rc, "ReferenceList_Get() failed" );
                    }
                    else
                    {
                        rc = report_ref_obj( vdb_mgr, path, idx, ref_obj, short_report );
                        ReferenceObj_Release( ref_obj );
                    }
                }
            }
        }
        ReferenceList_Release( reflist );
    }
    return rc;
}

static rc_t report_references( const VDBManager *vdb_mgr, VFSManager * vfs_mgr, const char * spec, bool short_report )
{
    rc_t rc = KOutMsg( "\nreporting references of '%s'\n", spec );
    if ( rc == 0 )
    {
        VPath * path = NULL;
        const VPath * local_cache = NULL;
        const KFile * remote_file = NULL;
        rc = VFSManagerResolveSpec ( vfs_mgr, spec, &path, &remote_file, &local_cache, true );
        if ( rc != 0 )
        {
            (void)LOGERR( klogErr, rc, "cannot resolve spec via VFSManager" );
        }
        else
        {
            char buffer[ 4096 ];
            size_t num_read;
            rc = VPathReadPath ( path, buffer, sizeof buffer, &num_read );
            if ( rc != 0 )
            {
                (void)LOGERR( klogErr, rc, "cannot read path from vpath" );
            }
            else
            {
                rc = KOutMsg( "resolved into '%s'\n", buffer );
                if ( rc == 0 )
                {
                    const KDBManager * kdb_mgr;
                    rc = VDBManagerOpenKDBManagerRead( vdb_mgr, &kdb_mgr );
                    if ( rc != 0 )
                    {
                        (void)LOGERR( klogErr, rc, "cannot get kdb-manager from vdb-manager" );
                    }
                    else
                    {
                        int path_type = ( KDBManagerPathType( kdb_mgr, buffer ) & ~ kptAlias );
                        switch( path_type )
                        {
                            case kptDatabase : rc = report_ref_database( vdb_mgr, buffer, short_report );
                                               break;

                            case kptTable    : rc = KOutMsg( "cannot report references on a table-object\n" );
                                               break;

                            default          : rc = KOutMsg( "the given object is not a vdb-database\n" );
                                               break;
                        }
                        KDBManagerRelease( kdb_mgr );
                    }
                }
            }
            KFileRelease( remote_file );
            VPathRelease ( local_cache );
            VPathRelease ( path );
        }
    }
    return rc;
}


rc_t report_on_reference( Args * args, bool short_report )
{
    uint32_t count;
    rc_t rc = ArgsParamCount( args, &count );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "ArgsParamCount() failed" );
    }
    else
    {
        KDirectory *dir; 
        rc = KDirectoryNativeDir( &dir );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "KDirectoryNativeDir() failed" );
        }
        else
        {
            const VDBManager *vdb_mgr;
            rc = VDBManagerMakeRead ( &vdb_mgr, dir );
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "VDBManagerMakeRead() failed" );
            }
            else
            {
                VFSManager * vfs_mgr;
                rc =  VFSManagerMake ( &vfs_mgr );
                if ( rc != 0 )
                {
                    (void)LOGERR( klogErr, rc, "cannot make vfs-manager" );
                }
                else
                {
                    uint32_t idx;
                    for ( idx = 0; idx < count && rc == 0; ++idx )
                    {
                        const char *param = NULL;
                        rc = ArgsParamValue( args, idx, &param );
                        if ( rc != 0 )
                        {
                            LOGERR( klogInt, rc, "ArgsParamvalue() failed" );
                        }
                        else
                        {
                            /* rc value not used, because it can be something that has no references */
                            report_references( vdb_mgr, vfs_mgr, param, short_report );
                        }
                    }
                    VFSManagerRelease ( vfs_mgr );
                }
                VDBManagerRelease( vdb_mgr );
            }
            KDirectoryRelease( dir );
        }
    }
    return rc;
}