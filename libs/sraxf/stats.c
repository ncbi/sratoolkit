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

#include <sra/sradb.h>
#include <vdb/xform.h>
#include <vdb/table.h>
#include <klib/data-buffer.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/rc.h>
#include <kdb/meta.h>
#include <os-native.h> /* strncasecmp */
#include <sysalloc.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

typedef struct sra_meta_stats_node_group_struct
{
    KMDataNode* node_spot_count;
    KMDataNode* node_base_count;
    KMDataNode* node_bio_base_count;
    KMDataNode* node_cmp_base_count;
    KMDataNode* node_spot_min;
    KMDataNode* node_spot_max;
} sra_meta_stats_node_group;

typedef struct sra_meta_stats_data_struct
{
    KMetadata *meta;
    bool compressed;
    sra_meta_stats_node_group table;
    sra_meta_stats_node_group dflt_grp;
    char* last_grp_name;
    uint64_t last_grp_name_len; /* strlen */
    uint64_t last_grp_name_sz; /* buffer size */
    sra_meta_stats_node_group last_grp;
    uint32_t grp_qty;
} sra_meta_stats_data;

static
rc_t sra_meta_stats_node_read(KMDataNode* node, void* value)
{
    rc_t rc = KMDataNodeReadAsU64(node, value);
    if ( rc != 0 )
    {
        if ( GetRCState ( rc ) == rcIncomplete && GetRCObject ( rc ) == rcTransfer )
        {
            * ( uint64_t* ) value = 0;
            rc = 0;
        }
    }
    return rc;
}

static
rc_t sra_meta_stats_node_group_update(sra_meta_stats_node_group* g,
    const int64_t spot_id, const uint32_t spot_len,
    const uint32_t bio_spot_len, const uint32_t cmp_spot_len)
{
    rc_t rc = 0;
    uint64_t u64;
    int64_t i64;

    if( (rc = sra_meta_stats_node_read(g->node_spot_count, &u64)) == 0 ) {
        if( u64 + 1 < u64 ) {
            rc = RC(rcVDB, rcFunction, rcUpdating, rcMetadata, rcOutofrange);
        } else {
            if( spot_id != 0 ) {
                ++u64;
            }
            rc = KMDataNodeWriteB64(g->node_spot_count, &u64);
        }
    }
    if( (rc = sra_meta_stats_node_read(g->node_base_count, &u64)) == 0 ) {
        if( u64 + 1 < u64 ) {
            rc = RC(rcVDB, rcFunction, rcUpdating, rcMetadata, rcOutofrange);
        } else {
            u64 += spot_len;
            rc = KMDataNodeWriteB64(g->node_base_count, &u64);
        }
    }
    if( (rc = sra_meta_stats_node_read(g->node_bio_base_count, &u64)) == 0 ) {
        if( u64 + bio_spot_len < u64 ) {
            rc = RC(rcVDB, rcFunction, rcUpdating, rcMetadata, rcOutofrange);
        } else {
            u64 += bio_spot_len;
            rc = KMDataNodeWriteB64(g->node_bio_base_count, &u64);
        }
    }
    if( g->node_cmp_base_count != NULL ) {
        if( (rc = sra_meta_stats_node_read(g->node_cmp_base_count, &u64)) == 0 ) {
            if( u64 + cmp_spot_len < u64 ) {
                rc = RC(rcVDB, rcFunction, rcUpdating, rcMetadata, rcOutofrange);
            } else {
                u64 += cmp_spot_len;
                rc = KMDataNodeWriteB64(g->node_cmp_base_count, &u64);
            }
        }
    }
    if( (rc = sra_meta_stats_node_read(g->node_spot_max, &i64)) == 0 ) {
        if( i64 == 0 || i64 < spot_id ) {
            i64 = spot_id;
            rc = KMDataNodeWriteB64(g->node_spot_max, &i64);
        }
    }
    if( (rc = sra_meta_stats_node_read(g->node_spot_min, &i64)) == 0 ) {
        if( i64 == 0 || i64 > spot_id ) {
            i64 = spot_id;
            rc = KMDataNodeWriteB64(g->node_spot_min, &i64);
        }
    }
    return rc;
}

