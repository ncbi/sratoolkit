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

#include <kns/extern.h>
#include <kns/manager.h>
#include <kns/socket.h>
#include <kns/impl.h>
#include <kns/endpoint.h>
#include <klib/rc.h>
#include <klib/log.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/out.h>

#include "stream-priv.h"

#include <sysalloc.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>

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
    int fd;
    char* path;
};

LIB_EXPORT rc_t CC KSocketAddRef( struct KSocket *self )
{
    return KStreamAddRef(&self->dad);
}

LIB_EXPORT rc_t CC KSocketRelease( struct KSocket *self )
{
    return KStreamRelease(&self->dad);
}

LIB_EXPORT
rc_t CC KSocketWhack ( KSocket *self )
{

    assert ( self != NULL );

    shutdown ( self -> fd, SHUT_WR );
    
    while ( 1 ) 
    {
        char buffer [ 1024 ];
        ssize_t result = recv ( self -> fd, buffer, sizeof buffer, MSG_DONTWAIT );
        if ( result <= 0 )
            break;
    }
    shutdown ( self ->fd, SHUT_RD );

    close ( self -> fd );

    if (self->path)
    {
        unlink(self->path);
        free(self->path);
    }
        
    free ( self );

    return 0;
}

static
rc_t HandleErrno ( const char *func_name, int lineno )
{
    int lerrno;
    rc_t rc = 0;
    
    switch ( lerrno = errno )
    {
    case EACCES: /* write permission denied */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcUnauthorized );            
        break;
    case EADDRINUSE: /* address is already in use */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcExists );
        break;
    case EADDRNOTAVAIL: /* requested address was not local */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcNotFound );
        break;
    case EAGAIN: /* no more free local ports or insufficient rentries in routing cache */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcExhausted );            
        break;
    case EAFNOSUPPORT: /* address didnt have correct address family in ss_family field */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcName, rcError );            
        break;
    case EALREADY: /* socket is non blocking and a previous connection has not yet completed */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUndefined );
        break;
    case EBADF: /* invalid sock fd */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInvalid );
        break;
    case ECONNREFUSED: /* remote host refused to allow network connection */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case ECONNRESET: /* connection reset by peer */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case EDESTADDRREQ: /* socket is not connection-mode and no peer address set */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInvalid );
        break;
    case EFAULT: /* buffer pointer points outside of process's adress space */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcOutofrange );
        break;
    case EINPROGRESS: /* call is in progress */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcUndefined );
        break;
    case EINTR: /* recv interrupted before any data available */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case EINVAL: /* invalid argument */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcParam, rcInvalid );
        break;
    case EISCONN: /* connected already */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcExists );
        break;
    case ELOOP: /* too many symbolic links in resolving addr */
        rc = RC ( rcNS, rcNoTarg, rcResolving, rcLink, rcExcessive );
        break;
    case EMFILE: /* process file table overflow */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        break;
    case EMSGSIZE: /* msg size too big */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMessage, rcExcessive );
        break;
    case ENAMETOOLONG: /* addr name is too long */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcName, rcExcessive );
        break;
    case ENETUNREACH: /* network is unreachable */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcNotAvailable );
        break;
    case ENOBUFS: /* output queue for a network connection was full. 
                     ( wont typically happen in linux. Packets are just silently dropped */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcInterrupted );
        break;
    case ENOENT: /* file does not exist */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcNotFound );
        break;
    case ENOMEM: /* Could not allocate memory */
        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcError );
        break;
    case ENOTCONN: /* socket has not been connected */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcInvalid );
        break;
    case ENOTDIR: /* component of path is not a directory */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcDirEntry, rcError );
        break;
    case ENOTSOCK: /* sock fd does not refer to socket */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcInvalid );
        break;
    case EOPNOTSUPP: /* bits in flags argument is inappropriate */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcParam, rcInvalid );
        break;
    case EPERM:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcMemory, rcUnauthorized );            
        break;
    case EPIPE: /* local end has been shut down. Will also receive SIGPIPE or MSG_NOSIGNAL */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcCanceled );
        break;
    case EPROTONOSUPPORT: /* specified protocol is not supported */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        break;
    case EROFS: /* socket inode on read only file system */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcReadonly );
        break;
    case ETIMEDOUT: /* timeout */
        rc = RC ( rcNS, rcNoTarg, rcReading, rcConnection, rcNotAvailable );
        break;
#if ! defined EAGAIN || ! defined EWOULDBLOCK || EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcId, rcError );
        break;
#endif
    default:
        rc = RC ( rcNS, rcNoTarg, rcReading, rcNoObj, rcError );
        PLOGERR (klogErr,
                 (klogErr, rc, "unknown system error '$(S)($(E))'",
                  "S=%!,E=%d", lerrno, lerrno));
    }
    
#if _DEBUGGING
    if ( rc != 0 )
        pLogMsg ( klogInfo, "$(RC)\n", "RC=%R", rc );
#endif

    return rc;
}

