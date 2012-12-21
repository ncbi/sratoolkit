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

#include <ktst/unit_test_suite.hpp>

#include <csignal> 
#include <windows.h>
#include <process.h>

using namespace ncbi::NK;

/* signal handlers for a single-test case thread */
void CC SigSubHandler(int sig)
{
    _endthreadex(sig);
}
void CC TermSubHandler() 
{
    SigSubHandler(SIGTERM);
}

struct TestCaseCall
{
    TestCaseCall(TestCase& obj, void(TestCase::*meth)())
        : object(&obj), method(meth)
    {
    }

    TestCase* object;
    void(TestCase::*method)();
};

void ThreadProc(void* call)
{
    signal(SIGABRT, SigSubHandler);
    signal(SIGFPE, SigSubHandler);
    signal(SIGILL, SigSubHandler);
    signal(SIGINT, SigSubHandler);
    signal(SIGSEGV, SigSubHandler);
    signal(SIGTERM, SigSubHandler);
    set_terminate(TermSubHandler);

    try
    {
        TestCaseCall* c=(TestCaseCall*)call;
        ((c->object)->*(c->method))();
    }
    catch (...)
    {
        _endthreadex(TestEnv::TEST_CASE_FAILED);
    }
    _endthreadex(0);
}

int TestEnv::RunProcessTestCase(TestCase& obj, void(TestCase::*meth)(), int timeout)
{
    TestCaseCall call(obj, meth);
    HANDLE thread = (HANDLE)_beginthread( ThreadProc, 0, &call );
    if (thread == NULL)
    {
        _REPORT_CRITICAL_ERROR_("TestEnv::RunProcessTestCase: failed to start a test case thread", __FILE__, __LINE__, true);
    }

    // make sure to restore main process's signal handlers before throwing an exception
#define CALL_FAILED(call)\
    _REPORT_CRITICAL_ERROR_("TestEnv::RunProcessTestCase: " call " failed", __FILE__, __LINE__, true);

    DWORD rc=0;
    DWORD result=WaitForSingleObject( (HANDLE)thread, timeout == 0 ? INFINITE : timeout*1000);
    try
    {
        switch (result)
        {
        case WAIT_OBJECT_0:
            if (GetExitCodeThread(thread, &rc) == 0)
            {
                CALL_FAILED("GetExitCodeThread");
            }
            break;
        case WAIT_TIMEOUT:
            if (!CloseHandle(thread))
            {
                CALL_FAILED("GetExitCodeThread");
            }
            rc=TEST_CASE_TIMED_OUT;
            break;
        default:
            CALL_FAILED("WaitForSingleObject");
            break;
        }
    }
    catch(...)
    {
        set_handlers(); 
        throw;
    }
#undef CALL_FAILED
    set_handlers(); 
    return (int)rc;
}

unsigned int TestEnv::Sleep(unsigned int seconds)
{
    ::Sleep(seconds*1000);
    return 0;
}

void TestEnv::set_handlers(void) 
{
    signal(SIGABRT, SigHandler);
    signal(SIGFPE, SigHandler);
    signal(SIGILL, SigHandler);
    signal(SIGINT, SigHandler);
    signal(SIGSEGV, SigHandler);
    signal(SIGTERM, SigHandler);
    set_terminate(TermHandler);
}

