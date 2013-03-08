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

#include "sra-pileup.vers.h"

#include "cmdline_cmn.h"
#include "reref.h"

#include <kapp/main.h>

#include <klib/out.h>
#include <klib/printf.h>
#include <klib/report.h>

#include <kfs/file.h>
#include <kfs/buffile.h>
#include <kfs/bzip.h>
#include <kfs/gzip.h>

#include <insdc/sra.h>
#include <vdb/manager.h>
#include <vdb/schema.h>
#include <sra/sraschema.h>
#include <align/manager.h>

#include <os-native.h>
#include <sysalloc.h>

#define COL_QUALITY "QUALITY"
#define COL_REF_ORIENTATION "REF_ORIENTATION"
#define COL_READ_FILTER "READ_FILTER"

#define OPTION_MINMAPQ "minmapq"
#define ALIAS_MINMAPQ  "q"

#define OPTION_DUPS    "duplicates"
#define ALIAS_DUPS     "d"

#define OPTION_MODE    "mode"
#define ALIAS_MODE     "m"

#define OPTION_NOQUAL  "noqual"
#define ALIAS_NOQUAL   "n"

#define OPTION_NOSKIP  "noskip"
#define ALIAS_NOSKIP   "s"

#define OPTION_SHOWID  "showid"
#define ALIAS_SHOWID   "i"

#define OPTION_SPOTGRP "spotgroups"
#define ALIAS_SPOTGRP  "p"

#define OPTION_SEQNAME "seqname"
#define ALIAS_SEQNAME  "e"

#define OPTION_REREF   "report-ref"
#define ALIAS_REREF    NULL

enum
{
    sra_pileup_samtools = 0,
    sra_pileup_counters = 1,
    sra_pileup_detect = 2
};

static const char * minmapq_usage[] = { "Minimum mapq-value, ", 
                                        "alignments with lower mapq",
                                        "will be ignored (default=0)", NULL };

static const char * dups_usage[] = { "process duplicates ( 0...off/1..on )", NULL };

static const char * mode_usage[] = { "Output-format: 0...samtools, 1...just counters",
                                     "(default=0)", NULL };

static const char * noqual_usage[] = { "Omit qualities in output", NULL };

static const char * noskip_usage[] = { "Does not skip reference-regions without alignments", NULL };

static const char * showid_usage[] = { "Shows alignment-id for every base", NULL };

static const char * spotgrp_usage[] = { "divide by spotgroups", NULL };

static const char * seqname_usage[] = { "use original seq-name", NULL };

static const char * reref_usage[] = { "report used references", NULL };

OptDef MyOptions[] =
{
    /*name,           alias,         hfkt, usage-help,    maxcount, needs value, required */
    { OPTION_MINMAPQ, ALIAS_MINMAPQ, NULL, minmapq_usage, 1,        true,        false },
    { OPTION_DUPS,    ALIAS_DUPS,    NULL, dups_usage,    1,        true,        false },
    { OPTION_MODE,    ALIAS_MODE,    NULL, mode_usage,    1,        true,        false },
    { OPTION_NOQUAL,  ALIAS_NOQUAL,  NULL, noqual_usage,  1,        false,       false },
    { OPTION_NOSKIP,  ALIAS_NOSKIP,  NULL, noskip_usage,  1,        false,       false },
    { OPTION_SHOWID,  ALIAS_SHOWID,  NULL, showid_usage,  1,        false,       false },
    { OPTION_SPOTGRP, ALIAS_SPOTGRP, NULL, spotgrp_usage, 1,        false,       false },
    { OPTION_SEQNAME, ALIAS_SEQNAME, NULL, seqname_usage, 1,        false,       false },
    { OPTION_REREF,   ALIAS_REREF,   NULL, reref_usage,   1,        false,       false }
};

/* =========================================================================================== */

typedef struct pileup_options
{
    common_options cmn;
    bool process_dups;
    bool omit_qualities;
    bool no_skip;
    bool show_id;
    bool div_by_spotgrp;
    bool use_seq_name;
    bool reref;
    uint32_t minmapq;
    uint32_t output_mode;
    uint32_t source_table;
} pileup_options;


typedef struct pileup_callback_data
{
    const AlignMgr *almgr;
    pileup_options *options;
} pileup_callback_data;


/* =========================================================================================== */

static rc_t get_str_option( const Args *args, const char *name, const char ** res )
{
    uint32_t count;
    rc_t rc = ArgsOptionCount( args, name, &count );
    *res = NULL;
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "ArgsOptionCount() failed" );
    }
    else
    {
        if ( count > 0 )
        {
            rc = ArgsOptionValue( args, name, 0, res );
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ArgsOptionValue() failed" );
            }
        }
    }
    return rc;
}


static rc_t get_uint32_option( const Args *args, const char *name,
                        uint32_t *res, const uint32_t def )
{
    const char * s;
    rc_t rc = get_str_option( args, name, &s );
    if ( rc == 0 && s != NULL )
        *res = atoi( s );
    else
        *res = def;
    return rc;
}


static rc_t get_bool_option( const Args *args, const char *name, bool *res, const bool def )
{
    uint32_t count;
    rc_t rc = ArgsOptionCount( args, name, &count );
    if ( rc == 0 && count > 0 )
    {
        *res = true;
    }
    else
    {
        *res = def;
    }
    return rc;
}


/* =========================================================================================== */


static rc_t get_pileup_options( Args * args, pileup_options *opts )
{
    rc_t rc = get_common_options( args, &opts->cmn );

    if ( rc == 0 )
        rc = get_uint32_option( args, OPTION_MINMAPQ, &opts->minmapq, 0 );

    if ( rc == 0 )
         rc = get_uint32_option( args, OPTION_MODE, &opts->output_mode, sra_pileup_samtools );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_DUPS, &opts->process_dups, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_NOQUAL, &opts->omit_qualities, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_NOSKIP, &opts->no_skip, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_SHOWID, &opts->show_id, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_SPOTGRP, &opts->div_by_spotgrp, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_SEQNAME, &opts->use_seq_name, false );

    if ( rc == 0 )
        rc = get_bool_option( args, OPTION_REREF, &opts->reref, false );

    return rc;
}

/* GLOBAL VARIABLES */
struct {
    KWrtWriter org_writer;
    void* org_data;
    KFile* kfile;
    uint64_t pos;
} g_out_writer = { NULL };

const char UsageDefaultName[] = "sra-pileup";

