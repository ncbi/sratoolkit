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

#include "ref_regions.h"
#include "cmdline_cmn.h"
#include "reref.h"
#include "ref_walker.h"

#include <kapp/main.h>

#include <klib/out.h>
#include <klib/printf.h>
#include <klib/report.h>
#include <klib/sort.h>
#include <klib/vector.h>

#include <kfs/file.h>
#include <kfs/buffile.h>
#include <kfs/bzip.h>
#include <kfs/gzip.h>

#include <insdc/sra.h>

#include <kdb/manager.h>

#include <vdb/manager.h>
#include <vdb/schema.h>
#include <vdb/report.h> /* ReportSetVDBManager() */
#include <vdb/vdb-priv.h> /* VDBManagerDisablePagemapThread() */

#include <sra/sraschema.h>
#include <align/manager.h>

#include <os-native.h>
#include <sysalloc.h>
#include <string.h>

#define COL_QUALITY "QUALITY"
#define COL_REF_ORIENTATION "REF_ORIENTATION"
#define COL_READ_FILTER "READ_FILTER"
#define COL_TEMPLATE_LEN "TEMPLATE_LEN"

#define OPTION_MINMAPQ "minmapq"
#define ALIAS_MINMAPQ  "q"

#define OPTION_DUPS    "duplicates"
#define ALIAS_DUPS     "d"

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

#define OPTION_MIN_M   "minmismatch"

#define OPTION_FUNC    "function"
#define ALIAS_FUNC     NULL

#define FUNC_COUNTERS   "count"
#define FUNC_STAT       "stat"
#define FUNC_RE_REF     "ref"
#define FUNC_RE_REF_EXT "ref-ex"
#define FUNC_DEBUG      "debug"
#define FUNC_MISMATCH   "mismatch"
#define FUNC_TEST       "test"

enum
{
    sra_pileup_samtools = 0,
    sra_pileup_counters = 1,
    sra_pileup_stat = 2,
    sra_pileup_report_ref = 3,
    sra_pileup_report_ref_ext = 4,
    sra_pileup_debug = 5,
    sra_pileup_mismatch = 6,
    sra_pileup_test = 7
};

static const char * minmapq_usage[]         = { "Minimum mapq-value, ", 
                                                "alignments with lower mapq",
                                                "will be ignored (default=0)", NULL };

static const char * dups_usage[]            = { "process duplicates ( 0...off/1..on )", NULL };

static const char * noqual_usage[]          = { "Omit qualities in output", NULL };

static const char * noskip_usage[]          = { "Does not skip reference-regions without alignments", NULL };

static const char * showid_usage[]          = { "Shows alignment-id for every base", NULL };

static const char * spotgrp_usage[]         = { "divide by spotgroups", NULL };

static const char * seqname_usage[]         = { "use original seq-name", NULL };

static const char * min_m_usage[]           = { "min percent of mismatches used in function mismatch, def is 5%", NULL };

static const char * func_ref_usage[]        = { "list references", NULL };
static const char * func_ref_ex_usage[]     = { "list references + coverage", NULL };
static const char * func_count_usage[]      = { "sort pileup with counters", NULL };
static const char * func_stat_usage[]       = { "strand/tlen statistic", NULL };
static const char * func_mismatch_usage[]   = { "only lines with mismatch", NULL };
static const char * func_usage[]            = { "alternative functionality", NULL };

OptDef MyOptions[] =
{
    /*name,           alias,         hfkt, usage-help,    maxcount, needs value, required */
    { OPTION_MINMAPQ, ALIAS_MINMAPQ, NULL, minmapq_usage, 1,        true,        false },
    { OPTION_DUPS,    ALIAS_DUPS,    NULL, dups_usage,    1,        true,        false },
    { OPTION_NOQUAL,  ALIAS_NOQUAL,  NULL, noqual_usage,  1,        false,       false },
    { OPTION_NOSKIP,  ALIAS_NOSKIP,  NULL, noskip_usage,  1,        false,       false },
    { OPTION_SHOWID,  ALIAS_SHOWID,  NULL, showid_usage,  1,        false,       false },
    { OPTION_SPOTGRP, ALIAS_SPOTGRP, NULL, spotgrp_usage, 1,        false,       false },
    { OPTION_SEQNAME, ALIAS_SEQNAME, NULL, seqname_usage, 1,        false,       false },
    { OPTION_MIN_M,   NULL,          NULL, min_m_usage,   1,        true,        false },    
    { OPTION_FUNC,    ALIAS_FUNC,    NULL, func_usage,    1,        true,        false }
};

/* =========================================================================================== */

typedef struct pileup_options
{
    common_options cmn;
    bool process_dups;
    bool omit_qualities;
    bool read_tlen;
    bool no_skip;
    bool show_id;
    bool div_by_spotgrp;
    bool use_seq_name;
    uint32_t minmapq;
    uint32_t min_mismatch;
    uint32_t source_table;
    uint32_t function;  /* sra_pileup_samtools, sra_pileup_counters, sra_pileup_stat, 
                           sra_pileup_report_ref, sra_pileup_report_ref_ext, sra_pileup_debug */
} pileup_options;


typedef struct pileup_callback_data
{
    const AlignMgr *almgr;
    pileup_options *options;
} pileup_callback_data;


typedef struct pileup_col_ids
{
    uint32_t idx_quality;
    uint32_t idx_ref_orientation;
    uint32_t idx_read_filter;
    uint32_t idx_template_len;
} pileup_col_ids;


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


static int cmp_pchar( const char * a, const char * b )
{
    int res = -1;
    if ( ( a != NULL )&&( b != NULL ) )
    {
        size_t len_a = string_size( a );
        size_t len_b = string_size( b );
        res = string_cmp ( a, len_a, b, len_b, ( len_a < len_b ) ? len_b : len_a );
    }
    return res;
}

/* =========================================================================================== */


static rc_t get_pileup_options( Args * args, pileup_options *opts )
{
    rc_t rc = get_common_options( args, &opts->cmn );
    opts->function = sra_pileup_samtools;

    if ( rc == 0 )
        rc = get_uint32_option( args, OPTION_MINMAPQ, &opts->minmapq, 0 );

    if ( rc == 0 )
        rc = get_uint32_option( args, OPTION_MIN_M, &opts->min_mismatch, 5 );
        
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
    {
        const char * fkt = NULL;
        rc = get_str_option( args, OPTION_FUNC, &fkt );
        if ( rc == 0 && fkt != NULL )
        {
            if ( cmp_pchar( fkt, FUNC_COUNTERS ) == 0 )
                opts->function = sra_pileup_counters;
            else if ( cmp_pchar( fkt, FUNC_STAT ) == 0 )
                opts->function = sra_pileup_stat;
            else if ( cmp_pchar( fkt, FUNC_RE_REF ) == 0 )
                opts->function = sra_pileup_report_ref;
            else if ( cmp_pchar( fkt, FUNC_RE_REF_EXT ) == 0 )
                opts->function = sra_pileup_report_ref_ext;
            else if ( cmp_pchar( fkt, FUNC_DEBUG ) == 0 )
                opts->function = sra_pileup_debug;
            else if ( cmp_pchar( fkt, FUNC_MISMATCH ) == 0 )
                opts->function = sra_pileup_mismatch;
            else if ( cmp_pchar( fkt, FUNC_TEST ) == 0 )
                opts->function = sra_pileup_test;
        }
    }
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
    HelpOptionLine ( ALIAS_DUPS, OPTION_DUPS, "dup-mode", dups_usage );
    HelpOptionLine ( ALIAS_SPOTGRP, OPTION_SPOTGRP, "spotgroups-modes", spotgrp_usage );
    HelpOptionLine ( ALIAS_SEQNAME, OPTION_SEQNAME, NULL, seqname_usage );
    HelpOptionLine ( NULL, OPTION_MIN_M, NULL, min_m_usage );
    
    HelpOptionLine ( NULL, "function ref",      NULL, func_ref_usage );
    HelpOptionLine ( NULL, "function ref-ex",   NULL, func_ref_ex_usage );
    HelpOptionLine ( NULL, "function count",    NULL, func_count_usage );
    HelpOptionLine ( NULL, "function stat",     NULL, func_stat_usage );
    HelpOptionLine ( NULL, "function mismatch", NULL, func_mismatch_usage );
    
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


static char _4na_2_ascii_tab[] =
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
    bool reverse;   /* orientation towards reference ( false...in ref-orientation / true...reverse) */
    int32_t tlen;   /* template-len, for statistical analysis */
    uint8_t * quality;  /* ptr to quality... ( for sam-output ) */
};


