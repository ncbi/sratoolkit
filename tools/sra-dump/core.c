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

#include <vdb/table.h> /* VTableRelease */
#include <kfg/config.h> /* KConfigDisableUserSettings */

#include <vdb/manager.h> /* VDBManagerRelease */
#include <vdb/dependencies.h> /* UIError */
#include <klib/report.h> /* ReportInit */
#include <vdb/report.h>
#include <klib/container.h>
#include <klib/log.h>
#include <klib/out.h>
#include <klib/status.h>
#include <kapp/main.h>
#include <kfs/directory.h>
#include <sra/sradb-priv.h>
#include <sra/types.h>
#include <os-native.h>
#include <sysalloc.h>

#include "debug.h"
#include "core.h"
#include "fasta_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ### checks to see if NREADS <= nreads_max defined in factory.h ##################################################### */

typedef struct MaxNReadsValidator_struct
{
    const SRAColumn* col;
} MaxNReadsValidator;


static rc_t MaxNReadsValidator_GetKey( const SRASplitter* cself, 
    const char** key, spotid_t spot, readmask_t* readmask )
{
    rc_t rc = 0;
    MaxNReadsValidator* self = ( MaxNReadsValidator* )cself;

    if ( self == NULL || key == NULL )
    {
        rc = RC( rcSRA, rcNode, rcExecuting, rcParam, rcNull );
    }
    else
    {
        const void* nreads = NULL;
        bitsz_t o = 0, sz = 0;
        uint64_t nn = 0;

        *key = "";
        if ( self->col != NULL )
        {
            rc = SRAColumnRead( self->col, spot, &nreads, &o, &sz );
            if ( rc == 0 )
            {
                switch( sz )
                {
                    case 8:
                        nn = *((const uint8_t*)nreads);
                        break;
                    case 16:
                        nn = *((const uint16_t*)nreads);
                        break;
                    case 32:
                        nn = *((const uint32_t*)nreads);
                        break;
                    case 64:
                        nn = *((const uint64_t*)nreads);
                        break;
                    default:
                        rc = RC( rcSRA, rcNode, rcExecuting, rcData, rcUnexpected );
                        break;
                }
                if ( nn > nreads_max )
                {
                    clear_readmask( readmask );
                    PLOGMSG(klogWarn, (klogWarn, "too many reads $(nreads) at spot id $(row), maximum $(max) supported, skipped",
                                       PLOG_3(PLOG_U64(nreads),PLOG_I64(row),PLOG_U32(max)), nn, spot, nreads_max));
                }
                else if ( nn == nreads_max - 1 )
                {
                    PLOGMSG(klogWarn, (klogWarn, "too many reads $(nreads) at spot id $(row), truncated to $(max)",
                                       PLOG_3(PLOG_U64(nreads),PLOG_I64(row),PLOG_U32(max)), nn + 1, spot, nreads_max));
                }
            }
        }
    }
    return rc;
}

typedef struct MaxNReadsValidatorFactory_struct
{
    const SRATable* table;
    const SRAColumn* col;
} MaxNReadsValidatorFactory;


static rc_t MaxNReadsValidatorFactory_Init( const SRASplitterFactory* cself )
{
    rc_t rc = 0;
    MaxNReadsValidatorFactory* self = ( MaxNReadsValidatorFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcConstructing, rcParam, rcNull );
    }
    else
    {
        rc = SRATableOpenColumnRead( self->table, &self->col, "NREADS", NULL );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) == rcNotFound || GetRCState( rc ) == rcExists )
            {
                rc = 0;
            }
        }
    }
    return rc;
}


static rc_t MaxNReadsValidatorFactory_NewObj( const SRASplitterFactory* cself, const SRASplitter** splitter )
{
    rc_t rc = 0;
    MaxNReadsValidatorFactory* self = ( MaxNReadsValidatorFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcExecuting, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitter_Make( splitter, sizeof(MaxNReadsValidator),
                               MaxNReadsValidator_GetKey, NULL, NULL, NULL );
        if ( rc == 0 )
        {
            ( (MaxNReadsValidator*)(*splitter) )->col = self->col;
        }
    }
    return rc;
}


static void MaxNReadsValidatorFactory_Release( const SRASplitterFactory* cself )
{
    if ( cself != NULL )
    {
        MaxNReadsValidatorFactory* self = ( MaxNReadsValidatorFactory* )cself;
        SRAColumnRelease( self->col );
    }
}