rc_t CC UsageSummary ( const char * progname )
{
    return KOutMsg ("\n"
                    "Usage:\n"
                    "  %s <path> [options]\n"
                    "\n", progname);
}


rc_t CC Usage ( const Args * args )
{
    const char * progname = UsageDefaultName;
    const char * fullpath = UsageDefaultName;
    rc_t rc;

    if ( args == NULL )
        rc = RC ( rcApp, rcArgv, rcAccessing, rcSelf, rcNull );
    else
        rc = ArgsProgram ( args, &fullpath, &progname );

    if ( rc )
        progname = fullpath = UsageDefaultName;

    UsageSummary ( progname );
    KOutMsg ( "Options:\n" );
    print_common_helplines();
    HelpOptionLine ( ALIAS_MINMAPQ, OPTION_MINMAPQ, "min. mapq", minmapq_usage );
    HelpOptionLine ( ALIAS_DUPS, OPTION_DUPS, "duplicates", dups_usage );
    HelpOptionLine ( ALIAS_MODE, OPTION_MODE, "output-modes", mode_usage );
    HelpOptionLine ( ALIAS_SPOTGRP, OPTION_SPOTGRP, "spotgroups-modes", spotgrp_usage );
    HelpOptionLine ( ALIAS_SEQNAME, OPTION_SEQNAME, "org. seq-name", seqname_usage );
    HelpOptionLine ( ALIAS_REREF, OPTION_REREF, "report reference", reref_usage );
    HelpOptionsStandard ();
    HelpVersion ( fullpath, KAppVersion() );
    return rc;
}


/* Version  EXTERN
 *  return 4-part version code: 0xMMmmrrrr, where
 *      MM = major release
 *      mm = minor release
 *    rrrr = bug-fix release
 */
ver_t CC KAppVersion ( void )
{
    return SRA_PILEUP_VERS;
}

/***************************************
    N (0x4E)  n (0x6E)  <--> 0x0
    A (0x41)  a (0x61)  <--> 0x1
    C (0x43)  c (0x63)  <--> 0x2
    M (0x4D)  m (0x6D)  <--> 0x3
    G (0x47)  g (0x67)  <--> 0x4
    R (0x52)  r (0x72)  <--> 0x5
    S (0x53)  s (0x73)  <--> 0x6
    V (0x56)  v (0x76)  <--> 0x7
    T (0x54)  t (0x74)  <--> 0x8
    W (0x57)  w (0x77)  <--> 0x9
    Y (0x59)  y (0x79)  <--> 0xA
    H (0x48)  h (0x68)  <--> 0xB
    K (0x4B)  k (0x6B)  <--> 0xC
    D (0x44)  d (0x64)  <--> 0xD
    B (0x42)  b (0x62)  <--> 0xE
    N (0x4E)  n (0x6E)  <--> 0xF
***************************************/


char _4na_2_ascii_tab[] =
{
/*  0x0  0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F */
    'N', 'A', 'C', 'M', 'G', 'R', 'S', 'V', 'T', 'W', 'Y', 'H', 'K', 'D', 'B', 'N',
    'n', 'a', 'c', 'm', 'g', 'r', 's', 'v', 't', 'w', 'y', 'h', 'k', 'd', 'b', 'n'
};


static char _4na_to_ascii( INSDC_4na_bin c, bool reverse )
{
    return _4na_2_ascii_tab[ ( c & 0x0F ) | ( reverse ? 0x10 : 0 ) ];
}


/* =========================================================================================== */


typedef struct dyn_string
{
    char * data;
    size_t allocated;
    size_t data_len;
} dyn_string;


static rc_t allocated_dyn_string ( dyn_string *self, size_t size )
{
    rc_t rc = 0;
    self->data_len = 0;
    self->data = malloc( size );
    if ( self->data != NULL )
    {
        self->allocated = size;
    }
    else
    {
        self->allocated = 0;
        rc = RC( rcApp, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
    }
    return rc;
}

static void free_dyn_string ( dyn_string *self )
{
    free( self->data );
    self->data = NULL;
    self->allocated = 0;
    self->data_len = 0;
}

static void reset_dyn_string( dyn_string *self )
{
    self->data_len = 0;
}

static rc_t expand_dyn_string( dyn_string *self, size_t new_size )
{
    rc_t rc = 0;
    if ( new_size > self->allocated )
    {
        self->data = realloc ( self->data, new_size );
        if ( self->data != NULL )
        {
            self->allocated = new_size;
        }
        else
        {
            self->allocated = 0;
            self->data_len = 0;
            rc = RC( rcApp, rcNoTarg, rcConstructing, rcMemory, rcExhausted );
        }
    }
    return rc;
}


static rc_t add_char_2_dyn_string( dyn_string *self, const char c )
{
    /* does nothing if self->data_len + 2 < self->allocated */
    rc_t rc = expand_dyn_string( self, self->data_len + 2 );
    if ( rc == 0 )
    {
        self->data[ self->data_len++ ] = c;
        self->data[ self->data_len ] = 0;
    }
    return rc;
}


static rc_t add_string_2_dyn_string( dyn_string *self, const char * s )
{
    rc_t rc;
    size_t size = string_size ( s );
    /* does nothing if self->data_len + size + 1 < self->allocated */
    rc = expand_dyn_string( self, self->data_len + size + 1 );
    if ( rc == 0 )
    {
        string_copy ( &(self->data[ self->data_len ]), self->allocated, s, size );
        self->data_len += size;
        self->data[ self->data_len ] = 0;
    }
    return rc;
}


static rc_t print_2_dyn_string( dyn_string * self, const char *fmt, ... )
{
    rc_t rc = 0;
    bool not_enough;

    do
    {
        size_t num_writ;
        va_list args;
        va_start ( args, fmt );
        rc = string_vprintf ( &(self->data[ self->data_len ]), 
                              self->allocated - ( self->data_len + 1 ),
                              &num_writ,
                              fmt,
                              args );
        va_end ( args );

        if ( rc == 0 )
        {
            self->data_len += num_writ;
            self->data[ self->data_len ] = 0;
        }
        not_enough = ( GetRCState( rc ) == rcInsufficient );
        if ( not_enough )
        {
            rc = expand_dyn_string( self, self->allocated + ( num_writ * 2 ) );
        }
    } while ( not_enough && rc == 0 );
    return rc;
}


/* =========================================================================================== */


static rc_t CC BufferedWriter ( void* self, const char* buffer, size_t bufsize, size_t* num_writ )
{
    rc_t rc = 0;

    assert( buffer != NULL );
    assert( num_writ != NULL );

    do {
        rc = KFileWrite( g_out_writer.kfile, g_out_writer.pos, buffer, bufsize, num_writ );
        if ( rc == 0 )
        {
            buffer += *num_writ;
            bufsize -= *num_writ;
            g_out_writer.pos += *num_writ;
        }
    } while ( rc == 0 && bufsize > 0 );
    return rc;
}


static rc_t set_stdout_to( bool gzip, bool bzip2, const char * filename, size_t bufsize )
{
    rc_t rc = 0;
    if ( gzip && bzip2 )
    {
        rc = RC( rcApp, rcFile, rcConstructing, rcParam, rcAmbiguous );
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
            KFile *of;
            rc = KDirectoryCreateFile ( dir, &of, false, 0664, kcmInit, "%s", filename );
            if ( rc == 0 )
            {
                KFile *buf;
                if ( gzip )
                {
                    KFile *gz;
                    rc = KFileMakeGzipForWrite( &gz, of );
                    if ( rc == 0 )
                    {
                        KFileRelease( of );
                        of = gz;
                    }
                }
                if ( bzip2 )
                {
                    KFile *bz;
                    rc = KFileMakeBzip2ForWrite( &bz, of );
                    if ( rc == 0 )
                    {
                        KFileRelease( of );
                        of = bz;
                    }
                }

                rc = KBufFileMakeWrite( &buf, of, false, bufsize );
                if ( rc == 0 )
                {
                    g_out_writer.kfile = buf;
                    g_out_writer.org_writer = KOutWriterGet();
                    g_out_writer.org_data = KOutDataGet();
                    rc = KOutHandlerSet( BufferedWriter, &g_out_writer );
                    if ( rc != 0 )
                    {
                        LOGERR( klogInt, rc, "KOutHandlerSet() failed" );
                    }
                }
                KFileRelease( of );
            }
            KDirectoryRelease( dir );
        }
    }
    return rc;
}


