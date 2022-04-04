// ******************************************************************
// *
// * proj : OpenXDK
// *
// * desc : Open Source XBox Development Kit
// *
// * file : ps.h
// *
// * note : XBox Kernel *Process Structure* Declarations
// *
// ******************************************************************
#ifndef XBOXKRNL_PS_H
#define XBOXKRNL_PS_H

#include "types.h"

#define X_THREAD_QUANTUM 60

namespace xbox
{
// ******************************************************************
// * PsCreateSystemThread
// ******************************************************************
XBSYSAPI EXPORTNUM(254) ntstatus_xt XBOXAPI PsCreateSystemThread
(
	OUT PHANDLE         ThreadHandle,
	OUT PHANDLE          ThreadId OPTIONAL,
	IN  PKSTART_ROUTINE StartRoutine,
	IN  PVOID           StartContext,
	IN  boolean_xt         DebuggerThread
);

// ******************************************************************
// * PsCreateSystemThreadEx
// ******************************************************************
XBSYSAPI EXPORTNUM(255) ntstatus_xt XBOXAPI PsCreateSystemThreadEx
(
	OUT PHANDLE         ThreadHandle,
	IN  ulong_xt           ThreadExtensionSize,
	IN  ulong_xt           KernelStackSize,
	IN  ulong_xt           TlsDataSize,
	OUT PHANDLE          ThreadId OPTIONAL,
	IN  PKSTART_ROUTINE StartRoutine,
	IN  PVOID           StartContext,
	IN  boolean_xt         CreateSuspended,
	IN  boolean_xt         DebuggerThread,
	IN  PKSYSTEM_ROUTINE SystemRoutine OPTIONAL
);

// ******************************************************************
// * 0x0100 - PsQueryStatistics()
// ******************************************************************
XBSYSAPI EXPORTNUM(256) ntstatus_xt XBOXAPI PsQueryStatistics
(
	IN OUT PPS_STATISTICS ProcessStatistics
);

// ******************************************************************
// * PsSetCreateThreadNotifyRoutine
// ******************************************************************
XBSYSAPI EXPORTNUM(257) ntstatus_xt XBOXAPI PsSetCreateThreadNotifyRoutine
(
	IN PCREATE_THREAD_NOTIFY_ROUTINE NotifyRoutine
);

// ******************************************************************
// * PsTerminateSystemThread
// ******************************************************************
XBSYSAPI EXPORTNUM(258) void_xt XBOXAPI PsTerminateSystemThread(IN ntstatus_xt ExitStatus);

XBSYSAPI EXPORTNUM(259) OBJECT_TYPE PsThreadObjectType;

PETHREAD PspGetCurrentThread();

}

#endif


