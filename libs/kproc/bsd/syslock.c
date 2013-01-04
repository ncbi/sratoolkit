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

#include <kproc/extern.h>
#include "syslock-priv.h"
#include "syscond-priv.h"
#include <kproc/timeout.h>
#include <kproc/lock.h>
#include <kproc/cond.h>
#include <os-native.h>
#include <kproc/lock.h>
#include <klib/rc.h>
#include <sysalloc.h>

#include <stdlib.h>
#include <errno.h>


/*--------------------------------------------------------------------------
 * pthread_mutex
 */
static
rc_t pthread_mutex_whack ( pthread_mutex_t *mutex )
{
    int status = pthread_mutex_destroy ( mutex );
    switch ( status )
    {
    case 0:
        break;
    case EBUSY:
        return RC ( rcPS, rcLock, rcDestroying, rcLock, rcBusy );
    case EINVAL:
        return RC ( rcPS, rcLock, rcDestroying, rcLock, rcInvalid );
    default:
        return RC ( rcPS, rcLock, rcDestroying, rcNoObj, rcUnknown );
    }
    return 0;
}

static
rc_t pthread_mutex_acquire ( pthread_mutex_t *mutex )
{
    int status = pthread_mutex_lock ( mutex );
    switch ( status )
    {
    case 0:
        break;
    case EDEADLK:
        return RC ( rcPS, rcLock, rcLocking, rcThread, rcDeadlock );
    case EINVAL:
        return RC ( rcPS, rcLock, rcLocking, rcLock, rcInvalid );
    default:
        return RC ( rcPS, rcLock, rcLocking, rcNoObj, rcUnknown );
    }
    return 0;
}

static
rc_t pthread_mutex_release ( pthread_mutex_t *mutex )
{
    int status = pthread_mutex_unlock ( mutex );
    switch ( status )
    {
    case 0:
        break;
    case EPERM:
        return RC ( rcPS, rcLock, rcUnlocking, rcThread, rcIncorrect );
    case EINVAL:
        return RC ( rcPS, rcLock, rcUnlocking, rcLock, rcInvalid );
    default:
        return RC ( rcPS, rcLock, rcUnlocking, rcNoObj, rcUnknown );
    }
    return 0;
}


/*--------------------------------------------------------------------------
 * pthread_cond
 */
static
rc_t pthread_cond_whack ( pthread_cond_t *cond )
{
    int status = pthread_cond_destroy ( cond );
    switch ( status )
    {
    case 0:
        break;
    case EBUSY:
        return RC ( rcPS, rcLock, rcDestroying, rcCondition, rcBusy );
    case EINVAL:
        return RC ( rcPS, rcLock, rcDestroying, rcCondition, rcInvalid );
    default:
        return RC ( rcPS, rcLock, rcDestroying, rcNoObj, rcUnknown );
    }
    return 0;
}

static
rc_t pthread_cond_ping ( pthread_cond_t *cond )
{
    int status = pthread_cond_signal ( cond );
    switch ( status )
    {
    case 0:
        break;
    default:
        return RC ( rcPS, rcLock, rcSignaling, rcCondition, rcUnknown );
    }
    return 0;
}


/*--------------------------------------------------------------------------
 * KLock
 *  a POSIX-style mutual exclusion lock
 */

/* Destroy
 */
static
rc_t KLockDestroy ( KLock *self )
{
    rc_t rc = pthread_mutex_whack ( & self -> mutex );
    if ( rc == 0 )
    {
        pthread_mutex_whack ( & self -> cond_lock );
        pthread_cond_whack ( & self -> cond );
    }
    return rc;
}

/* Whack
 */
static
rc_t KLockWhack ( KLock *self )
{
    rc_t rc = KLockDestroy ( self );
    if ( rc == 0 )
        free ( self );
    return rc;
}

/* Init
 */
