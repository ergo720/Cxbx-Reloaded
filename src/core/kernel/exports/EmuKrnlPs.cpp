// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *  (c) 2016 Patrick van Logchem <pvanlogchem@gmail.com>
// *
// *  All rights reserved
// *
// ******************************************************************

#define LOG_PREFIX CXBXR_MODULE::PS
#include <common\util\CxbxUtil.h>


#include <core\kernel\exports\xboxkrnl.h> // For PsCreateSystemThreadEx, etc.
#include <process.h> // For __beginthreadex(), etc.
#include <float.h> // For _controlfp constants

#include "Logging.h" // For LOG_FUNC()
#include "EmuKrnlLogging.h"
#include "core\kernel\init\CxbxKrnl.h" // For CxbxKrnl_TLS
#include "core\kernel\support\Emu.h" // For EmuLog(LOG_LEVEL::WARNING, )
#include "core\kernel\support\EmuFS.h" // For EmuGenerateFS
#include "core\kernel\exports\EmuKrnlKe.h"
#include <map>
#include <mutex>

// prevent name collisions
namespace NtDll
{
#include "core\kernel\support\EmuNtDll.h"
};

#define PSP_MAX_CREATE_THREAD_NOTIFY 16 /* TODO : Should be 8 */

// PsCreateSystemThread proxy parameters
struct PCSTProxyParam
{
	IN xbox::PVOID m_StartRoutine;
	IN xbox::PVOID m_StartContext;
	IN xbox::PVOID m_SystemRoutine;
	IN std::condition_variable *m_Cv;
	IN std::mutex *m_Mtx;
	OUT bool *m_Ready;
	OUT xbox::PKTHREAD *m_Kthread;
	PCSTProxyParam(xbox::PVOID StartRoutine, xbox::PVOID StartContext, xbox::PVOID SystemRoutine, std::condition_variable *Cv, std::mutex *Mtx,
		bool *Ready, xbox::PKTHREAD *Kthread) : m_StartRoutine(StartRoutine), m_StartContext(StartContext), m_SystemRoutine(SystemRoutine), m_Mtx(Mtx),
		m_Cv(Cv), m_Ready(Ready), m_Kthread(Kthread) {};
};

// Global Variable(s)
extern PVOID g_pfnThreadNotification[PSP_MAX_CREATE_THREAD_NOTIFY] = { NULL };
extern int g_iThreadNotificationCount = 0;

// Separate function for logging, otherwise in PCSTProxy __try wont work (Compiler Error C2712)
void LOG_PCSTProxy
(
	PVOID StartRoutine,
	PVOID StartContext,
	PVOID SystemRoutine
)
{
	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(StartRoutine)
		LOG_FUNC_ARG(StartContext)
		LOG_FUNC_ARG(SystemRoutine)
		LOG_FUNC_END;
}

// Overload which doesn't change affinity
void InitXboxThread()
{
	// initialize FS segment selector
	EmuGenerateFS(CxbxKrnl_TLS, CxbxKrnl_TLSData);

	_controlfp(_PC_53, _MCW_PC); // Set Precision control to 53 bits (verified setting)
	_controlfp(_RC_NEAR, _MCW_RC); // Set Rounding control to near (unsure about this)
}

void InitAndRegisterXboxThread()
{
	InitXboxThread();
	xbox::PKTHREAD kthread = xbox::KeGetCurrentThread();
	RegisterThread(reinterpret_cast<xbox::PETHREAD>(kthread)->UniqueThread, kthread);
}

