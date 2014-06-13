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
#include <kproc/timeout.h>

#include "../stream-priv.h"

#include <sysalloc.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <os-native.h>

#include <WINDOWS.H>

#define LOG

#define SHUT_RD 0
#define SHUT_WR 1
typedef SSIZE_T ssize_t;

static rc_t HandleErrnoEx ( const char *func, unsigned int lineno );
#define HandleErrno() HandleErrnoEx ( __func__, __LINE__ )

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
 
 /*
  * On Windows, we have 2 different mechanisms to implement KSockets, 
  * WinSock based for Ipv4 connections and named pipes based for IPC
  */
  
struct KSocket
{   /* the "base type" for KIpv4Socket and PKIPCSocket */
    KStream dad;

    int32_t read_timeout;
    int32_t write_timeout;
};

LIB_EXPORT rc_t CC KSocketAddRef( struct KSocket *self )
{   /* this will handle all derived types */
    return KStreamAddRef(&self->dad);
}

LIB_EXPORT rc_t CC KSocketRelease( struct KSocket *self )
{   /* this will handle all derived types */
    return KStreamRelease(&self->dad);
}

/*
    KIpv4Socket
*/
struct KIpv4Socket
{
    KSocket dad;
    SOCKET fd;
};
typedef struct KIpv4Socket KIpv4Socket;

static
rc_t CC KIpv4SocketWhack ( KSocket *base )
{
    KIpv4Socket* self = (KIpv4Socket*)base;
    rc_t rc = 0;
    
    assert ( self != NULL );

    if (shutdown ( self -> fd, SHUT_WR ) != -1)
    {
        while ( 1 ) 
        {
            char buffer [ 1024 ];
            ssize_t result = recv ( self -> fd, buffer, sizeof buffer, 0 );
            if ( result <= 0 )
                break;
        }
        if (shutdown ( self -> fd, SHUT_RD ) != -1)
        {
            if ( closesocket ( self -> fd ) == SOCKET_ERROR )
                rc = RC ( rcNS, rcNoTarg, rcClosing, rcNoObj, rcError );
                /* maybe report */
        }
        else
            rc = HandleErrno();
    }
    else
        rc = HandleErrno();
    
    free ( self );

    return rc;
}