static
rc_t KLockInit ( KLock *self )
{
    int status = pthread_mutex_init ( & self -> mutex, NULL );
    if ( status == 0 )
    {
        status = pthread_mutex_init ( & self -> cond_lock, NULL );
        if ( status == 0 )
        {
            status = pthread_cond_init ( & self -> cond, NULL );
            if ( status == 0 )
            {
                self -> waiters = 0;
                atomic32_set ( & self -> refcount, 1 );
                return 0;
            }

            pthread_mutex_destroy ( & self -> cond_lock );
        }

        pthread_mutex_destroy ( & self -> mutex );
    }

    switch ( status )
    {
    case EAGAIN:
        return RC ( rcPS, rcLock, rcConstructing, rcResources, rcInsufficient );
    case ENOMEM:
        return RC ( rcPS, rcLock, rcConstructing, rcMemory, rcInsufficient );
    }

    return RC ( rcPS, rcLock, rcConstructing, rcNoObj, rcUnknown );
 }


/* Make
 *  make a simple mutex
 */
LIB_EXPORT rc_t CC KLockMake ( KLock **lockp )
{
    rc_t rc;
    if ( lockp == NULL )
        rc = RC ( rcPS, rcLock, rcConstructing, rcParam, rcNull );
    else
    {
        KLock *lock = malloc ( sizeof * lock );
        if ( lock == NULL )
            rc = RC ( rcPS, rcLock, rcConstructing, rcMemory, rcExhausted );
        else
        {
            rc = KLockInit ( lock );
            if ( rc == 0 )
            {
                * lockp = lock;
                return 0;
            }

            free ( lock );
        }

        * lockp = NULL;
    }
    return rc;
}


/* AddRef
 * Release
 */
LIB_EXPORT rc_t CC KLockAddRef ( const KLock *cself )
{
    if ( cself != NULL )
        atomic32_inc ( & ( ( KLock* ) cself ) -> refcount );
    return 0;
}

LIB_EXPORT rc_t CC KLockRelease ( const KLock *cself )
{
    KLock *self = ( KLock* ) cself;
    if ( cself != NULL )
    {
        if ( atomic32_dec_and_test ( & self -> refcount ) )
        {
            atomic32_set ( & self -> refcount, 1 );
            return KLockWhack ( self );
        }
    }
    return 0;
}


/* Acquire
 *  acquires lock
 */
LIB_EXPORT rc_t CC KLockAcquire ( KLock *self )
{
    if ( self == NULL )
        return RC ( rcPS, rcLock, rcLocking, rcSelf, rcNull );

    return pthread_mutex_acquire ( & self -> mutex );
}

LIB_EXPORT rc_t CC KLockTimedAcquire ( KLock *self, timeout_t *tm )
{
    rc_t rc;
    int status;

    if ( self == NULL )
        return RC ( rcPS, rcLock, rcLocking, rcSelf, rcNull );

    status = pthread_mutex_trylock ( & self -> mutex );
    switch ( status )
    {
    case 0:
        return 0;
    case EBUSY:
        if ( tm != NULL )
            break;
        return RC ( rcPS, rcLock, rcLocking, rcLock, rcBusy );
    case EINVAL:
        return RC ( rcPS, rcLock, rcLocking, rcLock, rcInvalid );
    default:
        return RC ( rcPS, rcLock, rcLocking, rcNoObj, rcUnknown );
    }

    if ( ! tm -> prepared )
        TimeoutPrepare ( tm );

    rc = pthread_mutex_acquire ( & self -> cond_lock );
    if ( rc != 0 )
        return rc;

    ++ self -> waiters;

    while ( 1 )
    {
        status = pthread_cond_timedwait ( & self -> cond, & self -> cond_lock, & tm -> ts );
        if ( status != 0 )
        {
            switch ( status )
            {
            case ETIMEDOUT:
                rc = RC ( rcPS, rcLock, rcLocking, rcTimeout, rcExhausted );
                break;
            case EINTR:
                rc = RC ( rcPS, rcLock, rcLocking, rcThread, rcInterrupted );
                break;
            default:
                rc = RC ( rcPS, rcLock, rcLocking, rcNoObj, rcUnknown );
            }
            break;
        }

        status = pthread_mutex_trylock ( & self -> mutex );
        if ( status != EBUSY )
        {
            switch ( status )
            {
            case 0:
                break;
            case EINVAL:
                rc = RC ( rcPS, rcLock, rcLocking, rcLock, rcInvalid );
                break;
            default:
                rc = RC ( rcPS, rcLock, rcLocking, rcNoObj, rcUnknown );
            }
            break;
        }
    }

    -- self -> waiters;
    pthread_mutex_release ( & self -> cond_lock );

    return rc;
}