static
rc_t sra_meta_stats_node_group_open(KMDataNode* parent, sra_meta_stats_node_group* g, bool compressed)
{
    rc_t rc = 0;
    assert(parent && g);

    if( (rc = KMDataNodeOpenNodeUpdate(parent, &g->node_spot_count, "SPOT_COUNT")) == 0 &&
        (rc = KMDataNodeOpenNodeUpdate(parent, &g->node_base_count, "BASE_COUNT")) == 0 &&
        (rc = KMDataNodeOpenNodeUpdate(parent, &g->node_bio_base_count, "BIO_BASE_COUNT")) == 0 &&
        (rc = KMDataNodeOpenNodeUpdate(parent, &g->node_spot_min, "SPOT_MIN")) == 0 &&
        (rc = KMDataNodeOpenNodeUpdate(parent, &g->node_spot_max, "SPOT_MAX")) == 0 ) {
        if( compressed ) { 
            rc = KMDataNodeOpenNodeUpdate(parent, &g->node_cmp_base_count, "CMP_BASE_COUNT");
        }
    }
    return rc;
}

static
void sra_meta_stats_node_group_release(sra_meta_stats_node_group* g)
{
    if ( g != NULL )
    {
        KMDataNodeRelease(g->node_spot_count);
        KMDataNodeRelease(g->node_base_count);
        KMDataNodeRelease(g->node_bio_base_count);
        KMDataNodeRelease(g->node_cmp_base_count);
        KMDataNodeRelease(g->node_spot_min);
        KMDataNodeRelease(g->node_spot_max);
        memset ( g, 0, sizeof * g );
    }
}

static
void CC sra_meta_stats_whack( void *data )
{
    sra_meta_stats_data *self = data;
    sra_meta_stats_node_group_release(&self->table);
    sra_meta_stats_node_group_release(&self->dflt_grp);
    free(self->last_grp_name);
    sra_meta_stats_node_group_release(&self->last_grp);
    KMetadataRelease(self->meta);
    free(self);
}

static
rc_t sra_meta_stats_make(sra_meta_stats_data** self, VTable* vtbl, bool has_spot_group, bool compressed)
{
    rc_t rc = 0;
    sra_meta_stats_data* data = calloc(1, sizeof(*data));

    assert(self != NULL && vtbl != NULL);

    if( data == NULL ) {
        rc = RC(rcVDB, rcFunction, rcConstructing, rcMemory, rcExhausted);
    } else if( (rc = VTableOpenMetadataUpdate(vtbl, &data->meta)) == 0 ) {
        KMDataNode* node;
        data->compressed = compressed;
        if( (rc = KMetadataOpenNodeUpdate(data->meta, &node, "STATS/TABLE")) == 0 ) {
            rc = sra_meta_stats_node_group_open(node, &data->table, compressed);
            KMDataNodeRelease(node);
        }
        if( rc == 0 && has_spot_group ) {
            if( (rc = KMetadataOpenNodeUpdate(data->meta, &node, "STATS/SPOT_GROUP/default")) == 0 ) {
                rc = sra_meta_stats_node_group_open(node, &data->dflt_grp, compressed);
                KMDataNodeRelease(node);
            }
        }
    }
    if( rc == 0 ) {
        *self = data;
    } else {
        sra_meta_stats_whack(data);
    }
    return rc;
}