static
rc_t CC KIpv4SocketTimedRead ( const KSocket *base,
    void *buffer, size_t bsize, size_t *num_read, timeout_t *tm )
{
    rc_t rc = 0;

    struct timeval ts;
    fd_set readFds;
    int selectRes;
    
    KIpv4Socket* self = (KIpv4Socket*)base;
    assert ( self != NULL );
    
    /* convert timeout (relative time) */
    if (tm != NULL)
    {
        ts.tv_sec = tm -> mS / 1000;
        ts.tv_usec = (tm -> mS % 1000) * 1000;
    }
    
    /* wait for socket to become readable */
    FD_ZERO(&readFds);
    FD_SET(self -> fd, &readFds);
    selectRes = select(0, &readFds, NULL, NULL, tm == NULL ? NULL : &ts);
    
    /* check for error */
    if (selectRes == -1)
    {
        rc = HandleErrno();
    }
    else if (selectRes == 0)
    {   /* timeout */
        rc = RC ( rcNS, rcStream, rcWriting, rcTimeout, rcExhausted );
    }
    else if (FD_ISSET(self -> fd, &readFds))
    {
        while ( rc == 0 )
        {
            ssize_t count = recv ( self -> fd, buffer, (int)bsize, 0 );

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
    }
    else
    {
        rc = HandleErrno();
    }
            
    return rc;
}

static
rc_t CC KIpv4SocketRead ( const KSocket *self,
    void *buffer, size_t bsize, size_t *num_read )
{
    timeout_t tm;
    assert ( self != NULL );

    if ( self -> read_timeout < 0 )
        return KIpv4SocketTimedRead ( self, buffer, bsize, num_read, NULL );

    TimeoutInit ( & tm, self -> read_timeout );
    return KIpv4SocketTimedRead ( self, buffer, bsize, num_read, & tm );
}

static
rc_t CC KIpv4SocketTimedWrite ( KSocket *base,
    const void *buffer, size_t bsize, size_t *num_writ, timeout_t *tm )
{
    rc_t rc = 0;
    
    struct timeval ts;
    fd_set writeFds;
    int selectRes;
    
    KIpv4Socket* self = (KIpv4Socket*)base;
    assert ( self != NULL );
    
    /* convert timeout (relative time) */
    if (tm != NULL)
    {
        ts.tv_sec = tm -> mS / 1000;
        ts.tv_usec = (tm -> mS % 1000) * 1000;
    }
    
    /* wait for socket to become writable */
    FD_ZERO(&writeFds);
    FD_SET(self -> fd, &writeFds);
    selectRes = select(0, NULL, &writeFds, NULL, tm == NULL ? NULL : &ts);
    
    /* check for error */
    if (selectRes == -1)
    {
        rc = HandleErrno();
    }
    else if (selectRes == 0)
    {   /* timeout */
        rc = RC ( rcNS, rcStream, rcWriting, rcTimeout, rcExhausted );
    }
    else if (FD_ISSET(self -> fd, &writeFds))
    {
        while ( rc == 0 )
        {
            ssize_t count = send ( self -> fd , buffer, (int)bsize, 0 );
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
    }
    else
    {
        rc = HandleErrno();
    }

    return rc;
}

static
rc_t CC KIpv4SocketWrite ( KSocket *self,
    const void *buffer, size_t bsize, size_t *num_writ )
{
    timeout_t tm;
    assert ( self != NULL );

    if ( self -> write_timeout < 0 )
        return KIpv4SocketTimedWrite ( self, buffer, bsize, num_writ, NULL );

    TimeoutInit ( & tm, self -> write_timeout );
    return KIpv4SocketTimedWrite ( self, buffer, bsize, num_writ, & tm );
}

static KStream_vt_v1 vtKIpv4Socket =
{
    1, 1,
    KIpv4SocketWhack,
    KIpv4SocketRead,
    KIpv4SocketWrite,
    KIpv4SocketTimedRead,
    KIpv4SocketTimedWrite
};

static
rc_t KNSManagerMakeIPv4Connection ( struct KNSManager const *self, 
                                    KStream **out, 
                                    const KEndPoint *from, 
                                    const KEndPoint *to,
                                    int32_t retryTimeout, 
                                    int32_t readMillis, 
                                    int32_t writeMillis )
{
    rc_t rc = 0;
    uint32_t retry_count = 0;
    SOCKET fd;

    assert ( self != NULL );
    assert ( out != NULL );

    * out = NULL;

    assert ( to != NULL );
    assert ( to -> type == epIPV4 );
    assert ( ( from == NULL || from -> type == to -> type ) );

    do
    {
        fd = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if ( fd == INVALID_SOCKET )
            rc = HandleErrno();
        else
        {
            struct sockaddr_in ss;
            memset ( & ss, 0, sizeof ss );
            ss . sin_family = AF_INET;
            if ( from != NULL )
            {
                ss . sin_port = htons ( from -> u. ipv4 . port );
                ss . sin_addr . s_addr = htonl ( from -> u . ipv4 . addr );
            }
            if ( bind ( fd, (const struct sockaddr*)&ss, sizeof ss  ) == SOCKET_ERROR ) 
                rc = HandleErrno();
                
            if (rc == 0)
            {
                ss . sin_port = htons ( to -> u . ipv4 . port );
                ss . sin_addr . s_addr = htonl ( to -> u . ipv4 . addr );
                
                if ( connect ( fd, (const struct sockaddr*)&ss, sizeof ss ) != SOCKET_ERROR )
                {   /* create the KSocket */
                    KIpv4Socket *ksock = calloc ( sizeof *ksock, 1 );
                    if ( ksock == NULL )
                        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                    else
                    {   /* initialize the KSocket */
                        rc = KStreamInit ( & ksock -> dad . dad, ( const KStream_vt* ) & vtKIpv4Socket,
                                           "KSocket", "tcp", true, true );
                        if ( rc == 0 )
                        {
                            ksock -> dad . read_timeout  = readMillis;
                            ksock -> dad . write_timeout = writeMillis;
                            ksock -> fd = fd;
                            *out = & ksock -> dad . dad;
                            return 0;
                        }
                        free(ksock);
                    }
                    /* we connected but then then ran out of memory or something bad like that, so no need to retry 
                       - simply close fd and return RC */
                    closesocket(fd);
                    return rc;
                }
                else /* connect () failed */
                    rc = HandleErrno();
            } 
            
            /* dump socket */
            closesocket(fd);
        }
        
        /* rc != 0 */
        if (retryTimeout < 0 || (int32_t)retry_count < retryTimeout)
        {   /* retry */
            Sleep ( 1000 ); /*ms*/
            ++retry_count;
            rc = 0;
        }            
    }
    while (rc == 0);
    
    return rc;
}

static rc_t KNSManagerMakeIPv4Listener ( const KNSManager *self, KSocket **out, const KEndPoint * ep )
{   /* this is for server side which we do not support for IPv4 */
    * out = NULL;
    return RC ( rcNS, rcNoTarg, rcValidating, rcFunction, rcUnsupported);
}

/*
 *  KIPCSocket
 */
enum { isIpcListener, isIpcPipeServer, isIpcPipeClient };
 
struct KIPCSocket
{
    KSocket dad;

    HANDLE pipe;
    wchar_t pipename [ 256 ];

    uint8_t type;
    HANDLE listenerPipe; /* only used iftype == isIpcListener */ 
};
typedef struct KIPCSocket KIPCSocket;

static
rc_t CC KIPCSocketWhack ( KSocket *base )
{
    rc_t rc = 0;

    KIPCSocket* self = (KIPCSocket*)base;
    assert ( self != NULL );
    pLogLibMsg(klogInfo, "$(b): KIPCSocketWhack()...", "b=%p", base); 

    switch (self->type)
    {
    case isIpcListener: /* an unconnected server-side pipe */
        pLogLibMsg(klogInfo, "$(b): isIpcListener", "b=%p", base);    
        if (self->listenerPipe != INVALID_HANDLE_VALUE)
        {   /* !!! In case there is an active call to ConnectNamedPipe() on some thread, "wake" the synchronous named pipe,
            otherwise DisconnectNamedPipe/CloseHandle will block forever */
            HANDLE hPipe = CreateFileW(self->pipename, 
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
            {
                rc = HandleErrno();
                pLogLibMsg(klogInfo, "$(b): DisconnectNamedPipe failed", "b=%p", base);    
            }
            
            if (!CloseHandle(self->listenerPipe) && rc == 0)
            {
                rc = HandleErrno();
                pLogLibMsg(klogInfo, "$(b): CloseHandle failed", "b=%p", base);    
            }
        }
        break;
    case isIpcPipeServer:
        pLogLibMsg(klogInfo, "$(b): isIpcPipeServer", "b=%p", base);  
        if (!FlushFileBuffers(self->pipe))
        {
            if (GetLastError() != ERROR_BROKEN_PIPE)
            {
                rc = HandleErrno();
                pLogLibMsg(klogInfo, "$(b): FlushFileBuffers failed, err=$(e)", "b=%p,e=%d", base, GetLastError());    
            }
        }
        if (!DisconnectNamedPipe(self->pipe) && rc == 0)
        {
            rc = HandleErrno();
            pLogLibMsg(klogInfo, "$(b): DisconnectNamedPipe failed", "b=%p", base);    
        }
        if (!CloseHandle(self->pipe) && rc == 0)
        {
            rc = HandleErrno();
            pLogLibMsg(klogInfo, "$(b): CloseHandle failed", "b=%p", base);    
        }
        break;
    case isIpcPipeClient:
        pLogLibMsg(klogInfo, "$(b): isIpcPipeClient", "b=%p", base);  
        if (!CloseHandle(self->pipe))
        {
            rc = HandleErrno();
            pLogLibMsg(klogInfo, "$(b): CloseHandle failed", "b=%p", base);    
        }
        break;
    }
    
    free ( self );

    return rc;
}

static
rc_t
WaitForData(const KIPCSocket* base, void* buffer, size_t bsize, size_t* num_read, uint32_t* tmMs, OVERLAPPED* overlap)
{   /* received a ERROR_NO_DATA trying to read from a pipe; wait for the data to arrive or a time out to expire */ 
    /* on success, will leave tmMs set to the remaining portion of timeout, if specified */
    uint32_t tm_decrement = 100; 
    pLogLibMsg(klogInfo, "$(b): no data on the pipe - going into a wait loop, tm=$(t)", "b=%p,t=%d", base, tmMs == 0 ? -1 : *tmMs);   
    while (true)
    {
        rc_t rc = 0;
        BOOL ret;
        DWORD count;
    
        if (tmMs != NULL)
        {
            if (*tmMs <= tm_decrement)
            {
                CloseHandle(overlap->hEvent);
                return RC ( rcNS, rcFile, rcReading, rcTimeout, rcExhausted );
            }
            *tmMs -= tm_decrement;
        }
        ret = ReadFile( base->pipe, buffer, (DWORD)bsize, &count, overlap ); /* *usually* returns FALSE in asynch mode */
        if (ret)
        {
            pLogLibMsg(klogInfo, "$(b): (wait loop) ReadFile completed synchronously, count=$(c)", "b=%p,c=%d", base, count);            
            assert ( num_read != NULL );
            * num_read = ( size_t ) count;
            CloseHandle(overlap->hEvent);
            return 0;
        }
        
        switch (GetLastError())
        {
        case ERROR_IO_PENDING:
            return 0; /* the caller will wait for completion */
            
        case ERROR_NO_DATA:
            pLogLibMsg(klogInfo, "$(b): (wait loop) Sleep($(t))", "b=%p,t=%d", base, tm_decrement);            
            Sleep(tm_decrement);
            break;
            
        case ERROR_SUCCESS: /* not expected in asynch mode */
            return RC ( rcNS, rcFile, rcReading, rcError, rcUnexpected);
            
        default:
            return HandleErrno();
        }
    }
    return 0;
}

static
rc_t CC KIPCSocketTimedRead ( const KSocket *base,
    void *buffer, size_t bsize, size_t *num_read, timeout_t *tm )
{
    rc_t rc = 0;
    OVERLAPPED overlap;
    
    KIPCSocket* self = (KIPCSocket*)base;
    assert ( self != NULL );
    assert ( num_read != NULL );

    pLogLibMsg(klogInfo, "$(b): KIPCSocketTimedRead($(t), $(buf), $(s))... ", "b=%p,t=%d,buf=%p,s=%d", base, tm == NULL ? -1 : tm -> mS, buffer, bsize);           

    /*TODO: wait for pipe to become readable? */
    memset(&overlap, 0, sizeof(overlap));
    overlap.hEvent = CreateEvent( 
                              NULL,     /* default security attribute */
                              TRUE,     /* manual reset event */
                              FALSE,    /* initial state = nonsignalled */
                              NULL); 
    if (overlap.hEvent != NULL)
    {
        DWORD count;
        BOOL ret = ReadFile( self->pipe, buffer, (DWORD)bsize, &count, &overlap ); /* *usually* returns FALSE in asynch mode */
        if (ret) /* done: must be synch mode */
        {
            pLogLibMsg(klogInfo, "$(b): ReadFile completed synchronously, count=$(c)", "b=%p,c=%d", base, count);            
            * num_read = ( size_t ) count;
            CloseHandle(overlap.hEvent);
            return 0;
        }
        
        *num_read = 0;
        /* asynch mode - wait for the operation to complete */
        if (GetLastError() == ERROR_NO_DATA) /* 232 */
        {
            pLogLibMsg(klogInfo, "$(b): ReadFile($(h)) returned FALSE, GetLastError() = ERROR_NO_DATA", "b=%p,h=%x", base, self->pipe);            
            rc = WaitForData(self, buffer, bsize, num_read, tm == NULL ? NULL : &tm -> mS, &overlap);
            if (*num_read != 0) /* read completed*/
            {
                CloseHandle(overlap.hEvent);
                return 0;
            }
            if (rc != 0)
            {
                CloseHandle(overlap.hEvent);
                return rc;
            }
        }   

        if (GetLastError() == ERROR_IO_PENDING) /* 997 */
        {
            pLogLibMsg(klogInfo, "$(b): ReadFile($(h)) returned FALSE, GetLastError() = ERROR_IO_PENDING", "b=%p,h=%x", base, self->pipe);            
            if (tm == NULL)
                pLogLibMsg(klogInfo, "$(b): waiting forever", "b=%p", base);            
            else
                pLogLibMsg(klogInfo, "$(b): waiting for $(t) ms", "b=%p,t=%d", base, tm -> mS);            
                
            switch (WaitForSingleObject(overlap.hEvent, tm == NULL ? INFINITE : tm -> mS ))
            {
                case WAIT_TIMEOUT:
                    pLogLibMsg(klogInfo, "$(b): timed out", "b=%p", base);            
                    rc = RC ( rcNS, rcFile, rcReading, rcTimeout, rcExhausted );
                    break;
                    
                case WAIT_OBJECT_0:
                {
                    DWORD count;
                    pLogLibMsg(klogInfo, "$(b): successful", "b=%p", base);   
                    if (GetOverlappedResult(self->pipe, &overlap, &count, TRUE)) /* wait to complete if necessary */
                    {
                        pLogLibMsg(klogInfo, "$(b): $(c) bytes read", "b=%p,c=%d", base, count);   
                        * num_read = ( size_t ) count;
                        rc = 0;
                    }
                    else
                    {
                        rc = HandleErrno();
                        pLogLibMsg(klogInfo, "$(b): GetOverlappedResult() failed", "b=%p", base);         
                    }
                    break;
                }
                
                default:
                    rc = HandleErrno();
                    pLogLibMsg(klogInfo, "$(b): WaitForSingleObject() failed", "b=%p", base);         
                    break;
            }
        }
        else if (GetLastError() == ERROR_SUCCESS)
        {
            pLogLibMsg(klogInfo, "$(b): ReadFile($(h)) returned FALSE, GetLastError() = ERROR_SUCCESS", "b=%p,h=%x", base, self->pipe);  
            rc = RC ( rcNS, rcFile, rcReading, rcError, rcUnexpected);
        }
        else
        {
            rc = HandleErrno();
        }
        CloseHandle(overlap.hEvent);
    }
    else
        rc = HandleErrno();
    
    return rc;
}

static
rc_t CC KIPCSocketRead ( const KSocket *self,
    void *buffer, size_t bsize, size_t *num_read )
{
    timeout_t tm;
    assert ( self != NULL );

    if ( self -> read_timeout < 0 )
        return KIPCSocketTimedRead ( self, buffer, bsize, num_read, NULL );

    TimeoutInit ( & tm, self -> read_timeout );
    return KIPCSocketTimedRead ( self, buffer, bsize, num_read, & tm );
}

static
rc_t CC KIPCSocketTimedWrite ( KSocket *base,
    const void *buffer, size_t bsize, size_t *num_writ, timeout_t *tm )
{
    rc_t rc = 0;
    OVERLAPPED overlap;

    KIPCSocket* self = (KIPCSocket*)base;
    assert ( self != NULL );
    
    pLogLibMsg(klogInfo, "$(b): KIPCSocketTimedWrite($(s), $(t))...", "b=%p,s=%d,t=%d", base, bsize, tm == NULL ? -1 : tm -> mS);

    memset(&overlap, 0, sizeof(overlap));
    overlap.hEvent = CreateEvent( 
                              NULL,     /* default security attribute */
                              TRUE,     /* manual reset event */
                              FALSE,    /* initial state = nonsignalled */
                              NULL); 
    if (overlap.hEvent != NULL)
    {
        DWORD count;
        BOOL ret = WriteFile( self->pipe, buffer, (DWORD)bsize, &count, &overlap ); /* returns FALSE in asynch mode */
        int err = GetLastError();
        /*pLogLibMsg(klogInfo, "$(b): WriteFile returned $(r), GetError() = $(e)", "b=%p,r=%s,e=%d", base, ret ? "TRUE" : "FALSE", err);            */
        if (ret) /* completed synchronously; either message is so short that is went out immediately, or the pipe is full */
        {   
            if (count > 0)
            {
                pLogLibMsg(klogInfo, "$(b): $(c) bytes written", "b=%p,c=%d", base, count);   
                assert ( num_writ != NULL );
                * num_writ = ( size_t ) count;
                CloseHandle(overlap.hEvent);
                return 0;
            }
            else 
            {   /* pipe is full - go into a wait loop */
                uint32_t tm_left = tm == NULL ? 0 : tm -> mS;
                uint32_t tm_decrement = 100; 
                pLogLibMsg(klogInfo, "$(b): pipe full - going into a wait loop for $(t) ms", "b=%p,t=%d", base, tm == NULL ? -1 : tm->mS);   
                while (count == 0)
                {
                    if (tm != NULL)
                    {
                        if (tm_left <= tm_decrement)
                        {
                            CloseHandle(overlap.hEvent);
                            return RC ( rcNS, rcFile, rcWriting, rcTimeout, rcExhausted );
                        }
                        tm_left -= tm_decrement;
                    }
                    
                    Sleep(1);/*ms*/
                    
                    pLogLibMsg(klogInfo, "$(b): write wait loop: attempting to WriteFile", "b=%p", base);   
                    ret = WriteFile( self->pipe, buffer, (DWORD)bsize, &count, &overlap ); /* returns FALSE in asynch mode */
                    err = GetLastError();
                    /*pLogLibMsg(klogInfo, "$(b): WriteFile returned $(r), GetError() = $(e)", "b=%p,r=%s,e=%d", base, ret ? "TRUE" : "FALSE", err);            */
                    if (!ret)
                        break; /* and proceed to handling the asynch mode*/
                }
            }
        }
        
        /* asynch mode - wait for the operation to complete */
        switch (err) /* set by the last call to WriteFile */
        {
        case NO_ERROR:
        case ERROR_IO_PENDING:
        {
            switch (WaitForSingleObject(overlap.hEvent, tm == NULL ? INFINITE : tm -> mS ))
            {
            case WAIT_TIMEOUT:
                pLogLibMsg(klogInfo, "$(b): timed out ", "b=%p", base);
                CloseHandle(overlap.hEvent);
                return RC ( rcNS, rcStream, rcWriting, rcTimeout, rcExhausted );

            case WAIT_OBJECT_0:
            {
                pLogLibMsg(klogInfo, "$(b): successful", "b=%p", base);   
                if (GetOverlappedResult(self->pipe, &overlap, &count, TRUE)) /* wait to complete if necessary */
                {
                    pLogLibMsg(klogInfo, "$(b): $(c) bytes written", "b=%p,c=%d", base, count);   
                    assert ( num_writ != NULL );
                    * num_writ = count;
                    CloseHandle(overlap.hEvent);
                    return 0;
                }
                rc = HandleErrno();
                pLogLibMsg(klogInfo, "$(b): GetOverlappedResult() failed", "b=%p", base);         
                break;
            }
            
            default:
                rc = HandleErrno();
                pLogLibMsg(klogInfo, "$(b): WaitForSingleObject() failed", "b=%p", base);         
                break;
            }
        }
        case ERROR_NO_DATA:
            /* the secret MS lore says when WriteFile to a pipe returns ERROR_NO_DATA, it's 
                "Pipe was closed (normal exit path)." - see http://support.microsoft.com/kb/190351 */
            CloseHandle(overlap.hEvent);
            return 0;

        default:
            rc = HandleErrno();
            pLogLibMsg(klogInfo, "$(b): WriteFile() failed", "b=%p", base);            
            break;
        }

        CloseHandle(overlap.hEvent);
    }
    else
        rc = HandleErrno();
        
    return rc;
}

static
rc_t CC KIPCSocketWrite ( KSocket *self,
    const void *buffer, size_t bsize, size_t *num_writ )
{
    timeout_t tm;
    assert ( self != NULL );

    if ( self -> write_timeout < 0 )
        return KIPCSocketTimedWrite ( self, buffer, bsize, num_writ, NULL );

    TimeoutInit ( & tm, self -> write_timeout );
    return KIPCSocketTimedWrite ( self, buffer, bsize, num_writ, & tm );
}

static KStream_vt_v1 vtKIPCSocket =
{
    1, 1,
    KIPCSocketWhack,
    KIPCSocketRead,
    KIPCSocketWrite,
    KIPCSocketTimedRead,
    KIPCSocketTimedWrite
};

static
rc_t KNSManagerMakeIPCConnection ( struct KNSManager const *self, 
                                   KStream **out, 
                                   const KEndPoint *to, 
                                   int32_t retryTimeout, 
                                   int32_t readMillis, 
                                   int32_t writeMillis)
{
    uint8_t retry_count = 0;
    rc_t rc = 0;
    char pipename[256];
    wchar_t pipenameW[256];
    size_t num_writ;
    
    assert ( self != NULL );
    assert ( out != NULL );

    * out = NULL;

    assert ( to != NULL );
    assert ( to->type == epIPC );

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
                               FILE_FLAG_OVERLAPPED,  /* using overlapped IO */
                               NULL);          /* no template file */
        if ( h != INVALID_HANDLE_VALUE )
        {   /* create the KSocket */
            DWORD dwMode = PIPE_READMODE_MESSAGE | PIPE_NOWAIT; /* need NOWAIT if pipe is created in asynch mode */
            if (SetNamedPipeHandleState( 
                  h,        /* pipe handle */
                  &dwMode,  /* new pipe mode */
                  NULL,     /* don't set maximum bytes */
                  NULL))    /* don't set maximum time */
            {
                KIPCSocket* ksock = calloc ( sizeof *ksock, 1 );

                if ( ksock == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                else
                {   
                    
                    /* initialize the KSocket */
                    rc = KStreamInit ( & ksock -> dad . dad, ( const KStream_vt* ) & vtKIPCSocket,
                                       "KSocket", "tcp", true, true );
                    if ( rc == 0 )
                    {
                        pLogLibMsg(klogInfo, "$(b): KNSManagerMakeIPCConnection($(e),'$(n)')", "b=%p,e=%p,n=%s", ksock, to, pipename);
                    
                        ksock -> dad . read_timeout  = readMillis;
                        ksock -> dad . write_timeout = writeMillis;
                        ksock -> type = isIpcPipeClient;
                        ksock -> pipe = h;
                        *out = & ksock -> dad . dad;
                        return 0;
                    }
                    free ( ksock );
                }
            }
            else
                rc = HandleErrno();
        }
        else /* CreateFileW failed */
        {
            switch (GetLastError())
            {
            case ERROR_PIPE_BUSY:
                LogLibMsg(klogInfo, "KNSManagerMakeIPCConnection: pipe busy, retrying");
                {
                    BOOL pipeAvailable = WaitNamedPipeW(pipenameW, NMPWAIT_USE_DEFAULT_WAIT);
                    if (pipeAvailable)
                    {
                        LogLibMsg(klogInfo, "KNSManagerMakeIPCConnection: WaitNamedPipeW returned TRUE");
                        continue;
                    }
                    /* time-out, try again */
                    rc = HandleErrno();
                    LogLibMsg(klogInfo, "KNSManagerMakeIPCConnection: WaitNamedPipeW returned FALSE(timeout)");
                    if ( retryTimeout < 0 || retry_count < retryTimeout )
                    {
                        Sleep(1000); /*ms*/
                        ++retry_count;
                        rc = 0;
                        continue;   
                    }
                }
                break;
                
            case (ERROR_FILE_NOT_FOUND):
                if ( retryTimeout < 0 || retry_count < retryTimeout )
                {
                    LogLibMsg(klogInfo, "KNSManagerMakeIPCConnection: pipe not found, retrying");
                    Sleep(1000); /*ms*/
                    ++retry_count;
                    rc = 0;
                    continue;
                }
                else
                    rc = HandleErrno();
                break;
                
            default:
                rc = HandleErrno();
                break;
            }
        }
        break;
    }
    return rc;
}

static
rc_t KNSManagerMakeIPCListener( struct KNSManager const *self, struct KSocket** out, struct KEndPoint const * ep )
{   
    rc_t rc = 0;
    KIPCSocket* ksock;

    assert ( self != NULL );
    assert ( out != NULL );

    * out = NULL;

    assert ( ep != NULL );
    assert (ep->type == epIPC);
    
    /* use named pipes to implement unix domain socket - like behavior */
    ksock = calloc ( sizeof *ksock, 1 );
    if ( ksock == NULL )
        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
    else
    {
        rc = KStreamInit ( & ksock -> dad . dad, ( const KStream_vt* ) & vtKIPCSocket,
                           "KSocket", "tcp", true, true );
        if ( rc == 0 )
        {
            size_t num_writ;
            char pipename[256];
            rc = string_printf(pipename, sizeof(pipename), &num_writ, "\\\\.\\pipe\\%s", ep->u.ipc_name);
            if (rc == 0)
            {
                string_cvt_wchar_copy(ksock->pipename, sizeof(ksock->pipename), pipename, num_writ);
                
                ksock -> type = isIpcListener;
                ksock -> listenerPipe = INVALID_HANDLE_VALUE;
                *out = & ksock -> dad;
                
                pLogLibMsg(klogInfo, "$(b): KNSManagerMakeIPCListener($(e),'$(n)')", "b=%p,e=%p,n=%s", ksock, ep, pipename);
                return 0;
            }
            KIPCSocketWhack( & ksock -> dad );
        }
        else
            free ( ksock );
    }
        
    pLogLibMsg(klogInfo, "$(b): KNSManagerMakeIPCListener failed", "b=%p", ksock);
    return rc;
}

/*
 * Entry points
*/
LIB_EXPORT 
rc_t CC KSocketAccept ( struct KSocket *listenerBase, struct KStream **out )
{
    rc_t rc = 0;
    KIPCSocket* listener = (KIPCSocket*)listenerBase;
    pLogLibMsg(klogInfo, "$(b): KSocketAccept", "b=%p", listener);

    if ( listener == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull);
        
    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    /* make sure listener points to a KIPCSocket */
    if (listener -> dad . dad . vt -> v1 . destroy != KIPCSocketWhack)
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);

    if (listener->type != isIpcListener)
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);
        
    listener->listenerPipe = CreateNamedPipeW(listener->pipename,    /* pipe name */
                                              FILE_FLAG_OVERLAPPED |   /* using overlapped IO */
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
        OVERLAPPED overlap;
        LogLibMsg(klogInfo, "KSocketAccept: calling CreateEvent");
        overlap.hEvent = CreateEvent( 
                                  NULL,    /* default security attribute */
                                  TRUE,    /* manual reset event */
                                  FALSE,    /* initial state = nonsignalled */
                                  NULL); 
        if (overlap.hEvent != NULL)
        {
            BOOL connected =  ConnectNamedPipe(listener->listenerPipe, &overlap);
            /*LogLibMsg(klogInfo, "KSocketAccept: calling ConnectNamedPipe");*/
            if ( !connected ) /* normal for asynch mode */
            {
                switch (GetLastError())
                {
                case ERROR_PIPE_CONNECTED: /* client connected since the call to CreateNamedPipeW */
                    break;
                    
                case ERROR_IO_PENDING:
                    LogLibMsg(klogInfo, "KSocketAccept: calling WaitForSingleObject");
                    if (WaitForSingleObject(overlap.hEvent, INFINITE) != WAIT_OBJECT_0)
                    {
                        rc = HandleErrno();
                        CloseHandle(overlap.hEvent);
                        LogLibMsg(klogInfo, "KSocketAccept: WaitForSingleObject failed");
                        return rc;
                    }
                    break;
                    
                default:
                    rc = HandleErrno();
                    CloseHandle(overlap.hEvent);
                    LogLibMsg(klogInfo, "KSocketAccept: ConnectNamedPipe failed");
                    return rc;
                }
            }
            /* we are connected, create the socket stream */
            {
                KIPCSocket *ksock = calloc ( sizeof *ksock, 1 );
                pLogLibMsg(klogInfo, "$(b): KSocketAccept", "b=%p", ksock);
                
                if ( ksock == NULL )
                {
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                    LogLibMsg(klogInfo, "KSocketAccept: calloc failed");
                }
                else
                {
                    rc = KStreamInit ( & ksock -> dad . dad, ( const KStream_vt* ) & vtKIPCSocket,
                                       "KSocket", "tcp", true, true );
                    if ( rc == 0 )
                    {
                        ksock -> type = isIpcPipeServer;
                        ksock -> pipe = listener->listenerPipe;
                        listener->listenerPipe = INVALID_HANDLE_VALUE; /* this is only to be used while ConnectNamedPipe() is in progress */
                        *out = & ksock -> dad . dad;
                        CloseHandle(overlap.hEvent);
                        return 0;
                    }
                    free ( ksock );
                    LogLibMsg(klogInfo, "KSocketAccept: KStreamInit failed");
                }
                CloseHandle(overlap.hEvent);
                return rc;
            }
        }
    }
    else
    {
        rc = HandleErrno();
        LogLibMsg(klogInfo, "KSocketAccept: CreateNamedPipeW failed");
    }
    return rc;
}   