static rc_t read_base_and_len( struct VCursor const *curs,
                               uint32_t column_idx,
                               int64_t row_id,
                               const void ** base,
                               uint32_t * len )
{
    uint32_t elem_bits, boff, len_intern;
    const void * ptr;
    rc_t rc = VCursorCellDataDirect ( curs, row_id, column_idx, &elem_bits, &ptr, &boff, &len_intern );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VCursorCellDataDirect() failed" );
    }
    else
    {
        if ( len != NULL ) *len = len_intern;
        if ( base != NULL ) *base = ptr;
    }
    return rc;
}


static rc_t CC populate_tooldata( void *obj, const PlacementRecord *placement,
        struct VCursor const *curs, INSDC_coord_zero ref_window_start, INSDC_coord_len ref_window_len,
        void *data, void * placement_ctx )
{
    tool_rec * rec = ( tool_rec * ) obj;
    pileup_callback_data * cb_data = ( pileup_callback_data * )data;
    pileup_col_ids * col_ids = placement_ctx;
    rc_t rc = 0;

    rec->quality = NULL;
    if ( !cb_data->options->process_dups )
    {
        const uint8_t * read_filter;
        uint32_t read_filter_len;
        rc = read_base_and_len( curs, col_ids->idx_read_filter, placement->id,
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
        rc = read_base_and_len( curs, col_ids->idx_ref_orientation, placement->id,
                                (const void **)&orientation, NULL );
        if ( rc == 0 )
            rec->reverse = *orientation;
    }

    if ( rc == 0 && !cb_data->options->omit_qualities )
    {
        const uint8_t * quality;
        uint32_t quality_len;

        rc = read_base_and_len( curs, col_ids->idx_quality, placement->id,
                                (const void **)&quality, &quality_len );
        if ( rc == 0 )
        {
            rec->quality = ( uint8_t * )rec;
            rec->quality += sizeof ( * rec );
            memcpy( rec->quality, quality, quality_len );
        }
    }

    if ( rc == 0 && cb_data->options->read_tlen )
    {
        const int32_t * tlen;
        uint32_t tlen_len;

        rc = read_base_and_len( curs, col_ids->idx_template_len, placement->id,
                                (const void **)&tlen, &tlen_len );
        if ( rc == 0 && tlen_len > 0 )
            rec->tlen = *tlen;
        else
            rec->tlen = 0;
    }
    else
        rec->tlen = 0;

    return rc;
}


static rc_t CC alloc_size( struct VCursor const *curs, int64_t row_id, size_t * size, void *data, void * placement_ctx )
{
    rc_t rc = 0;
    tool_rec * rec;
    pileup_callback_data *cb_data = ( pileup_callback_data * )data;
    pileup_col_ids * col_ids = placement_ctx;
    *size = ( sizeof *rec );

    if ( !cb_data->options->omit_qualities )
    {
        uint32_t q_len;
        rc = read_base_and_len( curs, col_ids->idx_quality, row_id, NULL, &q_len );
        if ( rc == 0 )
            *size += q_len;
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
            if ( reverse )
                rc = add_char_2_dyn_string( line, '<' );
            else
                rc = add_char_2_dyn_string( line, '>' );
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
        rc = print_2_dyn_string( line, "(%,lu:%,d-%,d/%u)",
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
        if ( rc == 0 )
            rc = Quitting();
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
                    rc = walk_reference( ref_iter, refname, options );
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
    return rc;
}


/* =========================================================================================== */

typedef struct walk_data walk_data;
struct walk_data
{
    void *data;                             /* opaque pointer to data passed to each function */
    ReferenceIterator *ref_iter;            /* the global reference-iter */
    pileup_options *options;                /* the tool-options */
    struct ReferenceObj const * ref_obj;    /* the current reference-object */
    const char * ref_name;                  /* the name of the current reference */
    INSDC_coord_zero ref_start;             /* start of the current reference */
    INSDC_coord_len ref_len;                /* length of the current reference */
    INSDC_coord_zero ref_window_start;      /* start of the current reference-window */
    INSDC_coord_len ref_window_len;         /* length of the current reference-window */
    INSDC_coord_zero ref_pos;               /* current position on the reference */
    uint32_t depth;                         /* coverage at the current position */
    INSDC_4na_bin ref_base;                 /* reference-base at the current position */
    const char * spotgroup;                 /* name of the current spotgroup ( can be NULL! ) */
    size_t spotgroup_len;                   /* length of the name of the current spotgroup ( can be 0 ) */
    const PlacementRecord *rec;             /* current placement-record */
    tool_rec * xrec;                        /* current extended placement-record (orientation, quality...) */
    int32_t state;                          /* state of the current placement at the current ref-pos ( bitmasked!) */
    INSDC_coord_zero seq_pos;               /* position inside the alignment at the current ref-pos */
};


typedef struct walk_funcs walk_funcs;
struct walk_funcs
{
    /* changing reference */
    rc_t ( CC * on_enter_ref ) ( walk_data * data );
    rc_t ( CC * on_exit_ref ) ( walk_data * data );

    /* changing reference-window */
    rc_t ( CC * on_enter_ref_window ) ( walk_data * data );
    rc_t ( CC * on_exit_ref_window ) ( walk_data * data );

    /* changing reference-position */
    rc_t ( CC * on_enter_ref_pos ) ( walk_data * data );
    rc_t ( CC * on_exit_ref_pos ) ( walk_data * data );

    /* changing spot-group */
    rc_t ( CC * on_enter_spotgroup ) ( walk_data * data );
    rc_t ( CC * on_exit_spotgroup ) ( walk_data * data );

    /* on each alignment */
    rc_t ( CC * on_placement ) ( walk_data * data );
};


static rc_t walk_placements( walk_data * data, walk_funcs * funcs )
{
    rc_t rc;
    do
    {
        rc = ReferenceIteratorNextPlacement ( data->ref_iter, &data->rec );
        if ( GetRCState( rc ) != rcDone )
        {
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextPlacement() failed" );
            }
            else
            {
                data->state = ReferenceIteratorState ( data->ref_iter, &data->seq_pos );
                data->xrec = ( tool_rec * ) PlacementRecordCast ( data->rec, placementRecordExtension1 );
                if ( funcs->on_placement != NULL )
                    rc = funcs->on_placement( data );
            }
        }
    } while ( rc == 0 );
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk_spot_group( walk_data * data, walk_funcs * funcs )
{
    rc_t rc;
    do
    {
        rc = ReferenceIteratorNextSpotGroup ( data->ref_iter, &data->spotgroup, &data->spotgroup_len );
        if ( GetRCState( rc ) != rcDone )
        {
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
            }
            else
            {
                if ( funcs->on_enter_spotgroup != NULL )
                    rc = funcs->on_enter_spotgroup( data );
                if ( rc == 0 )
                    rc = walk_placements( data, funcs );
                if ( rc == 0 && funcs->on_exit_spotgroup != NULL )
                    rc = funcs->on_exit_spotgroup( data );
            }
        }
    } while ( rc == 0 );
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk_ref_pos( walk_data * data, walk_funcs * funcs )
{
    rc_t rc;
    do
    {
        rc = ReferenceIteratorNextPos ( data->ref_iter, !data->options->no_skip );
        if ( GetRCState( rc ) != rcDone )
        {
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextPos() failed" );
            }
            else
            {
                rc = ReferenceIteratorPosition ( data->ref_iter, &data->ref_pos, &data->depth, &data->ref_base );
                if ( rc != 0 )
                {
                    LOGERR( klogInt, rc, "ReferenceIteratorPosition() failed" );
                }
                else
                {
                    if ( funcs->on_enter_ref_pos != NULL )
                        rc = funcs->on_enter_ref_pos( data );
                    if ( rc == 0 )
                        rc = walk_spot_group( data, funcs );
                    if ( rc == 0 && funcs->on_exit_ref_pos != NULL )
                        rc = funcs->on_exit_ref_pos( data );
                }
            }
            if ( rc == 0 ) { rc = Quitting(); }
        }
    } while ( rc == 0 );
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk_ref_window( walk_data * data, walk_funcs * funcs )
{
    rc_t rc;
    do
    {
        rc = ReferenceIteratorNextWindow ( data->ref_iter, &data->ref_window_start, &data->ref_window_len );
        if ( GetRCState( rc ) != rcDone )
        {
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextWindow() failed" );
            }
            else
            {
                if ( funcs->on_enter_ref_window != NULL )
                    rc = funcs->on_enter_ref_window( data );
                if ( rc == 0 )
                    rc = walk_ref_pos( data, funcs );
                if ( rc == 0 && funcs->on_exit_ref_window != NULL )
                    rc = funcs->on_exit_ref_window( data );
            }
        }
    } while ( rc == 0 );
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    return rc;
}


static rc_t walk( walk_data * data, walk_funcs * funcs )
{
    rc_t rc;

    data->ref_start = 0;
    data->ref_len = 0;
    data->ref_name = NULL;
    data->ref_obj = NULL;
    data->ref_window_start = 0;
    data->ref_window_len = 0;
    data->ref_pos = 0;
    data->depth = 0;
    data->ref_base = 0;
    data->spotgroup = NULL;
    data->spotgroup_len = 0;
    data->rec = NULL;
    data->xrec = NULL;
    data->state = 0;
    data->seq_pos = 0;

    do
    {
        rc = ReferenceIteratorNextReference( data->ref_iter, &data->ref_start, &data->ref_len, &data->ref_obj );
        if ( GetRCState( rc ) != rcDone )
        {
            if ( rc != 0 )
            {
                LOGERR( klogInt, rc, "ReferenceIteratorNextReference() failed" );
            }
            else if ( data->ref_obj != NULL )
            {
                if ( data->options->use_seq_name )
                    rc = ReferenceObj_Name( data->ref_obj, &data->ref_name );
                else
                    rc = ReferenceObj_SeqId( data->ref_obj, &data->ref_name );
                if ( rc != 0 )
                {
                    if ( data->options->use_seq_name )
                    {
                        LOGERR( klogInt, rc, "ReferenceObj_Name() failed" );
                    }
                    else
                    {
                        LOGERR( klogInt, rc, "ReferenceObj_SeqId() failed" );
                    }
                }
                else
                {
                    if ( funcs->on_enter_ref != NULL )
                        rc = funcs->on_enter_ref( data );
                    if ( rc == 0 )
                        rc = walk_ref_window( data, funcs );
                    if ( rc == 0 && funcs->on_exit_ref != NULL )
                        rc = funcs->on_exit_ref( data );
                }
            }
        }
    } while ( rc == 0 );
    if ( GetRCState( rc ) == rcDone ) { rc = 0; }
    if ( GetRCState( rc ) == rcCanceled ) { rc = 0; }
    return rc;
}


/* =========================================================================================== */

static rc_t CC walk_debug_enter_ref( walk_data * data )
{   return KOutMsg( "ENTER REF '%s' ( %u.%u )\n", data->ref_name, data->ref_start, data->ref_len );   }

static rc_t CC walk_debug_exit_ref( walk_data * data )
{   return KOutMsg( "EXIT  REF '%s' ( %u.%u )\n", data->ref_name, data->ref_start, data->ref_len );   }

static rc_t CC walk_debug_enter_ref_window( walk_data * data )
{   return KOutMsg( "  ENTER REF-WINDOW ( %u.%u )\n", data->ref_window_start, data->ref_window_len );   }

static rc_t CC walk_debug_exit_ref_window( walk_data * data )
{   return KOutMsg( "  EXIT  REF-WINDOW ( %u.%u )\n", data->ref_window_start, data->ref_window_len );   }

static rc_t CC walk_debug_enter_ref_pos( walk_data * data )
{   return KOutMsg( "    ENTER REF-POS ( %u / d=%u / '%c' )\n", data->ref_pos, data->depth, _4na_to_ascii( data->ref_base, false ) );   }

static rc_t CC walk_debug_exit_ref_pos( walk_data * data )
{   return KOutMsg( "    EXIT  REF-POS ( %u / d=%u / '%c' )\n", data->ref_pos, data->depth, _4na_to_ascii( data->ref_base, false ) );   }

static rc_t CC walk_debug_enter_sg( walk_data * data )
{   return KOutMsg( "      ENTER SPOTGROUP '%s'\n", data->spotgroup );   }

static rc_t CC walk_debug_exit_sg( walk_data * data )
{   return KOutMsg( "      EXIT SPOTGROUP '%s'\n", data->spotgroup );   }

static rc_t CC walk_debug_placement( walk_data * data )
{
    char c = ( data->xrec->reverse ? 'R' : 'F' );
    return KOutMsg( "        PLACEMENT #%lu %c ( TLEN %i )\n", data->rec->id, c, data->xrec->tlen );
}


static rc_t walk_debug( ReferenceIterator *ref_iter, pileup_options *options )
{
    rc_t rc;
    walk_data data;
    walk_funcs funcs;

    data.ref_iter = ref_iter;
    data.options = options;
    
    funcs.on_enter_ref = walk_debug_enter_ref;
    funcs.on_exit_ref = walk_debug_exit_ref;

    funcs.on_enter_ref_window = walk_debug_enter_ref_window;
    funcs.on_exit_ref_window = walk_debug_exit_ref_window;

    funcs.on_enter_ref_pos = walk_debug_enter_ref_pos;
    funcs.on_exit_ref_pos = walk_debug_exit_ref_pos;

    funcs.on_enter_spotgroup = walk_debug_enter_sg;
    funcs.on_exit_spotgroup = walk_debug_exit_sg;
    
    funcs.on_placement = walk_debug_placement;

    rc = walk( &data, &funcs );
    return rc;
}


static uint32_t percent( uint32_t v1, uint32_t v2 )
{
    uint32_t sum = v1 + v2;
    uint32_t res = 0;
    if ( sum > 0 )
        res = ( ( v1 * 100 ) / sum );
    return res;
}

/* =========================================================================================== */

typedef struct indel_fragment
{
    BSTNode node;
    const char * bases;
    uint32_t len;
    uint32_t count;
} indel_fragment;


static indel_fragment * make_indel_fragment( const char * bases, uint32_t len )
{
    indel_fragment * res = malloc( sizeof * res );
    if ( res != NULL )
    {
        res->bases = string_dup ( bases, len );
        if ( res->bases == NULL )
        {
            free( res );
            res = NULL;
        }
        else
        {
            res->len = len;
            res->count = 1;
        }
    }
    return res;
}


static void CC free_indel_fragment( BSTNode * n, void * data )
{
    indel_fragment * fragment = ( indel_fragment * ) n;
    if ( fragment != NULL )
    {
        free( ( void * ) fragment->bases );
        free( fragment );
    }
}


static void free_fragments( BSTree * fragments )
{    
    BSTreeWhack ( fragments, free_indel_fragment, NULL );
}


typedef struct find_fragment_ctx
{
    const char * bases;
    uint32_t len;
} find_fragment_ctx;


static int CC cmp_fragment_vs_find_ctx( const void *item, const BSTNode *n )
{
    const indel_fragment * fragment = ( const indel_fragment * )n;
    const find_fragment_ctx * fctx = ( const find_fragment_ctx * )item;
    return string_cmp ( fctx->bases, fctx->len, fragment->bases, fragment->len, -1 );
}


static int CC cmp_fragment_vs_fragment( const BSTNode *item, const BSTNode *n )
{
    const indel_fragment * f1 = ( const indel_fragment * )item;
    const indel_fragment * f2 = ( const indel_fragment * )n;
    return string_cmp ( f1->bases, f1->len, f2->bases, f2->len, -1 );
}


static void count_indel_fragment( BSTree * fragments, const INSDC_4na_bin *bases, uint32_t len )
{
    find_fragment_ctx fctx;

    fctx.bases = malloc( len );
    if ( fctx.bases != NULL )
    {
        indel_fragment * fragment;
        uint32_t i;

        fctx.len = len;
        for ( i = 0; i < len; ++i )
            ( ( char * )fctx.bases )[ i ] = _4na_to_ascii( bases[ i ], false );

        fragment = ( indel_fragment * ) BSTreeFind ( fragments, &fctx, cmp_fragment_vs_find_ctx );
        if ( fragment == NULL )
        {
            fragment = make_indel_fragment( fctx.bases, len );
            if ( fragment != NULL )
            {
                rc_t rc = BSTreeInsert ( fragments, ( BSTNode * )fragment, cmp_fragment_vs_fragment );
                if ( rc != 0 )
                    free_indel_fragment( ( BSTNode * )fragment, NULL );
            }
        }
        else
            fragment->count++;

        free( ( void * ) fctx.bases );
    }
}


typedef struct walk_fragment_ctx
{
    rc_t rc;
    uint32_t n;
} walk_fragment_ctx;


static void CC on_fragment( BSTNode *n, void *data )
{
    walk_fragment_ctx * wctx = data;
    const indel_fragment * fragment = ( const indel_fragment * )n;
    if ( wctx->rc == 0 )
    {
        if ( wctx->n == 0 )
            wctx->rc = KOutMsg( "%u-%.*s", fragment->count, fragment->len, fragment->bases );
        else
            wctx->rc = KOutMsg( "|%u-%.*s", fragment->count, fragment->len, fragment->bases );
        wctx->n++;
    }
}


static rc_t print_fragments( BSTree * fragments )
{
    walk_fragment_ctx wctx;
    wctx.rc = 0;
    wctx.n = 0;
    BSTreeForEach ( fragments, false, on_fragment, &wctx );
    return wctx.rc;
}

/* =========================================================================================== */

typedef struct pileup_counters
{
    uint32_t matches;
    uint32_t mismatches[ 4 ];
    uint32_t inserts;
    uint32_t deletes;
    uint32_t forward;
    uint32_t reverse;
    uint32_t starting;
    uint32_t ending;
    BSTree insert_fragments;
    BSTree delete_fragments;
} pileup_counters;


static void clear_counters( pileup_counters * counters )
{
    uint32_t i;

    counters->matches = 0;
    for ( i = 0; i < 4; ++i )
        counters->mismatches[ i ] = 0;
    counters->inserts = 0;
    counters->deletes = 0;
    counters->forward = 0;
    counters->reverse = 0;
    counters->starting = 0;
    counters->ending = 0;
    BSTreeInit( &(counters->insert_fragments) );
    BSTreeInit( &(counters->delete_fragments) );
}


static void walk_counter_state( ReferenceIterator *ref_iter, int32_t state, bool reverse,
                                pileup_counters * counters )
{
    if ( ( state & align_iter_invalid ) == align_iter_invalid )
        return;

    if ( ( state & align_iter_skip ) != align_iter_skip )
    {
        if ( ( state & align_iter_match ) == align_iter_match )
            (counters->matches)++;
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

    if ( reverse )
        (counters->reverse)++;
    else
        (counters->forward)++;

    if ( ( state & align_iter_insert ) == align_iter_insert )
    {
        const INSDC_4na_bin *bases;
        uint32_t n = ReferenceIteratorBasesInserted ( ref_iter, &bases );
        (counters->inserts) += n;
        count_indel_fragment( &(counters->insert_fragments), bases, n );
    }

    if ( ( state & align_iter_delete ) == align_iter_delete )
    {
        const INSDC_4na_bin *bases;
        INSDC_coord_zero ref_pos;
        uint32_t n = ReferenceIteratorBasesDeleted ( ref_iter, &ref_pos, &bases );
        if ( bases != NULL )
        {
            (counters->deletes) += n;
            count_indel_fragment( &(counters->delete_fragments), bases, n );
            free( (void *) bases );
        }
    }

    if ( ( state & align_iter_first ) == align_iter_first )
        ( counters->starting)++;

    if ( ( state & align_iter_last ) == align_iter_last )
        ( counters->ending)++;
}


static rc_t print_counter_line( const char * ref_name,
                                INSDC_coord_zero ref_pos,
                                INSDC_4na_bin ref_base,
                                uint32_t depth,
                                pileup_counters * counters )
{
    char c = _4na_to_ascii( ref_base, false );

    rc_t rc = KOutMsg( "%s\t%u\t%c\t%u\t", ref_name, ref_pos + 1, c, depth );

    if ( rc == 0 && counters->matches > 0 )
        rc = KOutMsg( "%u", counters->matches );

    if ( rc == 0 /* && counters->mismatches[ 0 ] > 0 */ )
        rc = KOutMsg( "\t%u-A", counters->mismatches[ 0 ] );

    if ( rc == 0 /* && counters->mismatches[ 1 ] > 0 */ )
        rc = KOutMsg( "\t%u-C", counters->mismatches[ 1 ] );

    if ( rc == 0 /* && counters->mismatches[ 2 ] > 0 */ )
        rc = KOutMsg( "\t%u-G", counters->mismatches[ 2 ] );

    if ( rc == 0 /* && counters->mismatches[ 3 ] > 0 */ )
        rc = KOutMsg( "\t%u-T", counters->mismatches[ 3 ] );

    if ( rc == 0 )
        rc = KOutMsg( "\tI:" );
    if ( rc == 0 )
        rc = print_fragments( &(counters->insert_fragments) );

    if ( rc == 0 )
        rc = KOutMsg( "\tD:" );
    if ( rc == 0 )
        rc = print_fragments( &(counters->delete_fragments) );

    if ( rc == 0 )
        rc = KOutMsg( "\t%u%%", percent( counters->forward, counters->reverse ) );

    if ( rc == 0 && counters->starting > 0 )
        rc = KOutMsg( "\tS%u", counters->starting );

    if ( rc == 0 && counters->ending > 0 )
        rc = KOutMsg( "\tE%u", counters->ending );

    if ( rc == 0 )
        rc = KOutMsg( "\n" );

    free_fragments( &(counters->insert_fragments) );
    free_fragments( &(counters->delete_fragments) );

    return rc;
}


/* ........................................................................................... */


static rc_t CC walk_counters_enter_ref_pos( walk_data * data )
{
    clear_counters( data->data );
    return 0;
}

static rc_t CC walk_counters_exit_ref_pos( walk_data * data )
{
    rc_t rc = print_counter_line( data->ref_name, data->ref_pos, data->ref_base, data->depth, data->data );
    return rc;
}

static rc_t CC walk_counters_placement( walk_data * data )
{
    walk_counter_state( data->ref_iter, data->state, data->xrec->reverse, data->data );
    return 0;
}

static rc_t walk_counters( ReferenceIterator *ref_iter, pileup_options *options )
{
    walk_data data;
    walk_funcs funcs;
    pileup_counters counters;

    data.ref_iter = ref_iter;
    data.options = options;
    data.data = &counters;

    funcs.on_enter_ref = NULL;
    funcs.on_exit_ref = NULL;

    funcs.on_enter_ref_window = NULL;
    funcs.on_exit_ref_window = NULL;

    funcs.on_enter_ref_pos = walk_counters_enter_ref_pos;
    funcs.on_exit_ref_pos = walk_counters_exit_ref_pos;

    funcs.on_enter_spotgroup = NULL;
    funcs.on_exit_spotgroup = NULL;

    funcs.on_placement = walk_counters_placement;

    return walk( &data, &funcs );
}


/* =========================================================================================== */


static rc_t print_mismatches_line( const char * ref_name,
                                   INSDC_coord_zero ref_pos,
                                   uint32_t depth,
                                   uint32_t min_mismatch_percent,
                                   pileup_counters * counters )
{
    rc_t rc = 0;
    if ( depth > 0 )
    {
        uint32_t total_mismatches = counters->mismatches[ 0 ] +
                                    counters->mismatches[ 1 ] +
                                    counters->mismatches[ 2 ] +
                                    counters->mismatches[ 3 ];
	if ( total_mismatches * 100 >= min_mismatch_percent * depth) 
        {
                rc = KOutMsg( "%s\t%u\t%u\t%u\n", ref_name, ref_pos + 1, depth, total_mismatches );
        }
    }
    
    free_fragments( &(counters->insert_fragments) );
    free_fragments( &(counters->delete_fragments) );

    return rc;
}


/* ........................................................................................... */


static rc_t CC walk_mismatches_enter_ref_pos( walk_data * data )
{
    clear_counters( data->data );
    return 0;
}

static rc_t CC walk_mismatches_exit_ref_pos( walk_data * data )
{
    rc_t rc = print_mismatches_line( data->ref_name, data->ref_pos,
                                     data->depth, data->options->min_mismatch, data->data );
    return rc;
}

static rc_t CC walk_mismatches_placement( walk_data * data )
{
    walk_counter_state( data->ref_iter, data->state, data->xrec->reverse, data->data );
    return 0;
}

static rc_t walk_mismatches( ReferenceIterator *ref_iter, pileup_options * options )
{
    walk_data data;
    walk_funcs funcs;
    pileup_counters counters;

    data.ref_iter = ref_iter;
    data.options = options;
    data.data = &counters;

    funcs.on_enter_ref = NULL;
    funcs.on_exit_ref = NULL;

    funcs.on_enter_ref_window = NULL;
    funcs.on_exit_ref_window = NULL;

    funcs.on_enter_ref_pos = walk_mismatches_enter_ref_pos;
    funcs.on_exit_ref_pos = walk_mismatches_exit_ref_pos;

    funcs.on_enter_spotgroup = NULL;
    funcs.on_exit_spotgroup = NULL;

    funcs.on_placement = walk_mismatches_placement;

    return walk( &data, &funcs );
}


/* =========================================================================================== */


typedef struct tlen_array
{
    uint32_t * values;
    uint32_t capacity;
    uint32_t members;
    uint32_t zeros;
} tlen_array;


static rc_t init_tlen_array( tlen_array * a, uint32_t init_capacity )
{
    rc_t rc = 0;
    a->values = malloc( sizeof ( a->values[ 0 ] ) * init_capacity );
    if ( a->values == NULL )
        rc = RC ( rcApp, rcArgv, rcAccessing, rcMemory, rcExhausted );
    else
    {
        a->capacity = init_capacity;
        a->members = 0;
        a->zeros = 0;
    }
    return rc;
}


static void finish_tlen_array( tlen_array * a )
{
    if ( a->values != NULL )
    {
        free( a->values );
        a->values = NULL;
    }
}


static rc_t realloc_tlen_array( tlen_array * a, uint32_t new_depth )
{
    rc_t rc = 0;
    if ( new_depth > a->capacity )
    {
        void * p = realloc( a->values, ( sizeof ( a->values[ 0 ] ) ) * new_depth );
        if ( a->values == NULL )
            rc = RC ( rcApp, rcArgv, rcAccessing, rcMemory, rcExhausted );
        else
        {
            a->values = p;
            a->capacity = new_depth;
        }
    }
    return rc;
}


static void remove_from_tlen_array( tlen_array * a, uint32_t count )
{
    if ( count > 0 )
    {
        if ( a->members < count )
            a->members = 0;
        else
        {
            a->members -= count;
            memmove( &(a->values[ 0 ]), &(a->values[ count ]), a->members * ( sizeof a->values[ 0 ] ) );
        }
    }
}


static bool add_tlen_to_array( tlen_array * a, uint32_t value )
{
    bool res = ( value != 0 );
    if ( !res )
        a->zeros++;
    else
        a->values[ a->members++ ] = value;
    return res;
}


#define INIT_WINDOW_SIZE 50
#define MAX_SEQLEN_COUNT 500000

typedef struct strand
{
    uint32_t alignment_count, window_size, window_max, seq_len_accu_count;
    uint64_t seq_len_accu;
    tlen_array tlen_w;          /* tlen accumulater for all alignmnts starting/ending in window ending at current position */
    tlen_array tlen_l;          /* array holding the length of all position-slices in the window */
    tlen_array zeros;
} strand;


typedef struct stat_counters
{
    strand pos;
    strand neg;
} stat_counters;


static rc_t prepare_strand( strand * strand, uint32_t initial_size )
{
    rc_t rc = init_tlen_array( &strand->tlen_w, initial_size );
    if ( rc == 0 )
        rc = init_tlen_array( &strand->tlen_l, initial_size );
    if ( rc == 0 )
        rc = init_tlen_array( &strand->zeros, initial_size );
    if ( rc == 0 )
    {
        strand->window_size = 0;
        strand->window_max = INIT_WINDOW_SIZE;
        strand->seq_len_accu_count = 0;
        strand->seq_len_accu = 0;
    }
    return rc;
}


static rc_t prepare_stat_counters( stat_counters * counters, uint32_t initial_size )
{
    rc_t rc = prepare_strand( &counters->pos, initial_size );
    if ( rc == 0 )
        rc = prepare_strand( &counters->neg, initial_size );
    return rc;
}


static void finish_strand( strand * strand )
{
    finish_tlen_array( &strand->tlen_w );
    finish_tlen_array( &strand->tlen_l );
    finish_tlen_array( &strand->zeros );
}


static void finish_stat_counters( stat_counters * counters )
{
    finish_strand( &counters->pos );
    finish_strand( &counters->neg );
}


static rc_t realloc_strand( strand * strand, uint32_t new_depth )
{
    rc_t rc = realloc_tlen_array( &strand->tlen_w, strand->tlen_w.members + new_depth );
    if ( rc == 0 )
        rc = realloc_tlen_array( &strand->tlen_l, strand->tlen_l.members + new_depth );
    if ( rc == 0 )
        rc = realloc_tlen_array( &strand->zeros, strand->zeros.members + new_depth );
    strand->alignment_count = 0;
    return rc;
}


static void on_new_ref_position_strand( strand * strand )
{
    if ( ( strand->seq_len_accu_count < MAX_SEQLEN_COUNT ) && ( strand->seq_len_accu_count > 0 ) )
    {
        uint64_t w = ( strand->seq_len_accu / strand->seq_len_accu_count );
        if ( w > strand->window_max )
            strand->window_max = w;
    }

    if ( strand->window_size >= strand->window_max )
    {
        uint32_t to_remove = strand->tlen_l.values[ 0 ];
        remove_from_tlen_array( &strand->tlen_w, to_remove );
        remove_from_tlen_array( &strand->tlen_l, 1 );

        to_remove = strand->zeros.values[ 0 ];
        strand->tlen_w.zeros -= to_remove;
        remove_from_tlen_array( &strand->zeros, 1 );
    }
    else
        strand->window_size++;
    strand->tlen_l.values[ strand->tlen_l.members++ ] = 0;
    strand->zeros.values[ strand->zeros.members++ ] = 0;
}


/*
static int32_t avg( tlen_array * a )
{
    int64_t sum = 0;
    int32_t i;
    for ( i = 0; i < a->members; ++i )
        sum += a->values[ i ];
    if ( a->members > 1 )
        sum /= a->members;
    return (int32_t) sum;
}
*/


static uint32_t medium( tlen_array * a )
{
    if ( a->members == 0 )
        return 0;
    else
        return a->values[ a->members >> 1 ];
}


static uint32_t percentil( tlen_array * a, uint32_t p )
{
    if ( a->members == 0 )
        return 0;
    else
        return a->values[ ( a->members * p ) / 100 ];
}


static rc_t print_header_line( void )
{
    return KOutMsg( "\nREFNAME----\tREFPOS\tREFBASE\tDEPTH\tSTRAND%%\tTL+#0\tTL+10%%\tTL+MED\tTL+90%%\tTL-#0\tTL-10%%\tTL-MED\tTL-90%%\n\n" );
}


/* ........................................................................................... */


static rc_t CC walk_stat_enter_ref_window( walk_data * data )
{
    stat_counters * counters = data->data;
    counters->pos.tlen_w.members = 0;
    counters->pos.tlen_l.members = 0;
    counters->neg.tlen_w.members = 0;
    counters->neg.tlen_l.members = 0;
    return 0;
}


static rc_t CC walk_stat_enter_ref_pos( walk_data * data )
{
    rc_t rc;
    stat_counters * counters = data->data;

    on_new_ref_position_strand( &counters->pos );
    on_new_ref_position_strand( &counters->neg );

    rc = realloc_strand( &counters->pos, data->depth );
    if ( rc == 0 )
        rc = realloc_strand( &counters->neg, data->depth );

    return rc;
}


static rc_t CC walk_stat_exit_ref_pos( walk_data * data )
{
    char c = _4na_to_ascii( data->ref_base, false );
    stat_counters * counters = data->data;

    /* REF-NAME, REF-POS, REF-BASE, DEPTH */
    rc_t rc = KOutMsg( "%s\t%u\t%c\t%u\t", data->ref_name, data->ref_pos + 1, c, data->depth );

    /* STRAND-ness */
    if ( rc == 0 )
        rc = KOutMsg( "%u%%\t", percent( counters->pos.alignment_count, counters->neg.alignment_count ) );

    /* TLEN-Statistic for sliding window, only starting/ending placements */
    if ( rc == 0 )
    {
        tlen_array * a = &counters->pos.tlen_w;
        if ( a->members > 1 )
            ksort_uint32_t ( a->values, a->members );

        rc = KOutMsg( "%u\t%u\t%u\t%u\t", a->zeros, percentil( a, 10 ), medium( a ), percentil( a, 90 ) );
        if ( rc == 0 )
        {
            a = &counters->neg.tlen_w;
            if ( a->members > 1 )
                ksort_uint32_t ( a->values, a->members );
            rc = KOutMsg( "%u\t%u\t%u\t%u\t", a->zeros, percentil( a, 10 ), medium( a ), percentil( a, 90 ) );
        }
    }

/*
    KOutMsg( "( %u,%u )\t", counters->pos.window_max, counters->neg.window_max );
    KOutMsg( "< %u.%u, %u.%u ( %u.%u, %u.%u ) >",
            counters->pos.tlen_w.members, counters->pos.tlen_w.capacity, counters->neg.tlen_w.members, counters->neg.tlen_w.capacity,
            counters->pos.tlen_l.members, counters->pos.tlen_l.capacity, counters->neg.tlen_l.members, counters->neg.tlen_l.capacity );
*/

    if ( rc == 0 )
        rc = KOutMsg( "\n" );

    return rc;
}


static void walk_strand_placement( strand * strand, int32_t tlen, INSDC_coord_len seq_len )
{
    tlen_array * a;
    uint32_t value =  ( tlen < 0 ) ? -tlen : tlen;
    if ( add_tlen_to_array( &strand->tlen_w, value ) )
        a = &strand->tlen_l;
    else
        a = &strand->zeros;
    a->values[ a->members - 1 ]++;

    if ( strand->seq_len_accu_count < MAX_SEQLEN_COUNT )
    {
        strand->seq_len_accu += seq_len;
        strand->seq_len_accu_count++;
    }
}


static rc_t CC walk_stat_placement( walk_data * data )
{
    int32_t state = data->state;
    if ( ( state & align_iter_invalid ) != align_iter_invalid )
    {
        bool reverse = data->xrec->reverse;
        stat_counters * counters = data->data;
        strand * strand = ( reverse ) ? &counters->neg : &counters->pos;

        strand->alignment_count++;

        /* for TLEN-statistic on starting/ending placements at this pos */
        if ( ( ( state & align_iter_last ) == align_iter_last )&&( reverse ) )
            walk_strand_placement( strand, data->xrec->tlen, data->rec->len );
        else if ( ( ( state & align_iter_first ) == align_iter_first )&&( !reverse ) )
            walk_strand_placement( strand, data->xrec->tlen, data->rec->len );
    }
    return 0;
}


static rc_t walk_stat( ReferenceIterator *ref_iter, pileup_options *options )
{
    walk_data data;
    walk_funcs funcs;
    stat_counters counters;

    rc_t rc = print_header_line();
    if ( rc == 0 )
        rc = prepare_stat_counters( &counters, 1024 );
    if ( rc == 0 )
    {
        data.ref_iter = ref_iter;
        data.options = options;
        data.data = &counters;

        funcs.on_enter_ref = NULL;
        funcs.on_exit_ref = NULL;

        funcs.on_enter_ref_window = walk_stat_enter_ref_window;
        funcs.on_exit_ref_window = NULL;

        funcs.on_enter_ref_pos = walk_stat_enter_ref_pos;
        funcs.on_exit_ref_pos = walk_stat_exit_ref_pos;

        funcs.on_enter_spotgroup = NULL;
        funcs.on_exit_spotgroup = NULL;

        funcs.on_placement = walk_stat_placement;

        rc = walk( &data, &funcs );

        finish_stat_counters( &counters );
    }
    return rc;
}


/* =========================================================================================== */

static rc_t add_quality_and_orientation( const VTable *tbl, const VCursor ** cursor,
                                         bool omit_qualities, bool read_tlen, pileup_col_ids * cursor_ids )
{
    rc_t rc = VTableCreateCursorRead ( tbl, cursor );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VTableCreateCursorRead() failed" );
    }

    if ( rc == 0 && !omit_qualities )
    {
        rc = VCursorAddColumn ( *cursor, &cursor_ids->idx_quality, COL_QUALITY );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(QUALITY) failed" );
        }
    }

    if ( rc == 0 )
    {
        rc = VCursorAddColumn ( *cursor, &cursor_ids->idx_ref_orientation, COL_REF_ORIENTATION );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(REF_ORIENTATION) failed" );
        }
    }

    if ( rc == 0 )
    {
        rc = VCursorAddColumn ( *cursor, &cursor_ids->idx_read_filter, COL_READ_FILTER );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(READ_FILTER) failed" );
        }
    }

    if ( rc == 0 && read_tlen )
    {
        rc = VCursorAddColumn ( *cursor, &cursor_ids->idx_template_len, COL_TEMPLATE_LEN );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VCursorAddColumn(TEMPLATE_LEN) failed" );
        }
    }
    return rc;
}