static void release_stdout_redirection( void )
{
    KFileRelease( g_out_writer.kfile );
    if( g_out_writer.org_writer != NULL )
    {
        KOutHandlerSet( g_out_writer.org_writer, g_out_writer.org_data );
    }
    g_out_writer.org_writer = NULL;
}


static rc_t CC write_to_FILE( void *f, const char *buffer, size_t bytes, size_t *num_writ )
{
    * num_writ = fwrite ( buffer, 1, bytes, f );
    if ( * num_writ != bytes )
        return RC( rcExe, rcFile, rcWriting, rcTransfer, rcIncomplete );
    return 0;
}


/* =========================================================================================== */


typedef struct tool_rec tool_rec;
struct tool_rec
{
    /* orientation towards reference ( false...in ref-orientation / true...reverse) */
    bool reverse;
    /* ptr to quality... */
    uint8_t * quality;
};


static rc_t read_base_and_len( struct VCursor const *curs,
                               const char * name,
                               int64_t row_id,
                               const void ** base,
                               uint32_t * len )
{
    uint32_t column_idx;
    rc_t rc = VCursorGetColumnIdx ( curs, &column_idx, name );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VCursorGetColumnIdx() failed" );
    }
    else
    {
        uint32_t elem_bits, boff, len_intern;
        const void * ptr;
        rc = VCursorCellDataDirect ( curs, row_id, column_idx, 
                                     &elem_bits, &ptr, &boff, &len_intern );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorCellDataDirect() failed" );
        }
        else
        {
            if ( len != NULL ) *len = len_intern;
            if ( base != NULL ) *base = ptr;
        }
    }
    return rc;
}


static rc_t CC populate_tooldata( void *obj, const PlacementRecord *placement,
        struct VCursor const *curs,
        INSDC_coord_zero ref_window_start, INSDC_coord_len ref_window_len,
        void *data )
{
    tool_rec * rec = ( tool_rec * ) obj;
    pileup_callback_data *cb_data = ( pileup_callback_data * )data;
    rc_t rc = 0;

    rec->quality = NULL;
    if ( !cb_data->options->process_dups )
    {
        const uint8_t * read_filter;
        uint32_t read_filter_len;
        rc = read_base_and_len( curs, COL_READ_FILTER, placement->id,
                                (const void **)&read_filter, &read_filter_len );
        if ( rc == 0 )
        {
            if ( ( *read_filter == SRA_READ_FILTER_REJECT )||
                 ( *read_filter == SRA_READ_FILTER_CRITERIA ) )
            {
                rc = RC( rcAlign, rcType, rcAccessing, rcId, rcIgnored );
            }
        }
    }

    if ( rc == 0 )
    {
        const bool * orientation;
        rc = read_base_and_len( curs, COL_REF_ORIENTATION, placement->id,
                                (const void **)&orientation, NULL );
        if ( rc == 0 )
        {
            rec->reverse = *orientation;
        }
    }

    if ( rc == 0 && !cb_data->options->omit_qualities )
    {
        const uint8_t * quality;
        uint32_t quality_len;

        rc = read_base_and_len( curs, COL_QUALITY, placement->id,
                                (const void **)&quality, &quality_len );
        if ( rc == 0 )
        {
            rec->quality = ( uint8_t * )rec;
            rec->quality += sizeof ( * rec );
            memcpy( rec->quality, quality, quality_len );
        }
    }
    return rc;
}


static rc_t CC alloc_size( struct VCursor const *curs, int64_t row_id, size_t * size, void *data )
{
    rc_t rc = 0;
    tool_rec * rec;
    pileup_callback_data *cb_data = ( pileup_callback_data * )data;
    *size = ( sizeof *rec );

    if ( !cb_data->options->omit_qualities )
    {
        uint32_t q_len;
        rc = read_base_and_len( curs, COL_QUALITY, row_id, NULL, &q_len );
        if ( rc == 0 )
        {
            *size += q_len;
        }
    }
    return rc;
}


