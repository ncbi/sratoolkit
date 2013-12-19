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


/*--------------------------------------------------------------------------
 * forwards
 */
#define KSTREAM_IMPL KSocket
typedef struct KSocket KSocket;

#include <kns/extern.h>
#include <kns/manager.h>
#include <kns/socket.h>
#include <kns/impl.h>
#include <kns/endpoint.h>
#include <klib/rc.h>
#include <klib/log.h>
#include <klib/printf.h>
#include <klib/text.h>
#include <sysalloc.h>

#include "../stream-priv.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#if 0
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <WINDOWS.H>

#include <sysalloc.h>


#define SHUT_RD 0
#define SHUT_WR 1
typedef int ssize_t;



/*--------------------------------------------------------------------------
 * KSocket
 *  a socket IS a stream
 *
 *  in Berkeley socket terminology, a STREAM implies a CONTINUOUS stream,
 *  which is implemented by the TCP connection. A "chunked" or discontiguous
 *  stream would be a datagram stream, implemented usually by UDP.
 *
 *  in VDB terminology, a STREAM is a fluid, moving target that is observed
 *  from a stationary point, whereas a FILE or OBJECT is a static stationary
 *  target observed from a movable window. This means that a STREAM cannot be
 *  addressed randomly, whereas a FILE or OBJECT can.
 */
struct KSocket
{
    KStream dad;
    union {
        SOCKET fd;
        wchar_t pipename[256];
        HANDLE pipe;
    } u;
    enum {isSocket, isIpcListener, isIpcPipeServer, isIpcPipeClient} type;
    HANDLE listenerPipe;
};

static rc_t HandleErrno();

LIB_EXPORT rc_t CC KSocketAddRef( struct KSocket *self )
{
    return KStreamAddRef(&self->dad);
}

LIB_EXPORT rc_t CC KSocketRelease( struct KSocket *self )
{
    return KStreamRelease(&self->dad);
}

static
rc_t CC KSocketWhack ( KSocket *self )
{
    rc_t rc = 0;
    assert ( self != NULL );

    switch (self->type)
    {
    case isSocket:
        {
            if (shutdown ( self -> u . fd, SHUT_WR ) != -1)
            {
                while ( 1 ) 
                {
                    char buffer [ 1024 ];
                    ssize_t result = recv ( self -> u . fd, buffer, sizeof buffer, 0 );
                    if ( result <= 0 )
                        break;
                }
                if (shutdown ( self -> u . fd, SHUT_RD ) != -1)
                {
                    if ( closesocket ( self ->  u . fd ) == SOCKET_ERROR )
                        rc = RC ( rcNS, rcNoTarg, rcClosing, rcNoObj, rcError );
                        /* maybe report */
                }
                else
                    rc = HandleErrno();
            }
            else
                rc = HandleErrno();
        }
        break;
    case isIpcListener: /* an unconnected server-side pipe */
        if (self->listenerPipe != INVALID_HANDLE_VALUE)
        {   /* !!! In case there is an active call to ConnectNamedPipe() on some thread, "wake" the synchronous named pipe,
            otherwise DisconnectNamedPipe/CloseHandle will block forever */
            HANDLE hPipe = CreateFileW(self->u.pipename, 
                                       GENERIC_READ, 
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                       NULL, 
                                       OPEN_EXISTING, 
                                       0, 
                                       NULL);
            if (hPipe != INVALID_HANDLE_VALUE)
                CloseHandle(hPipe);

            /* now, Disconnect/Close the original pipe */
            if (!DisconnectNamedPipe(self->listenerPipe) && rc == 0)
                rc = HandleErrno();
            if (!CloseHandle(self->listenerPipe) && rc == 0)
                rc = HandleErrno();
        }
        break;
    case isIpcPipeServer:
        {
            if (!FlushFileBuffers(self->u.pipe))
                rc = HandleErrno();
            if (!DisconnectNamedPipe(self->u.pipe) && rc == 0)
                rc = HandleErrno();
            if (!CloseHandle(self->u.pipe) && rc == 0)
                rc = HandleErrno();
        }
        break;
    case isIpcPipeClient:
        if (!CloseHandle(self->u.pipe))
            rc = HandleErrno();
        break;
    }
    
    free ( self );

    return rc;
}

