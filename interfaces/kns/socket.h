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
#ifndef _h_kns_socket_
#define _h_kns_socket_

#ifndef _h_kns_extern_
#include <kns/extern.h>
#endif

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * forwards
 */
struct KStream;
struct KEndPoint;
struct KNSManager;

/*--------------------------------------------------------------------------
 * KSocket
 */
typedef struct KSocket KSocket;


/* MakeConnection
 *  create a connection-oriented stream
 *
 *  "conn" [ OUT ] - a stream for communication with the server
 *
 *  "from" [ IN ] - client endpoint
 *
 *  "to" [ IN ] - server endpoint 
 *
 *  both endpoints have to be of type epIP; creates a TCP connection
 */
KNS_EXTERN rc_t CC KNSManagerMakeConnection ( struct KNSManager const * self,
    struct KStream **conn, struct KEndPoint const *from, struct KEndPoint const *to );

/* MakeIPCConnection
 *  create a connection-oriented stream connected to an IPC server
 *
 *  "conn" [ OUT ] - a stream for communication with the server
 *
 *  "to" [ IN ] - server endpoint (must be of type epIPC)
 *
 *  "max_retries" [ IN ] - number of retries to be made if connection is refused.
 *  Will sleep for 1 second between retries
 */
KNS_EXTERN rc_t CC KNSManagerMakeIPCConnection ( struct KNSManager const *self,
    struct KStream **conn, struct KEndPoint const *to, uint32_t max_retries );

/* MakeListener
 *  create a listener socket for accepting incoming IPC connections
 *
 *  "ep" [ IN ] - a endpoint of type epIPC
 *
 *  "listener" [ IN ] - a listener socket, to be called KSocketListen() on
 */
KNS_EXTERN rc_t CC KNSManagerMakeListener ( struct KNSManager const *self,
    KSocket **listener, struct KEndPoint const * ep );

/* AddRef
 * Release
 */
KNS_EXTERN rc_t CC KSocketAddRef ( KSocket *self );
KNS_EXTERN rc_t CC KSocketRelease ( KSocket *self );


/* Listen
 *  wait for an incoming connection
 *
 *  "conn" [ OUT ] - a stream for communication with the client 
 */
KNS_EXTERN rc_t CC KSocketListen ( KSocket *self, struct KStream **conn );


#ifdef __cplusplus
}
#endif

#endif /* _h_kns_socket_ */