static rc_t walk_ref_position( ReferenceIterator *ref_iter,
                               const PlacementRecord *rec,
                               dyn_string *line,
                               char * qual,
                               pileup_options *options )
{
    rc_t rc = 0;
    INSDC_coord_zero seq_pos;
    int32_t state = ReferenceIteratorState ( ref_iter, &seq_pos );
    tool_rec *xrec = ( tool_rec * ) PlacementRecordCast ( rec, placementRecordExtension1 );
    bool reverse = xrec->reverse;

    if ( !options->omit_qualities )
    {
        *qual = xrec->quality[ seq_pos ];
    }

    if ( ( state & align_iter_invalid ) == align_iter_invalid )
    {
        return add_char_2_dyn_string( line, '?' );
    }

    if ( ( state & align_iter_first ) == align_iter_first )
    {
        char s[ 3 ];
        int32_t c = rec->mapq + 33;
        if ( c > '~' ) { c = '~'; }
        if ( c < 33 ) { c = 33; }
        s[ 0 ] = '^';
        s[ 1 ] = c;
        s[ 2 ] = 0;
        rc = add_string_2_dyn_string( line, s );
    }

    if ( rc == 0 )
    {
        if ( ( state & align_iter_skip ) == align_iter_skip )
        {
            rc = add_char_2_dyn_string( line, '*' );
            if ( !options->omit_qualities )
                *qual = xrec->quality[ seq_pos + 1 ];
        }
        else
        {
            if ( ( state & align_iter_match ) == align_iter_match )
                rc = add_char_2_dyn_string( line, ( reverse ? ',' : '.' ) );
            else
                rc = add_char_2_dyn_string( line, _4na_to_ascii( state, reverse ) );
        }
    }

    if ( ( state & align_iter_insert ) == align_iter_insert )
    {
        const INSDC_4na_bin *bases;
        uint32_t i;
        uint32_t n = ReferenceIteratorBasesInserted ( ref_iter, &bases );
        
        rc = print_2_dyn_string( line, "+%u", n );
        for ( i = 0; i < n && rc == 0; ++i )
        {
            rc = add_char_2_dyn_string( line, _4na_to_ascii( bases[ i ], reverse ) );
        }
    }

    if ( ( state & align_iter_delete ) == align_iter_delete )
    {
        const INSDC_4na_bin *bases;
        INSDC_coord_zero ref_pos;
        uint32_t n = ReferenceIteratorBasesDeleted ( ref_iter, &ref_pos, &bases );
        if ( bases != NULL )
        {
            uint32_t i;
            rc = print_2_dyn_string( line, "-%u", n );
            for ( i = 0; i < n && rc == 0; ++i )
            {
                rc = add_char_2_dyn_string( line, _4na_to_ascii( bases[ i ], reverse ) );
            }
            free( (void *) bases );
        }
    }

    if ( ( ( state & align_iter_last ) == align_iter_last )&& ( rc == 0 ) )
        rc = add_char_2_dyn_string( line, '$' );

    if ( options->show_id )
        rc = print_2_dyn_string( line, "(%lu:%u-%u/%u)",
                                 rec->id, rec->pos + 1, rec->pos + rec->len, seq_pos );

    return rc;
}