static rc_t MaxNReadsValidatorFactory_Make( const SRASplitterFactory** cself, const SRATable* table )
{
    rc_t rc = 0;
    MaxNReadsValidatorFactory* obj = NULL;

    if( cself == NULL || table == NULL )
    {
        rc = RC( rcSRA, rcType, rcAllocating, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitterFactory_Make( cself, eSplitterSpot, sizeof( *obj ),
                                     MaxNReadsValidatorFactory_Init,
                                     MaxNReadsValidatorFactory_NewObj,
                                     MaxNReadsValidatorFactory_Release);
        if ( rc == 0 )
        {
            obj = ( MaxNReadsValidatorFactory* )*cself;
            obj->table = table;
        }
    }
    return rc;
}

/* ### READ_FILTER splitter/filter ##################################################### */

enum EReadFilterSplitter_names
{
    EReadFilterSplitter_pass = 0,
    EReadFilterSplitter_reject,
    EReadFilterSplitter_criteria,
    EReadFilterSplitter_redacted,
    EReadFilterSplitter_unknown,
    EReadFilterSplitter_max
};


typedef struct ReadFilterSplitter_struct
{
    const SRAColumn* col_rdf;
    SRAReadFilter read_filter;
    SRASplitter_Keys keys[5];
} ReadFilterSplitter;


static rc_t ReadFilterSplitter_GetKeySet( const SRASplitter* cself,
        const SRASplitter_Keys** key, uint32_t* keys, spotid_t spot, const readmask_t* readmask )
{
    rc_t rc = 0;
    ReadFilterSplitter* self = ( ReadFilterSplitter* )cself;

    if ( self == NULL || key == NULL )
    {
        rc = RC( rcSRA, rcNode, rcExecuting, rcParam, rcNull );
    }
    else
    {
        const INSDC_SRA_read_filter* rdf;
        bitsz_t o = 0, sz = 0;

        *keys = 0;
        if ( self->col_rdf != NULL )
        {
            rc = SRAColumnRead( self->col_rdf, spot, (const void **)&rdf, &o, &sz );
            if ( rc == 0 )
            {
                int32_t j, i = sz / sizeof( INSDC_SRA_read_filter ) / 8;
                *key = self->keys;
                *keys = sizeof( self->keys ) / sizeof( self->keys[ 0 ] );
                for ( j = 0; j < *keys; j++ )
                {
                    clear_readmask( self->keys[ j ].readmask );
                }
                while ( i > 0 )
                {
                    i--;
                    if ( self->read_filter != 0xFF && self->read_filter != rdf[i] )
                    {
                        /* skip by filter value != to command line */
                    }
                    else if ( rdf[ i ] == SRA_READ_FILTER_PASS )
                    {
                        set_readmask( self->keys[ EReadFilterSplitter_pass ].readmask, i );
                    }
                    else if ( rdf[ i ] == SRA_READ_FILTER_REJECT )
                    {
                        set_readmask( self->keys[ EReadFilterSplitter_reject ].readmask, i );
                    }
                    else if( rdf[ i ] == SRA_READ_FILTER_CRITERIA )
                    {
                        set_readmask( self->keys[ EReadFilterSplitter_criteria ].readmask, i );
                    }
                    else if( rdf[ i ] == SRA_READ_FILTER_REDACTED )
                    {
                        set_readmask( self->keys[ EReadFilterSplitter_redacted ].readmask, i );
                    }
                    else
                    {
                        set_readmask( self->keys[ EReadFilterSplitter_unknown ].readmask, i );
                        PLOGMSG( klogWarn, ( klogWarn,
                                 "unknown READ_FILTER value $(value) at spot id $(row)",
                                 PLOG_2( PLOG_U8( value ), PLOG_I64( row ) ), rdf[ i ], spot ) );
                    }
                }
            }
        }
    }
    return rc;
}


typedef struct ReadFilterSplitterFactory_struct
{
    const SRATable* table;
    const SRAColumn* col_rdf;
    SRAReadFilter read_filter;
} ReadFilterSplitterFactory;


static rc_t ReadFilterSplitterFactory_Init( const SRASplitterFactory* cself )
{
    rc_t rc = 0;
    ReadFilterSplitterFactory* self = ( ReadFilterSplitterFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcConstructing, rcParam, rcNull );
    }
    else
    {
        rc = SRATableOpenColumnRead( self->table, &self->col_rdf, "READ_FILTER", sra_read_filter_t );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) == rcNotFound )
            {
                LOGMSG( klogWarn, "Column READ_FILTER was not found, param ignored" );
                rc = 0;
            }
            else if ( GetRCState( rc ) == rcExists )
            {
                rc = 0;
            }
        }
    }
    return rc;
}


static rc_t ReadFilterSplitterFactory_NewObj( const SRASplitterFactory* cself, const SRASplitter** splitter )
{
    rc_t rc = 0;
    ReadFilterSplitterFactory* self = ( ReadFilterSplitterFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcExecuting, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitter_Make( splitter, sizeof(ReadFilterSplitter), NULL,
                               ReadFilterSplitter_GetKeySet, NULL, NULL );
        if ( rc == 0 )
        {
            ( (ReadFilterSplitter*)(*splitter) )->col_rdf = self->col_rdf;
            ( (ReadFilterSplitter*)(*splitter) )->read_filter = self->read_filter;
            ( (ReadFilterSplitter*)(*splitter) )->keys[ EReadFilterSplitter_pass ].key = "pass";
            ( (ReadFilterSplitter*)(*splitter) )->keys[ EReadFilterSplitter_reject ].key = "reject";
            ( (ReadFilterSplitter*)(*splitter) )->keys[ EReadFilterSplitter_criteria ].key = "criteria";
            ( (ReadFilterSplitter*)(*splitter) )->keys[ EReadFilterSplitter_redacted ].key = "redacted";
            ( (ReadFilterSplitter*)(*splitter) )->keys[ EReadFilterSplitter_unknown ].key = "unknown";
        }
    }
    return rc;
}


static void ReadFilterSplitterFactory_Release( const SRASplitterFactory* cself )
{
    if ( cself != NULL )
    {
        ReadFilterSplitterFactory* self = ( ReadFilterSplitterFactory* )cself;
        SRAColumnRelease( self->col_rdf );
    }
}