static
int socket_wait ( int fd, int events, int timeout )
{
    int i;
    struct pollfd fds [ 1 ];
        
    /* poll for data with no delay */
    for ( i = 0; i < 2; ++ i )
    {
        fds [ 0 ] . fd = fd;
        fds [ 0 ] . events = events;
        fds [ 0 ] . revents = 0;
        
        if ( poll ( fds, sizeof fds / sizeof fds [ 0 ], 0 ) != 0 )
            return fds [ 0 ] . revents;
    }
        
    /* poll for data */
    if ( timeout != 0 )
    {
        fds [ 0 ] . fd = fd;
        fds [ 0 ] . events = events;
        fds [ 0 ] . revents = 0;
        
        if ( poll ( fds, sizeof fds / sizeof fds [ 0 ], timeout ) != 0 )
        {
            /* experiment - try another poll with no timeout */
            fds [ 0 ] . fd = fd;
            fds [ 0 ] . events = events;
            fds [ 0 ] . revents = 0;
            poll ( fds, sizeof fds / sizeof fds [ 0 ], 0 );

            return fds [ 0 ] . revents;
        }
    }
    
    return 0;
}

static
rc_t CC KSocketRead ( const KSocket *self,
    void *buffer, size_t bsize, size_t *num_read )
{
    rc_t rc = 0;
    int timeout = 1000;
    
    size_t total = 0;
    char *b = buffer;
    
    assert ( self != NULL );

    while ( 1 )
    {
        ssize_t count = 0;
        
        /* wait for socket to become readable */
        int revents = socket_wait ( self -> fd, POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND, timeout );
        if ( revents != 0 )
            count = recv ( self -> fd, & b [ total ], bsize - total, 0 );
        if ( count <= 0 )
        {
            if ( total != 0 || count == 0 )
            {
                assert ( num_read != NULL );
                * num_read = total;
                return 0;
            }
            
            if ( errno != EAGAIN )
            {
                rc = HandleErrno ( __func__, __LINE__ );
                if ( rc != 0 )
                    break;
            }
        }
        else
        {
            total += count;
            timeout = 0;
        }
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
        
        /* wait for socket to become writable */
        socket_wait ( self -> fd, POLLOUT | POLLWRNORM | POLLWRBAND, 1000 );

        count = send ( self -> fd, buffer, bsize, 0 );
        if ( count >= 0 )
        {
            assert ( num_writ != NULL );
            * num_writ = count;
            return 0;
        }
        
        rc = HandleErrno ( __func__, __LINE__ );
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
rc_t MakeSocketPath(const char* name, char* buf, size_t buf_size)
{
    size_t num_writ;
    /*struct passwd* pwd;
    pwd = getpwuid(geteuid());
    if (pwd == NULL)
        return HandleErrno();
    return string_printf(buf, buf_size, &num_writ, "%s/.ncbi/%s", pwd->pw_dir, name);*/
    return string_printf(buf, buf_size, &num_writ, "%s/.ncbi/%s", getenv("HOME"), name);
}

LIB_EXPORT
rc_t CC KNSManagerMakeConnection ( struct KNSManager const * self,KStream **out, const KEndPoint *from, const KEndPoint *to )
{
    rc_t rc;
    int fd;

    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );
        
    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( to == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if ( (from != NULL && from->type != epIPV4) || to->type != epIPV4 )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);
        
    /* create the OS socket */
    fd = socket ( AF_INET, SOCK_STREAM, 0 );
    if ( fd != -1 )
    {
        int flag;
        struct sockaddr_in ss;
        memset ( &ss, 0, sizeof ss );
        ss . sin_family = AF_INET;
        if ( from != NULL )
        {
            ss . sin_port = htons ( from -> u . ipv4 . port );
            ss . sin_addr . s_addr = htonl ( from -> u . ipv4 . addr );
        }
        
        /* disable nagle algorithm */
        flag = 1;
        setsockopt ( fd, IPPROTO_TCP, TCP_NODELAY, ( char* ) & flag, sizeof flag );
        
        
        if ( from == NULL || bind ( fd, ( struct sockaddr *) & ss, sizeof ss ) == 0) 
        {
            memset ( & ss, 0, sizeof ss );
            ss . sin_family = AF_INET;
            ss . sin_port = htons ( to -> u . ipv4 . port );
            ss . sin_addr . s_addr = htonl ( to -> u . ipv4 . addr );                   
            if ( connect ( fd, (struct sockaddr *)&ss, sizeof ss ) == 0 )
            {
                /* create the KSocket */
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
                        /* set non-blocking mode */
                        flag = fcntl ( fd, F_GETFL );
                        fcntl ( fd, F_SETFL, flag | O_NONBLOCK );
        
                        ksock -> fd = fd;
                        *out = & ksock -> dad;
                        return 0;
                    }
                    /* free the KSocket */
                    free ( ksock );
                }
            }
            else /* connect() failed */
                rc = HandleErrno( __func__, __LINE__ );
        }
        else /* bind() failed */
            rc = HandleErrno( __func__, __LINE__ );
        close(fd);
    }
    else /* socket() failed */
        rc = HandleErrno( __func__, __LINE__ );
    return rc;
}

