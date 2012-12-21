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

#ifndef _STDINT_H
#define _STDINT_H

#ifdef __cplusplus
extern "C" {
#endif

/* perhaps not the best place for this, but it helps reduce the
   number of artificial includes for compatibility */
#ifndef __inline__
#define __inline__ __inline
#endif

typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;

typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;

#ifndef INT8_MAX
#define INT8_MAX _I8_MAX
#endif

#ifndef INT8_MIN
#define INT8_MIN _I8_MIN
#endif

#ifndef UINT8_MAX
#define UINT8_MAX _UI8_MAX
#endif

#ifndef INT16_MAX
#define INT16_MAX _I16_MAX
#endif

#ifndef INT16_MIN
#define INT16_MIN _I16_MIN
#endif

#ifndef UINT16_MAX
#define UINT16_MAX _UI16_MAX
#endif

#ifndef INT32_MAX
#define INT32_MAX _I32_MAX
#endif

#ifndef INT32_MIN
#define INT32_MIN _I32_MIN
#endif

#ifndef UINT32_MAX
#define UINT32_MAX _UI32_MAX
#endif

#ifndef INT64_MAX
#define INT64_MAX _I64_MAX
#endif

#ifndef INT64_MIN
#define INT64_MIN _I64_MIN
#endif

#ifndef UINT64_MAX
#define UINT64_MAX _UI64_MAX
#endif

#ifndef SIZE_T_MAX
#define SIZE_T_MAX ((size_t)(-(1ULL)))
#endif

#if !defined(__cplusplus)  ||  defined(__STDC_CONSTANT_MACROS)
#ifndef UINT64_C
#define UINT64_C(x) (x ## ULL)
#endif
#ifndef INT64_C
#define INT64_C(x) (x ## LL)
#endif
#endif

#ifndef __func__
#define __func__ __FUNCTION__
#endif

#ifdef __cplusplus
}
#endif

#endif