static rc_t walk_alignments( ReferenceIterator *ref_iter,
                             dyn_string *line,
                             dyn_string *qualities,
                             pileup_options *options )
{
    uint32_t depth = 0;
    rc_t rc;
    do
    {
        const PlacementRecord *rec;
        rc = ReferenceIteratorNextPlacement ( ref_iter, &rec );
        if ( rc == 0 )
            rc = walk_ref_position( ref_iter, rec, line, &( qualities->data[ depth++ ] ), options );
    } while ( rc == 0 );

    if ( !options->omit_qualities )
    {
        uint32_t i;
        add_char_2_dyn_string( line, '\t' );
        for ( i = 0; i < depth; ++i )
        {
            add_char_2_dyn_string( line, qualities->data[ i ] + 33 );
        }
    }

    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk_spot_groups( ReferenceIterator *ref_iter,
                             dyn_string *line,
                             dyn_string *qualities,
                             pileup_options *options )
{
    rc_t rc;
    do
    {
        rc = ReferenceIteratorNextSpotGroup ( ref_iter, NULL, NULL );
        if ( rc == 0 )
            add_char_2_dyn_string( line, '\t' );
        if ( rc == 0 )
            rc = walk_alignments( ref_iter, line, qualities, options );
    } while ( rc == 0 );

    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk_position( ReferenceIterator *ref_iter,
                           const char * refname,
                           dyn_string *line,
                           dyn_string *qualities,
                           pileup_options *options )
{
    INSDC_coord_zero pos;
    uint32_t depth;
    INSDC_4na_bin base;

    rc_t rc = ReferenceIteratorPosition ( ref_iter, &pos, &depth, &base );
    if ( rc != 0 )
    {
        if ( GetRCState( rc ) != rcDone )
        {
            LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
        }
    }
    else if ( ( depth > 0 )||( options->no_skip ) )
    {
        rc = expand_dyn_string( line, ( 5 * depth ) + 100 );
        if ( rc == 0 )
        {
            rc = expand_dyn_string( qualities, depth + 100 );
            if ( rc == 0 )
            {
                char c = _4na_to_ascii( base, false );

                reset_dyn_string( line );
                rc = print_2_dyn_string( line, "%s\t%u\t%c\t%u", refname, pos + 1, c, depth );
                if ( rc == 0 )
                {
                    if ( depth > 0 )
                    {
                        rc = walk_spot_groups( ref_iter, line, qualities, options );
                    }

                    if ( rc == 0 )
                    {
                        /* only one KOutMsg() per line... */
                        KOutMsg( "%s\n", line->data );
                    }

                    if ( GetRCState( rc ) == rcDone )
                    {
                        rc = 0;
                    }
                }
            }
        }
    } 
    return rc;
}


static rc_t walk_reference_window( ReferenceIterator *ref_iter,
                                   const char * refname,
                                   dyn_string *line,
                                   dyn_string *qualities,
                                   pileup_options *options )
{
    rc_t rc = 0;
    while ( rc == 0 )
    {
        rc = ReferenceIteratorNextPos ( ref_iter, !options->no_skip );
        if ( rc != 0 )
        {
            if ( GetRCState( rc ) != rcDone )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
            }
        }
        else
        {
            rc = walk_position( ref_iter, refname, line, qualities, options );
        }
        if ( rc == 0 )
        {
            rc = Quitting();
        }
    }
    if ( GetRCState( rc ) == rcDone ) rc = 0;
    return rc;
}


static rc_t walk_reference( ReferenceIterator *ref_iter,
                            const char * refname,
                            pileup_options *options )
{
    dyn_string line;
    rc_t rc = allocated_dyn_string ( &line, 4096 );
    if ( rc == 0 )
    {
        dyn_string qualities;
        rc = allocated_dyn_string ( &qualities, 4096 );
        if ( rc == 0 )
        {
            while ( rc == 0 )
            {
                rc = Quitting ();
                if ( rc == 0 )
                {
                    INSDC_coord_zero first_pos;
                    INSDC_coord_len len;
                    rc = ReferenceIteratorNextWindow ( ref_iter, &first_pos, &len );
                    if ( rc != 0 )
                    {
                        if ( GetRCState( rc ) != rcDone )
                        {
                            LOGERR( klogInt, rc, "ReferenceIteratorNextWindow() failed" );
                        }
                    }
                    else
                    {
                        rc = walk_reference_window( ref_iter, refname, &line, &qualities, options );
                    }
                }
            }
            free_dyn_string ( &qualities );
        }
        free_dyn_string ( &line );
    }
    if ( GetRCState( rc ) == rcDone ) rc = 0;
    return rc;
}


/* =========================================================================================== */


typedef struct pileup_counters
{
    uint32_t matches;
    uint32_t mismatches[ 4 ];
    uint32_t inserts;
    uint32_t deletes;
    dyn_string ins;
    dyn_string del;
} pileup_counters;


static void clear_counters( pileup_counters * counters )
{
    uint32_t i;

    counters->matches = 0;
    for ( i = 0; i < 4; ++i )
        counters->mismatches[ i ] = 0;
    counters->inserts = 0;
    counters->deletes = 0;
    reset_dyn_string( &(counters->ins) );
    reset_dyn_string( &(counters->del) );
}

static rc_t prepare_counters( pileup_counters * counters )
{
    rc_t rc = allocated_dyn_string ( &(counters->ins), 1024 );
    if ( rc == 0 )
    {
        rc = allocated_dyn_string ( &(counters->del), 1024 );
    }
    return rc;
}


static void finish_counters( pileup_counters * counters )
{
    free_dyn_string ( &(counters->ins) );
    free_dyn_string ( &(counters->del) );
}


static void walk_counter_state( ReferenceIterator *ref_iter,
                                pileup_counters * counters )
{
    INSDC_coord_zero seq_pos;
    int32_t state = ReferenceIteratorState ( ref_iter, &seq_pos );

    if ( ( state & align_iter_invalid ) == align_iter_invalid )
    {
        return;
    }

    if ( ( state & align_iter_skip ) != align_iter_skip )
    {
        if ( ( state & align_iter_match ) == align_iter_match )
        {
            (counters->matches)++;
        }
        else
        {
            char c = _4na_to_ascii( state, false );
            switch( c )
            {
                case 'A' : ( counters->mismatches[ 0 ] )++; break;
                case 'C' : ( counters->mismatches[ 1 ] )++; break;
                case 'G' : ( counters->mismatches[ 2 ] )++; break;
                case 'T' : ( counters->mismatches[ 3 ] )++; break;
            }
        }
    }

    if ( ( state & align_iter_insert ) == align_iter_insert )
    {
        const INSDC_4na_bin *bases;
        uint32_t i, n = ReferenceIteratorBasesInserted ( ref_iter, &bases );
        (counters->inserts) += n;
        for ( i = 0; i < n; ++i )
        {
            add_char_2_dyn_string( &(counters->ins), _4na_to_ascii( bases[ i ], false ) );
        }
    }

    if ( ( state & align_iter_delete ) == align_iter_delete )
    {
        const INSDC_4na_bin *bases;
        INSDC_coord_zero ref_pos;
        uint32_t n = ReferenceIteratorBasesDeleted ( ref_iter, &ref_pos, &bases );
        if ( bases != NULL )
        {
            uint32_t i;
            (counters->deletes) += n;
            for ( i = 0; i < n; ++i )
            {
                add_char_2_dyn_string( &(counters->del), _4na_to_ascii( bases[ i ], false ) );
            }
            free( (void *) bases );
        }
    }
}


static void print_counter_line( const char * refname,
                                INSDC_coord_zero pos,
                                INSDC_4na_bin base,
                                uint32_t depth,
                                pileup_counters * counters )
{
    char c = _4na_to_ascii( base, false );
    KOutMsg( "%s\t%u\t%c\t%u\t", refname, pos + 1, c, depth );
    if ( counters->matches > 0 )
        KOutMsg( "%u=", counters->matches );
    if ( counters->mismatches[ 0 ] > 0 )
        KOutMsg( "%uA", counters->mismatches[ 0 ] );
    if ( counters->mismatches[ 1 ] > 0 )
        KOutMsg( "%uC", counters->mismatches[ 1 ] );
    if ( counters->mismatches[ 2 ] > 0 )
        KOutMsg( "%uG", counters->mismatches[ 2 ] );
    if ( counters->mismatches[ 3 ] > 0 )
        KOutMsg( "%uT", counters->mismatches[ 3 ] );
    if ( counters->inserts > 0 )
    {
        KOutMsg( "%uI%s", counters->inserts, counters->ins.data );
    }
    if ( counters->deletes > 0 )
    {
        KOutMsg( "%uD%s", counters->deletes, counters->del.data );
    }
    KOutMsg( "\n" );
}

static rc_t walk_counter_position( ReferenceIterator *ref_iter,
                                   const char * refname,
                                   pileup_counters * counters )
{
    INSDC_coord_zero pos;
    uint32_t depth;
    INSDC_4na_bin base;

    rc_t rc = ReferenceIteratorPosition ( ref_iter, &pos, &depth, &base );
    if ( rc != 0 )
    {
        if ( GetRCState( rc ) != rcDone )
        {
            LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
        }
    }
    else if ( depth > 0 )
    {
        const PlacementRecord *rec;
        rc_t rc1 = ReferenceIteratorNextPlacement ( ref_iter, &rec );
        clear_counters( counters );
        while ( rc1 == 0 )
        {
            walk_counter_state( ref_iter, counters );
            rc1 = ReferenceIteratorNextPlacement ( ref_iter, &rec );
        }

        if ( GetRCState( rc1 ) == rcDone ) { rc = 0; } else { rc = rc1; }
        if ( rc == 0 )
        {
            print_counter_line( refname, pos, base, depth, counters );
        }
    } 
    return rc;
}


static rc_t walk_just_counters( ReferenceIterator *ref_iter,
                                const char * refname,
                                bool skip_empty )
{
    pileup_counters counters;
    rc_t rc = prepare_counters( &counters );
    if ( rc == 0 )
    {
        while ( rc == 0 )
        {
            rc = Quitting ();
            if ( rc == 0 )
            {
                /* this is the 2nd level of walking the reference-iterator: 
                   visiting each position (that has alignments) on this reference */
                rc = ReferenceIteratorNextPos ( ref_iter, skip_empty );
                if ( rc != 0 )
                {
                    if ( GetRCState( rc ) != rcDone )
                    {
                        LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
                    }
                }
                else
                {
                    rc = walk_counter_position( ref_iter, refname, &counters );
                    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
                }
            }
        }
        finish_counters( &counters );
    }
    return rc;
}


/* =========================================================================================== */


enum { fsm_INIT = 0, fsm_DATA, fsm_GAP1, fsm_GAP2 };

typedef struct fsm_context fsm_context;
struct fsm_context
{
    uint32_t state;
    const char * refname;
    INSDC_coord_zero start;
    INSDC_coord_zero end;
};


static void fsm_initialize( fsm_context * ctx, const char * refname )
{
    ctx->state = fsm_INIT;
    ctx->refname = refname;
    ctx->start = 0;
    ctx->end = 0;
}

static void fsm_finalize( fsm_context * ctx, INSDC_coord_zero pos )
{
    switch( ctx->state )
    {
        case fsm_DATA : ;
        case fsm_GAP1 : KOutMsg( "%s:%u-%u\n", ctx->refname, ctx->start, pos ); break;
    }
}

/* transition into state 'fsm_DATA' */
static void fsm_data( fsm_context * ctx, INSDC_coord_zero pos )
{
    switch( ctx->state )
    {
        case fsm_INIT : ;
        case fsm_GAP2 : ctx->start = pos; break;
    }
    ctx->state = fsm_DATA;
}

/* transition into state 'fsm_GAP1' */
static void fsm_gap1( fsm_context * ctx, INSDC_coord_zero pos )
{
    if ( ctx->state == fsm_DATA )
    {
        ctx->end = pos;
    }
    ctx->state = fsm_GAP1;
}

/* transition into state 'fsm_GAP2' */
static void fsm_gap2( fsm_context * ctx )
{
    if ( ctx->state == fsm_GAP1 )
    {
        KOutMsg( "%s:%u-%u\n", ctx->refname, ctx->start, ctx->end );
    }
    ctx->state = fsm_GAP2;
}


static void fsm_run( fsm_context * ctx, uint32_t depth, INSDC_coord_zero pos, uint32_t maxgap )
{
    switch( ctx->state )
    {
        case fsm_INIT : if ( depth > 0 )
                            fsm_data( ctx, pos );
                        else
                            fsm_gap2( ctx );
                        break;

        case fsm_DATA : if ( depth == 0 )
                            fsm_gap1( ctx, pos );
                        break;

        case fsm_GAP1 : if ( ( pos - ctx->end ) > maxgap )
                            fsm_gap2( ctx );
                        if ( depth > 0 )
                            fsm_data( ctx, pos );
                        break;

        case fsm_GAP2 : if ( depth > 0 )
                            fsm_data( ctx, pos );
                        break;
    }

}

#if 0
static void fsm_show( fsm_context * ctx, INSDC_coord_zero pos )
{
    switch( ctx->state )
    {
        case fsm_INIT : KOutMsg( "[%u].INIT\n", pos ); break;
        case fsm_DATA : KOutMsg( "[%u].DATA\n", pos ); break;
        case fsm_GAP1 : KOutMsg( "[%u].GAP1\n", pos ); break;
        case fsm_GAP2 : KOutMsg( "[%u].GAP2\n", pos ); break;
    }
}
#endif

static rc_t walk_and_detect( ReferenceIterator *ref_iter,
                             const char * refname, uint32_t maxgap )
{
    rc_t rc = 0;
    INSDC_coord_zero pos;

    /* here we have a little FSM with these states: INIT, DATA, GAP1, GAP2 */
    fsm_context ctx;
    fsm_initialize( &ctx, refname );
    while ( rc == 0 )
    {
        rc = Quitting ();
        if ( rc == 0 )
        {
            /* this is the 2nd level of walking the reference-iterator: 
               visiting each position (that has alignments) on this reference */
            rc = ReferenceIteratorNextPos ( ref_iter, true );
            if ( rc != 0 )
            {
                if ( GetRCState( rc ) != rcDone )
                {
                    LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
                }
            }
            else
            {
                uint32_t depth;
                rc = ReferenceIteratorPosition ( ref_iter, &pos, &depth, NULL );
                if ( rc == 0 )
                {
                    fsm_run( &ctx, depth, pos, maxgap );
                    /* fsm_show( &ctx, pos ); */
                }
                if ( GetRCState( rc ) == rcDone ) { rc = 0; }
            }
        }
    }
    fsm_finalize( &ctx, pos );
    return 0;
}


/* =========================================================================================== */


static rc_t walk_ref_iter( ReferenceIterator *ref_iter, pileup_options *options )
{
    rc_t rc = 0;
    while( rc == 0 )
    {
        /* this is the 1st level of walking the reference-iterator: 
           visiting each (requested) reference */
        struct ReferenceObj const * refobj;

        rc = ReferenceIteratorNextReference( ref_iter, NULL, NULL, &refobj );
        if ( rc == 0 )
        {
            if ( refobj != NULL )
            {
                const char * refname = NULL;
                if ( options->use_seq_name )
                    rc = ReferenceObj_Name( refobj, &refname );
                else
                    rc = ReferenceObj_SeqId( refobj, &refname );
                if ( rc == 0 )
                {
                    switch( options->output_mode )
                    {
                        case sra_pileup_samtools : rc = walk_reference( ref_iter, refname, options );
                                                   break;

                        case sra_pileup_counters : rc = walk_just_counters( ref_iter, refname, true );
                                                   break;

                        case sra_pileup_detect : rc = walk_and_detect( ref_iter, refname, 200 );
                                                 break;

                        default : KOutMsg( "unknown output-mode '%u'\n", options->output_mode );
                                  break;

                    }
                    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
                }
                else
                {
                    if ( options->use_seq_name )
                    {
                        LOGERR( klogInt, rc, "ReferenceObj_Name() failed" );
                    }
                    else
                    {
                        LOGERR( klogInt, rc, "ReferenceObj_SeqId() failed" );
                    }
                }
            }
        }
        else
        {
            if ( GetRCState( rc ) != rcDone )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextReference() failed" );
            }
        }
    }
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    if ( GetRCState( rc ) == rcCanceled ) { rc = 0; }
    /* RC ( rcExe, rcProcess, rcExecuting, rcProcess, rcCanceled ); */
    return rc;
}


