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

#include <vfs/extern.h>
#include <os-native.h>
#include <vfs/path.h>
#include <vfs/manager.h>
#include <vfs/resolver.h>

#include "path-priv.h"

#include <sra/srapath.h> /* this only looks like its a module problem */
#include <kfs/directory.h>
#include <klib/refcount.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/out.h>
#include <klib/log.h>
#include <klib/debug.h>
#include <klib/rc.h>

#include <stdlib.h>
#include <string.h>
#include <sysalloc.h>
#include <ctype.h>
#include <assert.h>

rc_t VPOptionMake (VPOption ** new_obj, VPOption_t name, const char * value, size_t size)
{
    VPOption * obj;

    assert (new_obj);

    obj = malloc (sizeof (*obj));
    if (obj == NULL)
        return RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);

    obj->name = name;
    StringInit (&obj->value, value, size, string_len (value, size));
    *new_obj = obj;
    return 0;
}

void CC VPOptionWhack (BSTNode * self, void * ignored)
{
    free (self);
}

int CC VPOptionCmp ( const void * item, const BSTNode * n )
{
    VPOption_t o = (VPOption_t)(size_t)item;
    const VPOption * b = (const VPOption *)n;

    return o - b->name;
}

int CC VPOptionSort ( const BSTNode * item, const BSTNode * n )
{
    const VPOption * a = (const VPOption *)item;
    const VPOption * b = (const VPOption *)n;

    return a->name - b->name;
}


#if 0
static
rc_t VPathMakeCanon (VPath * self)
{
    char * low;  /* 'root' location in path */
    char * dst;  /* target reference for removing . and .. nodes */
    char * last; /* '/' at end of previous node */
    char * lim;  /* '\0' at end of the path string coming in */


    end = self->path.addr + self->path.size;
    low = self->path.addr;
    if (low[1] == '/') /* path starts with // which we allow in windows */
        ++low;
    dst = last = low;

    for (;;)
    {
        char * src; /* '/' or '\0' at end of this path node */

        src = strchr (prev + 1, '/');
        if (src == NULL)
            src = lim;

        /* detect special sequences based on length of node */
        switch (src-last)
        {
        case 1: /* an empty node name - only allowed in the beginning */
            last = src;  /* skip over */
            if (src != lim)
                continue;
            break;
        case 2:
            if (last[1] == '.')
            {
                last = src;  /* skip over */
                if (src != lim)
                    continue;
            }
            break;

        case 3:
            if ((last[1] == '.') && (last[2] == '.'))
            {
                /* remove previous node name */
                dst[0] = '\0';
                dst = strrchr (low, '/');
                if ((dst == NULL) || (dst < low))
                    return RC (rcFS, rcPath, rcAccessing, rcPath, rcInvalid);

                last = src;
                if (src != lim)
                    continue;
            }
            break;
        }

        assert (src >= last);
        if (dst != last)
            memmove (dst, last, src-last);
    }
}
#endif
/* Destroy
 */
rc_t VPathDestroy (const VPath * cself)
{
    if (cself)
    {
        VPath * self = (VPath*)cself;
        PATH_DEBUG (("-----\n%s: %p %p\n\n", __func__, cself, cself->storage ));
        BSTreeWhack (&self->options, VPOptionWhack, NULL);
        free (self->storage);
        free (self);
    }
    return 0;
}


static const char class_name[] = "VPath";

/* AddRef
 *  creates a new reference
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC VPathAddRef ( const VPath *self )
{
    if ( self != NULL )
    {
        switch (KRefcountAdd (&self->refcount, class_name))
        {
        case krefLimit:
        case krefNegative:
            return RC (rcFS, rcPath, rcAttaching, rcRange, rcInvalid);
        }
    }
    return 0;
}


/* Release
 *  discard reference to file
 *  ignores NULL references
 */
LIB_EXPORT rc_t CC VPathRelease ( const VPath *self )
{
    if ( self != NULL )
    {
        switch (KRefcountDrop (&self->refcount, class_name))
        {
        case krefWhack:
            VPathDestroy (self);
            break;
        case krefLimit:
        case krefNegative:
            return RC (rcFS, rcPath, rcReleasing, rcRange, rcInvalid);
        }
    }
    return 0;
}


/* not fully reselient to bad input */
static __inline__
char decode_nibble (char c)
{
    if ((c >= '0') && (c <= '9'))
        return (c - '0');
    if ((c >= 'a') && (c <= 'z'))
        return (c - 'a');
    if ((c >= 'A') && (c <= 'Z'))
        return (c - 'A');
    return (0);
}


static __inline__
bool string_decode (char * p)
{
    char * q;
    size_t limit;
    size_t ix;

    q = p;
    limit = string_size (p);

    for (ix = 0; ix < limit; ++ix)
    {
        if (p[0] == '%')
        {
            if ((ix + 2 > limit) || ! isxdigit (p[1]) || ! isxdigit (p[2]))
                return false;
            *q = decode_nibble (*++p) << 4;
            *q += decode_nibble (*++p);
            
        }
        else if (q != p)
            *q = *p;
        ++p;
        ++q;
    }
    if (p != q)
        *q = '\0';
    return true;
}


#if 0
static __inline__
rc_t StringDecode (String * self)
{
    size_t limit;
    size_t ix;
    char * p;
    char * q;

    p = q = (char *)self->addr;
    limit = self->size;
    for (ix = 0; ix < limit; ++ix)
    {
        if (p[0] == '%')
        {
            if ((ix + 2 > limit) || ! isxdigit (p[1]) || ! isxdigit (p[2]))
                return RC (rcFS, rcPath, rcDecoding, rcPath, rcInvalid);
            *q = decode_nibble (p[1]) << 4;
            *q += decode_nibble (p[2]);
        }
        else if (q != p)
            *q = *p;
        ++p;
        ++q;
    }
    if (p != q)
    {
        *q = '\0';
        self->size = q - self->addr;
        self->len = string_len (self->addr, self->size);
    }
    return 0;
}
#endif

/*
 * Coded against the ABNF from RFC 3987 International Resource Identifiers and
 * RFC 3986 Universal Resource Identifiers
 */

static __inline__
bool is_sub_delim (int ch)
{
    switch (ch)
    {
    case '!': case '$': case '&': case '\'':
    case '(': case ')': case '*': case '+':
    case ',': case ';': case '=':
        return true;
    default:
        return false;
    }
}


static __inline__
bool is_gen_delim (int ch)
{
    switch (ch)
    {
    case ':': case '/': case '?': case '#': case '[': case ']': case '@':
        return true;
    default:
        return false;
    }
}


static __inline__
bool is_reserved (int ch)
{
    return is_gen_delim (ch) || is_sub_delim (ch);
}


static __inline__
bool is_unreserved (int ch)
{
    return (isalnum (ch) || /* ALPHA and DIGIT */
            (ch == '-') ||
            (ch == '.') ||
            (ch == '_') ||
            (ch == '~'));
}