static
rc_t CC sra_meta_stats_update(sra_meta_stats_data* self,
    const int64_t spot_id, const uint32_t spot_len,
    const uint32_t bio_spot_len, const uint32_t cmp_spot_len,
    bool has_grp, const char* grp, uint64_t grp_len)
{
    rc_t rc = 0;
    const uint32_t max_grp_qty = 10000;

    assert(self != NULL);

    rc = sra_meta_stats_node_group_update(&self->table, spot_id, spot_len, bio_spot_len, cmp_spot_len);
    if( has_grp && self->grp_qty <= max_grp_qty && rc == 0 )
    {
        /* an empty group is considered default */
        if( grp_len == 0 || grp == NULL || grp[0] == '\0' ||
            (grp_len == 7 && strncasecmp("default", grp, grp_len) == 0 ) )
        {
            rc = sra_meta_stats_node_group_update(&self->dflt_grp, spot_id, spot_len, bio_spot_len, cmp_spot_len);
        }
        else
        {
            size_t i;
            KMDataNode* n;
            const KMDataNode *cn;
            bool new_group, unsafe;

            /* look for cached node */
            if ( self->last_grp_name != NULL &&
                 self->last_grp_name_len == grp_len &&
                 strncmp(self->last_grp_name, grp, grp_len) == 0 )
            {
                return sra_meta_stats_node_group_update(&self->last_grp, spot_id, spot_len, bio_spot_len, cmp_spot_len);
            }

            /* release cached group */
            sra_meta_stats_node_group_release(&self->last_grp);

            /* realloc cached name */
            if ( self->last_grp_name == NULL || grp_len >= self->last_grp_name_sz )
            {
                char *p = realloc ( self -> last_grp_name, grp_len + 1 );
                if ( p == NULL )
                    return RC ( rcXF, rcFunction, rcExecuting, rcMemory, rcExhausted );
    
                self -> last_grp_name = p;
                self -> last_grp_name_sz = grp_len + 1;
            }

            /* sanitize name */
            for ( unsafe = false, i = 0; i < grp_len; ++ i )
            {
                if ( ( self -> last_grp_name [ i ] = grp [ i ] ) == '/' )
                {
                    unsafe = true;
                    self -> last_grp_name [ i ] = '\\';
                }
            }
            self -> last_grp_name_len = i;
            self -> last_grp_name [ i ] = 0;

            /* look for new group */
            new_group = true;
            rc = KMetadataOpenNodeRead(self->meta, &cn, "STATS/SPOT_GROUP/%s", self->last_grp_name );
            if ( rc == 0 )
            {
                new_group = false;
                KMDataNodeRelease ( cn );
            }

            /* detect abusive quantity of nodes */
            if ( new_group && ++self->grp_qty > max_grp_qty )
            {
                rc = KMetadataOpenNodeUpdate(self->meta, &n, "STATS");
                if( rc == 0 )
                {
                    sra_meta_stats_node_group_release(&self->dflt_grp);
                    KMDataNodeDropChild(n, "SPOT_GROUP");
                    KMDataNodeRelease(n);
                    free(self->last_grp_name);
                    self->last_grp_name = NULL;
                }
                return rc;
            }

            /* create new or cache existing group */
            rc = KMetadataOpenNodeUpdate(self->meta, &n, "STATS/SPOT_GROUP/%s", self->last_grp_name );
            if ( rc == 0 )
            {
                rc = sra_meta_stats_node_group_open(n, &self->last_grp, self->compressed);
                if (rc == 0 && new_group) {
                    if (unsafe)
                    {
                        char value [ 512 ], *v = value;
                        if ( grp_len >= sizeof value )
                            v = malloc ( grp_len + 1 );
                        if ( v == NULL )
                            rc = RC ( rcXF, rcFunction, rcExecuting, rcMemory, rcExhausted );
                        else
                        {
                            rc = string_printf ( v, grp_len + 1, NULL, "%.*s", ( uint32_t ) grp_len, grp );
                            assert ( rc == 0 );
                            rc = KMDataNodeWriteAttr(n, "name", v);
                            if ( rc == 0 )
                                memcpy ( self->last_grp_name, grp, grp_len );
                            if ( v != value )
                                free ( v );
                        }
                    }
                    if ( rc == 0 )
                        rc = sra_meta_stats_node_group_update(&self->last_grp, 0, 0, 0, 0);
                }
                KMDataNodeRelease(n);

                if( rc == 0 )
                    rc = sra_meta_stats_node_group_update(&self->last_grp, spot_id, spot_len, bio_spot_len, cmp_spot_len);
            }
        }
    }
    return rc;
}

static
rc_t CC sra_meta_stats_trigger(void *data, const VXformInfo *info, int64_t row_id,
                               VRowResult *rslt, uint32_t argc, const VRowData argv[])
{
    uint32_t i, bio_spot_len;
    const char* grp = NULL;
    uint64_t len = 0;

    uint32_t spot_len = argv[0].u.data.elem_count;
    /* take nreads from read_len */
    uint32_t nreads = argv[1].u.data.elem_count;
    /* get read_len and read_type */
    const INSDC_coord_len *read_len = argv[1].u.data.base;
    const INSDC_SRA_xread_type *read_type = argv[2].u.data.base;
    read_len += argv[1].u.data.first_elem;
    read_type += argv[2].u.data.first_elem;

    assert(argc >= 3 && argc <= 4);
    assert(nreads == argv[2].u.data.elem_count);

    for(i = bio_spot_len = 0; i < nreads; i++) {
        if( (read_type[i] & SRA_READ_TYPE_BIOLOGICAL) != 0 ) {
            bio_spot_len += read_len[i];
        }
    }
    if( argc == 4 ) {
        /* get group name and length */
        grp = argv[3].u.data.base;
        len = argv[3].u.data.elem_count;
        grp += argv[3].u.data.first_elem;
    }
    return sra_meta_stats_update(data, row_id, spot_len, bio_spot_len, 0, argc == 4, grp, len);
}

