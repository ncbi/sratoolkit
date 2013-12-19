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
#include "kns_mgr_priv.h"

#include <klib/rc.h>

#include <WINDOWS.H>

rc_t KNSManagerInit ( struct KNSManager *self )
{
    rc_t rc = 0;
    WSADATA wsaData;

    if ( WSAStartup ( MAKEWORD ( 2, 2 ), &wsaData ) != 0 )
    {
        int lerrno;
        switch ( lerrno = WSAGetLastError () )
        {
        case WSASYSNOTREADY:
        case WSAVERNOTSUPPORTED:
        case WSAEINPROGRESS:
        case WSAEPROCLIM:
        case WSAEFAULT:
        default:
            rc = RC ( rcNS, rcNoTarg, rcInitializing, rcNoObj, rcError );
        }
    }
    return rc;
} 

void KNSManagerCleanup ( struct KNSManager *self )
{
    WSACleanup ();
}