LIB_EXPORT
rc_t CC KNSManagerMakeIPCConnection ( struct KNSManager const *self, KStream **out, const KEndPoint *to, uint32_t max_retries )
{
    rc_t rc;
    uint32_t retry_count = 0;
    struct sockaddr_un ss;

    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );
    
    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( to == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if ( to->type != epIPC )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);

    memset ( & ss, 0, sizeof ss );
    ss.sun_family = AF_UNIX;
    rc = MakeSocketPath(to->u.ipc_name, ss.sun_path, sizeof(ss.sun_path));
    /* create the OS socket */
    while (rc == 0)
    {
        int fd = socket ( AF_UNIX, SOCK_STREAM, 0 );
        if ( fd != -1 )
        {
            if ( connect ( fd, (struct sockaddr *)&ss, sizeof(ss) ) == 0 )
            {   /* create the KSocket */
                KSocket *ksock = calloc ( sizeof *ksock, 1 );
                if ( ksock == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcNoObj, rcNull ); 
                else
                {   /* initialize the KSocket */
                    rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                                       "KSocket", "tcp", true, true );
                    if ( rc == 0 )
                    {
                        ksock -> fd = fd;
                        *out = & ksock -> dad;
                        return 0;
                    }
                    free(ksock);
                }
            }
            else
            {
                rc = HandleErrno( __func__, __LINE__ );
                close(fd);
                
                if ( retry_count < max_retries && 
                    (GetRCState(rc) == rcCanceled || GetRCState(rc) == rcNotFound) )
                {   
                    sleep(1);
                    ++retry_count;
                    rc = 0;
                    continue;
                }
            }
        }
        else /* socket() failed */
            rc = HandleErrno( __func__, __LINE__ );
        break;
    }
    return rc;
}

LIB_EXPORT
rc_t CC KNSManagerMakeListener( struct KNSManager const *self, struct KSocket** out, struct KEndPoint const * ep )
{   
    int fd;
    rc_t rc;

    if ( self == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcSelf, rcNull );

    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( ep == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcNull);

    if ( ep->type != epIPC )
        return RC ( rcNS, rcNoTarg, rcValidating, rcParam, rcInvalid);
        
     /* create the OS socket */
    fd = socket ( AF_UNIX, SOCK_STREAM, 0 );
    if ( fd >= 0 )
    {
        struct sockaddr_un ss;
        memset ( & ss, 0, sizeof ss );
        ss.sun_family = AF_UNIX;
        rc = MakeSocketPath(ep->u.ipc_name, ss.sun_path, sizeof(ss.sun_path));
        if (rc == 0)
        {
            unlink(ss.sun_path);
            if ( bind( fd,(struct sockaddr *)&ss, sizeof(ss) ) == 0 )
            {   /* create the KSocket */
                KSocket *ksock = calloc ( sizeof *ksock, 1 );
                if ( ksock == NULL )
                    rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcExhausted ); 
                else 
                {
                    char* path = string_dup(ss.sun_path, string_measure(ss.sun_path, NULL));
                    if (path == NULL)
                        rc = RC ( rcNS, rcNoTarg, rcAllocating, rcMemory, rcExhausted ); 
                    else
                    {   /* initialize the KSocket */
                        rc = KStreamInit ( & ksock -> dad, ( const KStream_vt* ) & vtKSocket,
                                           "KSocket", "tcp", true, true );
                        if ( rc == 0 )
                        {
                            ksock -> fd = fd;
                            ksock -> path = path;
                            *out = ksock;
                            return 0;
                        }
                        free(path);
                    }
                    free(ksock);
                    return rc;
                }
            }
            else /* bind() failed */
                rc = HandleErrno( __func__, __LINE__ );
        }
        close(fd);
    }
    else /* socket() failed */
        rc = HandleErrno( __func__, __LINE__ );
        
    return rc;
}

LIB_EXPORT 
rc_t CC KSocketListen ( struct KSocket *listener, struct KStream **out )
{
    rc_t rc;
    
    if ( listener == NULL )
        return RC ( rcNS, rcNoTarg, rcValidating, rcSelf, rcNull);

    if ( out == NULL )
        return RC ( rcNS, rcStream, rcConstructing, rcParam, rcNull );

    * out = NULL;

    if ( listen(listener->fd, 5) == 0)
    {
        struct sockaddr_un remote;
        socklen_t t = sizeof(remote);
        int fd = accept(listener->fd, (struct sockaddr *)&remote, &t);
        
        if ( fd != -1) 
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
                    ksock -> fd = fd;
                    *out = &ksock->dad;
                    return 0;
                }
                free(ksock);
            }
            close(fd);
        }
        else /* accept() failed */
            rc = HandleErrno( __func__, __LINE__ );
    }
    else /* listen() failed */
        rc = HandleErrno( __func__, __LINE__ );
    return rc;
}   
    
    
    