static rc_t prepare_prim_cursor( const VDatabase *db, const VCursor ** cursor,
                                 bool omit_qualities, bool read_tlen, pileup_col_ids * cursor_ids )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "PRIMARY_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(PRIMARY_ALIGNMENT) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities, read_tlen, cursor_ids );
        VTableRelease ( tbl );
    }
    return rc;
}


static rc_t prepare_sec_cursor( const VDatabase *db, const VCursor ** cursor,
                                bool omit_qualities, bool read_tlen, pileup_col_ids * cursor_ids )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "SECONDARY_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(SECONDARY_ALIGNMENT) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities, read_tlen, cursor_ids );
        VTableRelease ( tbl );
    }
    return rc;
}


static rc_t prepare_evidence_cursor( const VDatabase *db, const VCursor ** cursor,
                                     bool omit_qualities, bool read_tlen, pileup_col_ids * cursor_ids )
{
    const VTable *tbl;
    rc_t rc = VDatabaseOpenTableRead ( db, &tbl, "EVIDENCE_ALIGNMENT" );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "VDatabaseOpenTableRead(EVIDENCE) failed" );
    }
    else
    {
        rc = add_quality_and_orientation( tbl, cursor, omit_qualities, read_tlen, cursor_ids );
        VTableRelease ( tbl );
    }
    return rc;
}