// PsCreateSystemThread proxy procedure
// Dxbx Note : The signature of PCSTProxy should conform to System.TThreadFunc !
static unsigned int WINAPI PCSTProxy
(
	IN PVOID Parameter
)
{
	CxbxSetThreadName("PsCreateSystemThread Proxy");

	PCSTProxyParam *iPCSTProxyParam = static_cast<PCSTProxyParam*>(Parameter);

	// Copy params to the stack so they can be freed
	PCSTProxyParam params = *iPCSTProxyParam;
	delete iPCSTProxyParam;

	LOG_PCSTProxy(
		params.m_StartRoutine,
		params.m_StartContext,
		params.m_SystemRoutine);

	std::unique_lock<std::mutex> lck(*params.m_Mtx);

	// Do minimal thread initialization
	InitXboxThread();

	xbox::PKTHREAD kthread = xbox::KeGetCurrentThread();
	*params.m_Kthread = kthread;
	*params.m_Ready = true;
	lck.unlock();
	params.m_Cv->notify_one();

	// NOTE: we cannot just call KeSuspendThread here, because the win32 SuspendThread returns before the thread is actually suspended.
	// Thus, we could be so unlucky that the thread that has spawned us calls ResumeThread before we were even suspended.
	xbox::KeWaitForSingleObject(&kthread->SuspendSemaphore, xbox::Executive, xbox::KernelMode, FALSE, xbox::zeroptr);

	auto routine = (xbox::PKSYSTEM_ROUTINE)params.m_SystemRoutine;
	// Debugging notice : When the below line shows up with an Exception dialog and a
	// message like: "Exception thrown at 0x00026190 in cxbx.exe: 0xC0000005: Access
	// violation reading location 0xFD001804.", then this is AS-DESIGNED behaviour!
	// (To avoid repetitions, uncheck "Break when this exception type is thrown").
	routine(xbox::PKSTART_ROUTINE(params.m_StartRoutine), params.m_StartContext);

	// This will also handle thread notification :
	LOG_TEST_CASE("Thread returned from SystemRoutine");
	xbox::PsTerminateSystemThread(xbox::status_success);

	return 0; // will never be reached
}

// Placeholder system function, instead of XapiThreadStartup
void PspSystemThreadStartup
(
	IN xbox::PKSTART_ROUTINE StartRoutine,
	IN PVOID StartContext
)
{
	// TODO : Call PspUnhandledExceptionInSystemThread(GetExceptionInformation())
	(StartRoutine)(StartContext);

	xbox::PsTerminateSystemThread(xbox::status_success);
}

// ******************************************************************
// * 0x00FE - PsCreateSystemThread()
// ******************************************************************
XBSYSAPI EXPORTNUM(254) xbox::ntstatus_xt NTAPI xbox::PsCreateSystemThread
(
	OUT PHANDLE         ThreadHandle,
	OUT PDWORD          ThreadId OPTIONAL,
	IN  PKSTART_ROUTINE StartRoutine,
	IN  PVOID           StartContext,
	IN  boolean_xt         DebuggerThread
)
{
	LOG_FORWARD("PsCreateSystemThreadEx");

	return PsCreateSystemThreadEx(
		/*OUT*/ThreadHandle,
		/*ThreadExtensionSize=*/0,
		/*KernelStackSize=*/KERNEL_STACK_SIZE,
		/*TlsDataSize=*/0,
		/*OUT*/ThreadId,
		/*StartRoutine=*/StartRoutine,
		StartContext,
		/*CreateSuspended=*/FALSE,
		/*DebuggerThread=*/DebuggerThread,
		/*SystemRoutine=*/PspSystemThreadStartup // instead of XapiThreadStartup
		);
}

