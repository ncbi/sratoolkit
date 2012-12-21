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

#include <search/extern.h>
#include <sysalloc.h>
#include "search-priv.h"

#include <stdlib.h>
#include <string.h>

const unsigned char* IUPAC_decode[256] = { (const unsigned char*)0xFFFF };

static
void IUPAC_init(void)
{
    if( IUPAC_decode[0] != NULL ) {
        const char** t = (const char**)IUPAC_decode;

        memset(t, 0, sizeof(t));
        t['A'] = t['a'] = "Aa";
        t['C'] = t['c'] = "Cc";
        t['G'] = t['g'] = "Gg";
        t['T'] = t['t'] = "Tt";
        t['U'] = t['u'] = "Uu";
        t['M'] = t['m'] = "AaCc";
        t['R'] = t['r'] = "AaGg";
        t['S'] = t['s'] = "CcGg";
        t['V'] = t['v'] = "AaCcGg";
        t['W'] = t['w'] = "AaTtUu";
        t['Y'] = t['y'] = "CcTtUu";
        t['K'] = t['k'] = "GgTtUu";
        t['B'] = t['b'] = "CcGgTtUu";
        t['D'] = t['d'] = "AaGgTtUu";
        t['H'] = t['h'] = "AaCcTtUu";
        t['N'] = t['n'] = t['.'] = "AaCcGgTtUuNn.-";
    }
}

rc_t na4_set_bits(const AgrepFlags mode, uint64_t* arr, const unsigned char c, const uint64_t val)
{
    if( mode & AGREP_PATTERN_4NA ) {
        const unsigned char* tr;
        tr = IUPAC_decode[c];
        if( tr == NULL ) {
            if( (mode & AGREP_ANYTHING_ELSE_IS_N) && (c == '.' || c == '-') ) {
                tr = IUPAC_decode['N'];
            }
            if( tr == NULL ) {
                return RC(rcText, rcString, rcSearching, rcConstraint, rcOutofrange);
            }
        }
        while( *tr != '\0' ) {
            if( mode & AGREP_TEXT_EXPANDED_2NA ) {
                switch(*tr) {
                    case 'A':
                        arr[0] |= val;
                        break;
                    case 'C':
                        arr[1] |= val;
                        break;
                    case 'G':
                        arr[2] |= val;
                        break;
                    case 'T':
                    case 'U':
                        arr[3] |= val;
                        break;
                    case 'N':
                        arr[4] |= val;
                        break;
                }
            }
            arr[*tr++] |= val;
        }
    }
    return 0;
}

LIB_EXPORT void CC FgrepFree( FgrepParams *self )
{
    if (self->dumb != NULL)
        FgrepDumbSearchFree(self->dumb);
    if (self->boyer != NULL)
        FgrepBoyerSearchFree(self->boyer);
    if (self->aho != NULL)
        FgrepAhoFree(self->aho);
    free(self);
}


LIB_EXPORT rc_t CC FgrepMake( FgrepParams **self, FgrepFlags mode, const char *strings[], uint32_t numstrings )
{
    *self = malloc(sizeof(FgrepParams));
    memset(*self, 0, sizeof(FgrepParams));
    (*self)->mode = mode;
    if (mode & FGREP_ALG_DUMB) {
        FgrepDumbSearchMake(&(*self)->dumb, strings, numstrings);
    }
    if (mode & FGREP_ALG_BOYERMOORE) {
        FgrepBoyerSearchMake(&(*self)->boyer, strings, numstrings);
    }
    if (mode & FGREP_ALG_AHOCORASICK) {
        FgrepAhoMake(&(*self)->aho, strings, numstrings);
    }
    return 0;
}

LIB_EXPORT uint32_t CC FgrepFindFirst( const FgrepParams *self, const char *buf, size_t len, FgrepMatch *match )
{
    if (self->mode & FGREP_ALG_DUMB) {
        return FgrepDumbFindFirst(self->dumb, buf, len, match);
    }
    if (self->mode & FGREP_ALG_BOYERMOORE) {
        return FgrepBoyerFindFirst(self->boyer, buf, len, match);
    }
    if (self->mode & FGREP_ALG_AHOCORASICK) {
        return FgrepAhoFindFirst(self->aho, buf, len, match);
    }
    /* Should maybe return error code, not 1/0 */
    return 0;
}