/* =========================================================================================== */


static rc_t add_quality_and_orientation( const VTable *tbl, const VCursor ** cursor, bool omit_qualities )
{
    rc_t rc = VTableCreateCursorRead ( tbl, cursor );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VTableCreateCursorRead() failed" );
    }

    if ( rc == 0 && !omit_qualities )
    {
        uint32_t quality_idx;
        rc = VCursorAddColumn ( *cursor, &quality_idx, COL_QUALITY );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(QUALITY) failed" );
        }
    }

    if ( rc == 0 )
    {
        uint32_t ref_orientation_idx;
        rc = VCursorAddColumn ( *cursor, &ref_orientation_idx, COL_REF_ORIENTATION );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(REF_ORIENTATION) failed" );
        }
    }

    if ( rc == 0 )
    {
        uint32_t read_filter_idx;
        rc = VCursorAddColumn ( *cursor, &read_filter_idx, COL_READ_FILTER );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(READ_FILTER) failed" );
        }
    }
    return rc;
}


static rc_t prepare_prim_cursor( const VDatabase *db, const VCursor ** cursor, bool omit_qualities )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "PRIMARY_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(PRIMARY_ALIGNMENT) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities );
        VTableRelease ( tbl );
    }
    return rc;
}


static rc_t prepare_sec_cursor( const VDatabase *db, const VCursor ** cursor, bool omit_qualities )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "SECONDARY_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(SECONDARY_ALIGNMENT) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities );
        VTableRelease ( tbl );
    }
    return rc;
}


