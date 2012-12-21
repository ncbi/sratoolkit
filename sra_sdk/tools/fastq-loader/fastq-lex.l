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
    
    #include <klib/rc.h>

	#include "fastq-parse.h"
	#include "fastq-tokens.h"    
	
    #define YYSTYPE FASTQToken

    /* code to populate token with internal values */
    #define YY_USER_ACTION              \
        yylval -> tokenText		= yytext;	\
        yylval -> tokenLength	= yyleng;	\
        yylval -> line_no		= yylineno;	\
        yylval -> column_no		= yyextra -> column; \
        yyextra -> column += yyleng; \
        yyextra -> length += yyleng; \
        yyextra -> lastToken = yylval; 

    #define ENDLINE \
        yyextra -> column=1;\
        return fqENDLINE;

    #define YY_INPUT(buf, result, max_size) \
        result = yyextra->input(yyextra, buf, max_size)

    #define ECHO

    static bool IsDecList(const char*, size_t*, int*);

    #define CHECK_FOR_DECLIST                               \
        int token;                                          \
        size_t firstNumLen = yylval -> tokenLength;         \
        if (IsDecList(yytext, & firstNumLen, & token))      \
        {                                                   \
            yyextra -> column -= (yyleng - firstNumLen);    \
            yyextra -> length -= (yyleng - firstNumLen);    \
            yylval -> tokenLength   = firstNumLen;          \
            BEGIN DECLIST;                                  \
            yyless(firstNumLen);                            \
            return token;                                   \
        }
%}

%option never-interactive nounistd yylineno reentrant bison-bridge stack noyywrap

%option prefix="FASTQ_"
%option extra-type="FASTQParseBlock*"

 /*%option debug*/

%x TAG_LINE
%x SEE_COORDS
%x PAST_COORDS
%x READ_NUMBER
%x NAMEVALUE
%x IN_SEQUENCE
%x QTAG_LINE
%x IN_QUALITY
%x INLINE_SEQUENCE
%x INLINE_QUALITY
%x DECLIST

ws          [ \t]+
digits      [0-9]+
alphanum    [A-Za-z0-9]+
eol         (\r\n|\r|\n)

/* there are more: =ACMGRSVTWYHKDBN. plus lowercase */
base [ACGTacgtNn.]   
cskey [ACGTacgt]
color   [0-3.]

asc33qual   [\x0E-\x1F \x21-\x7E]+
asc64qual   [\x40-\x7E]+

%%

^@                              { BEGIN TAG_LINE; return yytext[0]; }
^>                              { BEGIN TAG_LINE; return yytext[0]; }

^\+                             { BEGIN QTAG_LINE; return yytext[0]; }

<INITIAL,TAG_LINE>
{
    {digits}                    { return fqNUMBER; }
    {alphanum}                  { return fqALPHANUM; }
    {ws}                        { return fqWS; }
    .                           { return yytext[0]; }
}

<TAG_LINE>[ \t]*{eol}           { BEGIN IN_SEQUENCE; ENDLINE; }

<IN_SEQUENCE>
{
    ^{base}+/{eol}              { BEGIN 0; return fqBASESEQ; }
    ^{cskey}{color}+/{eol}      { BEGIN 0; return fqCOLORSEQ; }
    ^{asc64qual}/{eol}          { CHECK_FOR_DECLIST; BEGIN 0; return fqASC64QUAL; }
    ^{asc33qual}/{eol}          { CHECK_FOR_DECLIST; BEGIN 0; return fqASC33QUAL; }
    ^.*$                        { BEGIN 0; return fqUNRECOGNIZED; }
}

<INLINE_SEQUENCE>
{
    {base}+                     { BEGIN 0; return fqBASESEQ; }
    {cskey}{color}+             { BEGIN 0; return fqCOLORSEQ; }
}

<QTAG_LINE>
{
    .+$                         { return fqTOKEN; /* do not bother to parse the inside of the line */ }
    {eol}                       { BEGIN IN_QUALITY; ENDLINE }
}

<IN_QUALITY,INLINE_QUALITY,DECLIST>
{
    {ws}                        { } 
    {digits}                    { BEGIN DECLIST; return fqNUMBER; }
}

