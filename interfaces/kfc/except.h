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

#ifndef _h_kfc_except_
#define _h_kfc_except_

#ifndef _h_kfc_defs_
#include <kfc/defs.h>
#endif

#undef ERROR
#undef FAILED

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * exception-related macros
 */

/* xc_sev_t
 *  severity
 */
typedef enum xc_sev_t { xc_sev_system, xc_sev_internal, xc_sev_user } xc_sev_t;


/* ANNOTATE
 *  make some annotation
 *  but not an error
 */
void ctx_annotate ( ctx_t ctx, uint32_t lineno, const char *msg, ... );
#define ANNOTATE( ... )                            \
    ctx_annotate ( ctx, __LINE__, __VA_ARGS__ )


/* ERROR
 *  make an annotation
 *  record an error as an xc_t
 */
void ctx_error ( ctx_t ctx, uint32_t lineno, xc_sev_t sev, xc_t xc, const char *msg, ... );
#define SYSTEM_ERROR( xc, ... )                                    \
    ctx_error ( ctx, __LINE__, xc_sev_system, xc, __VA_ARGS__ )
#define INTERNAL_ERROR( xc, ... )                                  \
    ctx_error ( ctx, __LINE__, xc_sev_internal, xc, __VA_ARGS__ )
#define USER_ERROR( xc, ... )                                      \
    ctx_error ( ctx, __LINE__, xc_sev_user, xc, __VA_ARGS__ )


/* ABORT
 *  make an annotation
 *  record an error as an x_t
 *  exit thread ( actually, exit process )
 */
void ctx_abort ( ctx_t ctx, uint32_t lineno, xc_t xc, const char *msg, ... );
#define ABORT( xc, ... )                               \
    ctx_abort ( ctx, __LINE__, xc, __VA_ARGS__ )
#define FATAL_ERROR( xc, ... )                         \
    ctx_abort ( ctx, __LINE__, xc, __VA_ARGS__ )


/* FAILED
 *  a test of rc within ctx_t
 */
#define FAILED() \
    ( ctx -> error != false )


/* TRY
 *  another C language "try" macro
 */
#define TRY( expr ) \
    expr; \
    if ( ! FAILED () )


/* CATCH
 *  attempts to catch rc on certain types
 */
bool ctx_xc_isa ( xc_t xc );
bool ctx_xstate_isa ( xstate_t xs );
bool ctx_xobj_isa ( xobj_t xs );

#define CATCH( xc ) \
    else if ( ctx_xc_isa ( xc ) )
#define CATCH_OBJ( xo ) \
    else if ( ctx_xobj_isa ( xo ) )
#define CATCH_STATE( xs ) \
    else if ( ctx_xstate_isa ( xs ) )
#define CATCH_ALL() \
    else


/* ON_FAIL
 *  reverses TRY logic
 *  generally used for less-structured code,
 *  e.g. ON_FAIL ( x ) return y;
 */
#define ON_FAIL( expr ) \
    expr; \
    if ( FAILED () )


/* CLEAR
 *  clears annotation and error
 *  used from within CATCH handler
 */
void ctx_clear_all ( ctx_t ctx );
#define CLEAR() \
    ctx_clear_all ( ctx )


/* CLEAR_ERR
 *  clears just error state, leaving any annotation
 */
void ctx_clear_error ( ctx_t ctx );
#define CLEAR_ERR() \
    ctx_clear_error ( ctx )

#ifdef __cplusplus
}
#endif


#endif /* _h_kfc_except_ */