static rc_t prepare_evidence_cursor( const VDatabase *db, const VCursor ** cursor, bool omit_qualities )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "EVIDENCE_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(EVIDENCE) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities );
        VTableRelease ( tbl );
    }
    return rc;
}


static void show_placement_params( const char * prefix, const ReferenceObj *refobj,
                                   uint32_t start, uint32_t end )
{
    const char * name;
    rc_t rc = ReferenceObj_SeqId( refobj, &name );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "ReferenceObj_SeqId() failed" );
    }
    else
    {
        KOutMsg( "prepare %s: <%s> %u..%u\n", prefix, name, start, end ) ;
    }
}


static rc_t CC prepare_section_cb( prepare_ctx * ctx, uint32_t start, uint32_t end )
{
    rc_t rc = 0;
    INSDC_coord_len len;
    if ( ctx->db == NULL || ctx->refobj == NULL )
    {
        rc = SILENT_RC ( rcApp, rcNoTarg, rcOpening, rcSelf, rcInvalid );
    }
    else
    {
        rc = ReferenceObj_SeqLength( ctx->refobj, &len );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "ReferenceObj_SeqLength() failed" );
        }
        else
        {
            rc_t rc1 = 0, rc2 = 0, rc3 = 0;

            if ( start == 0 ) start = 1;
            if ( ( end == 0 )||( end > len + 1 ) )
            {
                end = ( len - start ) + 1;
            }
            /* depending on ctx->select prepare primary, secondary or both... */
            if ( ctx->use_primary_alignments )
            {
                const VCursor * prim_align_cursor = NULL;
                rc1 = prepare_prim_cursor( ctx->db, &prim_align_cursor, ctx->omit_qualities );
                if ( rc1 == 0 )
                {
    /*                show_placement_params( "primary", ctx->refobj, start, end ); */
                    rc1 = ReferenceIteratorAddPlacements ( ctx->ref_iter,       /* the outer ref-iter */
                                                          ctx->refobj,          /* the ref-obj for this chromosome */
                                                          start - 1,            /* start ( zero-based ) */
                                                          end - start + 1,      /* length */
                                                          NULL,                 /* ref-cursor */
                                                          prim_align_cursor,    /* align-cursor */
                                                          primary_align_ids,    /* which id's */
                                                          ctx->spot_group );    /* what read-group */
                    if ( rc1 != 0 )
                    {
                        LOGERR( klogInt, rc1, "ReferenceIteratorAddPlacements(prim) failed" );
                    }
                    VCursorRelease( prim_align_cursor );
                }
            }
            if ( ctx->use_secondary_alignments )
            {
                const VCursor * sec_align_cursor = NULL;
                rc2 = prepare_sec_cursor( ctx->db, &sec_align_cursor, ctx->omit_qualities );
                if ( rc2 == 0 )
                {
    /*                show_placement_params( "secondary", ctx->refobj, start, end ); */
                    rc2 = ReferenceIteratorAddPlacements ( ctx->ref_iter,       /* the outer ref-iter */
                                                          ctx->refobj,          /* the ref-obj for this chromosome */
                                                          start - 1,            /* start ( zero-based ) */
                                                          end - start + 1,      /* length */
                                                          NULL,                 /* ref-cursor */
                                                          sec_align_cursor,     /* align-cursor */
                                                          secondary_align_ids,  /* which id's */
                                                          ctx->spot_group );    /* what read-group */
                    if ( rc2 != 0 )
                    {
                        LOGERR( klogInt, rc2, "ReferenceIteratorAddPlacements(sec) failed" );
                    }
                    VCursorRelease( sec_align_cursor );
                }
            }

            if ( ctx->use_evidence_alignments )
            {
                const VCursor * ev_align_cursor = NULL;
                rc3 = prepare_evidence_cursor( ctx->db, &ev_align_cursor, ctx->omit_qualities );
                if ( rc3 == 0 )
                {
    /*                show_placement_params( "evidende", ctx->refobj, start, end ); */
                    rc3 = ReferenceIteratorAddPlacements ( ctx->ref_iter,       /* the outer ref-iter */
                                                          ctx->refobj,          /* the ref-obj for this chromosome */
                                                          start - 1,            /* start ( zero-based ) */
                                                          end - start + 1,      /* length */
                                                          NULL,                 /* ref-cursor */
                                                          ev_align_cursor,      /* align-cursor */
                                                          evidence_align_ids,   /* which id's */
                                                          ctx->spot_group );    /* what read-group */
                    if ( rc3 != 0 )
                    {
                        LOGERR( klogInt, rc3, "ReferenceIteratorAddPlacements(evidence) failed" );
                    }
                    VCursorRelease( ev_align_cursor );
                }
            }

            if ( rc1 == SILENT_RC( rcAlign, rcType, rcAccessing, rcRow, rcNotFound ) )
            { /* from allocate_populate_rec */
                rc = rc1;
            }
            else if ( rc1 == 0 )
                rc = 0;
            else if ( rc2 == 0 )
                rc = 0;
            else if ( rc3 == 0 )
                rc = 0;
            else
                rc = rc1;
        }
    }
    return rc;
}


