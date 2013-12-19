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

#ifndef _h_klib_defs_
#define _h_klib_defs_

#ifndef _h_klib_callconv_
#include <klib/callconv.h>
#endif

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/*--------------------------------------------------------------------------
 * rc_t
 *  upon success, all functions will return code 0
 *  other codes indicate failure or additional status information
 */
typedef uint32_t rc_t;


/*--------------------------------------------------------------------------
 * bitsz_t
 *  where size_t always denotes a quantity of bytes,
 *  bitsz_t denotes a quantity of bits.
 */
typedef uint64_t bitsz_t;


/*--------------------------------------------------------------------------
 * fptr_t
 *  "generic" function pointer type
 *  has very little real use other than to calm down compilers
 */
typedef int ( CC * fptr_t ) ( void );


/*--------------------------------------------------------------------------
 * remove_t
 *  usually when message signatures change, the compiler can
 *  and will pick up the changes and trigger an error. sometimes
 *  it will just issue a warning, and other times will not pick
 *  up on the significance of a change.
 *
 *  to ensure that a change of signature gets caught everywhere
 *  by the compiler, we can introduce an extra parameter that
 *  causes us to visit all dependent code.
 */
typedef struct remove_t remove_t;


/*--------------------------------------------------------------------------
 * KTime_t
 *  64 bit time_t
 *  operations are declared in <klib/time.h>
 */
typedef int64_t KTime_t;


/*--------------------------------------------------------------------------
 * ver_t
 *  32 bit 4 part type
 */
typedef uint32_t ver_t;

/* GetMajor
 *  return major component
 */
#define VersionGetMajor( self ) \
    ( ( self ) >> 24 )

/* GetMinor
 *  return minor component
 */
#define VersionGetMinor( self ) \
    ( ( ( self ) >> 16 ) & 0xFF )

/* GetRelease
 *  return release component
 */
#define VersionGetRelease( self ) \
    ( ( self ) & 0xFFFF )


/*--------------------------------------------------------------------------
 * KCreateMode
 *  values are defined in <kfs/defs.h>
 */
typedef uint32_t KCreateMode;


/*--------------------------------------------------------------------------
 * stringize
 *  it is useful to be able to convert PP defines on the command line
 */
#define stringize( tok ) tok_to_string ( tok )
#define tok_to_string( tok ) # tok


/*--------------------------------------------------------------------------
 * __mod__, __file__ and __fext__
 *  these guys are slightly different from __FILE__
 *  and they complement __func__
 */
#if ! defined __mod__ && defined __mod_name__
#define __mod__ stringize ( __mod_name__ )
#endif

#if ! defined __file__ && defined __file_name__
#define __file__ stringize ( __file_name__ )
#endif

#if ! defined __fext__ && defined __file_ext__
#define __fext__ stringize ( __file_ext__ )
#endif


#if 1

/*--------------------------------------------------------------------------
 * LPFX
 * SHLX
 * MODX
 *  take their input from make
 */
#ifndef LIBPREFIX
 #define LPFX ""
#else
 #define LPFXSTR2( str ) # str
 #define LPFXSTR( str ) LPFXSTR2 ( str )
 #define LPFX LPFXSTR ( LIBPREFIX )
#endif
#ifndef SHLIBEXT
 #define SHLX ""
#else
 #define SHLXSTR2( str ) "." # str
 #define SHLXSTR( str ) SHLXSTR2 ( str )
 #define SHLX SHLXSTR ( SHLIBEXT )
#endif
#ifndef MODEXT
 #define MODX SHLX
#else
 #define MODXSTR2( str ) "." # str
 #define MODXSTR( str ) MODXSTR2 ( str )
 #define MODX MODXSTR ( MODIBEXT )
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /*  _h_klib_defs_ */
