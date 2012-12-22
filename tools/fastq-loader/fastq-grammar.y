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
 
%{  
    #include <ctype.h>
    #include <stdlib.h>

	#include "fastq-parse.h"

   	#define YYSTYPE FASTQToken
    #define YYLEX_PARAM pb->scanner
    #define YYDEBUG 1

	#include "fastq-tokens.h"

    static uint64_t string_touint(const char* str, size_t length);
    static void AddBinaryQuality(FASTQParseBlock* pb, int8_t value);
    static void SetReadNumber(FASTQParseBlock* pb, const FASTQToken* token);
    static void SetTagLine(FASTQParseBlock* pb, const char* start, size_t length, const FASTQToken* coords);
    static void SetSpotGroup(FASTQParseBlock* pb, const FASTQToken* token);

%}

%pure-parser
%parse-param {FASTQParseBlock* pb }
%lex-param {FASTQParseBlock* pb }

%name-prefix="FASTQ_"

%token fqNUMBER
%token fqALPHANUM
%token fqWS
%token fqENDLINE
%token fqBASESEQ
%token fqCOLORSEQ
%token fqTOKEN
%token fqASC33QUAL
%token fqASC64QUAL
%token fqUNRECOGNIZED
%token fqENDOFTEXT 0

%%

sequence
    : readLines qualityLines    { return 1; }
    | readLines                 { if (yychar != YYEMPTY && yychar != YYEOF) FASTQ_unlex(pb, & yylval); return 1; }
    | readQualityLines          { return 1; }
    | qualityLines              { return 1; } 
    | name ':' coords ':'           { FASTQScan_inline_sequence(pb); } 
                 read ':'           { FASTQScan_inline_quality(pb); } 
                 quality endline    { return 1; }
    | name error endline        { return 1; }
    | endfile                   { return 0; }
    ;

endfile
    : fqENDOFTEXT
    | endline fqENDOFTEXT
    | endline endfile
    ;

endline
    : fqENDLINE
    ;

readLines
    : header  endline  read endline  
    | qheader endline  read endline  
    | header  endline error endline
    | error   endline  read endline
    ;

readQualityLines
    : qheader endline quality endline
    | qheader endline   error endline
    ;

read
    : fqBASESEQ                  { pb->record->seq.read = string_dup($1.tokenText, $1.tokenLength); pb->record->seq.is_colorspace = false; }
    | fqCOLORSEQ                 { pb->record->seq.read = string_dup($1.tokenText, $1.tokenLength); pb->record->seq.is_colorspace = true; }
    ;

header 
    : headerStart tagLine;

headerStart
    : '@' { pb->tagStart = $1.tokenText + 1; }
    ;

qheader 
    : qheaderStart tagLine;

qheaderStart
    : '>' { pb->tagStart = $1.tokenText + 1; }
    ;

tagLine    
    : name ':' coords tail  { SetTagLine(pb, $1.tokenText, $1.tokenLength + $2.tokenLength + $3.tokenLength + $4.tokenLength, &$3); }  
    | name ':' coords       { SetTagLine(pb, $1.tokenText, $1.tokenLength + $2.tokenLength + $3.tokenLength, &$3); }  
    | name                  { SetTagLine(pb, $1.tokenText, $1.tokenLength, 0); /* coords may be embbedded in the name with '_' as the separator */ }
    | coords tail           { SetTagLine(pb, $1.tokenText, $1.tokenLength + $2.tokenLength, &$1); }  
    | coords                { SetTagLine(pb, $1.tokenText, $1.tokenLength, &$1); }  
    ;

name
    : fqALPHANUM        { $$ = $1; }
    | name '_'          { $$ = $1; $$.tokenLength += $2.tokenLength; }
    | name '-'          { $$ = $1; $$.tokenLength += $2.tokenLength; }
    | name fqALPHANUM   { $$ = $1; $$.tokenLength += $2.tokenLength; }
    | name fqNUMBER     { $$ = $1; $$.tokenLength += $2.tokenLength; }
    ;

coords
    : fqNUMBER ':' fqNUMBER ':' signedNumber ':' signedNumber 
        { $$ = $1; $$.tokenLength += ($2.tokenLength + $3.tokenLength + $4.tokenLength + $5.tokenLength + $6.tokenLength + $7.tokenLength); }
    ;

signedNumber
    : '+' fqNUMBER  { pb->signedNumber =    (int8_t)string_touint($2.tokenText, $2.tokenLength); }
    | '-' fqNUMBER  { pb->signedNumber =  - (int8_t)string_touint($2.tokenText, $2.tokenLength); }
    | fqNUMBER      { pb->signedNumber =    (int8_t)string_touint($1.tokenText, $1.tokenLength); }
    ;

tail
    : tailPiece
    | tail tailPiece
    ;