<IN_QUALITY>
{
    ^{asc64qual}/{eol}          { CHECK_FOR_DECLIST; return fqASC64QUAL; }
    ^{asc33qual}/{eol}          { CHECK_FOR_DECLIST; return fqASC33QUAL; }
    {eol}                       { BEGIN 0; ENDLINE; }
    .                           { return yytext[0]; }
}

<INLINE_QUALITY>
{
    {digits}/{eol}              { BEGIN DECLIST; return fqNUMBER; }
    {asc64qual}/{eol}           { CHECK_FOR_DECLIST; return fqASC64QUAL; }
    {asc33qual}/{eol}           { CHECK_FOR_DECLIST; return fqASC33QUAL; }
    {eol}                       { BEGIN 0; ENDLINE; }
    .                           { return yytext[0]; }
}

<DECLIST>
{
    {digits}/{eol}              { return fqNUMBER; }
    {eol}                       { BEGIN 0; ENDLINE; }
    .                           { return yytext[0]; }
}

{eol}                           { ENDLINE; }

%%    

rc_t CC FASTQScan_yylex_init(FASTQParseBlock* sb, bool debug)
{
	if (yylex_init_extra(sb, &sb->scanner) != 0)
	{
		return RC ( rcKFG, rcMgr, rcParsing, rcMemory, rcExhausted );
	}		

	yyset_debug(debug ? 1 : 0, sb->scanner);

    yy_push_state(INITIAL, sb->scanner);

    sb->column = 1;

	return 0;
}

void CC FASTQScan_yylex_destroy(FASTQParseBlock* sb)
{
	if (sb->scanner)
	{
		yylex_destroy(sb->scanner);
	}		
	sb->scanner=0;
}

void CC FASTQScan_inline_sequence(FASTQParseBlock* pb)
{
    yy_pop_state(pb->scanner);
    yy_push_state(INLINE_SEQUENCE, pb->scanner);
}

void CC FASTQScan_inline_quality(FASTQParseBlock* pb)
{
    yy_pop_state(pb->scanner);
    yy_push_state(INLINE_QUALITY, pb->scanner);
}

/* 
 * True if the token appears to be a comma/whitespace separated list of signed decimals, each no longer than 3 decimal positions.
 */
bool IsDecList(const char* token, size_t* size, int* p_newToken)
{
    size_t firstTokenSize = 0;
    size_t curNumberSize = 0;
    int newToken;
    bool inFirstToken = true;
    size_t i;
    for (i=0; i < *size; ++i)
    {
        char ch=token[i];
        if (strchr("0123456789, -+", ch) == NULL)
        {
            return false;
        }
        if (inFirstToken)
        {
            if (i == 0)
            {   /* decide on the first character */ 
                if (isdigit(ch))
                {
                    newToken = fqNUMBER;
                    firstTokenSize = 1;
                    curNumberSize = 1;
                }
                else if (ch == '-' || ch == '+')
                {
                    newToken = ch;
                    firstTokenSize = 1;
                    inFirstToken = false;
                }
                else
                {
                    return false;
                }
            }
            else if (isdigit(ch))
            {   /* keep on tracking the first token's size */
                ++firstTokenSize;
                ++curNumberSize;
                if (curNumberSize > 3)
                {
                    return false;
                }
            }
            else
            {   /* end of the first token */ 
                curNumberSize = 0;
                inFirstToken = false;
            }
        }
        else if (isdigit(ch))
        {   /* keep on tracking the current number's size */
            ++curNumberSize;
            if (curNumberSize > 3)
            {
                return false;
            }
        }
        else
        {
            curNumberSize = 0;
        }
    }
    *size = firstTokenSize;
    *p_newToken = newToken;
    return true;
}

void CC FASTQ_unlex(FASTQParseBlock* pb, FASTQToken* token)
{
    size_t i;
    struct yyguts_t* yyg = (struct yyguts_t*)pb->scanner;
    for (i = 0; i < token->tokenLength; ++i)
    {
        yyunput(token->tokenText[token->tokenLength-1-i], yyg->yytext_r, pb->scanner);
    }
    yyextra -> column -= token->tokenLength; 
    yyextra -> length -= token->tokenLength; 
}