static __inline__
bool is_pct_encoded (const char * str)
{
    return (
        (str[0] == '%') &&
        (isxdigit (str[1])) &&
        (isxdigit (str[2])));
}


static __inline__
const char * eat_dec_octet (const char * str)
{
    /* not a number */
    if (! isdigit (str[0]))
        return str;

    /* 0-9 single digit */
    if (! isdigit (str[1]))
        return str+1;

    /* no leading zeroes */
    if (str[0] == '0')
        return str;

    /* 10-99 */
    if (! isdigit (str[2]))
        return str+2;

    /* 1000 and up */
    if (isdigit (str[2]))
        return str;

    /* 100-199 */
    if (str[0] == '1')
        return str+3;

    /* 300-999 */
    if (str[0] != '2')
        return str;

    /* 200-249 */
    if (str[1] < '5')
        return str+2;

    /* 260-299 */
    if (str[1] > '5')
        return str;

    /* 256-259 */
    if (str[2] > '5')
        return str;

    /* 250-255 */
    return str+2;
}


static __inline__
const char * eat_IPv4address (const char * str)
{
    const char * t0;
    const char * t1;

    t0 = str;

    t1 = eat_dec_octet (t0);
    if (t1 == t0)
        return str;

    if (*t1 != '.')
        return str;

    t0 = t1 + 1;

    t1 = eat_dec_octet (t0);
    if (t1 == t0)
        return str;

    if (*t1 != '.')
        return str;

    t0 = t1 + 1;

    t1 = eat_dec_octet (t0);
    if (t1 == t0)
        return str;

    if (*t1 != '.')
        return str;

    t0 = t1 + 1;

    t1 = eat_dec_octet (t0);
    if (t1 == t0)
        return str;

    return t1;
}


static __inline__
const char * eat_IPv6address (const char * str)
{
    /* not going there yet */
    return str;
}




static __inline__
bool is_scheme_char (int ch)
{
    return (isalnum (ch) ||
            (ch == '+') ||
            (ch == '-') ||
            (ch == ','));
}


static __inline__
bool is_scheme (const char * str)
{
    if ( !isalpha (*str++))
        return false;
    while (*str)
        if ( ! is_scheme_char (*str++))
            return false;
    return true;
}


static __inline__
bool is_iprivate (int ch )
{
    return (((ch >= 0x00E000) && (ch <= 0x00F8FF)) ||
            ((ch >= 0x0F0000) && (ch <= 0x0FFFFD)) ||
            ((ch >= 0x100000) && (ch <= 0x10FFFD)));
}


static __inline__
bool is_ucschar (int ch)
{
    return (((ch >= 0xA0)   && (ch <= 0xD7FF)) ||
            ((ch >= 0xF900) && (ch <= 0xFDCF)) ||
            ((ch >= 0xFDF0) && (ch <= 0xFFEF)) ||
            ((ch >= 0x10000) && (ch <= 0x1FFFD)) ||
            ((ch >= 0x20000) && (ch <= 0x2FFFD)) ||
            ((ch >= 0x30000) && (ch <= 0x3FFFD)) ||
            ((ch >= 0x40000) && (ch <= 0x4FFFD)) ||
            ((ch >= 0x50000) && (ch <= 0x5FFFD)) ||
            ((ch >= 0x60000) && (ch <= 0x6FFFD)) ||
            ((ch >= 0x70000) && (ch <= 0x7FFFD)) ||
            ((ch >= 0x80000) && (ch <= 0x8FFFD)) ||
            ((ch >= 0x90000) && (ch <= 0x9FFFD)) ||
            ((ch >= 0xA0000) && (ch <= 0xAFFFD)) ||
            ((ch >= 0xB0000) && (ch <= 0xBFFFD)) ||
            ((ch >= 0xC0000) && (ch <= 0xCFFFD)) ||
            ((ch >= 0xD0000) && (ch <= 0xDFFFD)) ||
            ((ch >= 0xE0000) && (ch <= 0xEFFFD)));
}

static __inline__
bool is_iunreserved (int ch)
{
    return is_unreserved (ch) || is_ucschar (ch);
}


static __inline__
bool is_ipchar (int ch)
{
    return is_iunreserved (ch) || is_sub_delim (ch) || 
        (ch == ':') || (ch == '@');
}


static __inline__
bool is_query (const char * str)
{
    for ( ; *str; ++str)
    {
        if (! is_ipchar (*str) &&
            ! is_iprivate (*str) &&
            (*str != '/') &&
            (*str != '?'))
        {
            return false;
        }
    }
    return true;
}


static __inline__
bool is_fragment (const char * str)
{
    for ( ; *str; ++str)
    {
        if (! is_ipchar (*str) &&
            ! is_sub_delim (*str) &&
            (*str != '/') &&
            (*str != '?'))
        {
            return false;
        }
    }
    return true;
}


#if 0
static __inline__
bool is_isegment (const char * str, size_t sz)
{
    const char * end = str + sz;
    size_t ix;
    int cnt;

    for (ix = 0; ix < sz; ix += cnt)
    {
        uint32_t ch;
        cnt = utf8_utf32 (&ch, str + ix, end);
        if (cnt <= 0)
            return false;
        if ( ! is_ipchar (ch))
            return false;
    }
    return true;
}
#endif


static __inline__
const char * eat_iuserinfo (const char * str)
{
    for (;;++str)
    {
        if (is_iunreserved (*str))
            ;
        else if (is_sub_delim (*str))
            ;
        else if (*str != ':')
            break;
    }
    return str;
}


static __inline__
const char * eat_ireg_name (const char * str)
{
    for (;;++str)
    {
        if (is_iunreserved (*str))
            ;
        else if (is_sub_delim (*str))
            ;
        else
            break;
    }
    return str;
}


static __inline__
const char * eat_port (const char * str)
{
    while (isdigit (*str))
        ++str;
    return str;
}


static __inline__
const char * eat_ihost (const char * str)
{
    /* not doing ip addresses yet */

    return eat_ireg_name (str);
}


static __inline__
const char * eat_iuserinfo_at (const char * str)
{
    const char * temp = eat_iuserinfo (str);
    if (temp == NULL)
        return temp;
    if (*++temp != '@')
        return NULL;
    return temp;
}


static __inline__
const char * eat_iauthority (const char * str)
{
    const char * temp;

    temp = eat_iuserinfo_at (str);

    if (temp != NULL)
        str = temp;

    temp = eat_ihost (str);
    if (temp == NULL)
        return false;
    str = temp;

    if (*str == ':')
        return eat_port (str+1);

    return str;
}


static __inline__
const char * eat_file_iauthority (const char * str)
{
    const char * temp;

    temp = eat_ihost (str);
    if (temp == NULL)
        return false;
    return temp;
}


static __inline__
const char * eat_isegment (const char * str)
{
    while (*str)
    {
        if (is_ipchar (*str))
        {
            ++str;
            continue;
        }
        if (is_pct_encoded (str))
        {
            str += 3;
            continue;
        }
        else
            break;
    }
    return str;
}


