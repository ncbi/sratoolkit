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

#include "linker-priv.h"
#include "schema-parse.h"
#include "xform-priv.h"

#include <kfs/dyload.h>
#include <klib/token.h>
#include <klib/symtab.h>
#include <klib/symbol.h>
#include <klib/out.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <stdlib.h>
#include <string.h>
#include <byteswap.h>
#include <assert.h>

extern VTRANSFACT_DECL ( vdb_cast );
extern VTRANSFACT_DECL ( vdb_redimension );
extern VTRANSFACT_DECL ( vdb_row_id );
extern VTRANSFACT_DECL ( vdb_row_len );
extern VTRANSFACT_DECL ( vdb_fixed_row_len );
extern VTRANSFACT_DECL ( vdb_compare );
extern VTRANSFACT_DECL ( vdb_no_compare );
extern VTRANSFACT_DECL ( vdb_range_validate );
extern VTRANSFACT_DECL ( vdb_merge );
extern VTRANSFACT_DECL ( vdb_split );
extern VTRANSFACT_DECL ( vdb_transpose );
extern VTRANSFACT_DECL ( vdb_detranspose );
extern VTRANSFACT_DECL ( vdb_delta_average );
extern VTRANSFACT_DECL ( vdb_undelta_average );
extern VTRANSFACT_DECL ( meta_read );
extern VTRANSFACT_DECL ( meta_value );
extern VTRANSFACT_DECL ( meta_attr_read );
extern VTRANSFACT_DECL ( idx_text_project );
extern VTRANSFACT_DECL ( idx_text_lookup );
extern VTRANSFACT_DECL ( parameter_read );
extern VTRANSFACT_DECL ( environment_read );

/* select is REALLY internal */
static
rc_t CC select_func ( void *self, const VXformInfo *info, int64_t row_id,
    VRowResult *rslt, uint32_t argc, const VRowData argv [] )
{
    return 0;
}

VTRANSFACT_BUILTIN_IMPL ( vdb_select, 1, 0, 0 ) ( const void *self,
    const VXfactInfo *info, VFuncDesc *rslt, const VFactoryParams *cp, const VFunctionParams *dp )
{
    /* set function pointer to non-NULL */
    rslt -> u . rf = select_func;
    rslt -> variant = vftSelect;
    return 0;
}

/* temporary silly stuff
 */

static
rc_t CC hello_func ( void *self, const VXformInfo *info, int64_t row_id,
    VRowResult *rslt, uint32_t argc, const VRowData argv [] )
{
    char *func_hello = self;
    OUTMSG (( "%s - row id %ld\n", func_hello, row_id ));
    return 0;
}

VTRANSFACT_BUILTIN_IMPL ( vdb_hello, 1, 0, 0 ) ( const void *self,
    const VXfactInfo *info, VFuncDesc *rslt, const VFactoryParams *cp, const VFunctionParams *dp )
{
    const char *fact_hello = "vdb:hello factory";
    const char *func_hello = "vdb:hello function";

    if ( cp -> argc > 0 )
    {
        fact_hello = cp -> argv [ 0 ] . data . ascii;
        if ( cp -> argc > 1 )
            func_hello = cp -> argv [ 1 ] . data . ascii;
    }

    rslt -> self = malloc ( strlen ( func_hello ) + 1 );
    if ( rslt -> self == NULL )
        return RC ( rcVDB, rcFunction, rcConstructing, rcMemory, rcExhausted );
    strcpy ( rslt -> self, func_hello );
    rslt -> whack = free;
    rslt -> u . rf = hello_func;
    rslt -> variant = vftRow;

    OUTMSG (( "%s - %u factory params, %u function params\n", fact_hello, cp -> argc, dp -> argc ));
    return 0;
}

/* InitFactories
 */
static
rc_t CC VLinkerEnterFactory ( KSymTable *tbl, const SchemaEnv *env,
    LFactory *lfact, const char *name )
{
    rc_t rc;

    KTokenSource src;
    KTokenText tt;
    KToken t;

    KTokenTextInitCString ( & tt, name, "VLinkerEnterFactory" );
    KTokenSourceInit ( & src, & tt );
    next_token ( tbl, & src, & t );

    rc = create_fqn ( tbl, & src, & t, env, ltFactory, lfact );
    if ( rc == 0 )
        lfact -> name = t . sym;

    return rc;
}