static
rc_t HandleErrno()
{
    int lerrno;
    rc_t rc;

    switch ( lerrno = WSAGetLastError() )
    {
    case ERROR_FILE_NOT_FOUND:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcFile, rcNotFound);            
        break;
    case ERROR_INVALID_HANDLE:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInvalid);            
        break;
    case ERROR_INVALID_PARAMETER:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcParam, rcInvalid);            
        break;
    case ERROR_PIPE_BUSY:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case WSAEACCES: /* write permission denied */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcUnauthorized );            
        break;
    case WSAEADDRINUSE:/* address is already in use */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcExists );
        break;
    case WSAEADDRNOTAVAIL: /* requested address was not local */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcNotFound );
        break;
    case WSAEAFNOSUPPORT: /* address didnt have correct address family in ss_family field */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcName, rcError );
        break;
    case WSAEALREADY: /* socket is non blocking and a previous connection has not yet completed */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUndefined );
        break;
    case WSAECONNABORTED: /* virtual circuit terminated. Application should close socket */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInterrupted );
        break;
    case WSAECONNREFUSED: /* remote host refused to allow network connection */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case WSAECONNRESET: /* connection reset by peer */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcCanceled );
        break;
    case WSAEFAULT: /* name paremeter is not valid part of addr space */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcOutofrange );
        break;
    case WSAEHOSTUNREACH: /* remote hoste cannot be reached at this time */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcNotAvailable );
        break;
    case WSAEINPROGRESS: /* call is in progress */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUndefined );
        break;
    case WSAEINVAL: /* invalid argument */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcParam, rcInvalid );
        break;
    case WSAEISCONN: /* connected already */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcExists );
        break;
    case WSAEMSGSIZE:  /* msg size too big */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMessage, rcExcessive );
        break;
    case WSAENETDOWN:/* network subsystem failed */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcFailed );
        break;
    case WSAENETRESET: /* connection broken due to keep-alive activity that 
                          detected a failure while in progress */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case WSAENETUNREACH: /* network is unreachable */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcNotAvailable );
        break;
    case WSAENOBUFS: /* output queue for a network connection was full. 
                     ( wont typically happen in linux. Packets are just silently dropped */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcInterrupted );
        break;
    case ERROR_PIPE_NOT_CONNECTED:
    case WSAENOTCONN: /* socket is not connected */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcInvalid );
        break;
    case WSANOTINITIALISED: /* Must have WSAStartup call */
        rc = RC ( rcNS, rcNoTarg, rcInitializing, rcEnvironment, rcUndefined );
        break;
    case WSAENOTSOCK: /* sock fd is not a socket */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInvalid );
        break;
    case WSAEOPNOTSUPP: /* socket is not stream-style such as SOCK_STREAM */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAEPROTONOSUPPORT: /* specified protocol is not supported */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        break;
    case WSAEPROTOTYPE: /* wrong type of protol for this socket */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAEPROVIDERFAILEDINIT: /* service provider failed to initialize */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        break;
    case WSAESHUTDOWN: /* socket had been shutdown */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAESOCKTNOSUPPORT: /* specifified socket type is not supported */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAETIMEDOUT: /* connection dropped because of network failure */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case WSAEWOULDBLOCK: /* socket is marked as non-blocking but the recv operation
                            would block */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcError );
        break;

    case WSAEINTR: /* call was canceled */
    case WSAEMFILE: /* no more socket fd available */
    default:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        PLOGERR (klogErr,
                 (klogErr, rc, "unknown system error '$(S)($(E))'",
                  "S=%!,E=%d", lerrno, lerrno));
    }
    return rc;
}

static
rc_t CC KSocketRead ( const KSocket *self,
    void *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc = 0;
    assert ( self != NULL );

    while ( rc == 0 )
    {
        ssize_t count;
        if ( self -> type == isSocket)
            count = recv ( self -> u . fd, buffer, (int)bsize, 0 );
        else if (!ReadFile( self->u.pipe, buffer, (DWORD)bsize, &count, NULL))
            count = -1;

        if ( count >= 0 )
        {
            assert ( num_read != NULL );
            * num_read = ( size_t ) count;
            return 0;
        }
        if (WSAGetLastError() != WSAEINTR)
            rc = HandleErrno();
        break;
    }
            
    return rc;
}

static
rc_t CC KSocketWrite ( KSocket *self,
    const void *buffer, size_t bsize, size_t *num_writ )
{
    rc_t rc = 0;
    assert ( self != NULL );

    while ( rc == 0 )
    {
        ssize_t count;
        if ( self -> type == isSocket)
            count = send ( self -> u . fd, buffer, (int)bsize, 0 );
        else if (!WriteFile( self->u.pipe, buffer, (DWORD)bsize, &count, NULL))
            count = -1;
        
        if ( count >= 0 )
        {
            assert ( num_writ != NULL );
            * num_writ = count;
            return 0;
        }
        if (WSAGetLastError() != WSAEINTR)
            rc = HandleErrno();

        break;        
    }

    return rc;
}