LIB_EXPORT rc_t CC KNSManagerMakeRetryTimedConnection ( const KNSManager * self,
    KStream **out, int32_t retryTimeout, int32_t readMillis, int32_t writeMillis,
    const KEndPoint *from, const KEndPoint *to )
{
    rc_t rc;

    if ( out == NULL )
        rc = RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );
        else if ( to == NULL )
            rc = RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );
        else if ( from != NULL && from -> type != to -> type )
            rc = RC ( rcNS, rcStream, rcConstructing, rcParam, rcIncorrect );
        else
        {
            switch ( to -> type )
            {
            case epIPV4:
                rc = KNSManagerMakeIPv4Connection ( self, out, from, to, retryTimeout, readMillis, writeMillis );
                break;
            case epIPC:
                rc = KNSManagerMakeIPCConnection ( self, out, to, retryTimeout, readMillis, writeMillis );
                break;
            default:
                rc = RC ( rcNS, rcStream, rcConstructing, rcParam, rcIncorrect );
            }

            if ( rc == 0 )
                return 0;
        }

        * out = NULL;
    }

    return rc;
}

LIB_EXPORT
rc_t CC KNSManagerMakeListener( struct KNSManager const *self, struct KSocket** out, struct KEndPoint const * ep )
{
    rc_t rc;

    if ( out == NULL )
        rc = RC ( rcNS, rcSocket, rcConstructing, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcNS, rcSocket, rcConstructing, rcSelf, rcNull );
        else if ( ep == NULL )
            rc = RC ( rcNS, rcSocket, rcConstructing, rcParam, rcNull );
        else
        {
            switch ( ep -> type )
            {
            case epIPV4:
                rc = KNSManagerMakeIPv4Listener ( self, out, ep );
                break;
            case epIPC:
                rc = KNSManagerMakeIPCListener ( self, out, ep );
                break;
            default:
                rc = RC ( rcNS, rcSocket, rcConstructing, rcParam, rcIncorrect );
            }

            if ( rc == 0 )
                return 0;
        }

        * out = NULL;
    }

    return rc;
}

