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

#ifndef _h_fastq_scan_
#define _h_fasta_scan_

#include <align/extern.h>
#include <klib/text.h>

#include <loader/common-reader-priv.h>

#ifdef __cplusplus
extern "C" {
#endif

/* values used in validating quality lines */
#define MIN_PHRED_33 33
#define MAX_PHRED_33 74
#define MIN_PHRED_64 64
#define MAX_PHRED_64 127

struct FastqSequence
{
    Sequence_vt sequence_vt;
    KRefcount   refcount;

    /* tagline components: */
    String spotname; /* tag line up to and including coordinates */
    String spotgroup; /* token following '#' */
    uint8_t readnumber; /* token following '/' 1 - IsFirst, 2 - IsSecond, 0 - dont know */

    /* not populated at this time: */
#if 0
    String rungroup; 
    String fmt_name; /* x and y replaced with $X and $Y */
    uint8_t coord_num;
    int32_t coords[16];
#endif

    char* read;
    bool is_colorspace;
    
    String  quality;
    uint8_t qualityOffset;
    
    bool lowQuality;
};

struct FastqRecord
{
    Record  dad;

    KDataBuffer source;
    struct FastqSequence    seq;
    Rejected*               rej; 
};

typedef struct FASTQToken
{ 
    const char* tokenText;
    size_t tokenLength;
    size_t line_no;
    size_t column_no;
} FASTQToken;

typedef struct FASTQParseBlock
{
    void* self;
    size_t (CC *input)(struct FASTQParseBlock* sb, char* buf, size_t max_size);
    uint8_t phredOffset;
    
    void* scanner;
    size_t length; /* input characters consumed for the current record */
    FASTQToken* lastToken;
    struct FastqRecord* record;
    size_t column;

    /* temporaries for bison: */
    KDataBuffer tagLine;

    size_t spotNameLength;
    bool spotNameDone;
    size_t spotGroupOffset;
    size_t spotGroupLength;

    KDataBuffer quality;
    size_t expectedQualityLines;
    
    uint8_t defaultReadNumber;
    
    bool fatalError;
} FASTQParseBlock;

extern rc_t CC FASTQScan_yylex_init(FASTQParseBlock* context, bool debug);
extern void CC FASTQScan_yylex_destroy(FASTQParseBlock* context);

/* explicit FLEX state control for bison*/
extern void CC FASTQScan_inline_sequence(FASTQParseBlock* pb);
extern void CC FASTQScan_inline_quality(FASTQParseBlock* pb);

extern void CC FASTQ_set_lineno (int line_number, void* scanner);

extern int CC FASTQ_lex(FASTQToken* pb, void * scanner);
extern void CC FASTQ_unlex(FASTQParseBlock* pb, FASTQToken* token);
extern void CC FASTQ_qualityContext(FASTQParseBlock* pb);

extern int FASTQ_debug; /* set to 1 to print Bison trace */ 

extern int CC FASTQ_parse(FASTQParseBlock* pb); /* 0 = end of input, 1 = success, a new record is in context->record, 2 - syntax error */

extern void CC FASTQ_error(FASTQParseBlock* pb, const char* msg);

#ifdef __cplusplus
}
#endif

#endif /* _h_fastq_scan_ */