static __inline__
const char * eat_isegment_nz (const char * str)
{
    const char * temp = eat_isegment (str);
    if (temp == str)
        return NULL;
    return temp;
}


#if 0
static __inline__
const char * eat_isegment_nz_nc (const char * str)
{
    const char * temp = str;

    while (is_iunreserved (*str) ||
           is_sub_delim (*str) ||
           (*str == '@'))
        ++str;
    if (temp == str)
        return false;
    return str;
}
#endif


#if 0
static __inline__
bool is_isegment_nz (const char * str, size_t sz)
{
    if (sz == 0)
        return false;
    return is_isegment (str, sz);
}
#endif


#if 0
static __inline__
bool is_isegment_nz_nc (const char * str, size_t sz)
{
    if (sz == 0)
        return false;
    if (string_chr (str, sz, ':') != NULL)
        return false;
    return is_isegment (str, sz);
}
#endif


#if 0
static __inline__
bool is_ireg_name (const char * str, size_t sz)
{
    const char * end = str + sz;
    size_t ix;
    int cnt;

    for (ix = 0; ix < sz; ix += cnt)
    {
        uint32_t ch;
        cnt = utf8_utf32 (&ch, str + ix, end);
        if (cnt <= 0)
            return false;
        if ( ! is_iunreserved (ch) &&
             ! is_sub_delim (ch))
            return false;
    }
    return true;
}
#endif


static __inline__
bool is_ipath_rootless (const char * str);

static __inline__
bool is_ipath_absolute (const char * str)
{
    if (*str++ != '/')
        return false;
    return is_ipath_rootless (str);
}


static __inline__
bool is_ipath_empty (const char * str)
{
    return (*str == '\0');
}


static __inline__
bool is_ipath_abempty (const char * str)
{
    for (;;)
    {
        if (is_ipath_empty (str))
            return true;

        else if (*str++ != '/')
            return false;

        else
        {
            const char * temp = eat_isegment (str);
            if (temp == NULL)
                return false;
            str = temp;
        }
    }
}


static __inline__
bool is_ipath_rootless (const char * str)
{
    str = eat_isegment_nz (str);
    if (str == NULL)
        return false;

    return is_ipath_abempty (str);
}


#if 0
static __inline__
bool is_ipath_noscheme (const char * str)
{
    str = eat_isegment_nz_nc (str);

    if (str == NULL)
        return false;

    return is_ipath_abempty (str);
}
#endif


static __inline__
bool is_hier (const char * str)
{
    if ((str[0] == '/') && (str[1] == '/'))
    {
        const char * temp = eat_iauthority (str);
        if (temp != NULL)
            if (is_ipath_abempty (temp))
                return true;
    }
    if (is_ipath_absolute (str))
        return true;

    if (is_ipath_rootless (str))
        return true;

    if (is_ipath_empty (str))
        return true;

    return false;
}


#if 0
/*
 * confusion and ambiguity in the world of RFCs...
 *  We're gonna go with the RFC-3987 definition of ihost as ireg-name
 */
static __inline__
const char * eat_hostname (const char * str)
{
    /* -----
     * hostname = ireg_name
     * ireg_name = *(iunreserved / pct_encoded / sub_delim
     *
     * legal terminators are NUL or '/'
     */
    while (is_iunreserved (*str) &&
           is_sub_delim (*str))
        ++str;
    switch (*str)
    {
    case '\0':
    case '/':
        return str;
    default:
        return NULL;
    }
}
#endif
#if 0
/* -----
 * number.number.number.numer 
 * does not range check number
 * sigh...
 */
static __inline__
const char * eat_ipv6 (const char * str)
{
#if 1
    return NULL; /* not gonna do them yet */
#else
    const char * temp;
    int digits;
    int colons;
    int doublecolons = 0;

    /* non empty first part */
    for (digits = 0; isxdigit (*str); ++str)
        ;
    if ((digits == 0) || (digits > 4))
        return NULL;

    if (*str++ != ':')
        return NULL;

    for (digits = 0; isxdigit (*str); ++str)
        ;
    if (digits == 0)
        doublecolons = 1;
    else if (digits > 4)
        return NULL;

    for (digits = 0; isxdigit (*str); ++str)
        ;
    if (digits == 0)
    {
        if (doublecolons > 0)
            return NULL;
        doublecolons = 1;
    }
    else if (digits > 4)
        return NULL;

    for (digits = 0; isxdigit (*str); ++str)
        ;
    if (digits == 0)
    {
        if (doublecolons > 0)
            return NULL;
        doublecolons = 1;
    }
    else if (digits > 4)
        return NULL;

/* ... eeek! */
    return NULL;
#endif
}
#endif
#if 0
/* -----
 * number.number.number.numer 
 * does not range check number
 */
static __inline__
const char * eat_ipv4 (const char * str)
{
    do
        if (! isdigit(*str++))
            return NULL;
    while (*str != '.');
    ++str;
    do
        if (! isdigit(*str++))
            return NULL;
    while (*str != '.');
    ++str;
    do
        if (! isdigit(*str++))
            return NULL;
    while (*str != '.');
    ++str;
    do
        if (! isdigit(*str++))
            return NULL;
    while (isdigit (*str));
    return str;
}
#endif
#if 0
static __inline__
const char * eat_hostnumber (const char * str)
{
    const char * temp;

    temp = eat_ipv4 (str);
    if (temp == NULL)
        temp = eat_ipv6 (str);
    return temp;
}
#endif
#if 0
static __inline__
const char * eat_host (const char * str)
{
    const char * temp;

    temp = eat_hostnumber (str);
    if (temp == NULL)
        temp = eat_hostname (str);
    return temp;
}
#endif


#if 0
static __inline__
const char * eat_fsegment (const char * str)
{
    for (;;++str)
    {
        if ((*str == '/') ||
            (*str == '\0'))
        {
            break;
        }
        else if ( ! is_ipchar (*str))
            return NULL;
    }
    return str;
}
#endif


/* fpath = fsegment *[ "/" fsegment ] */
#if 0
static __inline__
bool is_fpath (const char * str)
{
    for (;;)
    {
        str = eat_fsegment (str);
        if (str == NULL)
            return false;
        if (*str == '\0')
            return true;
        if (*str != '/')
            return false;
        ++str;
    }
    return false; /* unreachable really */
}
#endif


static __inline__
bool is_file_hier (const char * str)
{
#if 0
    const char * temp;
    /* 
     * we'll expect the "file://host/fpath"
     * but also accept the inccorect "file:/fpath"
     */
    if (*str++ != '/')
        return false;

    if (*str == '/') /* must be correct version */
    {
        ++str;
        temp = eat_host (str);
        if (temp == NULL)
            return false;
        str = temp;
        if (*str++ != '/')
            return false;
    }
    return is_fpath (str);
#else
#if 0
    if ((str[0] == '/') && (str[1] == '/'))
    {
        const char * temp = eat_file_iauthority (str);
        if (temp != NULL)
            if (is_ipath_abempty (temp))
                return true;
    }
    if (is_ipath_absolute (str))
        return true;

    if (is_ipath_rootless (str))
        return true;

    if (is_ipath_empty (str))
        return true;

    return false;
#else
    /* by this point we aren't limited to ASCII  leave it for KFS*/
    return true;
#endif
#endif
}