LIB_EXPORT rc_t CC AgrepMake( AgrepParams **self, AgrepFlags mode, const char *pattern )
{
    rc_t rc = 0;

    if( self == NULL || pattern == NULL ) {
        rc = RC(rcText, rcString, rcSearching, rcParam, rcNull);
    } else if( (*self = malloc(sizeof(AgrepParams))) == NULL ) {
        rc = RC(rcText, rcString, rcSearching, rcMemory, rcExhausted);
    } else {
        memset((*self), 0, sizeof(**self));
        (*self)->mode = mode;
        if( mode & AGREP_PATTERN_4NA ) {
            size_t i, l = strlen(pattern);
            if( IUPAC_decode[0] != NULL ) {
                IUPAC_init();
            }
            if( l == 0 ) {
                rc = RC(rcText, rcString, rcSearching, rcParam, rcOutofrange);
            }
            for(i = 0; rc == 0 && i < l; i++) {
                if( IUPAC_decode[(signed char)(pattern[i])] == NULL ) {
                    rc = RC(rcText, rcString, rcSearching, rcParam, rcOutofrange);
                }
            }
        } else if( !(mode & AGREP_MODE_ASCII) ) {
            rc = RC(rcText, rcString, rcSearching, rcParam, rcUnsupported);
        }
        if( rc == 0 ) {
            if(mode & AGREP_ALG_WUMANBER) {
                if( (rc = AgrepWuMake(&(*self)->wu, mode, pattern)) == 0 ) {
                    rc = AgrepDPMake(&(*self)->dp, mode, pattern);
                }
            } else if(mode & AGREP_ALG_MYERS) {
                if( (rc = AgrepMyersMake(&(*self)->myers, mode, pattern)) == 0 ) {
                    rc = AgrepDPMake(&(*self)->dp, mode, pattern);
                }
            } else if(mode & AGREP_ALG_MYERS_UNLTD) {
                if( (rc = MyersUnlimitedMake(&(*self)->myersunltd, mode, pattern)) == 0 ) {
                    rc = AgrepDPMake(&(*self)->dp, mode, pattern);
                }
            } else if(mode & AGREP_ALG_DP) {
                rc = AgrepDPMake(&(*self)->dp, mode, pattern);
            } else {
                rc = RC(rcText, rcString, rcSearching, rcParam, rcInvalid);
            }
        }
    }
    if( rc != 0 ) {
        AgrepWhack(*self);
    }
    return rc;
}

LIB_EXPORT void CC AgrepWhack( AgrepParams *self )
{
    if( self != NULL ) {
        if( self->wu ) {
            AgrepWuFree(self->wu);
        }
        if( self->myers ) {
            AgrepMyersFree(self->myers);
        }
        if( self->myersunltd ) {
            MyersUnlimitedFree(self->myersunltd);
        }
        if( self->dp ) {
            AgrepDPFree(self->dp);
        }
        free(self);
    }
}

LIB_EXPORT void CC AgrepFindAll( const AgrepCallArgs *args )
{
    if( args != NULL ) {
        const AgrepParams *self = args->self;

        if(self->mode & AGREP_ALG_WUMANBER) {
            AgrepWuFindAll(args);
        } else if(self->mode & AGREP_ALG_MYERS) {
            MyersFindAll(args);
        } else if(self->mode & AGREP_ALG_MYERS_UNLTD) {
            MyersUnlimitedFindAll(args);
        } else if (self->mode & AGREP_ALG_DP) {
            AgrepDPFindAll(args);
        }
    }
}

LIB_EXPORT uint32_t CC AgrepFindFirst( const AgrepParams *self, int32_t threshold, const char *buf, size_t len, AgrepMatch *match )
{
    if( self != NULL && buf != NULL && match != NULL ) {
        if (self->mode & AGREP_ALG_WUMANBER) {
            return AgrepWuFindFirst(self->wu, threshold, buf, (int32_t)len, match);
        }
        if (self->mode & AGREP_ALG_MYERS) {
            return MyersFindFirst(self->myers, threshold, buf, len, match);
        }
        if (self->mode & AGREP_ALG_MYERS_UNLTD) {
            return MyersUnlimitedFindFirst(self->myersunltd, threshold, buf, len, match);
        }
        if (self->mode & AGREP_ALG_DP) {
            return AgrepDPFindFirst(self->dp, threshold, self->mode, buf, (int32_t)len, match);
        }
    }
    /* Not sure this is the right thing to return. */
    return 0;
}

static 
rc_t CC AgrepFindBestCallback(const void *cbinfo, const AgrepMatch *matchinfo, AgrepContinueFlag *flag)
{
    AgrepMatch *best = (AgrepMatch *)cbinfo;
    if (best->score == -1 || best->score > matchinfo->score) {
        *best = *matchinfo;
    }
    return 0;
}

LIB_EXPORT uint32_t CC AgrepFindBest( const AgrepParams *self, int32_t threshold, const char *buf, int32_t len, AgrepMatch *match )
{
    if( self != NULL && buf != NULL && match != NULL ) {
        AgrepCallArgs args;

        args.self = self;
        args.threshold = threshold;
        args.buf = buf;
        args.buflen = len;
        args.cb = AgrepFindBestCallback;
        args.cbinfo = match;

        match->score = -1;
        
        AgrepFindAll(&args);
        if (match->score != -1) {
            return 1;
        }
    }
    return 0;
}
