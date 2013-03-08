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
#ifndef _h_kns_request_
#define _h_kns_request_

#ifndef _h_kns_extern_
#include <kns/extern.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


struct String;
struct KNSManager;
struct KDataBuffer;

typedef struct KCurlRequest KCurlRequest;

KNS_EXTERN rc_t CC KNSManagerMakeRequest( struct KNSManager const *kns_mgr, KCurlRequest **self, const char * url, bool verbose );

KNS_EXTERN rc_t CC KCurlRequestAddRef ( const KCurlRequest *self );

KNS_EXTERN rc_t CC KCurlRequestRelease( const KCurlRequest *self );

KNS_EXTERN rc_t CC KCurlRequestAddFields( KCurlRequest *self, const char * fields );

KNS_EXTERN rc_t CC KCurlRequestAddSFields( KCurlRequest *self, struct String const * fields );

KNS_EXTERN rc_t CC KCurlRequestAddField( KCurlRequest *self, const char * name, const char * value );

KNS_EXTERN rc_t CC KCurlRequestAddSField( KCurlRequest *self, struct String const * name, struct String const * value );

KNS_EXTERN rc_t CC KCurlRequestPerform( KCurlRequest *self, struct KDataBuffer * buffer );


#ifdef __cplusplus
}
#endif

#endif