// ******************************************************************
// * 0x00FF - PsCreateSystemThreadEx()
// ******************************************************************
// Creates a system thread.
// ThreadHandle: Receives the thread handle
// ThreadExtensionSize: Unsure how this works (everything I've seen uses 0)
// KernelStackSize: Size of the allocation for both stack and TLS data
// TlsDataSize: Size within KernelStackSize to use as TLS data
// ThreadId: Receives the thread ID number
// StartRoutine: Called when the thread is created (by XapiThreadStartup)
// StartContext: Parameter StartRoutine
// CreateSuspended: TRUE to create the thread as a suspended thread
// DebuggerThread: TRUE to allocate the stack from Debug Kit memory
// SystemRoutine: System function (normally XapiThreadStartup) called when the thread is created
//
// New to the XBOX.
XBSYSAPI EXPORTNUM(255) xbox::ntstatus_xt NTAPI xbox::PsCreateSystemThreadEx
(
	OUT PHANDLE         ThreadHandle,
	IN  ulong_xt           ThreadExtensionSize,
	IN  ulong_xt           KernelStackSize,
	IN  ulong_xt           TlsDataSize,
	OUT PDWORD          ThreadId OPTIONAL,
	IN  PKSTART_ROUTINE StartRoutine,
	IN  PVOID           StartContext,
	IN  boolean_xt         CreateSuspended,
	IN  boolean_xt         DebuggerThread,
	IN  PKSYSTEM_ROUTINE SystemRoutine OPTIONAL
)
{
	LOG_FUNC_BEGIN
		LOG_FUNC_ARG_OUT(ThreadHandle)
		LOG_FUNC_ARG(ThreadExtensionSize)
		LOG_FUNC_ARG(KernelStackSize)
		LOG_FUNC_ARG(TlsDataSize)
		LOG_FUNC_ARG_OUT(ThreadId)
		LOG_FUNC_ARG(StartRoutine)
		LOG_FUNC_ARG(StartContext)
		LOG_FUNC_ARG(CreateSuspended)
		LOG_FUNC_ARG(DebuggerThread)
		LOG_FUNC_ARG(SystemRoutine)
		LOG_FUNC_END;

	// TODO : Arguments to use : TlsDataSize, DebuggerThread

	// use default kernel stack size if lesser specified
	if (KernelStackSize < KERNEL_STACK_SIZE)
		KernelStackSize = KERNEL_STACK_SIZE;

	// Double the stack size, this is to account for the overhead HLE patching adds to the stack
	KernelStackSize *= 2;

	// round up to the next page boundary if un-aligned
	KernelStackSize = RoundUp(KernelStackSize, PAGE_SIZE);

    // create thread, using our special proxy technique
    {
		std::condition_variable cv;
		std::mutex mtx;
		std::unique_lock lck(mtx);

        DWORD dwThreadId = 0;
		bool ready = false;
		xbox::PKTHREAD kthread;

        // PCSTProxy is responsible for cleaning up this pointer
		PCSTProxyParam *iPCSTProxyParam = new PCSTProxyParam((PVOID)StartRoutine, StartContext, (PVOID)SystemRoutine, &cv, &mtx, &ready, &kthread);

		/*
		// call thread notification routine(s)
		if (g_iThreadNotificationCount != 0)
		{
			for (int i = 0; i < 16; i++)
			{
				// TODO: This is *very* wrong, ps notification routines are NOT the same as XApi notification routines
				// TODO: XAPI notification routines are already handeld by XapiThreadStartup and don't need to be called by us
				// TODO: This type of notification routine is PCREATE_THREAD_NOTIFY_ROUTINE, which takes an ETHREAD pointer as well as Thread ID as input
				// TODO: This is impossible to support currently, as we do not create or register Xbox ETHREAD objects, so we're better to skip it entirely!
				xbox::XTHREAD_NOTIFY_PROC pfnNotificationRoutine = (xbox::XTHREAD_NOTIFY_PROC)g_pfnThreadNotification[i];

				// If the routine doesn't exist, don't execute it!
				if (pfnNotificationRoutine == NULL)
					continue;

				EmuLog(LOG_LEVEL::DEBUG, "Calling pfnNotificationRoutine[%d] (0x%.8X)", g_iThreadNotificationCount, pfnNotificationRoutine);

				pfnNotificationRoutine(TRUE);
			}
		}*/

        HANDLE handle = reinterpret_cast<HANDLE>(_beginthreadex(NULL, KernelStackSize, PCSTProxy, iPCSTProxyParam, 0, reinterpret_cast<unsigned int*>(&dwThreadId)));
		if (handle == NULL) {
			delete iPCSTProxyParam;
			RETURN(xbox::status_insufficient_resources);
		}

		if (ThreadId != zeroptr) {
			*ThreadId = dwThreadId;
		}

		// wait until kthread is created and UniqueThread has been set
		cv.wait(lck, [&ready]() {
			return ready;
			});

		// close the handle returned by _beginthreadex because we use the duplicate created in the new thread
		CloseHandle(handle);

		handle = reinterpret_cast<xbox::PETHREAD>(kthread)->UniqueThread;
		*ThreadHandle = handle;
		g_AffinityPolicy->SetAffinityXbox(handle);
		RegisterThread(handle, kthread);

		// we need to call KeSuspendThread because KeResumeThread expects the thread to be suspended with SuspendThread and not with KeWaitForSingleObject
		KeSuspendThread(kthread);
		KeReleaseSemaphore(&kthread->SuspendSemaphore, 0, 1, FALSE);

		// Now that ThreadId is populated and affinity is changed, resume the thread (unless the guest passed CREATE_SUSPENDED)
		if (!CreateSuspended) {
			KeResumeThread(kthread);
		}

		// Note : DO NOT use iPCSTProxyParam anymore, since ownership is transferred to the proxy (which frees it too)

		// Log ThreadID identical to how GetCurrentThreadID() is rendered :
		EmuLog(LOG_LEVEL::DEBUG, "Created Xbox proxy thread. Handle : 0x%X, ThreadId : [0x%.4X]", handle, dwThreadId);
	}

	RETURN(xbox::status_success);
}