static
rc_t CC sra_meta_stats_cmp_trigger(void *data, const VXformInfo *info, int64_t row_id,
                                   VRowResult *rslt, uint32_t argc, const VRowData argv[])
{
    uint32_t i, bio_spot_len;
    const char* grp = NULL;
    uint64_t len = 0;

    uint32_t cmp_spot_len = argv[0].u.data.elem_count;
    uint32_t spot_len = argv[1].u.data.elem_count;
    /* take nreads from read_len */
    uint32_t nreads = argv[2].u.data.elem_count;
    /* get read_len and read_type */
    const INSDC_coord_len *read_len = argv[2].u.data.base;
    const INSDC_SRA_xread_type *read_type = argv[3].u.data.base;
    read_len += argv[2].u.data.first_elem;
    read_type += argv[3].u.data.first_elem;

    assert(data != NULL);
    assert(argc >= 4 && argc <= 5);
    assert(nreads == argv[3].u.data.elem_count);

    for(i = bio_spot_len = 0; i < nreads; i++) {
        if( (read_type[i] & SRA_READ_TYPE_BIOLOGICAL) != 0 ) {
            bio_spot_len += read_len[i];
        }
    }
    if( argc == 5 ) {
        /* get group name and length */
        grp = argv[4].u.data.base;
        len = argv[4].u.data.elem_count;
        grp += argv[4].u.data.first_elem;
    }
    return sra_meta_stats_update(data, row_id, spot_len, bio_spot_len, cmp_spot_len, argc == 5, grp, len);
}

static
rc_t CC sra_meta_stats_cmpf_trigger(void *data, const VXformInfo *info, int64_t row_id,
                                   VRowResult *rslt, uint32_t argc, const VRowData argv[])
{
    uint32_t i, bio_spot_len;
    const char* grp = NULL;
    uint64_t len = 0;

    uint32_t cmp_spot_len = argv[0].u.data.elem_count;
    const uint32_t* spot_len = argv[1].u.data.base;
    /* take nreads from read_len */
    uint32_t nreads = argv[2].u.data.elem_count;
    /* get read_len and read_type */
    const INSDC_coord_len *read_len = argv[2].u.data.base;
    const INSDC_SRA_xread_type *read_type = argv[3].u.data.base;
    spot_len += argv[1].u.data.first_elem;
    read_len += argv[2].u.data.first_elem;
    read_type += argv[3].u.data.first_elem;

    assert(data != NULL);
    assert(argc >= 4 && argc <= 5);
    assert(nreads == argv[3].u.data.elem_count);

    for(i = bio_spot_len = 0; i < nreads; i++) {
        if( (read_type[i] & SRA_READ_TYPE_BIOLOGICAL) != 0 ) {
            bio_spot_len += read_len[i];
        }
    }
    if( argc == 5 ) {
        /* get group name and length */
        grp = argv[4].u.data.base;
        len = argv[4].u.data.elem_count;
        grp += argv[4].u.data.first_elem;
    }
    return sra_meta_stats_update(data, row_id, *spot_len, bio_spot_len, cmp_spot_len, argc == 5, grp, len);
}

VTRANSFACT_IMPL ( NCBI_SRA_stats_trigger, 1, 0, 0 )
    ( const void *self, const VXfactInfo *info, VFuncDesc *rslt,
      const VFactoryParams *cp, const VFunctionParams *dp )
{
    rc_t rc;
    sra_meta_stats_data *data;

    assert(dp->argc >= 3 && dp->argc <= 4);

    if( (rc = sra_meta_stats_make(&data, (VTable*)info->tbl, dp->argc > 3, false)) == 0 ) {
        rslt->self = data;
        rslt->whack = sra_meta_stats_whack;
        rslt->variant = vftNonDetRow;
        rslt->u.rf = sra_meta_stats_trigger;
    }
    return rc;
}

VTRANSFACT_IMPL ( NCBI_SRA_cmp_stats_trigger, 1, 0, 0 )
    ( const void *self, const VXfactInfo *info, VFuncDesc *rslt,
      const VFactoryParams *cp, const VFunctionParams *dp )
{
    rc_t rc;
    sra_meta_stats_data *data;

    assert(dp->argc >= 4 && dp->argc <= 5);

    if( (rc = sra_meta_stats_make(&data, (VTable*)info->tbl, dp->argc > 4, true)) == 0 ) {
        rslt->self = data;
        rslt->whack = sra_meta_stats_whack;
        rslt->variant = vftNonDetRow;
        rslt->u.rf = sra_meta_stats_cmp_trigger;
    }
    return rc;
}

VTRANSFACT_IMPL ( NCBI_SRA_cmpf_stats_trigger, 1, 0, 0 )
    ( const void *self, const VXfactInfo *info, VFuncDesc *rslt,
      const VFactoryParams *cp, const VFunctionParams *dp )
{
    rc_t rc;
    sra_meta_stats_data *data;

    assert(dp->argc >= 4 && dp->argc <= 5);

    if( (rc = sra_meta_stats_make(&data, (VTable*)info->tbl, dp->argc > 4, true)) == 0 ) {
        rslt->self = data;
        rslt->whack = sra_meta_stats_whack;
        rslt->variant = vftNonDetRow;
        rslt->u.rf = sra_meta_stats_cmpf_trigger;
    }
    return rc;
}