#if 0
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
#endif


static rc_t make_cursor_ids( Vector *cursor_id_vector, pileup_col_ids ** cursor_ids )
{
    rc_t rc;
    pileup_col_ids * ids = malloc( sizeof * ids );
    if ( ids == NULL )
        rc = RC ( rcApp, rcNoTarg, rcOpening, rcMemory, rcExhausted );
    else
    {
        rc = VectorAppend ( cursor_id_vector, NULL, ids );
        if ( rc != 0 )
            free( ids );
        else
            *cursor_ids = ids;
    }
    return rc;
}


static rc_t CC prepare_section_cb( prepare_ctx * ctx, uint32_t start, uint32_t end )
{
    rc_t rc = 0;
    INSDC_coord_len len;
    if ( ctx->db == NULL || ctx->refobj == NULL )
    {
        rc = SILENT_RC ( rcApp, rcNoTarg, rcOpening, rcSelf, rcInvalid );
        /* it is opened in prepare_db_table even if ctx->db == NULL */
        PLOGERR( klogErr, ( klogErr, rc, "failed to process $(path)",
            "path=%s", ctx->path == NULL ? "input argument" : ctx->path));
        ReportSilence();
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
            pileup_col_ids * cursor_ids;

            if ( start == 0 ) start = 1;
            if ( ( end == 0 )||( end > len + 1 ) )
            {
                end = ( len - start ) + 1;
            }

            /* depending on ctx->select prepare primary, secondary or both... */
            if ( ctx->use_primary_alignments )
            {
                const VCursor * prim_align_cursor = NULL;
                rc1 = make_cursor_ids( ctx->data, &cursor_ids );
                if ( rc1 != 0 )
                {
                    LOGERR( klogInt, rc1, "cannot create cursor-ids for prim. alignment cursor" );
                }
                else
                {
                    rc1 = prepare_prim_cursor( ctx->db, &prim_align_cursor, ctx->omit_qualities, ctx->read_tlen, cursor_ids );
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
                                                              ctx->spot_group,      /* what read-group */
                                                              cursor_ids            /* placement-context */
                                                             );
                        if ( rc1 != 0 )
                        {
                            LOGERR( klogInt, rc1, "ReferenceIteratorAddPlacements(prim) failed" );
                        }
                        VCursorRelease( prim_align_cursor );
                    }
                }
            }

            if ( ctx->use_secondary_alignments )
            {
                const VCursor * sec_align_cursor = NULL;
                rc2 = make_cursor_ids( ctx->data, &cursor_ids );
                if ( rc2 != 0 )
                {
                    LOGERR( klogInt, rc2, "cannot create cursor-ids for sec. alignment cursor" );
                }
                else
                {
                    rc2 = prepare_sec_cursor( ctx->db, &sec_align_cursor, ctx->omit_qualities, ctx->read_tlen, cursor_ids );
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
                                                              ctx->spot_group,      /* what read-group */
                                                              cursor_ids            /* placement-context */
                                                             );
                        if ( rc2 != 0 )
                        {
                            LOGERR( klogInt, rc2, "ReferenceIteratorAddPlacements(sec) failed" );
                        }
                        VCursorRelease( sec_align_cursor );
                    }
                }
            }

            if ( ctx->use_evidence_alignments )
            {
                const VCursor * ev_align_cursor = NULL;
                rc3 = make_cursor_ids( ctx->data, &cursor_ids );
                if ( rc3 != 0 )
                {
                    LOGERR( klogInt, rc3, "cannot create cursor-ids for ev. alignment cursor" );
                }
                else
                {
                    rc3 = prepare_evidence_cursor( ctx->db, &ev_align_cursor, ctx->omit_qualities, ctx->read_tlen, cursor_ids );
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
                                                              ctx->spot_group,      /* what read-group */
                                                              cursor_ids            /* placement-context */
                                                             );
                        if ( rc3 != 0 )
                        {
                            LOGERR( klogInt, rc3, "ReferenceIteratorAddPlacements(evidence) failed" );
                        }
                        VCursorRelease( ev_align_cursor );
                    }
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
    Vector *cursor_ids;
} foreach_arg_ctx;