/* Unlock
 *  releases lock
 */
LIB_EXPORT rc_t CC KLockUnlock ( KLock *self )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcLock, rcUnlocking, rcSelf, rcNull );

    /* release the guy */
    rc = pthread_mutex_release ( & self -> mutex );
    if ( rc != 0 )
        return rc;

    /* check for and signal any waiters */
    rc = pthread_mutex_acquire ( & self -> cond_lock );
    if ( rc == 0 )
    {
        if ( self -> waiters != 0 )
            pthread_cond_ping ( & self -> cond );
        pthread_mutex_release ( & self -> cond_lock );
    }

    return 0;
}


/*--------------------------------------------------------------------------
 * KRWLock
 *  a POSIX-style read/write lock
 */
struct KRWLock
{
    KLock lock;
    KCondition rcond;
    KCondition wcond;
    uint32_t rwait;
    uint32_t wwait;
    int32_t count;
    atomic32_t refcount;
};


/* Whack
 */
static
rc_t KRWLockWhack ( KRWLock *self )
{
    rc_t rc;

    if ( self -> count || self -> rwait || self -> wwait )
        return RC ( rcPS, rcRWLock, rcDestroying, rcRWLock, rcBusy );

    rc = KLockDestroy ( & self -> lock );
    if ( rc == 0 )
    {
        KConditionDestroy ( & self -> rcond );
        KConditionDestroy ( & self -> wcond );
        free ( self );
    }

    return rc;
}


/* Make
 *  make a simple read/write lock
 */
LIB_EXPORT rc_t CC KRWLockMake ( KRWLock **lockp )
{
    rc_t rc;

    if ( lockp == NULL )
        rc = RC ( rcPS, rcRWLock, rcConstructing, rcParam, rcNull );
    else
    {
        KRWLock *lock = malloc ( sizeof * lock );
        if ( lock == NULL )
            rc = RC ( rcPS, rcRWLock, rcConstructing, rcMemory, rcExhausted );
        else
        {
            rc = KLockInit ( & lock -> lock );
            if ( rc == 0 )
            {
                rc = KConditionInit ( & lock -> rcond );
                if ( rc == 0 )
                {
                    rc = KConditionInit ( & lock -> wcond );
                    if ( rc == 0 )
                    {
                        lock -> rwait = lock -> wwait = 0;
                        lock -> count = 0;
                        atomic32_set ( & lock -> refcount, 1 );
                        * lockp = lock;
                        return 0;
                    }

                    KConditionDestroy ( & lock -> rcond );
                }

                KLockDestroy ( & lock -> lock );
            }

            free ( lock );
        }

        * lockp = NULL;
    }

    return rc;
}


/* AddRef
 * Release
 */
LIB_EXPORT rc_t CC KRWLockAddRef ( const KRWLock *cself )
{
    if ( cself != NULL )
        atomic32_inc ( & ( ( KRWLock* ) cself ) -> refcount );
    return 0;
}