tailPiece
    : ':'                           { $$ = $1; }
    | ':' fqNUMBER                  { $$ = $1; $$.tokenLength += $2.tokenLength; }
    | '.' fqNUMBER                  { $$ = $1; $$.tokenLength += $2.tokenLength; }
    
    | '#' fqNUMBER                  { $$ = $1; $$.tokenLength += $2.tokenLength; SetSpotGroup(pb, & $2); }
    | '#' fqALPHANUM                { $$ = $1; $$.tokenLength += $2.tokenLength; SetSpotGroup(pb, & $2); }
    | '/' fqNUMBER                  { $$ = $1; $$.tokenLength += $2.tokenLength; SetReadNumber(pb, & $2); }

    | fqWS fqALPHANUM '=' value     { $$ = $1; $$.tokenLength += ($2.tokenLength + $3.tokenLength + $4.tokenLength); }
    ;

value
    : anyToken                      
    | value anyToken                { $$ = $1; $$.tokenLength += $2.tokenLength; }
    ;

anyToken
    : fqNUMBER
    | fqALPHANUM
    |'_'
    ;

qualityLines
    : qualityHeader endline quality endline
    | qualityHeader endline error endline
    ;

qualityHeader
    : '+'                 
    | qualityHeader fqTOKEN
    ;

quality
    : fqASC33QUAL                { 
                                    pb->record->seq.qualityBase     = 33; 
                                    pb->record->seq.qualityType     = QT_Phred; 
                                    pb->record->seq.quality         = (int8_t*)string_dup($1.tokenText, $1.tokenLength); 
                                    pb->record->seq.qualityLength   = $1.tokenLength;
                                 }
    | fqASC64QUAL                { 
                                    pb->record->seq.qualityBase     = 64; 
                                    pb->record->seq.qualityType     = QT_Phred; 
                                    pb->record->seq.quality         = (int8_t*)string_dup($1.tokenText, $1.tokenLength); 
                                    pb->record->seq.qualityLength   = $1.tokenLength;
                                 }
    | decList                    {  /*consume the quality buffer */
                                    pb->record->seq.qualityBase     = 0; 
                                    pb->record->seq.qualityType     = QT_LogOdds; 
                                    pb->record->seq.quality         = pb->quality.buffer; 
                                    pb->record->seq.qualityLength   = pb->quality.curIdx;
                                    pb->quality.buffer = 0; 
                                    pb->quality.curIdx = 0;
                                 }
    ;

decList
    : signedNumber              { AddBinaryQuality(pb, (int8_t)pb->signedNumber); }
    | decList signedNumber      { AddBinaryQuality(pb, (int8_t)pb->signedNumber); }
    | decList ',' signedNumber  { AddBinaryQuality(pb, (int8_t)pb->signedNumber); }
    ;

%%

static
uint64_t string_touint(const char* str, size_t length)
{
    size_t i;
    uint64_t ret = 0;
    for (i = 0; i < length; ++i)
    {
        if (isdigit(str[i]))
        {
            ret = ret*10 + (str[i] - '0');
        }
    }
    return ret;
}

void AddBinaryQuality(FASTQParseBlock* pb, int8_t value)
{
    #define ChunkSize 128

    if (pb->quality.buffer == 0) 
    {   /* allocate based on the size of the previous buffer, if any */
        if (pb->quality.bufLen == 0)
        {
            pb->quality.bufLen = ChunkSize;
        }
        pb->quality.buffer = (int8_t*)malloc(pb->quality.bufLen);
        pb->quality.curIdx = 0;
    }
    else if (pb->quality.curIdx == pb->quality.bufLen)
    {   /* grow the buffer */
        pb->quality.bufLen += ChunkSize;
        pb->quality.buffer = (int8_t*)realloc(pb->quality.buffer, pb->quality.bufLen);
    }
    pb->quality.buffer[pb->quality.curIdx] = value;
    ++ pb->quality.curIdx;

    #undef ChunkSize
}

void SetReadNumber(FASTQParseBlock* pb, const FASTQToken* token)
{
    if (token->tokenLength == 1)
    {
        switch (token->tokenText[0])
        {
        case '1': pb->record->seq.readnumber = 1; return;
        case '2': pb->record->seq.readnumber = 2; return;
        }
    }
    pb->record->seq.readnumber = 0;
}

void SetTagLine(FASTQParseBlock* pb, const char* start, size_t length, const FASTQToken* coords)
{
    pb->record->seq.tagline = string_dup(start, length);
    if (coords != 0)
    {   /* assumes that a buffer switch can only happen at a line boundary*/ 
        StringInit(& pb->record->seq.spotname, pb->record->seq.tagline, coords->tokenText - start + coords->tokenLength + 1, coords->tokenText - start + coords->tokenLength);
    }
    else
    {
        StringInit(& pb->record->seq.spotname, pb->record->seq.tagline, length + 1, length);
    }
    if (pb->spotGroupOffset != 0)
    {
        StringInit(& pb->record->seq.spotgroup, pb->record->seq.tagline + pb->spotGroupOffset, pb->spotGroupLength, pb->spotGroupLength);
        pb->spotGroupOffset = 0;
    }
    else
    {
        StringInit(& pb->record->seq.spotgroup, 0, 0, 0);
    }
}

void SetSpotGroup(FASTQParseBlock* pb, const FASTQToken* token)
{
    pb->spotGroupOffset = token->tokenText - pb->tagStart;    
    pb->spotGroupLength = token->tokenLength;
}