/* called for each source-file/accession */
static rc_t CC on_argument( const char * path, const char * spot_group, void * data )
{
    rc_t rc = 0;
    foreach_arg_ctx * ctx = ( foreach_arg_ctx * )data;

    int path_type = ( VDBManagerPathType ( ctx->vdb_mgr, "%s", path ) & ~ kptAlias );
    if ( path_type != kptDatabase )
    {
        rc = RC ( rcApp, rcNoTarg, rcOpening, rcItem, rcUnsupported );
        PLOGERR( klogErr, ( klogErr, rc, "failed to open '$(path)', it is not a vdb-database", "path=%s", path ) );
    }
    else
    {
        const VDatabase *db;
        rc = VDBManagerOpenDBRead ( ctx->vdb_mgr, &db, ctx->vdb_schema, "%s", path );
        if ( rc != 0 )
        {
            rc = RC ( rcApp, rcNoTarg, rcOpening, rcItem, rcUnsupported );
            PLOGERR( klogErr, ( klogErr, rc, "failed to open '$(path)'", "path=%s", path ) );
        }
        else
        {
            bool is_csra = VDatabaseIsCSRA ( db );
            VDatabaseRelease ( db );
            if ( !is_csra )
            {
                rc = RC ( rcApp, rcNoTarg, rcOpening, rcItem, rcUnsupported );
                PLOGERR( klogErr, ( klogErr, rc, "failed to open '$(path)', it is not a csra-database", "path=%s", path ) );
            }
            else
            {
                prepare_ctx prep;   /* from cmdline_cmn.h */

                prep.omit_qualities = ctx->options->omit_qualities;
                prep.read_tlen = ctx->options->read_tlen;
                prep.use_primary_alignments = ( ( ctx->options->cmn.tab_select & primary_ats ) == primary_ats );
                prep.use_secondary_alignments = ( ( ctx->options->cmn.tab_select & secondary_ats ) == secondary_ats );
                prep.use_evidence_alignments = ( ( ctx->options->cmn.tab_select & evidence_ats ) == evidence_ats );
                prep.ref_iter = ctx->ref_iter;
                prep.spot_group = spot_group;
                prep.on_section = prepare_section_cb;
                prep.data = ctx->cursor_ids;
                prep.path = path;

                
                rc = prepare_ref_iter( &prep, ctx->vdb_mgr, ctx->vdb_schema, path, ctx->ranges ); /* cmdline_cmn.c */
                if ( rc == 0 && prep.db == NULL )
                {
                    rc = RC ( rcApp, rcNoTarg, rcOpening, rcSelf, rcInvalid );
                    LOGERR( klogInt, rc, "unsupported source" );
                }
            }
        }
    }
    return rc;
}