LIB_EXPORT rc_t CC KRWLockRelease ( const KRWLock *cself )
{
    KRWLock *self = ( KRWLock* ) cself;
    if ( cself != NULL )
    {
        if ( atomic32_dec_and_test ( & self -> refcount ) )
        {
            atomic32_set ( & self -> refcount, 1 );
            return KRWLockWhack ( self );
        }
    }
    return 0;
}


/* AcquireShared
 *  acquires read ( shared ) lock
 */
LIB_EXPORT rc_t CC KRWLockAcquireShared ( KRWLock *self )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcRWLock, rcLocking, rcSelf, rcNull );

    rc = KLockAcquire ( & self -> lock );
    if ( rc == 0 )
    {
        ++ self -> rwait;
        while ( self -> count < 0 || self -> wwait != 0 )
        {
            rc = KConditionWait ( & self -> rcond, & self -> lock );
            if ( rc != 0 )
                break;
        }
        -- self -> rwait;

        if ( rc == 0 )
            ++ self -> count;

        KLockUnlock ( & self -> lock );
    }

    return rc;
}

LIB_EXPORT rc_t CC KRWLockTimedAcquireShared ( KRWLock *self, timeout_t *tm )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcRWLock, rcLocking, rcSelf, rcNull );

    rc = KLockTimedAcquire ( & self -> lock, tm );
    if ( rc == 0 )
    {
        ++ self -> rwait;
        while ( self -> count < 0 || self -> wwait != 0 )
        {
            rc = KConditionTimedWait ( & self -> rcond, & self -> lock, tm );
            if ( rc != 0 )
                break;
        }
        -- self -> rwait;

        if ( rc == 0 )
            ++ self -> count;

        KLockUnlock ( & self -> lock );
    }

    return rc;
}


/* AcquireExcl
 *  acquires write ( exclusive ) lock
 */
LIB_EXPORT rc_t CC KRWLockAcquireExcl ( KRWLock *self )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcRWLock, rcLocking, rcSelf, rcNull );

    rc = KLockAcquire ( & self -> lock );
    if ( rc == 0 )
    {
        ++ self -> wwait;
        while ( self -> count != 0 )
        {
            rc = KConditionWait ( & self -> wcond, & self -> lock );
            if ( rc != 0 )
                break;
        }
        -- self -> wwait;

        if ( rc == 0 )
            self -> count = -1;

        KLockUnlock ( & self -> lock );
    }

    return rc;
}

LIB_EXPORT rc_t CC KRWLockTimedAcquireExcl ( KRWLock *self, timeout_t *tm )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcRWLock, rcLocking, rcSelf, rcNull );

    rc = KLockTimedAcquire ( & self -> lock, tm );
    if ( rc == 0 )
    {
        ++ self -> wwait;
        while ( self -> count != 0 )
        {
            rc = KConditionTimedWait ( & self -> wcond, & self -> lock, tm );
            if ( rc != 0 )
                break;
        }
        -- self -> wwait;

        if ( rc == 0 )
            self -> count = -1;

        KLockUnlock ( & self -> lock );
    }

    return rc;
}


/* Unlock
 *  releases lock
 */
LIB_EXPORT rc_t CC KRWLockUnlock ( KRWLock *self )
{
    rc_t rc;

    if ( self == NULL )
        return RC ( rcPS, rcRWLock, rcUnlocking, rcSelf, rcNull );

    rc = KLockAcquire ( & self -> lock );
    if ( rc == 0 )
    {
        /* release the count */
        if ( self -> count < 0 )
            self -> count = 0;
        else if ( self -> count > 0 )
            -- self -> count;

        /* if there are writers waiting... */
        if ( self -> wwait != 0 )
        {
            /* don't bother unless the lock is free */
            if ( self -> count == 0 )
                KConditionSignal ( & self -> wcond );
        }

        /* if there are readers waiting */
        else if ( self -> rwait != 0 )
        {
            /* any number of readers can come through now */
            KConditionBroadcast ( & self -> rcond );
        }

        KLockUnlock ( & self -> lock );
    }

    return rc;
}
