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

#ifndef _h_kfc_xc_
#define _h_kfc_xc_

#ifndef _h_kfc_extern_
#include <kfc/extern.h>
#endif

#ifndef _h_kfc_defs_
#include <kfc/defs.h>
#endif


/*--------------------------------------------------------------------------
 * XOBJ
 *  objects that can have problems
 */
XOBJ ( xoSelf, "target object reference", rcSelf );
XOBJ ( xoParam, "parameter", rcParam );
XOBJ ( xoString, "string", rcString );
XOBJ ( xoMemory, "process memory", rcMemory );
XOBJ ( xoError, "error", rcNoObj );
XOBJ ( xoBehavior, "behavior", rcFunction );
XOBJ ( xoTable, "table", rcTable );
XOBJ ( xoCursor, "cursor", rcCursor );
XOBJ ( xoColumn, "column", rcColumn );
XOBJ ( xoInteger, "integer", rcParam );
XOBJ ( xoRow, "row", rcRow );


/*--------------------------------------------------------------------------
 * XSTATE
 *  states that things can be in
 */
XSTATE ( xsIsNull, "is null", rcNull );
XSTATE ( xsEmpty, "is empty", rcEmpty );
XSTATE ( xsExhausted, "exhausted", rcExhausted );
XSTATE ( xsUnexpected, "unexpected", rcUnexpected );
XSTATE ( xsUnimplemented, "unimplemented", rcUnknown );
XSTATE ( xsCreateFailed, "failed to create", rcUnknown );
XSTATE ( xsOpenFailed, "failed to open", rcUnknown );
XSTATE ( xsNotFound, "not found", rcNotFound );
XSTATE ( xsReadFailed, "failed to read", rcUnknown );
XSTATE ( xsOutOfBounds, "out of bounds", rcOutofrange );
XSTATE ( xsAccessFailed, "failed to access", rcUnknown );


/*--------------------------------------------------------------------------
 * XC
 *  error types
 */
XC ( xcSelfNull, xoSelf, xsIsNull );
XC ( xcParamNull, xoParam, xsIsNull );
XC ( xcStringEmpty, xoString, xsEmpty );
XC ( xcNoMemory, xoMemory, xsExhausted );
XC ( xcUnexpected, xoError, xsUnexpected );
XC ( xcUnimplemented, xoBehavior, xsUnimplemented );
XC ( xcTableOpenFailed, xoTable, xsOpenFailed );
XC ( xcCursorCreateFailed, xoCursor, xsCreateFailed );
XC ( xcCursorOpenFailed, xoCursor, xsOpenFailed );
XC ( xcColumnNotFound, xoColumn, xsNotFound );
XC ( xcColumnReadFailed, xoColumn, xsReadFailed );
XC ( xcIntegerOutOfBounds, xoInteger, xsOutOfBounds );
XC ( xcCursorAccessFailed, xoCursor, xsAccessFailed );
XC ( xcRowNotFound, xoRow, xsNotFound );



#endif /* _h_kfc_xc_ */