/* free all cursor-ids-blocks created in parallel with the alignment-cursor */
void CC cur_id_vector_entry_whack( void *item, void *data )
{
    pileup_col_ids * ids = item;
    free( ids );
}


static rc_t pileup_main( Args * args, pileup_options *options )
{
    foreach_arg_ctx arg_ctx;
    pileup_callback_data cb_data;
    KDirectory *dir;
    Vector cur_ids_vector;

    /* (1) make the align-manager ( necessary to make a ReferenceIterator... ) */
    rc_t rc = AlignMgrMakeRead ( &cb_data.almgr );
    if ( rc != 0 )
    {
        LOGERR( klogInt, rc, "AlignMgrMake() failed" );
    }

    VectorInit ( &cur_ids_vector, 0, 20 );
    cb_data.options = options;
    arg_ctx.options = options;
    arg_ctx.vdb_schema = NULL;
    arg_ctx.cursor_ids = &cur_ids_vector;

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
        else
        {
            ReportSetVDBManager( arg_ctx.vdb_mgr );
        }
    }


    if ( rc == 0 && options->cmn.no_mt )
    {
        rc = VDBManagerDisablePagemapThread ( arg_ctx.vdb_mgr );
        if ( rc != 0 )
        {
            LOGERR( klogInt, rc, "VDBManagerDisablePagemapThread() failed" );
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

    if ( rc == 0 )
    {
        switch( options->function )
        {
            case sra_pileup_counters    : options->omit_qualities = true;
                                          options->read_tlen = false;
                                          break;

            case sra_pileup_stat        : options->omit_qualities = true;
                                          options->read_tlen = true;
                                          break;

            case sra_pileup_debug       : options->omit_qualities = true;
                                          options->read_tlen = true;
                                          break;

            case sra_pileup_samtools    : options->read_tlen = false;
                                          break;
                                          
            case sra_pileup_mismatch    :  options->omit_qualities = true;
                                          options->read_tlen = false;
                                          break;
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
        switch( options->function )
        {
            case sra_pileup_stat        : rc = walk_stat( arg_ctx.ref_iter, options ); break;
            case sra_pileup_counters    : rc = walk_counters( arg_ctx.ref_iter, options ); break;
            case sra_pileup_debug       : rc = walk_debug( arg_ctx.ref_iter, options ); break;
            case sra_pileup_mismatch    : rc = walk_mismatches( arg_ctx.ref_iter, options ); break;
            default :  rc = walk_ref_iter( arg_ctx.ref_iter, options ); break;
        }
        /* ============================================== */
    }

    if ( arg_ctx.vdb_mgr != NULL ) VDBManagerRelease( arg_ctx.vdb_mgr );
    if ( arg_ctx.vdb_schema != NULL ) VSchemaRelease( arg_ctx.vdb_schema );
    if ( dir != NULL ) KDirectoryRelease( dir );
    if ( arg_ctx.ref_iter != NULL ) ReferenceIteratorRelease( arg_ctx.ref_iter );
    if ( cb_data.almgr != NULL ) AlignMgrRelease ( cb_data.almgr );
    VectorWhack ( &cur_ids_vector, cur_id_vector_entry_whack, NULL );

    return rc;
}


/* =========================================================================================== */

static rc_t CC pileup_test_enter_ref( ref_walker_data * rwd )
{
    return KOutMsg( "\nentering >%s<\n", rwd->ref_name );
}

static rc_t CC pileup_test_exit_ref( ref_walker_data * rwd )
{
    return KOutMsg( "exit >%s<\n", rwd->ref_name );
}

static rc_t CC pileup_test_enter_ref_window( ref_walker_data * rwd )
{
    return KOutMsg( "   enter window >%s< [ %,lu ... %,lu ]\n", rwd->ref_name, rwd->ref_start, rwd->ref_end );
}

static rc_t CC pileup_test_exit_ref_window( ref_walker_data * rwd )
{
    return KOutMsg( "   exit window >%s< [ %,lu ... %,lu ]\n", rwd->ref_name, rwd->ref_start, rwd->ref_end );
}

static rc_t CC pileup_test_enter_ref_pos( ref_walker_data * rwd )
{
    return KOutMsg( "   enter pos [ %,lu ], d=%u\n", rwd->pos, rwd->depth );
}

static rc_t CC pileup_test_exit_ref_pos( ref_walker_data * rwd )
{
    return KOutMsg( "   exit pos [ %,lu ], d=%u\n", rwd->pos, rwd->depth );
}

static rc_t CC pileup_test_enter_spot_group( ref_walker_data * rwd )
{
    return KOutMsg( "       enter spot-group [ %,lu ], %.*s\n", rwd->pos, rwd->spot_group_len, rwd->spot_group );
}

static rc_t CC pileup_test_exit_spot_group( ref_walker_data * rwd )
{
    return KOutMsg( "       exit spot-group [ %,lu ], %.*s\n", rwd->pos, rwd->spot_group_len, rwd->spot_group );
}

static rc_t CC pileup_test_alignment( ref_walker_data * rwd )
{
    return KOutMsg( "          alignment\n" );
}


static rc_t pileup_test( Args * args, pileup_options *options )
{
    struct ref_walker * walker;

    /* create walker */
    rc_t rc = ref_walker_create( &walker );
    if ( rc == 0 )
    {
        uint32_t idx, count;

        /* add sources to walker */
        rc = ArgsParamCount( args, &count );
        for ( idx = 0; idx < count && rc == 0; ++idx )
        {
            const char * src = NULL;
            rc = ArgsParamValue( args, idx, &src );
            if ( rc == 0 && src != NULL )
                rc = ref_walker_add_source( walker, src );
        }

        /* add ranges to walker */
        if ( rc == 0 )
        {
            rc = ArgsOptionCount( args, OPTION_REF, &count );
            for ( idx = 0; idx < count && rc == 0; ++idx )
            {
                const char * s = NULL;
                rc = ArgsOptionValue( args, OPTION_REF, idx, &s );
                if ( rc == 0 && s != NULL )
                    rc = ref_walker_parse_and_add_range( walker, s );
            }
        }

        /* set callbacks for walker */
        if ( rc == 0 )
        {
            ref_walker_callbacks callbacks = 
                {   pileup_test_enter_ref,
                    pileup_test_exit_ref,
                    pileup_test_enter_ref_window,
                    pileup_test_exit_ref_window,
                    pileup_test_enter_ref_pos,
                    pileup_test_exit_ref_pos,
                    pileup_test_enter_spot_group,
                    pileup_test_exit_spot_group,
                    pileup_test_alignment };
            rc = ref_walker_set_callbacks( walker, &callbacks );
        }

        /* let the walker call the callbacks while iterating over the sources/ranges */
        if ( rc == 0 )
            rc = ref_walker_walk( walker, NULL );

        /* destroy the walker */
        ref_walker_destroy( walker );
    }
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
                        if ( options.function == sra_pileup_report_ref ||
                             options.function == sra_pileup_report_ref_ext )
                        {
                            rc = report_on_reference( args, options.function == sra_pileup_report_ref_ext ); /* reref.c */
                        }
                        else if ( options.function == sra_pileup_test )
                        {
                            rc = pileup_test( args, &options ); /* see above */
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
