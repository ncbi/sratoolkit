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

#define TRACK_REFERENCES 0

#include "linker-priv.h"
#include <sysalloc.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


extern VTRANSFACT_DECL ( meta_write );
extern VTRANSFACT_DECL ( meta_attr_write );

/* InitFactories
 */
rc_t VLinkerInitFactories ( VLinker *self, struct KSymTable *tbl, struct SchemaEnv const *env )
{
    static VLinkerIntFactory fact [] =
    {
        { meta_write, "meta:write" },
        { meta_attr_write, "meta:attr:write" }
    };

    rc_t rc = VLinkerInitFactoriesRead ( self, tbl, env );
    if ( rc == 0 )
        rc = VLinkerAddFactories ( self, fact, sizeof fact / sizeof fact [ 0 ], tbl, env );
    return rc;
}