rc_t VLinkerAddFactories ( VLinker *self,
    const VLinkerIntFactory *fact, uint32_t count,
    KSymTable *tbl, const SchemaEnv *env )
{
    uint32_t i;
    for ( i = 0; i < count; ++ i )
    {
        rc_t rc;
        LFactory *lfact = malloc ( sizeof * lfact );
        if ( lfact == NULL )
            return RC ( rcVDB, rcFunction, rcRegistering, rcMemory, rcExhausted );

        /* invoke factory to get description */
        rc = ( * fact [ i ] . f ) ( & lfact -> desc );
        if ( rc != 0 )
        {
            free ( lfact );
            return rc;
        }

        /* I am intrinsic and have no dl symbol */
        lfact -> addr = NULL;
        lfact -> name = NULL;
        lfact -> external = false;

        /* add to linker */
        rc = VectorAppend ( & self -> fact, & lfact -> id, lfact );
        if ( rc != 0 )
        {
            LFactoryWhack ( lfact, NULL );
            return rc;
        }

        /* create name */
        rc = VLinkerEnterFactory ( tbl, env, lfact, fact [ i ] . name );
        if ( rc != 0 )
        {
            void *ignore;
            VectorSwap ( & self -> fact, lfact -> id, NULL, & ignore );
            LFactoryWhack ( lfact, NULL );
            return rc;
        }
    }

    return 0;
}

/* InitFactories
 */
rc_t VLinkerInitFactoriesRead ( VLinker *self,  KSymTable *tbl, const SchemaEnv *env )
{
    static VLinkerIntFactory fact [] =
    {
        { vdb_cast, "vdb:cast" },
        { vdb_redimension, "vdb:redimension" },
        { vdb_row_id, "vdb:row_id" },
        { vdb_row_len, "vdb:row_len" },
        { vdb_fixed_row_len, "vdb:fixed_row_len" },
        { vdb_select, "vdb:select" },
        { vdb_compare, "vdb:compare" },
        { vdb_no_compare, "vdb:no_compare" },
        { vdb_range_validate, "vdb:range_validate" },
        { vdb_merge, "vdb:merge" },
        { vdb_split, "vdb:split" },
        { vdb_transpose, "vdb:transpose" },
        { vdb_detranspose, "vdb:detranspose" },
        { vdb_delta_average, "vdb:delta_average" },
        { vdb_undelta_average, "vdb:undelta_average" },
        { meta_read, "meta:read" },
        { meta_value, "meta:value" },
        { meta_attr_read, "meta:attr:read" },
        { idx_text_project, "idx:text:project" },
        { idx_text_lookup, "idx:text:lookup" },
        { parameter_read, "parameter:read" },
/*        { environment_read, "environment:read" }, */

        { vdb_hello, "vdb:hello" }
    };

    return VLinkerAddFactories ( self, fact, sizeof fact / sizeof fact [ 0 ], tbl, env );
}


/* MakeIntrinsic
 *  creates an initial, intrinsic linker
 *  pre-loaded with intrinsic factories
 */
rc_t VLinkerMakeIntrinsic ( VLinker **lp )
{
    KDyld *dl;
    rc_t rc = KDyldMake ( & dl );
    if ( rc == 0 )
    {
        rc = VLinkerMake ( lp, NULL, dl );
        KDyldRelease ( dl );

        if ( rc == 0 )
        {
            KSymTable tbl;
            VLinker *self = * lp;

            /* create symbol table with no intrinsic scope */
            rc = KSymTableInit ( & tbl, NULL );
            if ( rc == 0 )
            {
                SchemaEnv env;
                SchemaEnvInit ( & env, EXT_SCHEMA_LANG_VERSION );

                /* make intrinsic scope modifiable */
                KSymTablePushScope ( & tbl, & self -> scope );

                /* add intrinsic functions */
                rc = VLinkerInitFactories ( self, & tbl, & env );
                if ( rc == 0 )
                {
                    KSymTableWhack ( & tbl );
                    return 0;
                }

                KSymTableWhack ( & tbl );
            }

            VLinkerRelease ( self );
        }
    }

    * lp = NULL;

    return rc;
}