typedef struct foreach_arg_ctx
{
    pileup_options *options;
    const VDBManager *vdb_mgr;
    VSchema *vdb_schema;
    ReferenceIterator *ref_iter;
    BSTree *ranges;
} foreach_arg_ctx;


/* called for each source-file/accession */
static rc_t CC on_argument( const char * path, const char * spot_group, void * data )
{
    rc_t rc = 0;
    foreach_arg_ctx * ctx = ( foreach_arg_ctx * )data;
    prepare_ctx prep;

    prep.omit_qualities = ctx->options->omit_qualities;
    prep.use_primary_alignments = ( ( ctx->options->cmn.tab_select & primary_ats ) == primary_ats );
    prep.use_secondary_alignments = ( ( ctx->options->cmn.tab_select & secondary_ats ) == secondary_ats );
    prep.use_evidence_alignments = ( ( ctx->options->cmn.tab_select & evidence_ats ) == evidence_ats );
    prep.ref_iter = ctx->ref_iter;
    prep.spot_group = spot_group;
    prep.on_section = prepare_section_cb;
    prep.data = NULL;

    rc = prepare_ref_iter( &prep, ctx->vdb_mgr, ctx->vdb_schema, path, ctx->ranges );
    if ( rc == 0 && prep.db == NULL )
    {
        rc = RC ( rcApp, rcNoTarg, rcOpening, rcSelf, rcInvalid );
        LOGERR( klogInt, rc, "unsupported source" );
    }
    return rc;
}


static rc_t pileup_main( Args * args, pileup_options *options )
{
    foreach_arg_ctx arg_ctx;
    pileup_callback_data cb_data;
    KDirectory *dir;

    /* (1) make the align-manager ( necessary to make a ReferenceIterator... ) */
    rc_t rc = AlignMgrMakeRead ( &cb_data.almgr );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "AlignMgrMake() failed" );
    }

    cb_data.options = options;
    arg_ctx.options = options;
    arg_ctx.vdb_schema = NULL;

    /* (2) make the reference-iterator */
    if ( rc == 0 )
    {
        PlacementRecordExtendFuncs cb_block;

        cb_block.data = &cb_data;
        cb_block.destroy = NULL;
        cb_block.populate = populate_tooldata;
        cb_block.alloc_size = alloc_size;
        cb_block.fixed_size = 0;

        rc = AlignMgrMakeReferenceIterator ( cb_data.almgr, &arg_ctx.ref_iter, &cb_block, options->minmapq );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "AlignMgrMakeReferenceIterator() failed" );
        }
    }

    /* (3) make a KDirectory ( necessary to make a vdb-manager ) */
    if ( rc == 0 )
    {
        rc = KDirectoryNativeDir( &dir );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "KDirectoryNativeDir() failed" );
        }
    }

    /* (4) make a vdb-manager */
    if ( rc == 0 )
    {
        rc = VDBManagerMakeRead ( &arg_ctx.vdb_mgr, dir );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VDBManagerMakeRead() failed" );
        }
    }


    /* (5) make a vdb-schema */
    if ( rc == 0 )
    {
        rc = VDBManagerMakeSRASchema( arg_ctx.vdb_mgr, &arg_ctx.vdb_schema );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VDBManagerMakeSRASchema() failed" );
        }
        else if ( options->cmn.schema_file != NULL )
        {
            rc = VSchemaParseFile( arg_ctx.vdb_schema, options->cmn.schema_file );
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "VSchemaParseFile() failed" );
            }
        }
    }

    /* (5) loop through the given input-filenames and load the ref-iter with it's input */
    if ( rc == 0 )
    {
        BSTree regions;
        rc = init_ref_regions( &regions, args ); /* cmdline_cmn.c */
        if ( rc == 0 )
        {
            bool empty = false;
            check_ref_regions( &regions ); /* sanitize input... */
            arg_ctx.ranges = &regions;
            rc = foreach_argument( args, dir, options->div_by_spotgrp, &empty, on_argument, &arg_ctx ); /* cmdline_cmn.c */
            if ( empty )
            {
                Usage ( args );
            }
            free_ref_regions( &regions );
        }
    }

    /* (6) walk the "loaded" ref-iterator ===> perform the pileup */
    if ( rc == 0 )
    {
        /* ============================================== */
        rc = walk_ref_iter( arg_ctx.ref_iter, options );
        /* ============================================== */
    }

    if ( arg_ctx.vdb_mgr != NULL ) VDBManagerRelease( arg_ctx.vdb_mgr );
    if ( arg_ctx.vdb_schema != NULL ) VSchemaRelease( arg_ctx.vdb_schema );
    if ( dir != NULL ) KDirectoryRelease( dir );
    if ( arg_ctx.ref_iter != NULL ) ReferenceIteratorRelease( arg_ctx.ref_iter );
    if ( cb_data.almgr != NULL ) AlignMgrRelease ( cb_data.almgr );
    return rc;
}


/* =========================================================================================== */

rc_t CC KMain( int argc, char *argv [] )
{
    rc_t rc = KOutHandlerSet( write_to_FILE, stdout );
    ReportBuildDate( __DATE__ );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "KOutHandlerSet() failed" );
    }
    else
    {
        Args * args;

        KLogHandlerSetStdErr();
        rc = ArgsMakeAndHandle( &args, argc, argv, 2,
            MyOptions, sizeof MyOptions / sizeof MyOptions [ 0 ],
            CommonOptions_ptr(), CommonOptions_count() );
        if ( rc == 0 )
        {
            rc = parse_inf_file( args ); /* cmdline_cmn.h */
            if ( rc == 0 )
            {
                pileup_options options;
                rc = get_pileup_options( args, &options );
                if ( rc == 0 )
                {
                    if ( options.cmn.output_file != NULL )
                    {
                        rc = set_stdout_to( options.cmn.gzip_output,
                                            options.cmn.bzip_output,
                                            options.cmn.output_file,
                                            32 * 1024 );
                    }

                    if ( rc == 0 )
                    {
                        if ( options.reref )
                        {
                            rc = report_on_reference( args, true ); /* reref.c */
                        }
                        else
                        {
                            /* ============================== */
                            rc = pileup_main( args, &options );
                            /* ============================== */
                        }
                    }

                    if ( options.cmn.output_file != NULL )
                        release_stdout_redirection();
                }
            }
            ArgsWhack( args );
        }
    }

    {
        /* Report execution environment if necessary */
        rc_t rc2 = ReportFinalize( rc );
        if ( rc == 0 )
            rc = rc2;
    }
    return rc;
}