// ******************************************************************
// * 0x0100 - PsQueryStatistics()
// ******************************************************************
XBSYSAPI EXPORTNUM(256) xbox::ntstatus_xt NTAPI xbox::PsQueryStatistics
(
	IN OUT PPS_STATISTICS ProcessStatistics
)
{
	LOG_FUNC_ONE_ARG_OUT(ProcessStatistics);

	NTSTATUS ret = xbox::status_success;

	if (ProcessStatistics->Length == sizeof(PS_STATISTICS)) {
		LOG_INCOMPLETE(); // TODO : Return number of threads and handles that currently exist
		ProcessStatistics->ThreadCount = 1;
		ProcessStatistics->HandleCount = 1;
	} else {
		ret = STATUS_INVALID_PARAMETER;
	}

	RETURN(ret);
}

// ******************************************************************
// * 0x0101 - PsSetCreateThreadNotifyRoutine()
// ******************************************************************
XBSYSAPI EXPORTNUM(257) xbox::ntstatus_xt NTAPI xbox::PsSetCreateThreadNotifyRoutine
(
	IN PCREATE_THREAD_NOTIFY_ROUTINE NotifyRoutine
)
{
	LOG_FUNC_ONE_ARG(NotifyRoutine);

	NTSTATUS ret = xbox::status_insufficient_resources;

	// Taken from xbox::EmuXRegisterThreadNotifyRoutine (perhaps that can be removed now) :

	// I honestly don't expect this to happen, but if it does...
	if (g_iThreadNotificationCount >= PSP_MAX_CREATE_THREAD_NOTIFY)
		CxbxKrnlCleanup("Too many thread notification routines installed\n");

	// Find an empty spot in the thread notification array
	for (int i = 0; i < PSP_MAX_CREATE_THREAD_NOTIFY; i++)
	{
		// If we find one, then add it to the array, and break the loop so
		// that we don't accidently register the same routine twice!
		if (g_pfnThreadNotification[i] == NULL)
		{
			g_pfnThreadNotification[i] = (PVOID)NotifyRoutine;
			g_iThreadNotificationCount++;
			ret = xbox::status_success;
			break;
		}
	}

	RETURN(ret);
}

// ******************************************************************
// * 0x0102 - PsTerminateSystemThread()
// ******************************************************************
// Exits the current system thread.  Must be called from a system thread.
//
// Differences from NT: None.
XBSYSAPI EXPORTNUM(258) xbox::void_xt NTAPI xbox::PsTerminateSystemThread
(
	IN ntstatus_xt ExitStatus
)
{
	LOG_FUNC_ONE_ARG(ExitStatus);

	/*
	// call thread notification routine(s)
	if (g_iThreadNotificationCount != 0)
	{
		for (int i = 0; i < 16; i++)
		{
			xbox::XTHREAD_NOTIFY_PROC pfnNotificationRoutine = (xbox::XTHREAD_NOTIFY_PROC)g_pfnThreadNotification[i];

			// If the routine doesn't exist, don't execute it!
			if (pfnNotificationRoutine == NULL)
				continue;

			EmuLog(LOG_LEVEL::DEBUG, "Calling pfnNotificationRoutine[%d] (0x%.8X)", g_iThreadNotificationCount, pfnNotificationRoutine);

			pfnNotificationRoutine(FALSE);
		}
	}*/

	xbox::KeFreePcr();
	_endthreadex(ExitStatus);
	// ExitThread(ExitStatus);
	// CxbxKrnlTerminateThread();
}

// ******************************************************************
// * 0x0103 - PsThreadObjectType
// ******************************************************************
XBSYSAPI EXPORTNUM(259) xbox::OBJECT_TYPE VOLATILE xbox::PsThreadObjectType =
{
	xbox::ExAllocatePoolWithTag,
	xbox::ExFreePool,
	NULL,
	NULL,
	NULL,
	(PVOID)offsetof(xbox::KTHREAD, Header),
	'erhT' // = first four characters of "Thread" in reverse
};