static rc_t ReadFilterSplitterFactory_Make( const SRASplitterFactory** cself,
            const SRATable* table, SRAReadFilter read_filter )
{
    rc_t rc = 0;
    ReadFilterSplitterFactory* obj = NULL;

    if ( cself == NULL || table == NULL )
    {
        rc = RC( rcSRA, rcType, rcAllocating, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitterFactory_Make( cself, eSplitterRead, sizeof( *obj ),
                                        ReadFilterSplitterFactory_Init,
                                        ReadFilterSplitterFactory_NewObj,
                                        ReadFilterSplitterFactory_Release );
        if ( rc == 0 )
        {
            obj = ( ReadFilterSplitterFactory* ) *cself;
            obj->table = table;
            obj->read_filter = read_filter;
        }
    }
    return rc;
}


/* ### SPOT_GROUP splitter/filter ##################################################### */

typedef struct SpotGroupSplitter_struct
{
    char cur_key[ 256 ];
    const SRAColumn* col;
    bool split;
    char* const* spot_group;
} SpotGroupSplitter;


static rc_t SpotGroupSplitter_GetKey( const SRASplitter* cself,
            const char** key, spotid_t spot, readmask_t* readmask )
{
    rc_t rc = 0;
    SpotGroupSplitter* self = ( SpotGroupSplitter* )cself;

    if ( self == NULL || key == NULL )
    {
        rc = RC( rcSRA, rcNode, rcExecuting, rcParam, rcNull );
    }
    else
    {
        *key = self->cur_key;
        if ( self->col != NULL )
        {
            const char* g = NULL;
            bitsz_t o = 0, sz = 0;
            rc = SRAColumnRead( self->col, spot, (const void **)&g, &o, &sz );
            if ( rc == 0 )
            {
                sz /= 8;
                /* truncate trailing \0 */
                while ( sz > 0 && g[ sz - 1 ] == '\0' )
                {
                    sz--;
                }
                if ( sz > sizeof( self->cur_key ) - 1 )
                {
                    rc = RC( rcSRA, rcNode, rcExecuting, rcBuffer, rcInsufficient );
                }
                else
                {
                    int i;
                    bool found = false;
                    memcpy( self->cur_key, g, sz );
                    self->cur_key[ sz ] = '\0';
                    for ( i = 0; self->spot_group[ i ] != NULL; i++ )
                    {
                        if ( strcmp( self->cur_key, self->spot_group[ i ] ) == 0 )
                        {
                            found = true;
                            break;
                        }
                    }
                    if ( self->spot_group[ 0 ] != NULL && !found )
                    {
                        /* list not empty and not in list -> skip */
                        *key = NULL;
                    }
                    else if ( !self->split )
                    {
                        *key = "";
                    }
                }
            }
        }
    }
    return rc;
}


typedef struct SpotGroupSplitterFactory_struct
{
    const SRATable* table;
    const SRAColumn* col;
    bool split;
    char* const* spot_group;
} SpotGroupSplitterFactory;


static rc_t SpotGroupSplitterFactory_Init( const SRASplitterFactory* cself )
{
    rc_t rc = 0;
    SpotGroupSplitterFactory* self = ( SpotGroupSplitterFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcConstructing, rcParam, rcNull );
    }
    else
    {
        rc = SRATableOpenColumnRead( self->table, &self->col, "SPOT_GROUP", vdb_ascii_t );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) == rcNotFound )
            {
                LOGMSG(klogWarn, "Column SPOT_GROUP was not found, param ignored");
                rc = 0;
            }
            else if ( GetRCState( rc ) == rcExists )
            {
                rc = 0;
            }
        }
    }
    return rc;
}


