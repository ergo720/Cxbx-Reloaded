// ******************************************************************
// *
// * proj : OpenXDK
// *
// * desc : Open Source XBox Development Kit
// *
// * file : dbg.h
// *
// * note : XBox Kernel *Debug* Declarations
// *
// ******************************************************************
#ifndef XBOXKRNL_DBG_H
#define XBOXKRNL_DBG_H

#include "types.h"

namespace xbox
{

XBSYSAPI EXPORTNUM(5) void_xt XBOXAPI DbgBreakPoint();
XBSYSAPI EXPORTNUM(6) void_xt XBOXAPI DbgBreakPointWithStatus
(
	IN ulong_xt Status
);
XBSYSAPI EXPORTNUM(7) ntstatus_xt XBOXAPI DbgLoadImageSymbols
(
	IN PANSI_STRING Name,
	IN PVOID Base,
	IN ulong_ptr_xt ProcessId
);
XBSYSAPI EXPORTNUM(8) ulong_xt _cdecl DbgPrint
(
	PCHAR  Format, ...
);
XBSYSAPI EXPORTNUM(10) ulong_xt XBOXAPI DbgPrompt
(
	IN PCCH Prompt,
	OUT PCH Response,
	IN ulong_xt MaximumResponseLength
);
XBSYSAPI EXPORTNUM(11) void_xt XBOXAPI DbgUnLoadImageSymbols
(
	IN PANSI_STRING Name,
	IN PVOID Base,
	IN ulong_ptr_xt ProcessId
);

}

#endif




