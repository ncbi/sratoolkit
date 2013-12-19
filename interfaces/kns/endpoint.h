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
#ifndef _h_kns_endpoint_
#define _h_kns_endpoint_

#ifndef _h_kns_extern_
#include <kns/extern.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * forwards
 */
struct String;
struct KNSManager;


/*--------------------------------------------------------------------------
 * KEndPoint
 *  describe a socket endpoint
 */
 
enum {
    epIPV4,
    epIPC
};
typedef uint32_t KEndPointType;

#define IPC_NAME_MAX 256

typedef struct KEndPoint KEndPoint;
struct KEndPoint
{
	union 
	{
		struct
		{
			uint32_t addr;
			uint16_t port;
		} ipv4;
		char ipc_name[IPC_NAME_MAX];
	} u;
    KEndPointType type;
};


/* InitIPv4Endpoint
 *  initialize the endpoint with an IPv4 address and port
 *
 *  "ep" [ OUT ] - address of endpoint block to be intialized
 *
 *  "ipv4" [ IN, DEFAULT 0 ] - binary IPv4 address in native integer byte order
 *   i.e. 0x7F000001 represents 127.0.0.1. if the special address 0 is given, it
 *   represents any host-local interface address.
 *
 *  "port" [ IN, DEFAULT 0 ] - binary port number in native integer byte order.
 *   if the special port number 0 is given, it represents any available port.
 */
KNS_EXTERN rc_t CC KNSManagerInitIPv4Endpoint ( struct KNSManager const *self,
    KEndPoint *ep, uint32_t ipv4, uint16_t port );


/* InitDNSEndpoint
 *  initialize the endpoint with a DNS name and a port number
 *
 *  "ep" [ OUT ] - address of endpoint block to be intialized
 *
 *  "dns" [ IN ] - textual DNS address.
 *
 *  "port" [ IN, DEFAULT 0 ] - binary port number in native integer byte order.
 *   if the special port number 0 is given, it represents any available port.
 */
KNS_EXTERN rc_t CC KNSManagerInitDNSEndpoint ( struct KNSManager const *self,
    KEndPoint *ep, struct String const *dns, uint16_t port );

/* InitIPCEndpoint
 *  initialize the endpoint for interprocess communication
 *
 *  "ep" [ OUT ] - address of endpoint block to be intialized
 *
 *  "name" [ IN ] - IPC channel name (a POSIX path to a Unix socket, or a name of a Windows pipe)
 */
KNS_EXTERN rc_t CC KNSManagerInitIPCEndpoint ( struct KNSManager const *self,
    KEndPoint *ep, struct String const * name );

#ifdef __cplusplus
}
#endif

#endif /* _h_kns_endpoint_ */