static __inline__
bool is_file_query (const char * str)
{
    return (*str == '\0');

}


static __inline__
bool is_file_fragment (const char * str)
{
    return (*str == '\0');
}


static __inline__
bool is_kfs_hier (const char * str)
{
    return is_file_hier (str);
}


static __inline__
bool is_acc_hier (const char * str)
{
    /* not really doing this now */
    return true;
}


static __inline__
const char * eat_kfs_query (const char * str, VPOption ** opt)
{
    /*
     *    query_entry = "encrypt" / "enc" / ( "pwfile=" hier-part ) / ( "pwfd=" fd )
     */
    assert (str);
    assert (opt);

    switch (tolower (str[0]))
    {
    case 'e':
        if (strncasecmp ("nc", str+1, 2) == 0)
        {
            const char * temp = NULL;

            if ((str[3] == '\0') || (str[3] == '&'))
                temp =  str + 3;

            else if ((strncasecmp ("rypt", str+3, 4) == 0) && 
                     ((str[7] == '\0') || (str[7] == '&')))
                temp = str + 7;

            if (temp)
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_encrypted, temp, 0);
                if (rc)
                    return false;
                *opt = o;
                return temp;
            }
        }
        break;

    case 'p':
        if (strncasecmp ("wfile=", str + 1, sizeof ("wfile=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("wfile=") - 1;
            const char * temp2 = temp1;

            while ((*temp1 != '\0') && (*temp1 != '&'))
                ++temp1;
            if (temp1 != temp2)
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_pwpath, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
            break;
        }
        if (strncasecmp ("wfd=", str + 1, sizeof ("wfd=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("wfd=") - 1;
            const char * temp2 = temp1;
            while (isdigit(*temp1))
                ++temp1;
            if (temp1 == temp2)
                break;
            if ((*temp1 == '\0') || (*temp1 == '&'))
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_pwfd, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
        }
        break;
    case 'r':
        if (strncasecmp ("eadgroup=", str + 1, sizeof ("eadgroup=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("eadgroup=") - 1;
            const char * temp2 = temp1;
            while (isprint(*temp1))
                ++temp1;
            if (temp1 == temp2)
                break;
            if ((*temp1 == '\0') || (*temp1 == '&'))
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_readgroup, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
        }
        break;
    case 't':
        if (strncasecmp ("emporary_pw_hack=", str + 1, sizeof ("emporary_pw_hack=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("emporary_pw_hack=") - 1;
            const char * temp2 = temp1;
            while (isalnum(*temp1))
                ++temp1;
            if (temp1 == temp2)
                break;
            if ((*temp1 == '\0') || (*temp1 == '&'))
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_temporary_pw_hack, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
        }
        else if (strncasecmp ("ic=", str + 1, sizeof ("ic=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("ic=") - 1;
            const char * temp2 = temp1;
            while ( * temp1 != 0 && * temp1 != '&' )
                ++temp1;
            if (temp1 == temp2)
                break;
            if ((*temp1 == '\0') || (*temp1 == '&'))
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_gap_ticket, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
        }
        break;
    case 'v':
        if (strncasecmp ("db-ctx=", str + 1, sizeof ("db-ctx=") - 1) == 0)
        {
            const char * temp1 = str + 1 + sizeof ("db-ctx=") - 1;
            const char * temp2 = temp1;
            while (isalnum(*temp1))
                ++temp1;
            if (temp1 == temp2)
                break;
            if ((*temp1 == '\0') || (*temp1 == '&'))
            {
                VPOption * o;
                rc_t rc;

                rc = VPOptionMake (&o, vpopt_vdb_ctx, temp2, temp1-temp2);
                if (rc)
                    return false;
                *opt = o;
                return temp1;
            }
        }
        break;
    }
    PATH_DEBUG (("%s: failed '%s'\n",__func__,str));
    return NULL;
}


static __inline__
bool is_kfs_query (const char * str, BSTree * tree)
{
    /* 
     *    query       = query_entry [ * ( "&" query_entry ) ]
     *
     *    query_entry = "encrypt" / "enc" / ( "pwfile=" hier-part ) / ( "pwfd=" fd )
     */
    const char * temp;

    if (*str == '\0')
        return true;

    for (;;)
    {
        VPOption * o = NULL;

        /* this returns an allocation */
        temp = eat_kfs_query (str, &o);
        if ( o == NULL )
            return false;
        if ( temp == NULL )
        {
            VPOptionWhack ( & o -> node, NULL );
            return false;
        }

        switch ( o -> name )
        {
        case vpopt_pwpath:
        case vpopt_pwfd:
        case vpopt_temporary_pw_hack:
        case vpopt_vdb_ctx:
        case vpopt_gap_ticket:
            /* can only have one of these */
            if ( BSTreeInsertUnique ( tree, &o->node, NULL, VPOptionSort ) != 0)
            {
                VPOptionWhack ( & o -> node, NULL );
                return false;
            }
            break;
        case vpopt_encrypted:
        case vpopt_readgroup:
            /* the behavior here appears to be
               tolerate repeats, but only insert first */
            if ( BSTreeInsertUnique ( tree, &o->node, NULL, VPOptionSort ) != 0)
                VPOptionWhack ( & o -> node, NULL );
            break;
        default:
            VPOptionWhack ( & o -> node, NULL );
            break;
        }

        str = temp;
        if (*str == '\0')
            break;
        if (*str == '&')
            /**(char *)str = '\0'*/;
        else
            return false;
        ++str;
    }
    return true;
}

static __inline__
bool is_http_query (const char * str, BSTree * tree)
{
    return is_kfs_query (str, tree);
}


static __inline__
bool is_acc_query (const char * str, BSTree * tree)
{
    return is_kfs_query (str, tree);
}


static __inline__
bool is_kfs_fragment (const char * str)
{
    return true;
/*     return (*str == '\0'); */
}


static __inline__
bool is_acc_fragment (const char * str)
{
    return (*str == '\0');
}



/*
 * file://host/path bue we allow file:/path
 *
 * RFC-1738
 *
 * fileurl = "file://" [host / "localhost" ] "/" fpath
 */
static
rc_t VPathMakeUriFile (VPath * self, char * new_allocation, 
                       size_t sz, char * hier, char * query,
                       char * fragment)
{
    rc_t rc;
    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    if ((!is_file_hier (hier)) ||
        (!is_file_query (query)) ||
        (!is_file_fragment (fragment)))
    {
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    rc = VPathTransformPathHier (&hier);
    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    if (rc)
    {
        self->storage = NULL;
        return rc;
    }
    StringInitCString (&self->path, hier);
    self->query = query;
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}

        
static
rc_t VPathMakeUriKfs (VPath * self, char * new_allocation, 
                      size_t sz, char * hier,
                      char * query, char * fragment)
{
    rc_t rc;
    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    if (!is_kfs_hier (hier))
    {
        PATH_DEBUG (("%s: failed is_kfs_hier '%s'\n",__func__, hier));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    if (!is_kfs_query (query, &self->options))
    {
        PATH_DEBUG (("%s: failed is_kfs_query '%s'\n",__func__, query));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    if (!is_kfs_fragment (fragment))
    {
        PATH_DEBUG (("%s: failed is_kfs_fragment '%s'\n",__func__, fragment));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    rc = VPathTransformPathHier (&hier);
    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    if (rc)
    {
        self->storage = NULL;
        return rc;
    }
    StringInitCString (&self->path, hier);
    self->query = query;
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}



static
rc_t VPathMakeUriAcc (VPath * self, char * new_allocation, 
                      size_t sz, char * hier,
                      char * query, char * fragment)
{
    rc_t rc;

    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    if (!is_acc_hier (hier))
    {
        PATH_DEBUG (("%s: failed is_acc_hier '%s'\n", __func__, hier));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    if (!is_acc_query (query, &self->options))
    {
        PATH_DEBUG (("%s: failed is_kfs_query '%s'\n",__func__, query));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    if (!is_kfs_fragment (fragment))
    {
        PATH_DEBUG (("%s: failed is_kfs_fragment '%s'\n",__func__, fragment));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    rc = VPathTransformPathHier (&hier);
    PATH_DEBUG (("%s: hier '%s' query '%s' fragment '%s'\n", __func__,
                 hier, query, fragment));
    if (rc)
    {
        self->storage = NULL;
        return rc;
    }
    StringInitCString (&self->path, hier);
    self->query = query;
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}


static
rc_t VPathMakeUriHttp (VPath * self, char * new_allocation, 
                      size_t sz, char * hier,
                      char * query, char * fragment)
{
    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    if (!is_http_query (query, &self->options))
    {
        PATH_DEBUG (("%s: failed is_http_query '%s'\n",__func__, query));
        return RC (rcFS, rcPath, rcConstructing, rcUri, rcInvalid);
    }

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    StringInitCString (&self->path, hier);
    self -> query = query;
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}


static
rc_t VPathMakeUriFtp (VPath * self, char * new_allocation, 
                      size_t sz, char * hier,
                      char * query, char * fragment)
{
    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    StringInitCString (&self->path, hier);
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}

static
rc_t VPathMakeUriNcbiLegrefseq (VPath * self, char * new_allocation, 
                                size_t sz, char * hier,
                                char * query, char * fragment)
{
    assert (self);
    assert (new_allocation);
    assert (sz);
    assert (hier);
    assert (query);
    assert (fragment);

    free (self->storage);
    self->storage = new_allocation;
    self->alloc_size = sz;

    StringInitCString (&self->path, hier);
    self->fragment = fragment;
    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    return 0;
}




static
VPUri_t scheme_type (const char * scheme)
{
    /* We have a "legal" scheme name. We'll only look for specific schemes we
     * support and mark all others as merely unsupported rather than 
     * differentiate types we don't care about.
     */
    switch (tolower(scheme[0]))
    {
    case '\0':
        PATH_DEBUG (("%s: no scheme\n",__func__));
        return vpuri_none;

    case 'f':
        if (strcasecmp ("file", scheme) == 0)
        {
            PATH_DEBUG (("%s: file scheme\n",__func__));
            return vpuri_file;
        }
        if (strcasecmp (FTP_SCHEME, scheme) == 0)
        {
            PATH_DEBUG (("%s: " FTP_SCHEME " scheme\n",__func__));
            return vpuri_ftp;
        }
        break;

    case 'n':
        if (strcasecmp (NCBI_FILE_SCHEME, scheme) == 0)
        {
            PATH_DEBUG (("%s: " NCBI_FILE_SCHEME " scheme\n",__func__));
            return vpuri_ncbi_vfs;
        }
        else if (strcasecmp (NCBI_ACCESSION_SCHEME, scheme) == 0)
        {
            PATH_DEBUG (("%s: " NCBI_ACCESSION_SCHEME " scheme\n",__func__));
            return vpuri_ncbi_acc;
        }
        break;

    case 'h':
        if (strcasecmp (HTTP_SCHEME, scheme) == 0)
        {
            PATH_DEBUG (("%s: " HTTP_SCHEME " scheme\n",__func__));
            return vpuri_http;
        }
        break;

    case 'x':
        if (strcasecmp (NCBI_LEGREFSEQ_SCHEME, scheme) == 0)
        {
            PATH_DEBUG (("%s: " NCBI_LEGREFSEQ_SCHEME " scheme\n",__func__));
            return vpuri_ncbi_legrefseq;
        }
        break;
    }

    return vpuri_not_supported;
}


/*
 * See RFC 3986 / RFC 3987
 * We will allow utf-8 in our URI with the extended Unicode characters not 
 * required to be %-encoded.  We would have to do this encoding if we wish
 * to pass this uri out of our environment.
 *
 * we demand a valid set of characters for the scheme but do no validation 
 * of the other parts waiting for a scheme specific parsing
 */

/* pcopy and scheme point to the same place - seems redundant */
static
rc_t VPathSplitUri (VPath * self, char ** pcopy, size_t * psiz, char ** scheme, char ** hier,
                    char ** query, char ** fragment)
{
    char * copy;
    size_t z;

    assert (self && pcopy && psiz && scheme && hier && query && fragment);

    *pcopy = *scheme = *hier = *query = *fragment = NULL;
    *psiz = 0;

    z = self->asciz_size + 1;
    copy = malloc (z);
    if (copy == NULL)
        return RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);
    strcpy (copy, self->storage);
    for (;;)
    {
        char * s; /* start/scheme */
        char * h; /* hier-part */
        char * q; /* query */
        char * f; /* fragment */
        char * e; /* EOS (terminating NUL */

        s = copy;

        /* point at NUL at end */
        f = q = e = s + strlen (self->storage);

        /* find the scheme - terminated by first ':' from the beginning (left) */
        h = strchr (s, ':');

        /* scheme must be present as must the ':' and not % encoded
         * and must have some size
         * the character set for scheme is very limited - ASCII Alphanumeric
         * with "-", ",", "_", and "~"
         */
        if ((h == NULL) || (h == s))
        {
            h = s;
            s = e;
            break;
        }

        *h++ = '\0';

        if (! is_scheme (s))
        {
            h[-1] = ':';
            h = s;
            s = e;
            break;
        }
        if (h != e)
        {
            f = strchr (h, '#');

            if (f == NULL)
                f = e;
            else
                *f++ = '\0';

            q = strchr (h, '?');

            if (q == NULL)
                q = e;
            else
                *q++ = '\0';
        }
#if 0
        if (! is_hier (h))
            break;

        if (! is_query (q))
            break;

        if (! is_fragment (f))
            break;
#endif
        if (! string_decode (h))
            break;

        if (! string_decode (q))
            break;

        if (! string_decode (f))
            break;

        *scheme = s;
        *hier = h;
        *query = q;
        *fragment = f;
        *psiz = z;      /* WHAT???? */
        *pcopy = copy;
        return 0;
    }

    free (copy);
    return SILENT_RC (rcFS, rcPath, rcParsing, rcUri, rcInvalid);
}


static
rc_t VPathParseURI (VPath * self)
{
    char * parsed_uri;
    char * scheme;
    char * hier;
    char * query;
    char * fragment;
    size_t allocated;
    rc_t rc;

    PATH_DEBUG (("%s: starting path '%s'\n",__func__,self->path.addr));
    rc = VPathSplitUri (self, &parsed_uri, &allocated, &scheme, &hier, &query, &fragment);
    PATH_DEBUG (("%s: allocated %p '%zu'\n",__func__,parsed_uri,allocated));
    if (rc == 0)
    {
        switch (self->scheme = scheme_type (scheme))
        {
        case vpuri_invalid:
            rc = RC (rcFS, rcPath, rcParsing, rcUri, rcInvalid);
            break;

        case vpuri_not_supported:
            rc = RC (rcFS, rcPath, rcParsing, rcUri, rcIncorrect);
            break;

        case vpuri_file:
            PATH_DEBUG (("%s: call VPathMakeUriFile\n",__func__));
            rc = VPathMakeUriFile (self, parsed_uri, allocated, hier, query,
                                   fragment);
            break;

        case vpuri_ncbi_vfs:
            PATH_DEBUG (("%s: call VPathMakeUriKfs\n",__func__));
            rc = VPathMakeUriKfs (self, parsed_uri, allocated, hier, query,
                                  fragment);
            break;

        case vpuri_ncbi_acc:
            PATH_DEBUG (("%s: call VPathMakeUriAcc\n",__func__));
            rc = VPathMakeUriAcc (self, parsed_uri, allocated, hier, query,
                                  fragment);
            break;

        case vpuri_http:
            PATH_DEBUG (("%s: call VPathMakeUriHttp\n",__func__));
            rc = VPathMakeUriHttp (self, parsed_uri, allocated, hier, query,
                                   fragment);

            break;

        case vpuri_ftp:
            PATH_DEBUG (("%s: call VPathMakeUriFtp\n",__func__));
            rc = VPathMakeUriFtp (self, parsed_uri, allocated, hier, query,
                                  fragment);

            break;

        case vpuri_ncbi_legrefseq:
            PATH_DEBUG (("%s: call VPathMakeUriNcbiLegrefseq\n",__func__));
            rc = VPathMakeUriNcbiLegrefseq (self, parsed_uri, allocated, hier, query,
                                            fragment);

            break;


        default:
            rc = RC (rcFS, rcPath, rcParsing, rcUri, rcCorrupt);
            break;
        }
        if (rc)
            free (parsed_uri);
    }
    return rc;
}


static
rc_t VPathAlloc (VPath ** pself, const char * path_string)
{
    rc_t rc;
    size_t z, zz;
    VPath * self;


    zz = 1 + (z = string_size (path_string));

    OFF_PATH_DEBUG(("%s: %s 'z' '%zu' zz '%zu'\n",__func__,path_string,z,zz));

    self = calloc (sizeof (*self), 1);
    if (self == NULL)
        rc = RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);
    else
    {
        self->storage = malloc (zz);
        if (self->storage == NULL)
            rc = RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);
        else
        {

            PATH_DEBUG (("-----\n%s: %p %zu %p %zu\n\n", __func__, self, sizeof (*self),
                         self->storage, zz));
            self->alloc_size = zz;

            memcpy (self->storage, path_string, zz);

            KRefcountInit (&self->refcount, 1, class_name, "init", self->storage);

            self->asciz_size = z;

            StringInit (&self->path, self->storage, z, string_len(self->storage, z));

            BSTreeInit (&self->options);

            self->fragment = self->storage + z;

            *pself = self;

            OFF_PATH_DEBUG (("%s: path '%S'\n", __func__, &self->path));

            return 0;
        }

        free (self);
    }
    return rc;
}


static
rc_t VPathMakeValidateParams (VPath ** new_path, const char * path)
{
    if (new_path == NULL)
        return RC (rcFS, rcPath, rcConstructing, rcSelf, rcNull);
    *new_path = NULL;
    if (path == NULL)
        return RC (rcFS, rcPath, rcConstructing, rcParam, rcNull);
    return 0;
}


LIB_EXPORT rc_t CC VPathMake ( VPath ** new_path, const char * posix_path)
{
    VPath * self;
    rc_t rc;

    rc = VPathMakeValidateParams (new_path, posix_path);
    if (rc == 0)
    {
        rc = VPathAlloc (&self, posix_path);
        if (rc == 0)
        {
            rc = VPathParseURI (self);

            /* ignore return - if its bad just leave the path alone even if 
             * it turns out to be bad later */
            *new_path = self;
            return 0;
        }
    }
    return rc;
}


LIB_EXPORT rc_t CC VPathMakeSysPath ( VPath ** new_path, const char * sys_path)
{
    VPath * self;
    rc_t rc;

    rc = VPathMakeValidateParams (new_path, sys_path);
    if (rc)
    {
        LOGERR (klogErr, rc, "error with VPathMakeValidateParams");
        return rc;
    }

    rc = VPathAlloc (&self, sys_path);
    if (rc)
        return rc;

    /* first try as URI then as a system specific path */
    rc = VPathParseURI (self);
    if (rc)
    {
        PATH_DEBUG (("%s: failed VPathParse URI '%R'\n",__func__,rc));
        rc = VPathTransformSysPath(self);
        if (rc)
            PATH_DEBUG (("%s: failed VPathTransformSysPath URI '%R'\n",__func__,rc));
    }
    if (rc)
        VPathDestroy (self);
    else
        *new_path = self;
    return rc;
}


LIB_EXPORT rc_t CC VPathMakeVFmt ( VPath ** new_path, const char * fmt, va_list args )
{
    size_t len;
    rc_t rc;
    char buffer [32*1024]; /* okay we really don't want any larger than this I suppose */

    rc = VPathMakeValidateParams (new_path, fmt);
    if (rc)
        return rc;

    rc = string_vprintf (buffer, sizeof (buffer), &len, fmt, args);
    if (rc)
        return rc;

    if (len >= sizeof buffer)
        return RC (rcFS, rcPath, rcConstructing, rcBuffer, rcInsufficient);

    return VPathMake (new_path, buffer);
}


LIB_EXPORT rc_t CC VPathMakeFmt ( VPath ** new_path, const char * fmt, ... )
{
    rc_t rc;
    va_list args;

    va_start (args, fmt);
    rc = VPathMakeVFmt (new_path, fmt, args);
    va_end (args);

    return rc;
}



/* -----
 * for this to work
 * the base path must be a directory or have no hiearchical part at all
 *
 * is the srapathmgr is not NULL we first try to resolve the relative path as 
 * an srapath 'alias'
 */
LIB_EXPORT rc_t CC VPathMakeDirectoryRelative ( VPath ** new_path, const KDirectory * basedir,
                                                const char * relative_path, SRAPath * srapathmgr )
{
    VPath * vpath = NULL;
    VPath * rpath = NULL;
    VPath * fpath = NULL;
    char sbuff [8192];
    rc_t rc;

/*     KOutMsg ("%s: %s\n", __func__, relative_path); */

    if (new_path == NULL)
        return RC (rcVFS, rcPath, rcConstructing, rcSelf, rcNull);

    *new_path = NULL;

    if ((basedir == NULL) ||
        (relative_path == NULL))
        return RC (rcVFS, rcPath, rcConstructing, rcParam, rcNull);

#if USE_VRESOLVER
    {
        VFSManager *vmgr;
        bool it_worked = false;
        const VPath * resolved = NULL;


        /*
         * First we'll see if the relative path is actually an accession
         * in which case we ignore the base dir
         */
        rc = VFSManagerMake (&vmgr);
        if (rc)
            ;
        else
        {
            VPath * accession;

            rc = VPathMake (&accession, relative_path);
            if (rc)
                ;
            else
            {
                VResolver * resolver;

                rc = VFSManagerGetResolver (vmgr, &resolver);
                if (rc)
                    ;
                else
                {
                    rc = VResolverLocal (resolver, accession, (const VPath **)&resolved);
                    if (rc == 0)
                        it_worked = true;
                        
                    else if (GetRCState (rc) == rcNotFound)
                    {
                        rc = VResolverRemote (resolver, accession, &resolved, NULL);
                        if (rc == 0)
                            it_worked = true;
                    }

                    VResolverRelease (resolver);
                }

                VPathRelease (accession);
            }
            VFSManagerRelease (vmgr);
        }
        if (it_worked)
        {
            /* RETURN HERE */

            /* TBD - why is "resolved" const? */
            *new_path = ( VPath* ) resolved;
            return 0;
        }
    }
#else
    /* 
     * if we have an srapath manager try to quick out of treating the
     * relative path as an SRAPath alias.
     *
     * we have to handle this before we build a realtive VPath because we've
     * decided a nake 
     */
    if (srapathmgr)
    {
/*     KOutMsg ("%s: 1 %s\n", __func__, relative_path); */
        rc = SRAPathFind (srapathmgr, relative_path, sbuff, sizeof sbuff);
        if (rc == 0)
        {
/*     KOutMsg ("%s: 1.1 %s\n", __func__, sbuff); */
            rc = VPathMake (&rpath, sbuff);
            if (rc == 0)
            {
/*                 KOutMsg ("%s: 1.2 %s\n", __func__, sbuff); */
                *new_path = rpath;
                return 0;                
            }
        }
    }
#endif
    /*
     * create a VPath off the relative path.  This will create one of three things
     * as of when this was writen:
     *  1: kfile_acs uri
     *  2: full path
     *  3: relative path
     *
     * Handling (1) is easy and mirrors the quick out above.
     */
    rc = VPathMakeSysPath (&rpath, relative_path);
    if (rc == 0)
    {
/*     KOutMsg ("%s: 2 %s\n", __func__, relative_path); */
        switch (rpath->scheme)
        {
        case vpuri_ncbi_acc:
            if (srapathmgr == NULL)
            {
                VPathRelease (rpath);
                return RC (rcVFS, rcPath, rcConstructing, rcMgr, rcNotFound);
            }
            else
            {
                rc = SRAPathFind (srapathmgr, rpath->path.addr, sbuff, sizeof sbuff);
                if (rc)
                {
                    VPathRelease (rpath);
                    return rc;
                }
                else
                {
                    rc = VPathMake (&vpath, sbuff);
                resolve_options:
                    if (rc == 0)
                    {
                        char * use_opts;

/*                         KOutMsg ("%s: 2.5 %s %s\n", __func__, sbuff, vpath->path.addr); */

                        use_opts = vpath->query;
/*                         KOutMsg ("%s: 2.6 %s %s\n", __func__, sbuff, vpath->path.addr); */
                        if ((use_opts == NULL) || (use_opts[0] == '\0'))
                        {
/*                         KOutMsg ("%s: 2.7 %s %s\n", __func__, sbuff, vpath->path.addr); */
                            use_opts = rpath->query;
                        }
/*                         KOutMsg ("%s: 2.8 %s %s\n", __func__, sbuff, vpath->path.addr); */

                        memmove (sbuff, "ncbi-file:", sizeof "ncbi-file:");
                        memmove (sbuff + sizeof "ncbi-file", vpath->path.addr, vpath->path.size + 1);
                        if (use_opts)
                        {
                            sbuff[sizeof "ncbi-file" + vpath->path.size] = '?';
                            memmove (sbuff + sizeof "ncbi-file" + vpath->path.size + 1, use_opts, string_size (use_opts) + 1);
                        }
/*                         KOutMsg ("%s: 2.9 %s %s\n", __func__, sbuff, vpath->path.addr); */

                        rc = VPathMake (&fpath, sbuff);
                        if (rc == 0)
                        {
/*                         KOutMsg ("%s: 2.10 %s %s\n", __func__, sbuff, vpath->path.addr); */

                            VPathRelease (vpath);
                            VPathRelease (rpath);
                            *new_path = fpath;
                            return 0;
                        }
                        VPathRelease (vpath);
                    }
                }
            }
            VPathRelease (rpath);
            break;

        case vpuri_none:
        case vpuri_ncbi_vfs:
        case vpuri_file:
            /* full path or relative? */
            if (rpath->path.addr[0] == '/')
            {
                /* full path is done */
                *new_path = rpath;
                return 0;
            }
            /* our relative path really is relative and must be resolved */
            rc = KDirectoryResolvePath (basedir, true, sbuff, sizeof sbuff,
                                        rpath->path.addr);
            if (rc == 0)
            {
                rc = VPathMake (&vpath, sbuff);
                if (rc == 0)
                {
                    goto resolve_options;
                }
            }
            break;

        case vpuri_http:
        case vpuri_ftp:
            *new_path = rpath;
            return 0;

        case vpuri_not_supported:
        case vpuri_invalid:
        default:
            break;
        }
        VPathRelease (rpath);
    }
    return rc;
}


LIB_EXPORT rc_t CC VPathMakeCurrentPath ( VPath ** new_path )
{
    char buff [4096];
    char * a = NULL;
    char * b = buff;
    size_t z;
    rc_t rc;

    rc = VPathGetCWD (b, sizeof buff);
    if (rc)
    {
        for (z = 2 * sizeof buff; rc; z += sizeof buff)
        {
            b = realloc (a, z);
            if (b == NULL)
            {
                free (a);
                return RC (rcFS, rcPath, rcConstructing, rcMemory, rcExhausted);
            }
            rc = VPathGetCWD (b, z);
        }
    }
    rc = VPathMakeSysPath (new_path, b);
    if (b != buff)
        free (b);
    return rc;
}


/* ----------
 * VPathReadPath
 *
 * Copy the path as a ASCIZ string to the buffer.  The form will be the KFS
 * internal "posix-path" form.
 */
LIB_EXPORT rc_t CC VPathReadPath (const VPath * self, char * buffer, size_t buffer_size,
                                  size_t * num_read)
{
    size_t z = StringSize (&self->path);

    if (buffer_size < z)
        return RC (rcFS, rcPath, rcReading, rcBuffer, rcInsufficient);


    PATH_DEBUG (("%s: path '%S' fragment '%s'\n", __func__,
                 &self->path, self->fragment));
    PATH_DEBUG (("%s: should copy '%*.*s' length '%zu'\n", __func__, z, z,
                 self->path.addr, z));

    memcpy ( buffer, self->path.addr, z );
    if ( buffer_size > z )
        buffer[ z ] = '\0';
    if ( num_read != NULL )
        *num_read = z;

    PATH_DEBUG (("%s: copied '%*.*s' length '%zu'\n", __func__, z, z,
                 buffer, z));
    return 0;
}


LIB_EXPORT rc_t CC VPathReadQuery (const VPath * self, char * buffer, size_t buffer_size,
                                   size_t * num_read)
{
    if (num_read == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcParam, rcNull);
    *num_read = 0;

    if (buffer == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcParam, rcNull);

    if (self == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcSelf, rcNull);

    *num_read = string_copy (buffer, buffer_size, self->query,
                             self->alloc_size);
    return 0;
}


LIB_EXPORT rc_t CC VPathReadFragment (const VPath * self, char * buffer, size_t buffer_size,
                                      size_t * num_read)
{
    if (num_read == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcParam, rcNull);
    *num_read = 0;

    if (buffer == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcParam, rcNull);

    if (self == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcSelf, rcNull);



    *num_read = string_copy (buffer, buffer_size, self->fragment, 
                             self->alloc_size);
    return 0;
}


/* ----------
 */
LIB_EXPORT rc_t CC VPathOption (const VPath * self, VPOption_t option,
                                char * buffer, size_t buffer_size,
                                size_t * num_read)
{
    size_t o = (size_t)option; /* some oddness to use a value instead of a pointer */
    BSTNode * n = BSTreeFind (&self->options, (void*)o, VPOptionCmp);
    VPOption * opt;

    if (n == NULL)
        return RC (rcFS, rcPath, rcAccessing, rcParam, rcNotFound);
    opt = (VPOption*)n;
    *num_read = string_copy (buffer, buffer_size, opt->value.addr, opt->value.size);
    return 0;
}


LIB_EXPORT VPUri_t VPathGetUri_t (const VPath * self)
{
    if (self)
    {
        VPUri_t v = self->scheme;
        if ((v < vpuri_invalid) ||
            (v >= vpuri_count))
            v = vpuri_invalid;
        return v;
    }
    return vpuri_invalid;
}


LIB_EXPORT rc_t CC VPathMakeString ( const VPath * self, const String ** uri )
{
    rc_t rc = 0;
    if ( uri == NULL )
        rc = RC ( rcVFS, rcPath, rcAccessing, rcParam, rcNull );
    else
    {
        *uri = NULL;
        if ( self == NULL )
            rc = RC ( rcVFS, rcPath, rcAccessing, rcSelf, rcNull );
        else
        {
            /* this version is a bit of a hack */
            struct
            {
                String s;
                char   b[ 1 ];
            } * t;
            size_t z = self->path.addr - self->storage;

            t = malloc ( sizeof * t + self->alloc_size );
            if ( t == NULL )
                rc = RC ( rcVFS, rcPath, rcAccessing, rcMemory, rcExhausted );
            else
            {
                char *s = &( t->b[ 0 ] );

                if ( z != 0 )
                {
                    memcpy ( s, self->storage, z - 1 );
                    s [ z - 1 ] = ':';
                }

                memcpy ( &s[ z ], self->path.addr, self->path.size );
                z += self->path.size;

                if ( self->query != NULL && self->query[ 0 ] != '\0' )
                {
                    size_t y = string_size ( self->query );
                    s[ z++ ] = '?';
                    memcpy ( &s[ z ], self->query, y );
                    z += y;
                }

                if ( self->fragment != NULL && self->fragment[ 0 ] != '\0' )
                {
                    size_t y = string_size ( self->fragment );
                    s[ z++ ] = '#';
                    memcpy ( &s[ z ], self->fragment, y );
                    z += y;
                }

                s[ z ] = '\0';
                StringInit ( &t->s, s, z, string_len ( s, z ) );
                *uri = &t->s;
            }
        }
    }
    return rc;
}


LIB_EXPORT rc_t CC VPathGetScheme ( const VPath * self, const String ** scheme )
{
    rc_t rc = 0;
    if ( scheme == NULL )
        rc = RC ( rcVFS, rcPath, rcAccessing, rcParam, rcNull );
    else
    {
        *scheme = NULL;
        if ( self == NULL )
            rc = RC ( rcVFS, rcPath, rcAccessing, rcSelf, rcNull );
        else
        {
            size_t z = ( self->path.addr - self->storage );
            if ( z > 0 )
            {
                struct
                {
                    String s;
                    char   b[ 1 ];
                } * t;
                t = malloc ( sizeof * t + z );
                if ( t == NULL )
                    rc = RC ( rcVFS, rcPath, rcAccessing, rcMemory, rcExhausted );
                else
                {
                    size_t i;
                    char *s = &( t->b[ 0 ] );
                    memcpy ( s, self->storage, z - 1 );
                    for ( i = 0; i < z; ++i )
                        s[ i ] = tolower( s[ i ] );
                    s[ z ] = '\0';
                    StringInit ( &t->s, s, z, string_len ( s, z ) );
                    *scheme = &t->s;
                }
            }
        }
    }
    return rc;
}


LIB_EXPORT rc_t CC VPathGetScheme_t( const VPath * self, VPUri_t * uri_type )
{
    rc_t rc = 0;
    if ( uri_type == NULL )
        rc = RC ( rcVFS, rcPath, rcAccessing, rcParam, rcNull );
    else
    {
        *uri_type = vpuri_invalid;
        if ( self == NULL )
            rc = RC ( rcVFS, rcPath, rcAccessing, rcSelf, rcNull );
        else
            *uri_type = self->scheme;
    }
    return rc;
}


LIB_EXPORT rc_t CC VPathGetPath ( const VPath * self, const String ** path )
{
    rc_t rc = 0;
    if ( path == NULL )
        rc = RC ( rcVFS, rcPath, rcAccessing, rcParam, rcNull );
    else
    {
        *path = NULL;
        if ( self == NULL )
            rc = RC ( rcVFS, rcPath, rcAccessing, rcSelf, rcNull );
        else
        {
            if ( StringSize ( &self->path ) > 0 )
                rc = StringCopy ( path, &self->path );
        }
    }
    return rc;
}