static KStream_vt_v1 vtKSocket =
{
    1, 0,
    KSocketWhack,
    KSocketRead,
    KSocketWrite
};

static
rc_t KSocketBind ( KSocket *self, const struct sockaddr *ss, size_t addrlen )
{
    rc_t rc = 0;
    
    if ( bind ( self -> u.fd, ss, (int)addrlen ) == SOCKET_ERROR )
        rc = HandleErrno();

    return rc;    
}


static
rc_t KSocketConnect ( KSocket *self, struct sockaddr *ss, size_t addrlen )
{
    rc_t rc = 0;

    if ( connect ( self -> u . fd, ss, (int)addrlen ) == SOCKET_ERROR )
        rc = HandleErrno();

    return rc;    
}

LIB_EXPORT
rc_t CC KNSManagerMakeConnection ( struct KNSManager const *self, KStream **out, const KEndPoint *from, const KEndPoint *to )
{
    rc_t rc;
    SOCKET fd;

    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );

    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( to == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if ( (from != NULL && from->type != epIPV4) || to->type != epIPV4 )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);

    fd = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( fd != INVALID_SOCKET )
    {
        struct sockaddr_in ss;
        memset ( & ss, 0, sizeof ss );
        ss . sin_family = AF_INET;
        if ( from != NULL )
        {
            ss . sin_port = htons ( from -> u. ipv4 . port );
            ss . sin_addr . s_addr = htonl ( from -> u . ipv4 . addr );
        }
        if ( bind ( fd, (const struct sockaddr*)&ss, sizeof ss  ) != SOCKET_ERROR ) 
        {
            ss . sin_port = htons ( to -> u . ipv4 . port );
            ss . sin_addr . s_addr = htonl ( to -> u . ipv4 . addr );
            
            if ( connect ( fd, (const struct sockaddr*)&ss, sizeof ss ) != SOCKET_ERROR )
            {   /* create the KSocket */
                KSocket *ksock = calloc ( sizeof *ksock, 1 );
                if ( ksock == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                else
                {
                    /* initialize the KSocket */
                    rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                                       "KSocket", "tcp", true, true );
                    if ( rc == 0 )
                    {
                        ksock -> type = isSocket;
                        ksock -> u . fd = fd;
                        *out = & ksock -> dad;
                        return 0;
                    }
                    free(ksock);
                }
                return rc;
            }
            else /* connect () failed */
                rc = HandleErrno();
        }
        else /* bind() failed */
            rc = HandleErrno();
        closesocket(fd);
    }
    else /* socket() failed */
        rc = HandleErrno();
    return rc;
}

LIB_EXPORT
rc_t CC KNSManagerMakeIPCConnection ( struct KNSManager const *self, KStream **out, const KEndPoint *to, uint8_t max_retries )
{
    uint8_t retry_count = 0;
    rc_t rc;
    char pipename[256];
    wchar_t pipenameW[256];
    size_t num_writ;
    
    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );

    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( to == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if ( to->type != epIPC )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);

    /* use named pipes to implement unix domain socket - like behavior */
    rc = string_printf(pipename, sizeof(pipename), &num_writ, "\\\\.\\pipe\\%s", to->u.ipc_name);
    if (rc == 0)
        string_cvt_wchar_copy(pipenameW, sizeof(pipenameW), pipename, num_writ);
        
    while (rc == 0)
    {
        HANDLE h = CreateFileW(pipenameW,       /* pipe name */
                               GENERIC_READ |  /* read and write access */
                                 GENERIC_WRITE, 
                               0,              /* no sharing */
                               NULL,           /* default security attributes */
                               OPEN_EXISTING,  /* opens existing pipe  */
                               0,              /* default attributes */
                               NULL);          /* no template file */
        if ( h != INVALID_HANDLE_VALUE )
        {   /* create the KSocket */
            KSocket* ksock;

            DWORD dwMode = PIPE_READMODE_MESSAGE; 
            if (SetNamedPipeHandleState( 
                  h,        /* pipe handle */
                  &dwMode,  /* new pipe mode */
                  NULL,     /* don't set maximum bytes */
                  NULL))    /* don't set maximum time */
            {
                ksock = calloc ( sizeof *ksock, 1 );
                if ( ksock == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                else
                {   /* initialize the KSocket */
                    rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                                       "KSocket", "tcp", true, true );
                    if ( rc == 0 )
                    {
                        ksock -> type = isIpcPipeClient;
                        ksock -> u.pipe = h;
                        *out = & ksock -> dad;
                        return 0;
                    }
                    free ( ksock );
                }
            }
            else
                rc = HandleErrno();
        }
        else if (GetLastError() == ERROR_PIPE_BUSY)
        {
            if (!WaitNamedPipeW(pipenameW, NMPWAIT_USE_DEFAULT_WAIT))
            {   // timeout, try again
                if ( retry_count < max_retries )
                {
                    Sleep(1000);/*ms*/
                    ++retry_count;
                    continue;   
                }
                rc = HandleErrno();
            }
            else
                continue;
        }
        else /* CreateFile() failed */
        {
            rc = HandleErrno();
            
            if ( retry_count < max_retries && 
                (GetRCState(rc) == rcCanceled || GetRCState(rc) == rcNotFound) )
            {   
                Sleep(1000);/*ms*/
                ++retry_count;
                rc = 0;
                continue;
            }
        }
        break;
    }
    return rc;
}