/* 
 * Local helpers
*/
static
rc_t HandleErrnoEx ( const char *func_name, unsigned int lineno )
{
    rc_t rc;
    int lerrno = WSAGetLastError();

    switch ( lerrno )
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
    case ERROR_SEM_TIMEOUT:
        rc = RC ( rcNS, rcStream, rcReading, rcTimeout, rcExhausted );
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
    case WSAEPROTOTYPE: /* wrong type of protocol for this socket */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAEPROVIDERFAILEDINIT: /* service provider failed to initialize */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        break;
    case ERROR_BROKEN_PIPE:
    case WSAESHUTDOWN: /* socket had been shutdown */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAESOCKTNOSUPPORT: /* specified socket type is not supported */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUnsupported );
        break;
    case WSAETIMEDOUT: /* connection dropped because of network failure */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case WSAEWOULDBLOCK: /* socket is marked as non-blocking but the recv operation
                            would block */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcError );
        break;

    case WSAEINTR: /* call was cancelled */
    case WSAEMFILE: /* no more socket fd available */
    default:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        PLOGERR (klogErr,
                 (klogErr, rc, "unknown system error '$(S)($(E))', line=$(L)",
                  "S=%!,E=%d,L=%d", lerrno, lerrno, lineno));
    }
    return rc;
}