static rc_t SpotGroupSplitterFactory_NewObj( const SRASplitterFactory* cself, const SRASplitter** splitter )
{
    rc_t rc = 0;
    SpotGroupSplitterFactory* self = ( SpotGroupSplitterFactory* )cself;

    if ( self == NULL )
    {
        rc = RC( rcSRA, rcType, rcExecuting, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitter_Make( splitter, sizeof( SpotGroupSplitter ),
                               SpotGroupSplitter_GetKey, NULL, NULL, NULL );
        if ( rc == 0 )
        {
            ( (SpotGroupSplitter*)(*splitter) )->col = self->col;
            ( (SpotGroupSplitter*)(*splitter) )->split = self->split;
            ( (SpotGroupSplitter*)(*splitter) )->spot_group = self->spot_group;
        }
    }
    return rc;
}


static void SpotGroupSplitterFactory_Release( const SRASplitterFactory* cself )
{
    if ( cself != NULL )
    {
        SpotGroupSplitterFactory* self = ( SpotGroupSplitterFactory* )cself;
        SRAColumnRelease( self->col );
    }
}


static rc_t SpotGroupSplitterFactory_Make( const SRASplitterFactory** cself,
            const SRATable* table, bool split, char* const spot_group[] )
{
    rc_t rc = 0;
    SpotGroupSplitterFactory* obj = NULL;

    if ( cself == NULL || table == NULL )
    {
        rc = RC( rcSRA, rcType, rcAllocating, rcParam, rcNull );
    }
    else
    {
        rc = SRASplitterFactory_Make( cself, eSplitterSpot, sizeof( *obj ),
                                             SpotGroupSplitterFactory_Init,
                                             SpotGroupSplitterFactory_NewObj,
                                             SpotGroupSplitterFactory_Release );
        if ( rc == 0 )
        {
            obj = ( SpotGroupSplitterFactory* ) *cself;
            obj->table = table;
            obj->split = split;
            obj->spot_group = spot_group;
        }
    }
    return rc;
}

/* ### Common dumper code ##################################################### */


static rc_t SRADumper_DumpRun( const SRATable* table,
        spotid_t minSpotId, spotid_t maxSpotId, const SRASplitterFactory* factories )
{
    rc_t rc = 0, rcr = 0;
    spotid_t spot = 0;
    make_readmask( readmask );
    const SRASplitter* root_splitter = NULL;

    rc = SRASplitterFactory_NewObj( factories, &root_splitter );

    for( spot = minSpotId; rc == 0 && spot <= maxSpotId; spot++ )
    {
        reset_readmask( readmask );
        rc = SRASplitter_AddSpot( root_splitter, spot, readmask );
        rc = rc ? rc : Quitting();
    }
    rcr = SRASplitter_Release( root_splitter );

    return rc ? rc : rcr;
}


static const SRADumperFmt_Arg KMainArgs[] =
{
    { "A", "accession", "accession", {"Replaces accession derived from <path> in filename(s) and deflines (only for single table dump)", NULL}},
    { "O", "outdir", "path", {"Output directory, default is working directory ( '.' )", NULL}},
    { "Z", "stdout", NULL, {"Output to stdout, all split data become joined into single stream", NULL}},
    { NULL, "gzip", NULL, {"Compress output using gzip", NULL}},
    { NULL, "bzip2", NULL, {"Compress output using bzip2", NULL}},
    { "N", "minSpotId", "rowid", {"Minimum spot id", NULL}},
    { "X", "maxSpotId", "rowid", {"Maximum spot id", NULL}},
    { "G", "spot-group", NULL, {"Split into files by SPOT_GROUP (member name)", NULL}},
    { NULL, "spot-groups", "[list]", {"Filter by SPOT_GROUP (member): name[,...]", NULL}},
    { "R", "read-filter", "[filter]", {"Split into files by READ_FILTER value",
                                      "optionally filter by a value: pass|reject|criteria|redacted", NULL}},
    { "T", "group-in-dirs", NULL, {"Split into subdirectories instead of files", NULL}},
    { "K", "keep-empty-files", NULL, {"Do not delete empty files", NULL}},
    { NULL, "table", "table-name", {"Table name within cSRA object, default is \"SEQUENCE\"", NULL}},

    { "h", "help", NULL, {"Output a brief explanation of program usage", NULL}},
    { "V", "version", NULL, {"Display the version of the program", NULL}},

    { "L", "log-level", "level", {"Logging level as number or enum string",
                                 "One of (fatal|sys|int|err|warn|info) or (0-5)",
                                 "Current/default is warn", NULL}},
    { "v", "verbose", NULL, {"Increase the verbosity level of the program",
                            "Use multiple times for more verbosity", NULL}},
    { NULL, "no-user-settings", NULL, {"Do not read any settings on startup. Use VDB_CONFIG environment var to set config file", NULL}},
    { NULL, OPTION_REPORT, NULL, {
"Control program execution environment report generation (if implemented).",
"One of (never|error|always). Default is error",
                      NULL}},
#if _DEBUGGING
        {"+", "debug", "Module[-Flag]", {"Turn on debug output for module",
                                         "All flags if not specified", NULL}},
#endif

    { NULL, NULL, NULL, {NULL}} /* terminator */
};


rc_t CC Usage ( const Args * args )
{
    return fasta_dump_usage ( args );
}


void CC SRADumper_PrintArg( const SRADumperFmt_Arg* arg )
{
    /* ??? */
}


static bool CanResolveAccessions( void )
{
    bool res = false;
    SRAPath *sra_path;
    rc_t rc = SRAPathMake( &sra_path, NULL );
    if ( rc == 0 )
    {
        res = true;
        SRAPathRelease( sra_path );
    }
    return res;
}


static void CoreUsage( const char* prog, const SRADumperFmt* fmt, bool brief, int exit_status )
{
    bool can_resolve_acc = CanResolveAccessions();
    if ( can_resolve_acc )
    {
        OUTMSG(( "\n"
                 "Usage:\n"
                 "  %s [options] <path [path...]>\n"
                 "  %s [options] [ -A ] <accession>\n"
                 "\n", prog, prog));
    }
    else
    {
        OUTMSG(( "\n"
                 "Usage:\n"
                 "  %s [options] <path [path...]>\n"
                 "\n", prog, prog));
    }

    if ( !brief )
    {
        if ( fmt->usage )
        {
            rc_t rc = fmt->usage( fmt, KMainArgs, can_resolve_acc ? 0 : 1 );
            if ( rc != 0 )
            {
                LOGERR(klogErr, rc, "Usage print failed");
            }
        }
        else
        {
            int k, i;
            const SRADumperFmt_Arg* d[ 2 ] = { KMainArgs, NULL };

            d[ 1 ] = fmt->arg_desc;
            for ( k = 0; k < ( sizeof( d ) / sizeof( d[0] ) ); k++ )
            {
                for ( i = can_resolve_acc ? 0 : 1;
                      d[k] != NULL && ( d[ k ][ i ].abbr != NULL || d[ k ][ i ].full != NULL );
                      i++ )
                {
                    if ( ( !fmt->gzip && strcmp( d[ k ][ i ].full, "gzip" ) == 0 ) ||
                         ( !fmt->bzip2 && strcmp (d[ k ][ i ].full, "bzip2" ) == 0 ) )
                    {
                        continue;
                    }
                    if ( k > 0 && i == 0 )
                    {
                        OUTMSG(("\nFormat options:\n\n"));
                    }
                    HelpOptionLine( d[ k ][ i ].abbr, d[ k ][ i ].full,
                                    d[ k ][ i ].param, (const char**)( d[ k ][ i ].descr ) );
                    if ( k == 0 && i == 0 )
                    {
                        OUTMSG(( "\nOptions:\n\n" ));
                    }
                }
            }
        }
    }
    else
    {
        OUTMSG(( "Use option --help for more information\n" ));
    }
    HelpVersion( prog, KAppVersion() );
    exit( exit_status );
}


static rc_t SRADumper_ArgsValidate( const char* prog, const SRADumperFmt* fmt )
{
    rc_t rc = 0;
    int k, i;

    /* set default log level */
    const char* default_log_level = "warn";
    rc = LogLevelSet( default_log_level );
    if ( rc != 0 )
    {
        PLOGERR( klogErr, ( klogErr, rc, "default log level to '$(lvl)'",
                            PLOG_S( lvl ), default_log_level ) );
        CoreUsage( prog, fmt, true, EXIT_FAILURE );
    }
    for ( i = 0; KMainArgs[ i ].abbr != NULL; i++ )
    {
        for ( k = 0; fmt->arg_desc != NULL && fmt->arg_desc[ k ].abbr != NULL; k++ )
        {
            if ( strcmp( fmt->arg_desc[ k ].abbr, KMainArgs[ i ].abbr ) == 0 ||
                 ( fmt->arg_desc[ k ].full != NULL && strcmp( fmt->arg_desc[ k ].full, KMainArgs[ i ].full ) == 0 ) )
            {
                rc = RC(rcExe, rcArgv, rcValidating, rcParam, rcDuplicate);
            }
        }
    }
    return rc;
}


bool CC SRADumper_GetArg( const SRADumperFmt* fmt, char const* const abbr, char const* const full,
                          int* i, int argc, char *argv[], const char** value )
{
    rc_t rc = 0;
    const char* arg = argv[*i];
    while ( *arg == '-' && *arg != '\0')
    {
        arg++;
    }
    if ( abbr != NULL && strcmp(arg, abbr) == 0 )
    {
        SRA_DUMP_DBG( 9, ( "GetArg key: '%s'\n", arg ) );
        arg = arg + strlen( abbr );
        if ( value != NULL && arg[0] == '\0' && (*i + 1) < argc )
        {
            arg = NULL;
            if ( argv[ *i + 1 ][ 0 ] != '-' )
            {
                /* advance only if next is not an option with '-' */
                *i = *i + 1;
                arg = argv[ *i ];
            }
        }
        else
        {
            arg = NULL;
        }
    }
    else if ( full != NULL && strcmp( arg, full ) == 0 )
    {
        SRA_DUMP_DBG( 9, ( "GetArg key: '%s'\n", arg ) );
        arg = NULL;
        if ( value != NULL && ( *i + 1 ) < argc )
        {
            if ( argv[ *i + 1 ][ 0 ] != '-' )
            {
                /* advance only if next is not an option with '-' */
                *i = *i + 1;
                arg = argv[ *i ];
            }
        }
    }
    else
    {
        return false;
    }

    SRA_DUMP_DBG( 9, ( "GetArg val: '%s'\n", arg ) );
    if ( value == NULL && arg != NULL )
    {
        rc = RC( rcApp, rcArgv, rcAccessing, rcParam, rcUnexpected );
    }
    else if ( value != NULL )
    {
        if ( arg == NULL && *value == '\0' )
        {
            rc = RC( rcApp, rcArgv, rcAccessing, rcParam, rcNotFound );
        }
        else if ( arg != NULL && arg[0] != '\0' )
        {
            *value = arg;
        }
    }
    if ( rc != 0 )
    {
        PLOGERR( klogErr, ( klogErr, rc, "$(a0)$(a1)$(a2)$(f0)$(f1): $(v)",
            PLOG_3(PLOG_S(a0),PLOG_S(a1),PLOG_S(a2))","PLOG_3(PLOG_S(f0),PLOG_S(f1),PLOG_S(v)),
            abbr ? "-": "", abbr ? abbr : "", abbr ? ", " : "", full ? "--" : "", full ? full : "", arg));
        CoreUsage( argv[ 0 ], fmt, true, EXIT_FAILURE );
    }
    return rc == 0 ? true : false;
}


static bool reportToUser( rc_t rc, char* argv0 )
{
    assert( argv0 );
    if ( rc == SILENT_RC( rcSRA, rcFormatter, rcConstructing,
        rcData, rcUnsupported ) )
    {
        const char* name = strpbrk( argv0, "/\\" );
        const char* last_name = name;
        if ( last_name )
        {
        ++last_name;
        }
        while ( name )
        {
            name = strpbrk( last_name, "/\\" );
            if ( name )
            {
                last_name = name;
                if ( last_name )
                {
                    ++last_name;
                }
            }
        }
        name = last_name ? last_name : argv0;
        if ( strcmp( "sff-dump", name ) == 0 )
        {
            OUTMSG ((
               "This run cannot be transformed into SFF format.\n"
               "Conversion cannot be completed because the source lacks\n"
               "one or more of the data series required by the SFF format.\n"
               "You should be able to dump it as FASTQ by running fastq-dump.\n"
               "\n" ));
            return true;
        }
    }
    return false;
}


/*******************************************************************************
 * KMain - defined for use with kapp library
 *******************************************************************************/
rc_t CC KMain ( int argc, char* argv[] )
{
    rc_t rc = 0;
    int i;
    const char* arg;
    uint64_t total_spots = 0;

    const SRAMgr* sraMGR = NULL;
    SRADumperFmt fmt;

    bool to_stdout = false, do_gzip = false, do_bzip2 = false;
    char const* outdir = NULL;
    spotid_t minSpotId = 1;
    spotid_t maxSpotId = ~0;
    bool sub_dir = false;
    bool keep_empty = false;
    const char* table_path[10240];
    int table_path_qty = 0;

    char const* D_option = NULL;
    char const* P_option = NULL;
    char P_option_buffer[4096];
    const char* accession = NULL;
    const char* table_name = NULL;
    
    bool spot_group_on = false;
    int spot_groups = 0;
    char* spot_group[128] = {NULL};
    bool read_filter_on = false;
    SRAReadFilter read_filter = 0xFF;

    bool failed_to_open = false;

    /* for the fasta-ouput of fastq-dump: branch out completely of 'common' code */
    if ( fasta_dump_requested( argc, argv ) )
    {
        return fasta_dump( argc, argv );
    }

    /* Prepare for the worst: report this information after disaster */
    ReportBuildDate ( __DATE__ );

    memset( &fmt, 0, sizeof( fmt ) );
    rc = SRADumper_Init( &fmt );
    if ( rc != 0 )
    {
        LOGERR(klogErr, rc, "formatter initialization");
        return 100;
    }
    else if ( fmt.get_factory == NULL )
    {
        rc = RC( rcExe, rcFormatter, rcValidating, rcInterface, rcNull );
        LOGERR( klogErr, rc, "formatter factory" );
        return 101;
    }
    else
    {
        rc = SRADumper_ArgsValidate( argv[0], &fmt );
        if ( rc != 0 )
        {
            LOGERR( klogErr, rc, "formatter args list" );
            return 102;
        }
    }

    if ( argc < 2 )
    {
        CoreUsage( argv[0], &fmt, true, EXIT_FAILURE );
        return 0;
    }

    for ( i = 1; i < argc; i++ )
    {
        arg = argv[ i ];
        if ( arg[ 0 ] != '-' )
        {
            uint32_t k;
            for ( k = 0; k < table_path_qty; k++ )
            {
                if ( strcmp( arg, table_path[ k ] ) == 0 )
                {
                    break;
                }
            }
            if ( k >= table_path_qty )
            {
                if ( ( table_path_qty + 1 ) >= ( sizeof( table_path ) / sizeof( table_path[ 0 ] ) ) )
                {
                    rc = RC( rcExe, rcArgv, rcReading, rcBuffer, rcInsufficient );
                    goto Catch;
                }
                table_path[ table_path_qty++ ] = arg;
            }
            continue;
        }
        arg = NULL;
        if ( SRADumper_GetArg( &fmt, "L", "log-level", &i, argc, argv, &arg ) )
        {
            rc = LogLevelSet( arg );
            if ( rc != 0 )
            {
                PLOGERR( klogErr, ( klogErr, rc, "log level $(lvl)", PLOG_S( lvl ), arg ) );
                goto Catch;
            }
        }
        else if ( SRADumper_GetArg( &fmt, NULL, OPTION_REPORT, &i, argc, argv, &arg ) )
        {
        }
        else if ( SRADumper_GetArg( &fmt, "+", "debug", &i, argc, argv, &arg ) )
        {
#if _DEBUGGING
            rc = KDbgSetString( arg );
            if ( rc != 0 )
            {
                PLOGERR( klogErr, ( klogErr, rc, "debug level $(lvl)", PLOG_S( lvl ), arg ) );
                goto Catch;
            }
#endif
        }
        else if ( SRADumper_GetArg( &fmt, "H", "help", &i, argc, argv, NULL ) ||
                  SRADumper_GetArg( &fmt, "?", "h", &i, argc, argv, NULL ) )
        {
            CoreUsage( argv[ 0 ], &fmt, false, EXIT_SUCCESS );

        }
        else if ( SRADumper_GetArg( &fmt, "V", "version", &i, argc, argv, NULL ) )
        {
            HelpVersion ( argv[ 0 ], KAppVersion() );
            return 0;
        }
        else if ( SRADumper_GetArg( &fmt, "v", NULL, &i, argc, argv, NULL ) )
        {
            KStsLevelAdjust( 1 );

        }
        else if ( SRADumper_GetArg( &fmt, "D", "table-path", &i, argc, argv, &D_option ) )
        {
            LOGMSG( klogErr, "option -D is deprecated, see --help" );
        }
        else if ( SRADumper_GetArg( &fmt, "P", "path", &i, argc, argv, &P_option ) )
        {
            LOGMSG( klogErr, "option -P is deprecated, see --help" );

        }
        else if ( SRADumper_GetArg( &fmt, "A", "accession", &i, argc, argv, &accession ) )
        {
        }
        else if ( SRADumper_GetArg( &fmt, "O", "outdir", &i, argc, argv, &outdir ) )
        {
        }
        else if ( SRADumper_GetArg( &fmt, "Z", "stdout", &i, argc, argv, NULL ) )
        {
            to_stdout = true;
        }
        else if ( fmt.gzip && SRADumper_GetArg( &fmt, NULL, "gzip", &i, argc, argv, NULL ) )
        {
            do_gzip = true;
        }
        else if ( fmt.bzip2 && SRADumper_GetArg( &fmt, NULL, "bzip2", &i, argc, argv, NULL ) )
        {
            do_bzip2 = true;
        }
        else if ( SRADumper_GetArg( &fmt, NULL, "table", &i, argc, argv, &table_name ) )
        {
        }
        else if ( SRADumper_GetArg( &fmt, "N", "minSpotId", &i, argc, argv, &arg ) )
        {
            minSpotId = AsciiToU32( arg, NULL, NULL );
        }
        else if ( SRADumper_GetArg( &fmt, "X", "maxSpotId", &i, argc, argv, &arg ) )
        {
            maxSpotId = AsciiToU32( arg, NULL, NULL );
        }
        else if ( SRADumper_GetArg( &fmt, "G", "spot-group", &i, argc, argv, NULL ) )
        {
            spot_group_on = true;
        }
        else if ( SRADumper_GetArg( &fmt, NULL, "spot-groups", &i, argc, argv, NULL ) )
        {
            if ( i + 1 < argc && argv[ i + 1 ][ 0 ] != '-' )
            {
                int f = 0, t = 0;
                i++;
                while ( argv[ i ][ t ] != '\0' )
                {
                    if ( argv[ i ][ t ] == ',' )
                    {
                        if ( t - f > 0 )
                        {
                            spot_group[ spot_groups++ ] = strndup( &argv[ i ][ f ], t - f );
                        }
                        f = t + 1;
                    }
                    t++;
                }
                if ( t - f > 0 )
                {
                    spot_group[ spot_groups++ ] = strndup( &argv[ i ][ f ], t - f );
                }
                if ( spot_groups < 1 )
                {
                    rc = RC( rcApp, rcArgv, rcReading, rcParam, rcEmpty );
                    PLOGERR( klogErr, ( klogErr, rc, "$(p)", PLOG_S( p ), argv[ i - 1 ] ) );
                    CoreUsage( argv[ 0 ], &fmt, false, EXIT_FAILURE );
                }
                spot_group[ spot_groups ] = NULL;
            }
        }
        else if ( SRADumper_GetArg( &fmt, "R", "read-filter", &i, argc, argv, NULL ) )
        {
            read_filter_on = true;
            if ( i + 1 < argc && argv[ i + 1 ][ 0 ] != '-' )
            {
                i++;
                if ( read_filter != 0xFF )
                {
                    rc = RC( rcApp, rcArgv, rcReading, rcParam, rcDuplicate );
                    PLOGERR( klogErr, ( klogErr, rc, "$(p): $(o)",
                             PLOG_2( PLOG_S( p ),PLOG_S( o ) ), argv[ i - 1 ], argv[ i ] ) );
                    CoreUsage( argv[ 0 ], &fmt, false, EXIT_FAILURE );
                }
                if ( strcasecmp( argv[ i ], "pass" ) == 0 )
                {
                    read_filter = SRA_READ_FILTER_PASS;
                }
                else if ( strcasecmp( argv[ i ], "reject" ) == 0 )
                {
                    read_filter = SRA_READ_FILTER_REJECT;
                }
                else if ( strcasecmp( argv[ i ], "criteria" ) == 0 )
                {
                    read_filter = SRA_READ_FILTER_CRITERIA;
                }
                else if ( strcasecmp( argv[ i ], "redacted" ) == 0 )
                {
                    read_filter = SRA_READ_FILTER_REDACTED;
                }
                else
                {
                    /* must be accession */
                    i--;
                }
            }
        }
        else if ( SRADumper_GetArg( &fmt, "T", "group-in-dirs", &i, argc, argv, NULL ) )
        {
            sub_dir = true;
        }
        else if ( SRADumper_GetArg( &fmt, "K", "keep-empty-files", &i, argc, argv, NULL ) )
        {
            keep_empty = true;
        }
        else if ( SRADumper_GetArg( &fmt, NULL, "no-user-settings", &i, argc, argv, NULL ) )
        {
             KConfigDisableUserSettings ();
        }
        else if ( fmt.add_arg && fmt.add_arg( &fmt, SRADumper_GetArg, &i, argc, argv ) )
        {
        }
        else
        {
            rc = RC( rcApp, rcArgv, rcReading, rcParam, rcIncorrect );
            PLOGERR( klogErr, ( klogErr, rc, "$(p)", PLOG_S( p ), argv[ i ] ) );
            CoreUsage( argv[ 0 ], &fmt, false, EXIT_FAILURE );
        }
    }

    if ( to_stdout )
    {
        if ( outdir != NULL || sub_dir || keep_empty ||
            spot_group_on || ( read_filter_on && read_filter == 0xFF ) )
        {
            LOGMSG( klogWarn, "stdout mode is set, some options are ignored" );
            spot_group_on = false;
            if ( read_filter == 0xFF )
            {
                read_filter_on = false;
            }
        }
        KOutHandlerSetStdErr();
        KStsHandlerSetStdErr();
        KLogHandlerSetStdErr();
        ( void ) KDbgHandlerSetStdErr();
    }

    if ( do_gzip && do_bzip2 )
    {
        rc = RC( rcApp, rcArgv, rcReading, rcParam, rcAmbiguous );
        LOGERR( klogErr, rc, "output compression method" );
        CoreUsage( argv[ 0 ], &fmt, false, EXIT_FAILURE );
    }

    if ( minSpotId > maxSpotId )
    {
        spotid_t temp = maxSpotId;
        maxSpotId = minSpotId;
        minSpotId = temp;
    }

    if ( table_path_qty == 0 )
    {
        if ( D_option != NULL && D_option[ 0 ] != '\0' )
        {
            /* support deprecated '-D' option */
            table_path[ table_path_qty++ ] = D_option;
        }
        else if ( accession == NULL || accession[ 0 ] == '\0' )
        {
            /* must have accession to proceed */
            rc = RC( rcExe, rcArgv, rcValidating, rcParam, rcEmpty );
            LOGERR( klogErr, rc, "expected accession" );
            goto Catch;
        }
        else if ( P_option != NULL && P_option[ 0 ] != '\0' )
        {
            /* support deprecated '-P' option */
            i = snprintf( P_option_buffer, sizeof( P_option_buffer ), "%s/%s", P_option, accession );
            if ( i < 0 || i >= sizeof( P_option_buffer ) )
            {
                rc = RC( rcExe, rcArgv, rcValidating, rcParam, rcExcessive );
                LOGERR( klogErr, rc, "path too long" );
                goto Catch;
            }
            table_path[ table_path_qty++ ] = P_option_buffer;
        }
        else
        {
            table_path[ table_path_qty++ ] = accession;
        }
    }

    rc = SRAMgrMakeRead( &sraMGR );
    if ( rc != 0 )
    {
        LOGERR( klogErr, rc, "failed to open SRA manager" );
        goto Catch;
    }
    else
    {
        rc = SRASplitterFactory_FilerInit( to_stdout, do_gzip, do_bzip2, sub_dir, keep_empty, outdir );
        if ( rc != 0 )
        {
            LOGERR( klogErr, rc, "failed to initialize files" );
            goto Catch;
        }
    }

    {
        const VDBManager* vmgr = NULL;
        rc_t rc2 = SRAMgrGetVDBManagerRead( sraMGR, &vmgr );
        if ( rc2 != 0 )
        {
            LOGERR( klogErr, rc2, "while calling SRAMgrGetVDBManagerRead" );
        }
        rc2 = ReportSetVDBManager( vmgr );
        VDBManagerRelease( vmgr );
    }


    /* loop tables */
    for ( i = 0; i < table_path_qty; i++ )
    {
        const SRASplitterFactory* fact_head = NULL;
        spotid_t smax, smin;

        SRA_DUMP_DBG( 5, ( "table path '%s', name '%s'\n", table_path[ i ], table_name ) );
        if ( table_name != NULL )
        {
            rc = SRAMgrOpenAltTableRead( sraMGR, &fmt.table, table_name, table_path[ i ] );
            if ( rc != 0 )
            {
                PLOGERR( klogErr, ( klogErr, rc, 
                    "failed to open '$(path):$(table)'", "path=%s,table=%s",
                    table_path[ i ], table_name ) );
                continue;
            }
        }

        ReportResetObject( table_path[ i ] );
        if ( fmt.table == NULL )
        {
            rc = SRAMgrOpenTableRead( sraMGR, &fmt.table, table_path[ i ] );
            if ( rc != 0 )
            {
                if ( UIError( rc, NULL, NULL ) )
                {
                    UITableLOGError( rc, NULL, true );
                }
                else
                {
                    PLOGERR( klogErr, ( klogErr, rc,
                            "failed to open '$(path)'", "path=%s",
                            table_path[ i ] ) );
                    if (GetRCState(rc) == rcNotFound) {
                        failed_to_open = true;
                    }
                }
                continue;
            }
        }

        /* infer accession from table_path if missing or more than one table */
        fmt.accession = table_path_qty > 1 ? NULL : accession;
        if ( fmt.accession == NULL || fmt.accession[ 0 ] == 0 )
        {
            char * basename;
            char *ext;
            size_t l;
            bool is_url = false;

            strcpy( P_option_buffer, table_path[ i ] );

            basename = strchr ( P_option_buffer, ':' );
            if ( basename )
            {
                ++basename;
                if ( basename [0] == '\0' )
                    basename = P_option_buffer;
                else
                    is_url = true;
            }
            else
                basename = P_option_buffer;

            if ( is_url )
            {
                ext = strchr ( basename, '#' );
                if ( ext )
                    ext[ 0 ] = '\0';
                ext = strchr ( basename, '?' );
                if ( ext )
                    ext[ 0 ] = '\0';
            }


            l = strlen( basename  );
            while ( strchr( "\\/", basename[ l - 1 ] ) != NULL )
            {
                basename[ --l ] = '\0';
            }
            fmt.accession = strrchr( basename, '/' );
            if ( fmt.accession++ == NULL )
            {
                fmt.accession = basename;
            }

            /* cut off [.lite].[c]sra[.nenc||.ncbi_enc] if any */
            ext = strrchr( fmt.accession, '.' );
            if ( ext != NULL )
            {
                if ( strcasecmp( ext, ".nenc" ) == 0 || strcasecmp( ext, ",ncbi_enc" ) == 0 )
                {
                    *ext = '\0';
                    ext = strrchr( fmt.accession, '.' );
                }
                if ( ext != NULL && ( strcasecmp( ext, ".sra" ) == 0 || strcasecmp( ext, ".csra" ) == 0 ) )
                {
                    *ext = '\0';
                    ext = strrchr( fmt.accession, '.' );
                    if ( ext != NULL && strcasecmp( ext, ".lite" ) == 0 )
                    {
                        *ext = '\0';
                    }
                }
            }
        }

        SRA_DUMP_DBG( 5, ( "accession: '%s'\n", fmt.accession ) );
        rc = SRASplitterFactory_FilerPrefix( accession ? accession : fmt.accession );

        while ( rc == 0 )
        {
            /* sort out the spot id range */
            if ( ( rc = SRATableMaxSpotId( fmt.table, &smax ) ) != 0 ||
                 ( rc = SRATableMinSpotId( fmt.table, &smin ) ) != 0 )
            {
                break;
            }

            {
                const struct VTable* tbl = NULL;
                rc_t rc2 = SRATableGetVTableRead( fmt.table, &tbl );
                if ( rc == 0 )
                {
                    rc = rc2;
                }
                rc2 = ReportResetTable( table_path[i], tbl );
                if ( rc == 0 )
                {
                    rc = rc2;
                }
                VTableRelease( tbl );   /* SRATableGetVTableRead adds Reference to tbl! */
            }

            /* test if we have to dump anything... */
            if ( smax < minSpotId || smin > maxSpotId )
            {
                break;
            }
            if ( smax > maxSpotId )
            {
                smax = maxSpotId;
            }
            if ( smin < minSpotId )
            {
                smin = minSpotId;
            }

            /* hack to reduce looping in AddSpot: needs redesign to pass nreads along through tree */
            if ( true ) /* ??? */
            {
                const SRAColumn* c = NULL;
                nreads_max = NREADS_MAX;
                rc = SRATableOpenColumnRead( fmt.table, &c, "PLATFORM", sra_platform_id_t );
                if ( rc == 0 )
                {
                    const INSDC_SRA_platform_id *platform;
                    bitsz_t o, z;
                    rc = SRAColumnRead( c, 1, (const void **)&platform, &o, &z );
                    if ( rc == 0 && platform != NULL )
                    {
                        if ( *platform != SRA_PLATFORM_PACBIO_SMRT )
                        {
                            nreads_max = 32;
                        }
                    }
                    SRAColumnRelease( c );
                }
                else if ( GetRCState( rc ) == rcNotFound && GetRCObject( rc ) == rcColumn )
                {
                    rc = 0;
                }
            }

            /* table dependent */
            rc = fmt.get_factory( &fmt, &fact_head );
            if ( rc != 0 )
            {
                break;
            }
            if ( fact_head == NULL )
            {
                rc = RC( rcExe, rcFormatter, rcResolving, rcInterface, rcNull );
                break;
            }

            if ( rc == 0 && ( spot_group_on || spot_groups > 0 ) )
            {
                const SRASplitterFactory* f = NULL;
                rc = SpotGroupSplitterFactory_Make( &f, fmt.table, spot_group_on, spot_group );
                if ( rc == 0 )
                {
                    rc = SRASplitterFactory_AddNext( f, fact_head );
                    if ( rc == 0 )
                    {
                        fact_head = f;
                    }
                    else
                    {
                        SRASplitterFactory_Release( f );
                    }
                }
            }

            if ( rc == 0 && read_filter_on )
            {
                const SRASplitterFactory* f = NULL;
                rc = ReadFilterSplitterFactory_Make( &f, fmt.table, read_filter );
                if ( rc == 0 )
                {
                    rc = SRASplitterFactory_AddNext( f, fact_head );
                    if ( rc == 0 )
                    {
                        fact_head = f;
                    }
                    else
                    {
                        SRASplitterFactory_Release( f );
                    }
                }
            }

            if ( rc == 0 )
            {
                /* this filter takes over head of chain to be first and kill off bad NREADS */
                const SRASplitterFactory* f = NULL;
                rc = MaxNReadsValidatorFactory_Make( &f, fmt.table );
                if ( rc == 0 )
                {
                    rc = SRASplitterFactory_AddNext( f, fact_head );
                    if ( rc == 0 )
                    {
                        fact_head = f;
                    }
                    else
                    {
                        SRASplitterFactory_Release( f );
                    }
                }
            }

            rc = SRASplitterFactory_Init( fact_head );
            if ( rc == 0 )
            {
                /* ********************************************************** */
                rc = SRADumper_DumpRun( fmt.table, smin, smax, fact_head );
                /* ********************************************************** */
                if ( rc == 0 )
                { 
                    uint64_t total = 0, file = 0;
                    SRASplitterFactory_FilerReport( &total, &file );
                    OUTMSG(( "Written %lu spots for %s\n", total - total_spots, table_path[ i ] ));
                    if ( to_stdout && total > 0 )
                    {
                        PLOGMSG( klogInfo, ( klogInfo, "$(t) biggest file has $(n) spots",
                            PLOG_2( PLOG_S( t ), PLOG_U64( n ) ), table_path[ i ], file ));
                    }
                    total_spots = total;
                }
            }
            break;
        }

        SRASplitterFactory_Release( fact_head );
        SRATableRelease( fmt.table );
        fmt.table = NULL;
        if ( rc == 0 )
        {
            PLOGMSG( klogInfo, ( klogInfo, "$(path)$(dot)$(table) $(spots) spots",
                    PLOG_4(PLOG_S(path),PLOG_S(dot),PLOG_S(table),PLOG_U32(spots)),
                    table_path[ i ], table_name ? ":" : "", table_name ? table_name : "", smax - smin + 1 ) );
        }
        else if ( !reportToUser( rc, argv [0 ] ) )
        {
            PLOGERR( klogErr, ( klogErr, rc, "failed $(path)$(dot)$(table)",
                    PLOG_3(PLOG_S(path),PLOG_S(dot),PLOG_S(table)),
                    table_path[ i ], table_name ? ":" : "", table_name ? table_name : "" ) );
        }
    }

Catch:
    if ( fmt.release )
    {
        rc_t rr = fmt.release( &fmt );
        if ( rr != 0 )
        {
            SRA_DUMP_DBG( 1, ( "formatter release error %R\n", rr ) );
        }
    }

    for ( i = 0; i < spot_groups; i++ )
    {
        free( spot_group[ i ] );
    }
    SRASplitterFiler_Release();
    SRAMgrRelease( sraMGR );
    OUTMSG(( "Written %lu spots total\n", total_spots ));


    if (failed_to_open) {
        ReportSilence();
    }
    {
        /* Report execution environment if necessary */
        rc_t rc2 = ReportFinalize( rc );
        if ( rc == 0 )
        {
            rc = rc2;
        }
    }
    return rc;
}