LIB_EXPORT
rc_t CC KNSManagerMakeListener( struct KNSManager const *self, struct KSocket** out, struct KEndPoint const * ep )
{   
    rc_t rc = 0;
    KSocket* ksock;

    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );
    
    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( ep == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if (ep->type != epIPC)
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);
    
    /* use named pipes to implement unix domain socket - like behavior */
    ksock = calloc ( sizeof *ksock, 1 );
    if ( ksock == NULL )
        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
    else
    {
        rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                           "KSocket", "tcp", true, true );
        if ( rc == 0 )
        {
            size_t num_writ;
            char pipename[256];
            rc = string_printf(pipename, sizeof(pipename), &num_writ, "\\\\.\\pipe\\%s", ep->u.ipc_name);
            if (rc == 0)
                string_cvt_wchar_copy(ksock->u.pipename, sizeof(ksock->u.pipename), pipename, num_writ);
                
            if (rc == 0)
            {
                ksock -> type = isIpcListener;
                ksock -> listenerPipe = INVALID_HANDLE_VALUE;
                *out = ksock;
                return 0;
            }
            KSocketWhack(ksock);
        }
        else
            free ( ksock );
    }
        
    return rc;
}

LIB_EXPORT 
rc_t CC KSocketListen ( struct KSocket *listener, struct KStream **out )
{
    rc_t rc = 0;

    if ( listener == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull);
        
    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if (listener->type != isIpcListener)
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);

    {
        listener->listenerPipe = CreateNamedPipeW(listener->u.pipename,    /* pipe name */
                                                  PIPE_ACCESS_DUPLEX,      /* read/write access  */
                                                  PIPE_TYPE_MESSAGE |      /* message type pipe  */
                                                  PIPE_READMODE_MESSAGE |  /* message-read mode  */
                                                  PIPE_WAIT,               /* blocking mode  */
                                                  PIPE_UNLIMITED_INSTANCES,/* max. instances   */
                                                  1024,                    /* output buffer size  */
                                                  1024,                    /* input buffer size  */
                                                  0,                       /* client time-out  */
                                                  NULL);                   /* default security attribute  */
        if ( listener->listenerPipe != INVALID_HANDLE_VALUE )
        {
            while (1)
            {   
                if ( ConnectNamedPipe(listener->listenerPipe, NULL) || 
                    GetLastError() == ERROR_PIPE_CONNECTED) /* = a client happened to connect between CreateNamePipe and ConnectNamedPipe, we are good */
                {
                    KSocket *ksock = calloc ( sizeof *ksock, 1 );
                    if ( ksock == NULL )
                        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                    else
                    {
                        rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                                           "KSocket", "tcp", true, true );
                        if ( rc == 0 )
                        {
                            ksock -> type = isIpcPipeServer;
                            ksock -> u.pipe = listener->listenerPipe;
                            listener->listenerPipe = INVALID_HANDLE_VALUE; /* this is only to be used while ConnectNamedPipe() is in progress */
                            *out = & ksock -> dad;
                            return 0;
                        }
                        free ( ksock );
                    }
                }
                else if (GetLastError() == ERROR_NO_DATA) /* no client */
                {   /* keep on listening */
                    Sleep(10); /*ms*/
                }
                else
                    break;
            }
        }
    }
    return HandleErrno();
}   
    
    
    
