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
// *
// *  All rights reserved
// *
// ******************************************************************

#define LOG_PREFIX CXBXR_MODULE::CXBXR
#define LOG_PREFIX_INIT CXBXR_MODULE::INIT

/* prevent name collisions */
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};
typedef void VOID;
#include "gui/resource/ResCxbx.h"
#include "core\kernel\init\CxbxKrnl.h"
#include "common\xbdm\CxbxXbdm.h" // For Cxbx_LibXbdmThunkTable
#include "CxbxVersion.h"
#include "core\kernel\support\Emu.h"
#include "devices\x86\EmuX86.h"
#include "core\kernel\support\EmuFile.h"
#include "core\kernel\support\EmuFS.h" // EmuInitFS
#include "EmuEEPROM.h" // For CxbxRestoreEEPROM, EEPROM, XboxFactoryGameRegion
#include "core\kernel\exports\EmuKrnl.h"
#include "core\kernel\exports\EmuKrnlKi.h"
#include "EmuShared.h"
#include "core\hle\D3D8\Direct3D9\Direct3D9.h" // For CxbxInitWindow, EmuD3DInit
#include "core\hle\DSOUND\DirectSound\DirectSound.hpp" // For CxbxInitAudio
#include "core\hle\Intercept.hpp"
#include "ReservedMemory.h" // For virtual_memory_placeholder
#include "core\kernel\memory-manager\VMManager.h"
#include "CxbxDebugger.h"
#include "common/util/cliConfig.hpp"
#include "common/util/xxhash.h"
#include "common/ReserveAddressRanges.h"
#include "common/xbox/Types.hpp"

#include <clocale>
#include <process.h>
#include <time.h> // For time()
#include <sstream> // For std::ostringstream

#include "devices\EEPROMDevice.h" // For g_EEPROM
#include "devices\Xbox.h" // For InitXboxHardware()
#include "devices\LED.h" // For LED::Sequence
#include "devices\SMCDevice.h" // For SMC Access
#include "common\crypto\EmuSha.h" // For the SHA1 functions
#include "Timer.h" // For Timer_Init
#include "common\input\InputManager.h" // For the InputDeviceManager

#ifdef VOID
#undef VOID
#endif
#include "lib86cpu.h"

/*! thread local storage */
Xbe::TLS *CxbxKrnl_TLS = NULL;
/*! thread local storage data */
void *CxbxKrnl_TLSData = NULL;
/*! xbe header structure */
Xbe::Header *CxbxKrnl_XbeHeader = NULL;
/*! parent window handle */

/*! indicates a debug kernel */
bool g_bIsDebugKernel = false;

HWND CxbxKrnl_hEmuParent = NULL;
DebugMode CxbxKrnl_DebugMode = DebugMode::DM_NONE;
std::string CxbxKrnl_DebugFileName = "";
Xbe::Certificate *g_pCertificate = NULL;

/*! thread handles */
static std::vector<HANDLE> g_hThreads;

char szFilePath_CxbxReloaded_Exe[MAX_PATH] = { 0 };
char szFolder_CxbxReloadedData[MAX_PATH] = { 0 };
char szFilePath_EEPROM_bin[MAX_PATH] = { 0 };
char szFilePath_Xbe[MAX_PATH*2] = { 0 }; // NOTE: LAUNCH_DATA_HEADER's szLaunchPath is MAX_PATH*2 = 520

std::string CxbxBasePath;
HANDLE CxbxBasePathHandle;
Xbe* CxbxKrnl_Xbe = NULL;
bool g_bIsChihiro = false;
bool g_bIsDebug = false;
bool g_bIsRetail = false;
DWORD_PTR g_CPUXbox = 0;
DWORD_PTR g_CPUOthers = 0;

// Indicates to disable/enable all interrupts when cli and sti instructions are executed
std::atomic_bool g_bEnableAllInterrupts = true;

// Set by the VMManager during initialization. Exported because it's needed in other parts of the emu
size_t g_SystemMaxMemory = 0;

HANDLE g_CurrentProcessHandle = 0; // Set in CxbxKrnlMain
bool g_bIsWine = false;

bool g_CxbxPrintUEM = false;
ULONG g_CxbxFatalErrorCode = FATAL_ERROR_NONE;

// Define function located in EmuXApi so we can call it from here
void SetupXboxDeviceTypes();

static constexpr xbaddr KERNEL_THUNK_ADDRESS = XBOX_KERNEL_BASE + offsetof(DUMMY_KERNEL, KrnlThunk);
static constexpr xbaddr KERNEL_VARIABLES_ADDRESS = XBOX_KERNEL_BASE + offsetof(DUMMY_KERNEL, KrnlVariables);

static constexpr size_t KernelVarsSize[34] =
{
	sizeof(xboxkrnl::ExEventObjectType),
	sizeof(xboxkrnl::ExMutantObjectType),
	sizeof(xboxkrnl::ExSemaphoreObjectType),
	sizeof(xboxkrnl::ExTimerObjectType),
	sizeof(xboxkrnl::HalDiskCachePartitionCount),
	sizeof(xboxkrnl::HalDiskModelNumber),
	sizeof(xboxkrnl::HalDiskSerialNumber),
	sizeof(xboxkrnl::IoCompletionObjectType),
	sizeof(xboxkrnl::IoDeviceObjectType),
	sizeof(xboxkrnl::IoFileObjectType),
	sizeof(xboxkrnl::KdDebuggerEnabled),
	sizeof(xboxkrnl::KdDebuggerNotPresent),
	sizeof(xboxkrnl::MmGlobalData),
	sizeof(xboxkrnl::KeInterruptTime),
	sizeof(xboxkrnl::KeSystemTime),
	sizeof(xboxkrnl::KeTickCount),
	sizeof(xboxkrnl::KeTimeIncrement),
	sizeof(xboxkrnl::KiBugCheckData),
	sizeof(xboxkrnl::LaunchDataPage),
	sizeof(xboxkrnl::ObDirectoryObjectType),
	sizeof(xboxkrnl::ObpObjectHandleTable),
	sizeof(xboxkrnl::ObSymbolicLinkObjectType),
	sizeof(xboxkrnl::PsThreadObjectType),
	sizeof(xboxkrnl::XboxEEPROMKey),
	sizeof(xboxkrnl::XboxHardwareInfo),
	sizeof(xboxkrnl::XboxHDKey),
	sizeof(xboxkrnl::XboxKrnlVersion),
	sizeof(xboxkrnl::XboxSignatureKey),
	sizeof(xboxkrnl::XeImageFileName),
	sizeof(xboxkrnl::XboxLANKey),
	sizeof(xboxkrnl::XboxAlternateSignatureKeys),
	sizeof(xboxkrnl::XePublicKeyData),
	sizeof(xboxkrnl::HalBootSMCVideoMode),
	sizeof(xboxkrnl::IdexChannelObject),
};

static constexpr xbaddr calc_krnl_var_addr(const int idx)
{
	uint32_t var_offset = 0;

	for (int i = 0; i < idx; i++) {
		var_offset += KernelVarsSize[i];
	}

	return KERNEL_VARIABLES_ADDRESS + var_offset;
}

static constexpr xbaddr calc_krnl_func_addr(const int idx)
{
	return KERNEL_THUNK_ADDRESS + idx * 4;
}

#define FUNC // kernel function
#define VARIABLE // kernel variable

#define DEVKIT // developer kit only functions
#define PROFILING // private kernel profiling functions
// A.k.a. _XBOX_ENABLE_PROFILING

// kernel thunk table
static constexpr xbaddr CxbxKrnl_KernelThunkTable[KERNEL_EXPORTS_NB] =
{
	(xbaddr)xbnullptr,           // "Undefined", this function doesn't exist    0x0000 (0)
	calc_krnl_func_addr(1),      // FUNC(AvGetSavedDataAddress),                0x0001 (1)
	calc_krnl_func_addr(2),      // FUNC(AvSendTVEncoderOption),                0x0002 (2)
	calc_krnl_func_addr(3),      // FUNC(AvSetDisplayMode),                     0x0003 (3)
	calc_krnl_func_addr(4),      // FUNC(AvSetSavedDataAddress),                0x0004 (4)
	calc_krnl_func_addr(5),      // FUNC(DbgBreakPoint),                        0x0005 (5)
	calc_krnl_func_addr(6),      // FUNC(DbgBreakPointWithStatus),              0x0006 (6)
	calc_krnl_func_addr(7),      // FUNC(DbgLoadImageSymbols),                  0x0007 (7) DEVKIT
	calc_krnl_func_addr(8),      // FUNC(DbgPrint),                             0x0008 (8)
	calc_krnl_func_addr(9),      // FUNC(HalReadSMCTrayState),                  0x0009 (9)
	calc_krnl_func_addr(10),     // FUNC(DbgPrompt),                            0x000A (10)
	calc_krnl_func_addr(11),     // FUNC(DbgUnLoadImageSymbols),                0x000B (11) DEVKIT
	calc_krnl_func_addr(12),     // FUNC(ExAcquireReadWriteLockExclusive)       0x000C (12)
	calc_krnl_func_addr(13),     // FUNC(ExAcquireReadWriteLockShared),         0x000D (13)
	calc_krnl_func_addr(14),     // FUNC(ExAllocatePool),                       0x000E (14)
	calc_krnl_func_addr(15),     // FUNC(ExAllocatePoolWithTag),                0x000F (15)
	calc_krnl_var_addr(0),       // VARIABLE(&ExEventObjectType),               0x0010 (16)
	calc_krnl_func_addr(17),     // FUNC(ExFreePool),                           0x0011 (17)
	calc_krnl_func_addr(18),     // FUNC(ExInitializeReadWriteLock),            0x0012 (18)
	calc_krnl_func_addr(19),     // FUNC(ExInterlockedAddLargeInteger),         0x0013 (19)
	calc_krnl_func_addr(20),     // FUNC(ExInterlockedAddLargeStatistic),       0x0014 (20)
	calc_krnl_func_addr(21),     // FUNC(ExInterlockedCompareExchange64),       0x0015 (21)
	calc_krnl_var_addr(1),       // VARIABLE(&ExMutantObjectType),              0x0016 (22)
	calc_krnl_func_addr(23),     // FUNC(ExQueryPoolBlockSize),                 0x0017 (23)
	calc_krnl_func_addr(24),     // FUNC(ExQueryNonVolatileSetting),            0x0018 (24)
	calc_krnl_func_addr(25),     // FUNC(ExReadWriteRefurbInfo),                0x0019 (25)
	calc_krnl_func_addr(26),     // FUNC(ExRaiseException),                     0x001A (26)
	calc_krnl_func_addr(27),     // FUNC(ExRaiseStatus),                        0x001B (27)
	calc_krnl_func_addr(28),     // FUNC(ExReleaseReadWriteLock),               0x001C (28)
	calc_krnl_func_addr(29),     // FUNC(ExSaveNonVolatileSetting),             0x001D (29)
	calc_krnl_var_addr(2),       // VARIABLE(ExSemaphoreObjectType),            0x001E (30)
	calc_krnl_var_addr(3),       // VARIABLE(ExTimerObjectType),                0x001F (31)
	calc_krnl_func_addr(32),     // FUNC(ExfInterlockedInsertHeadList),         0x0020 (32)
	calc_krnl_func_addr(33),     // FUNC(ExfInterlockedInsertTailList),         0x0021 (33)
	calc_krnl_func_addr(34),     // FUNC(ExfInterlockedRemoveHeadList),         0x0022 (34)
	calc_krnl_func_addr(35),     // FUNC(FscGetCacheSize),                      0x0023 (35)
	calc_krnl_func_addr(36),     // FUNC(FscInvalidateIdleBlocks),              0x0024 (36)
	calc_krnl_func_addr(37),     // FUNC(FscSetCacheSize),                      0x0025 (37)
	calc_krnl_func_addr(38),     // FUNC(HalClearSoftwareInterrupt),            0x0026 (38)
	calc_krnl_func_addr(39),     // FUNC(HalDisableSystemInterrupt),            0x0027 (39)
	calc_krnl_var_addr(4),       // VARIABLE(HalDiskCachePartitionCount),       0x0028 (40)  A.k.a. "IdexDiskPartitionPrefixBuffer"
	calc_krnl_var_addr(5),       // VARIABLE(HalDiskModelNumber),               0x0029 (41)
	calc_krnl_var_addr(6),       // VARIABLE(HalDiskSerialNumber),              0x002A (42)
	calc_krnl_func_addr(43),     // FUNC(HalEnableSystemInterrupt),             0x002B (43)
	calc_krnl_func_addr(44),     // FUNC(HalGetInterruptVector),                0x002C (44)
	calc_krnl_func_addr(45),     // FUNC(HalReadSMBusValue),                    0x002D (45)
	calc_krnl_func_addr(46),     // FUNC(HalReadWritePCISpace),                 0x002E (46)
	calc_krnl_func_addr(47),     // FUNC(HalRegisterShutdownNotification),      0x002F (47)
	calc_krnl_func_addr(48),     // FUNC(HalRequestSoftwareInterrupt),          0x0030 (48)
	calc_krnl_func_addr(49),     // FUNC(HalReturnToFirmware),                  0x0031 (49)
	calc_krnl_func_addr(50),     // FUNC(HalWriteSMBusValue),                   0x0032 (50)
	calc_krnl_func_addr(51),     // FUNC(KRNL(InterlockedCompareExchange)),     0x0033 (51)
	calc_krnl_func_addr(52),     // FUNC(KRNL(InterlockedDecrement)),           0x0034 (52)
	calc_krnl_func_addr(53),     // FUNC(KRNL(InterlockedIncrement)),           0x0035 (53)
	calc_krnl_func_addr(54),     // FUNC(KRNL(InterlockedExchange)),            0x0036 (54)
	calc_krnl_func_addr(55),     // FUNC(KRNL(InterlockedExchangeAdd)),         0x0037 (55)
	calc_krnl_func_addr(56),     // FUNC(KRNL(InterlockedFlushSList)),          0x0038 (56)
	calc_krnl_func_addr(57),     // FUNC(KRNL(InterlockedPopEntrySList)),       0x0039 (57)
	calc_krnl_func_addr(58),     // FUNC(KRNL(InterlockedPushEntrySList)),      0x003A (58)
	calc_krnl_func_addr(59),     // FUNC(IoAllocateIrp),                        0x003B (59)
	calc_krnl_func_addr(60),     // FUNC(IoBuildAsynchronousFsdRequest),        0x003C (60)
	calc_krnl_func_addr(61),     // FUNC(IoBuildDeviceIoControlRequest),        0x003D (61)
	calc_krnl_func_addr(62),     // FUNC(IoBuildSynchronousFsdRequest),         0x003E (62)
	calc_krnl_func_addr(63),     // FUNC(IoCheckShareAccess),                   0x003F (63)
	calc_krnl_var_addr(7),       // VARIABLE(IoCompletionObjectType),           0x0040 (64)
	calc_krnl_func_addr(65),     // FUNC(IoCreateDevice),                       0x0041 (65)
	calc_krnl_func_addr(66),     // FUNC(IoCreateFile),                         0x0042 (66)
	calc_krnl_func_addr(67),     // FUNC(IoCreateSymbolicLink),                 0x0043 (67)
	calc_krnl_func_addr(68),     // FUNC(IoDeleteDevice),                       0x0044 (68)
	calc_krnl_func_addr(69),     // FUNC(IoDeleteSymbolicLink),                 0x0045 (69)
	calc_krnl_var_addr(8),       // VARIABLE(IoDeviceObjectType),               0x0046 (70)
	calc_krnl_var_addr(9),       // VARIABLE(IoFileObjectType),                 0x0047 (71)
	calc_krnl_func_addr(72),     // FUNC(IoFreeIrp),                            0x0048 (72)
	calc_krnl_func_addr(73),     // FUNC(IoInitializeIrp),                      0x0049 (73)
	calc_krnl_func_addr(74),     // FUNC(IoInvalidDeviceRequest),               0x004A (74)
	calc_krnl_func_addr(75),     // FUNC(IoQueryFileInformation),               0x004B (75)
	calc_krnl_func_addr(76),     // FUNC(IoQueryVolumeInformation),             0x004C (76)
	calc_krnl_func_addr(77),     // FUNC(IoQueueThreadIrp),                     0x004D (77)
	calc_krnl_func_addr(78),     // FUNC(IoRemoveShareAccess),                  0x004E (78)
	calc_krnl_func_addr(79),     // FUNC(IoSetIoCompletion),                    0x004F (79)
	calc_krnl_func_addr(80),     // FUNC(IoSetShareAccess),                     0x0050 (80)
	calc_krnl_func_addr(81),     // FUNC(IoStartNextPacket),                    0x0051 (81)
	calc_krnl_func_addr(82),     // FUNC(IoStartNextPacketByKey),               0x0052 (82)
	calc_krnl_func_addr(83),     // FUNC(IoStartPacket),                        0x0053 (83)
	calc_krnl_func_addr(84),     // FUNC(IoSynchronousDeviceIoControlRequest),  0x0054 (84)
	calc_krnl_func_addr(85),     // FUNC(IoSynchronousFsdRequest),              0x0055 (85)
	calc_krnl_func_addr(86),     // FUNC(IofCallDriver),                        0x0056 (86)
	calc_krnl_func_addr(87),     // FUNC(IofCompleteRequest),                   0x0057 (87)
	calc_krnl_var_addr(10),      // VARIABLE(KdDebuggerEnabled),                0x0058 (88)
	calc_krnl_var_addr(11),      // VARIABLE(KdDebuggerNotPresent),             0x0059 (89)
	calc_krnl_func_addr(90),     // FUNC(IoDismountVolume),                     0x005A (90)
	calc_krnl_func_addr(91),     // FUNC(IoDismountVolumeByName),               0x005B (91)
	calc_krnl_func_addr(92),     // FUNC(KeAlertResumeThread),                  0x005C (92)
	calc_krnl_func_addr(93),     // FUNC(KeAlertThread),                        0x005D (93)
	calc_krnl_func_addr(94),     // FUNC(KeBoostPriorityThread),                0x005E (94)
	calc_krnl_func_addr(95),     // FUNC(KeBugCheck),                           0x005F (95)
	calc_krnl_func_addr(96),     // FUNC(KeBugCheckEx),                         0x0060 (96)
	calc_krnl_func_addr(97),     // FUNC(KeCancelTimer),                        0x0061 (97)
	calc_krnl_func_addr(98),     // FUNC(KeConnectInterrupt),                   0x0062 (98)
	calc_krnl_func_addr(99),     // FUNC(KeDelayExecutionThread),               0x0063 (99)
	calc_krnl_func_addr(100),    // FUNC(KeDisconnectInterrupt),                0x0064 (100)
	calc_krnl_func_addr(101),    // FUNC(KeEnterCriticalRegion),                0x0065 (101)
	calc_krnl_var_addr(12),      // VARIABLE(MmGlobalData),                     0x0066 (102)
	calc_krnl_func_addr(103),    // FUNC(KeGetCurrentIrql),                     0x0067 (103)
	calc_krnl_func_addr(104),    // FUNC(KeGetCurrentThread),                   0x0068 (104)
	calc_krnl_func_addr(105),    // FUNC(KeInitializeApc),                      0x0069 (105)
	calc_krnl_func_addr(106),    // FUNC(KeInitializeDeviceQueue),              0x006A (106)
	calc_krnl_func_addr(107),    // FUNC(KeInitializeDpc),                      0x006B (107)
	calc_krnl_func_addr(108),    // FUNC(KeInitializeEvent),                    0x006C (108)
	calc_krnl_func_addr(109),    // FUNC(KeInitializeInterrupt),                0x006D (109)
	calc_krnl_func_addr(110),    // FUNC(KeInitializeMutant),                   0x006E (110)
	calc_krnl_func_addr(111),    // FUNC(KeInitializeQueue),                    0x006F (111)
	calc_krnl_func_addr(112),    // FUNC(KeInitializeSemaphore),                0x0070 (112)
	calc_krnl_func_addr(113),    // FUNC(KeInitializeTimerEx),                  0x0071 (113)
	calc_krnl_func_addr(114),    // FUNC(KeInsertByKeyDeviceQueue),             0x0072 (114)
	calc_krnl_func_addr(115),    // FUNC(KeInsertDeviceQueue),                  0x0073 (115)
	calc_krnl_func_addr(116),    // FUNC(KeInsertHeadQueue),                    0x0074 (116)
	calc_krnl_func_addr(117),    // FUNC(KeInsertQueue),                        0x0075 (117)
	calc_krnl_func_addr(118),    // FUNC(KeInsertQueueApc),                     0x0076 (118)
	calc_krnl_func_addr(119),    // FUNC(KeInsertQueueDpc),                     0x0077 (119)
	calc_krnl_var_addr(13),      // VARIABLE(KeInterruptTime),                  0x0078 (120) KeInterruptTime
	calc_krnl_func_addr(121),    // FUNC(KeIsExecutingDpc),                     0x0079 (121)
	calc_krnl_func_addr(122),    // FUNC(KeLeaveCriticalRegion),                0x007A (122)
	calc_krnl_func_addr(123),    // FUNC(KePulseEvent),                         0x007B (123)
	calc_krnl_func_addr(124),    // FUNC(KeQueryBasePriorityThread),            0x007C (124)
	calc_krnl_func_addr(125),    // FUNC(KeQueryInterruptTime),                 0x007D (125)
	calc_krnl_func_addr(126),    // FUNC(KeQueryPerformanceCounter),            0x007E (126)
	calc_krnl_func_addr(127),    // FUNC(KeQueryPerformanceFrequency),          0x007F (127)
	calc_krnl_func_addr(128),    // FUNC(KeQuerySystemTime),                    0x0080 (128)
	calc_krnl_func_addr(129),    // FUNC(KeRaiseIrqlToDpcLevel),                0x0081 (129)
	calc_krnl_func_addr(130),    // FUNC(KeRaiseIrqlToSynchLevel),              0x0082 (130)
	calc_krnl_func_addr(131),    // FUNC(KeReleaseMutant),                      0x0083 (131)
	calc_krnl_func_addr(132),    // FUNC(KeReleaseSemaphore),                   0x0084 (132)
	calc_krnl_func_addr(133),    // FUNC(KeRemoveByKeyDeviceQueue),             0x0085 (133)
	calc_krnl_func_addr(134),    // FUNC(KeRemoveDeviceQueue),                  0x0086 (134)
	calc_krnl_func_addr(135),    // FUNC(KeRemoveEntryDeviceQueue),             0x0087 (135)
	calc_krnl_func_addr(136),    // FUNC(KeRemoveQueue),                        0x0088 (136)
	calc_krnl_func_addr(137),    // FUNC(KeRemoveQueueDpc),                     0x0089 (137)
	calc_krnl_func_addr(138),    // FUNC(KeResetEvent),                         0x008A (138)
	calc_krnl_func_addr(139),    // FUNC(KeRestoreFloatingPointState),          0x008B (139)
	calc_krnl_func_addr(140),    // FUNC(KeResumeThread),                       0x008C (140)
	calc_krnl_func_addr(141),    // FUNC(KeRundownQueue),                       0x008D (141)
	calc_krnl_func_addr(142),    // FUNC(KeSaveFloatingPointState),             0x008E (142)
	calc_krnl_func_addr(143),    // FUNC(KeSetBasePriorityThread),              0x008F (143)
	calc_krnl_func_addr(144),    // FUNC(KeSetDisableBoostThread),              0x0090 (144)
	calc_krnl_func_addr(145),    // FUNC(KeSetEvent),                           0x0091 (145)
	calc_krnl_func_addr(146),    // FUNC(KeSetEventBoostPriority),              0x0092 (146)
	calc_krnl_func_addr(147),    // FUNC(KeSetPriorityProcess),                 0x0093 (147)
	calc_krnl_func_addr(148),    // FUNC(KeSetPriorityThread),                  0x0094 (148)
	calc_krnl_func_addr(149),    // FUNC(KeSetTimer),                           0x0095 (149)
	calc_krnl_func_addr(150),    // FUNC(KeSetTimerEx),                         0x0096 (150)
	calc_krnl_func_addr(151),    // FUNC(KeStallExecutionProcessor),            0x0097 (151)
	calc_krnl_func_addr(152),    // FUNC(KeSuspendThread),                      0x0098 (152)
	calc_krnl_func_addr(153),    // FUNC(KeSynchronizeExecution),               0x0099 (153)
	calc_krnl_var_addr(14),      // VARIABLE(KeSystemTime),                     0x009A (154) KeSystemTime
	calc_krnl_func_addr(155),    // FUNC(KeTestAlertThread),                    0x009B (155)
	calc_krnl_var_addr(15),      // VARIABLE(KeTickCount),                      0x009C (156)
	calc_krnl_var_addr(16),      // VARIABLE(KeTimeIncrement),                  0x009D (157)
	calc_krnl_func_addr(158),    // FUNC(KeWaitForMultipleObjects),             0x009E (158)
	calc_krnl_func_addr(159),    // FUNC(KeWaitForSingleObject),                0x009F (159)
	calc_krnl_func_addr(160),    // FUNC(KfRaiseIrql),                          0x00A0 (160)
	calc_krnl_func_addr(161),    // FUNC(KfLowerIrql),                          0x00A1 (161)
	calc_krnl_var_addr(17),      // VARIABLE(KiBugCheckData),                   0x00A2 (162)
	calc_krnl_func_addr(163),    // FUNC(KiUnlockDispatcherDatabase),           0x00A3 (163)
	calc_krnl_var_addr(18),      // VARIABLE(LaunchDataPage),                   0x00A4 (164)
	calc_krnl_func_addr(165),    // FUNC(MmAllocateContiguousMemory),           0x00A5 (165)
	calc_krnl_func_addr(166),    // FUNC(MmAllocateContiguousMemoryEx),         0x00A6 (166)
	calc_krnl_func_addr(167),    // FUNC(MmAllocateSystemMemory),               0x00A7 (167)
	calc_krnl_func_addr(168),    // FUNC(MmClaimGpuInstanceMemory),             0x00A8 (168)
	calc_krnl_func_addr(169),    // FUNC(MmCreateKernelStack),                  0x00A9 (169)
	calc_krnl_func_addr(170),    // FUNC(MmDeleteKernelStack),                  0x00AA (170)
	calc_krnl_func_addr(171),    // FUNC(MmFreeContiguousMemory),               0x00AB (171)
	calc_krnl_func_addr(172),    // FUNC(MmFreeSystemMemory),                   0x00AC (172)
	calc_krnl_func_addr(173),    // FUNC(MmGetPhysicalAddress),                 0x00AD (173)
	calc_krnl_func_addr(174),    // FUNC(MmIsAddressValid),                     0x00AE (174)
	calc_krnl_func_addr(175),    // FUNC(MmLockUnlockBufferPages),              0x00AF (175)
	calc_krnl_func_addr(176),    // FUNC(MmLockUnlockPhysicalPage),             0x00B0 (176)
	calc_krnl_func_addr(177),    // FUNC(MmMapIoSpace),                         0x00B1 (177)
	calc_krnl_func_addr(178),    // FUNC(MmPersistContiguousMemory),            0x00B2 (178)
	calc_krnl_func_addr(179),    // FUNC(MmQueryAddressProtect),                0x00B3 (179)
	calc_krnl_func_addr(180),    // FUNC(MmQueryAllocationSize),                0x00B4 (180)
	calc_krnl_func_addr(181),    // FUNC(MmQueryStatistics),                    0x00B5 (181)
	calc_krnl_func_addr(182),    // FUNC(MmSetAddressProtect),                  0x00B6 (182)
	calc_krnl_func_addr(183),    // FUNC(MmUnmapIoSpace),                       0x00B7 (183)
	calc_krnl_func_addr(184),    // FUNC(NtAllocateVirtualMemory),              0x00B8 (184)
	calc_krnl_func_addr(185),    // FUNC(NtCancelTimer),                        0x00B9 (185)
	calc_krnl_func_addr(186),    // FUNC(NtClearEvent),                         0x00BA (186)
	calc_krnl_func_addr(187),    // FUNC(NtClose),                              0x00BB (187)
	calc_krnl_func_addr(188),    // FUNC(NtCreateDirectoryObject),              0x00BC (188)
	calc_krnl_func_addr(189),    // FUNC(NtCreateEvent),                        0x00BD (189)
	calc_krnl_func_addr(190),    // FUNC(NtCreateFile),                         0x00BE (190)
	calc_krnl_func_addr(191),    // FUNC(NtCreateIoCompletion),                 0x00BF (191)
	calc_krnl_func_addr(192),    // FUNC(NtCreateMutant),                       0x00C0 (192)
	calc_krnl_func_addr(193),    // FUNC(NtCreateSemaphore),                    0x00C1 (193)
	calc_krnl_func_addr(194),    // FUNC(NtCreateTimer),                        0x00C2 (194)
	calc_krnl_func_addr(195),    // FUNC(NtDeleteFile),                         0x00C3 (195)
	calc_krnl_func_addr(196),    // FUNC(NtDeviceIoControlFile),                0x00C4 (196)
	calc_krnl_func_addr(197),    // FUNC(NtDuplicateObject),                    0x00C5 (197)
	calc_krnl_func_addr(198),    // FUNC(NtFlushBuffersFile),                   0x00C6 (198)
	calc_krnl_func_addr(199),    // FUNC(NtFreeVirtualMemory),                  0x00C7 (199)
	calc_krnl_func_addr(200),    // FUNC(NtFsControlFile),                      0x00C8 (200)
	calc_krnl_func_addr(201),    // FUNC(NtOpenDirectoryObject),                0x00C9 (201)
	calc_krnl_func_addr(202),    // FUNC(NtOpenFile),                           0x00CA (202)
	calc_krnl_func_addr(203),    // FUNC(NtOpenSymbolicLinkObject),             0x00CB (203)
	calc_krnl_func_addr(204),    // FUNC(NtProtectVirtualMemory),               0x00CC (204)
	calc_krnl_func_addr(205),    // FUNC(NtPulseEvent),                         0x00CD (205)
	calc_krnl_func_addr(206),    // FUNC(NtQueueApcThread),                     0x00CE (206)
	calc_krnl_func_addr(207),    // FUNC(NtQueryDirectoryFile),                 0x00CF (207)
	calc_krnl_func_addr(208),    // FUNC(NtQueryDirectoryObject),               0x00D0 (208)
	calc_krnl_func_addr(209),    // FUNC(NtQueryEvent),                         0x00D1 (209)
	calc_krnl_func_addr(210),    // FUNC(NtQueryFullAttributesFile),            0x00D2 (210)
	calc_krnl_func_addr(211),    // FUNC(NtQueryInformationFile),               0x00D3 (211)
	calc_krnl_func_addr(212),    // FUNC(NtQueryIoCompletion),                  0x00D4 (212)
	calc_krnl_func_addr(213),    // FUNC(NtQueryMutant),                        0x00D5 (213)
	calc_krnl_func_addr(214),    // FUNC(NtQuerySemaphore),                     0x00D6 (214)
	calc_krnl_func_addr(215),    // FUNC(NtQuerySymbolicLinkObject),            0x00D7 (215)
	calc_krnl_func_addr(216),    // FUNC(NtQueryTimer),                         0x00D8 (216)
	calc_krnl_func_addr(217),    // FUNC(NtQueryVirtualMemory),                 0x00D9 (217)
	calc_krnl_func_addr(218),    // FUNC(NtQueryVolumeInformationFile),         0x00DA (218)
	calc_krnl_func_addr(219),    // FUNC(NtReadFile),                           0x00DB (219)
	calc_krnl_func_addr(220),    // FUNC(NtReadFileScatter),                    0x00DC (220)
	calc_krnl_func_addr(221),    // FUNC(NtReleaseMutant),                      0x00DD (221)
	calc_krnl_func_addr(222),    // FUNC(NtReleaseSemaphore),                   0x00DE (222)
	calc_krnl_func_addr(223),    // FUNC(NtRemoveIoCompletion),                 0x00DF (223)
	calc_krnl_func_addr(224),    // FUNC(NtResumeThread),                       0x00E0 (224)
	calc_krnl_func_addr(225),    // FUNC(NtSetEvent),                           0x00E1 (225)
	calc_krnl_func_addr(226),    // FUNC(NtSetInformationFile),                 0x00E2 (226)
	calc_krnl_func_addr(227),    // FUNC(NtSetIoCompletion),                    0x00E3 (227)
	calc_krnl_func_addr(228),    // FUNC(NtSetSystemTime),                      0x00E4 (228)
	calc_krnl_func_addr(229),    // FUNC(NtSetTimerEx),                         0x00E5 (229)
	calc_krnl_func_addr(230),    // FUNC(NtSignalAndWaitForSingleObjectEx),     0x00E6 (230)
	calc_krnl_func_addr(231),    // FUNC(NtSuspendThread),                      0x00E7 (231)
	calc_krnl_func_addr(232),    // FUNC(NtUserIoApcDispatcher),                0x00E8 (232)
	calc_krnl_func_addr(233),    // FUNC(NtWaitForSingleObject),                0x00E9 (233)
	calc_krnl_func_addr(234),    // FUNC(NtWaitForSingleObjectEx),              0x00EA (234)
	calc_krnl_func_addr(235),    // FUNC(NtWaitForMultipleObjectsEx),           0x00EB (235)
	calc_krnl_func_addr(236),    // FUNC(NtWriteFile),                          0x00EC (236)
	calc_krnl_func_addr(237),    // FUNC(NtWriteFileGather),                    0x00ED (237)
	calc_krnl_func_addr(238),    // FUNC(NtYieldExecution),                     0x00EE (238)
	calc_krnl_func_addr(239),    // FUNC(ObCreateObject),                       0x00EF (239)
	calc_krnl_var_addr(19),      // VARIABLE(ObDirectoryObjectType),            0x00F0 (240)
	calc_krnl_func_addr(241),    // FUNC(ObInsertObject),                       0x00F1 (241)
	calc_krnl_func_addr(242),    // FUNC(ObMakeTemporaryObject),                0x00F2 (242)
	calc_krnl_func_addr(243),    // FUNC(ObOpenObjectByName),                   0x00F3 (243)
	calc_krnl_func_addr(244),    // FUNC(ObOpenObjectByPointer),                0x00F4 (244)
	calc_krnl_var_addr(20),      // VARIABLE(ObpObjectHandleTable),             0x00F5 (245)
	calc_krnl_func_addr(246),    // FUNC(ObReferenceObjectByHandle),            0x00F6 (246)
	calc_krnl_func_addr(247),    // FUNC(ObReferenceObjectByName),              0x00F7 (247)
	calc_krnl_func_addr(248),    // FUNC(ObReferenceObjectByPointer),           0x00F8 (248)
	calc_krnl_var_addr(21),      // VARIABLE(ObSymbolicLinkObjectType),         0x00F9 (249)
	calc_krnl_func_addr(250),    // FUNC(ObfDereferenceObject),                 0x00FA (250)
	calc_krnl_func_addr(251),    // FUNC(ObfReferenceObject),                   0x00FB (251)
	calc_krnl_func_addr(252),    // FUNC(PhyGetLinkState),                      0x00FC (252)
	calc_krnl_func_addr(253),    // FUNC(PhyInitialize),                        0x00FD (253)
	calc_krnl_func_addr(254),    // FUNC(PsCreateSystemThread),                 0x00FE (254)
	calc_krnl_func_addr(255),    // FUNC(PsCreateSystemThreadEx),               0x00FF (255)
	calc_krnl_func_addr(256),    // FUNC(PsQueryStatistics),                    0x0100 (256)
	calc_krnl_func_addr(257),    // FUNC(PsSetCreateThreadNotifyRoutine),       0x0101 (257)
	calc_krnl_func_addr(258),    // FUNC(PsTerminateSystemThread),              0x0102 (258)
	calc_krnl_var_addr(22),      // VARIABLE(PsThreadObjectType),               0x0103 (259)
	calc_krnl_func_addr(260),    // FUNC(RtlAnsiStringToUnicodeString),         0x0104 (260)
	calc_krnl_func_addr(261),    // FUNC(RtlAppendStringToString),              0x0105 (261)
	calc_krnl_func_addr(262),    // FUNC(RtlAppendUnicodeStringToString),       0x0106 (262)
	calc_krnl_func_addr(263),    // FUNC(RtlAppendUnicodeToString),             0x0107 (263)
	calc_krnl_func_addr(264),    // FUNC(RtlAssert),                            0x0108 (264)
	calc_krnl_func_addr(265),    // FUNC(RtlCaptureContext),                    0x0109 (265)
	calc_krnl_func_addr(266),    // FUNC(RtlCaptureStackBackTrace),             0x010A (266)
	calc_krnl_func_addr(267),    // FUNC(RtlCharToInteger),                     0x010B (267)
	calc_krnl_func_addr(268),    // FUNC(RtlCompareMemory),                     0x010C (268)
	calc_krnl_func_addr(269),    // FUNC(RtlCompareMemoryUlong),                0x010D (269)
	calc_krnl_func_addr(270),    // FUNC(RtlCompareString),                     0x010E (270)
	calc_krnl_func_addr(271),    // FUNC(RtlCompareUnicodeString),              0x010F (271)
	calc_krnl_func_addr(272),    // FUNC(RtlCopyString),                        0x0110 (272)
	calc_krnl_func_addr(273),    // FUNC(RtlCopyUnicodeString),                 0x0111 (273)
	calc_krnl_func_addr(274),    // FUNC(RtlCreateUnicodeString),               0x0112 (274)
	calc_krnl_func_addr(275),    // FUNC(RtlDowncaseUnicodeChar),               0x0113 (275)
	calc_krnl_func_addr(276),    // FUNC(RtlDowncaseUnicodeString),             0x0114 (276)
	calc_krnl_func_addr(277),    // FUNC(RtlEnterCriticalSection),              0x0115 (277)
	calc_krnl_func_addr(278),    // FUNC(RtlEnterCriticalSectionAndRegion),     0x0116 (278)
	calc_krnl_func_addr(279),    // FUNC(RtlEqualString),                       0x0117 (279)
	calc_krnl_func_addr(280),    // FUNC(RtlEqualUnicodeString),                0x0118 (280)
	calc_krnl_func_addr(281),    // FUNC(RtlExtendedIntegerMultiply),           0x0119 (281)
	calc_krnl_func_addr(282),    // FUNC(RtlExtendedLargeIntegerDivide),        0x011A (282)
	calc_krnl_func_addr(283),    // FUNC(RtlExtendedMagicDivide),               0x011B (283)
	calc_krnl_func_addr(284),    // FUNC(RtlFillMemory),                        0x011C (284)
	calc_krnl_func_addr(285),    // FUNC(RtlFillMemoryUlong),                   0x011D (285)
	calc_krnl_func_addr(286),    // FUNC(RtlFreeAnsiString),                    0x011E (286)
	calc_krnl_func_addr(287),    // FUNC(RtlFreeUnicodeString),                 0x011F (287)
	calc_krnl_func_addr(288),    // FUNC(RtlGetCallersAddress),                 0x0120 (288)
	calc_krnl_func_addr(289),    // FUNC(RtlInitAnsiString),                    0x0121 (289)
	calc_krnl_func_addr(290),    // FUNC(RtlInitUnicodeString),                 0x0122 (290)
	calc_krnl_func_addr(291),    // FUNC(RtlInitializeCriticalSection),         0x0123 (291)
	calc_krnl_func_addr(292),    // FUNC(RtlIntegerToChar),                     0x0124 (292)
	calc_krnl_func_addr(293),    // FUNC(RtlIntegerToUnicodeString),            0x0125 (293)
	calc_krnl_func_addr(294),    // FUNC(RtlLeaveCriticalSection),              0x0126 (294)
	calc_krnl_func_addr(295),    // FUNC(RtlLeaveCriticalSectionAndRegion),     0x0127 (295)
	calc_krnl_func_addr(296),    // FUNC(RtlLowerChar),                         0x0128 (296)
	calc_krnl_func_addr(297),    // FUNC(RtlMapGenericMask),                    0x0129 (297)
	calc_krnl_func_addr(298),    // FUNC(RtlMoveMemory),                        0x012A (298)
	calc_krnl_func_addr(299),    // FUNC(RtlMultiByteToUnicodeN),               0x012B (299)
	calc_krnl_func_addr(300),    // FUNC(RtlMultiByteToUnicodeSize),            0x012C (300)
	calc_krnl_func_addr(301),    // FUNC(RtlNtStatusToDosError),                0x012D (301)
	calc_krnl_func_addr(302),    // FUNC(RtlRaiseException),                    0x012E (302)
	calc_krnl_func_addr(303),    // FUNC(RtlRaiseStatus),                       0x012F (303)
	calc_krnl_func_addr(304),    // FUNC(RtlTimeFieldsToTime),                  0x0130 (304)
	calc_krnl_func_addr(305),    // FUNC(RtlTimeToTimeFields),                  0x0131 (305)
	calc_krnl_func_addr(306),    // FUNC(RtlTryEnterCriticalSection),           0x0132 (306)
	calc_krnl_func_addr(307),    // FUNC(RtlUlongByteSwap),                     0x0133 (307)
	calc_krnl_func_addr(308),    // FUNC(RtlUnicodeStringToAnsiString),         0x0134 (308)
	calc_krnl_func_addr(309),    // FUNC(RtlUnicodeStringToInteger),            0x0135 (309)
	calc_krnl_func_addr(310),    // FUNC(RtlUnicodeToMultiByteN),               0x0136 (310)
	calc_krnl_func_addr(311),    // FUNC(RtlUnicodeToMultiByteSize),            0x0137 (311)
	calc_krnl_func_addr(312),    // FUNC(RtlUnwind),                            0x0138 (312)
	calc_krnl_func_addr(313),    // FUNC(RtlUpcaseUnicodeChar),                 0x0139 (313)
	calc_krnl_func_addr(314),    // FUNC(RtlUpcaseUnicodeString),               0x013A (314)
	calc_krnl_func_addr(315),    // FUNC(RtlUpcaseUnicodeToMultiByteN),         0x013B (315)
	calc_krnl_func_addr(316),    // FUNC(RtlUpperChar),                         0x013C (316)
	calc_krnl_func_addr(317),    // FUNC(RtlUpperString),                       0x013D (317)
	calc_krnl_func_addr(318),    // FUNC(RtlUshortByteSwap),                    0x013E (318)
	calc_krnl_func_addr(319),    // FUNC(RtlWalkFrameChain),                    0x013F (319)
	calc_krnl_func_addr(320),    // FUNC(RtlZeroMemory),                        0x0140 (320)
	calc_krnl_var_addr(23),      // VARIABLE(XboxEEPROMKey),                    0x0141 (321)
	calc_krnl_var_addr(24),      // VARIABLE(XboxHardwareInfo),                 0x0142 (322)
	calc_krnl_var_addr(25),      // VARIABLE(XboxHDKey),                        0x0143 (323)
	calc_krnl_var_addr(26),      // VARIABLE(XboxKrnlVersion),                  0x0144 (324)
	calc_krnl_var_addr(27),      // VARIABLE(XboxSignatureKey),                 0x0145 (325)
	calc_krnl_var_addr(28),      // VARIABLE(XeImageFileName),                  0x0146 (326)
	calc_krnl_func_addr(327),    // FUNC(XeLoadSection),                        0x0147 (327)
	calc_krnl_func_addr(328),    // FUNC(XeUnloadSection),                      0x0148 (328)
	calc_krnl_func_addr(329),    // FUNC(READ_PORT_BUFFER_UCHAR),               0x0149 (329)
	calc_krnl_func_addr(330),    // FUNC(READ_PORT_BUFFER_USHORT),              0x014A (330)
	calc_krnl_func_addr(331),    // FUNC(READ_PORT_BUFFER_ULONG),               0x014B (331)
	calc_krnl_func_addr(332),    // FUNC(WRITE_PORT_BUFFER_UCHAR),              0x014C (332)
	calc_krnl_func_addr(333),    // FUNC(WRITE_PORT_BUFFER_USHORT),             0x014D (333)
	calc_krnl_func_addr(334),    // FUNC(WRITE_PORT_BUFFER_ULONG),              0x014E (334)
	calc_krnl_func_addr(335),    // FUNC(XcSHAInit),                            0x014F (335)
	calc_krnl_func_addr(336),    // FUNC(XcSHAUpdate),                          0x0150 (336)
	calc_krnl_func_addr(337),    // FUNC(XcSHAFinal),                           0x0151 (337)
	calc_krnl_func_addr(338),    // FUNC(XcRC4Key),                             0x0152 (338)
	calc_krnl_func_addr(339),    // FUNC(XcRC4Crypt),                           0x0153 (339)
	calc_krnl_func_addr(340),    // FUNC(XcHMAC),                               0x0154 (340)
	calc_krnl_func_addr(341),    // FUNC(XcPKEncPublic),                        0x0155 (341)
	calc_krnl_func_addr(342),    // FUNC(XcPKDecPrivate),                       0x0156 (342)
	calc_krnl_func_addr(343),    // FUNC(XcPKGetKeyLen),                        0x0157 (343)
	calc_krnl_func_addr(344),    // FUNC(XcVerifyPKCS1Signature),               0x0158 (344)
	calc_krnl_func_addr(345),    // FUNC(XcModExp),                             0x0159 (345)
	calc_krnl_func_addr(346),    // FUNC(XcDESKeyParity),                       0x015A (346)
	calc_krnl_func_addr(347),    // FUNC(XcKeyTable),                           0x015B (347)
	calc_krnl_func_addr(348),    // FUNC(XcBlockCrypt),                         0x015C (348)
	calc_krnl_func_addr(349),    // FUNC(XcBlockCryptCBC),                      0x015D (349)
	calc_krnl_func_addr(350),    // FUNC(XcCryptService),                       0x015E (350)
	calc_krnl_func_addr(351),    // FUNC(XcUpdateCrypto),                       0x015F (351)
	calc_krnl_func_addr(352),    // FUNC(RtlRip),                               0x0160 (352)
	calc_krnl_var_addr(29),      // VARIABLE(XboxLANKey),                       0x0161 (353)
	calc_krnl_var_addr(30),      // VARIABLE(XboxAlternateSignatureKeys),       0x0162 (354)
	calc_krnl_var_addr(31),      // VARIABLE(XePublicKeyData),                  0x0163 (355)
	calc_krnl_var_addr(32),      // VARIABLE(HalBootSMCVideoMode),              0x0164 (356)
	calc_krnl_var_addr(33),      // VARIABLE(IdexChannelObject),                0x0165 (357)
	calc_krnl_func_addr(358),    // FUNC(HalIsResetOrShutdownPending),          0x0166 (358)
	calc_krnl_func_addr(359),    // FUNC(IoMarkIrpMustComplete),                0x0167 (359)
	calc_krnl_func_addr(360),    // FUNC(HalInitiateShutdown),                  0x0168 (360)
	calc_krnl_func_addr(361),    // FUNC(RtlSnprintf),                          0x0169 (361)
	calc_krnl_func_addr(362),    // FUNC(RtlSprintf),                           0x016A (362)
	calc_krnl_func_addr(363),    // FUNC(RtlVsnprintf),                         0x016B (363)
	calc_krnl_func_addr(364),    // FUNC(RtlVsprintf),                          0x016C (364)
	calc_krnl_func_addr(365),    // FUNC(HalEnableSecureTrayEject),             0x016D (365)
	calc_krnl_func_addr(366),    // FUNC(HalWriteSMCScratchRegister),           0x016E (366)
	calc_krnl_func_addr(367),    // FUNC(UnknownAPI367),                        0x016F (367)
	calc_krnl_func_addr(368),    // FUNC(UnknownAPI368),                        0x0170 (368)
	calc_krnl_func_addr(369),    // FUNC(UnknownAPI369),                        0x0171 (369)
	calc_krnl_func_addr(370),    // FUNC(XProfpControl),                        0x0172 (370) PROFILING
	calc_krnl_func_addr(371),    // FUNC(XProfpGetData),                        0x0173 (371) PROFILING
	calc_krnl_func_addr(372),    // FUNC(IrtClientInitFast),                    0x0174 (372) PROFILING
	calc_krnl_func_addr(373),    // FUNC(IrtSweep),                             0x0175 (373) PROFILING
	calc_krnl_func_addr(374),    // FUNC(MmDbgAllocateMemory),                  0x0176 (374) DEVKIT ONLY!
	calc_krnl_func_addr(375),    // FUNC(MmDbgFreeMemory),                      0x0177 (375) DEVKIT ONLY!
	calc_krnl_func_addr(376),    // FUNC(MmDbgQueryAvailablePages),             0x0178 (376) DEVKIT ONLY!
	calc_krnl_func_addr(377),    // FUNC(MmDbgReleaseAddress),                  0x0179 (377) DEVKIT ONLY!
	calc_krnl_func_addr(378),    // FUNC(MmDbgWriteCheck),                      0x017A (378) DEVKIT ONLY!
};


void ApplyMediaPatches()
{
	// Patch the XBE Header to allow running from all media types
	g_pCertificate->dwAllowedMedia |= 0
		| XBEIMAGE_MEDIA_TYPE_HARD_DISK
		| XBEIMAGE_MEDIA_TYPE_DVD_X2
		| XBEIMAGE_MEDIA_TYPE_DVD_CD
		| XBEIMAGE_MEDIA_TYPE_CD
		| XBEIMAGE_MEDIA_TYPE_DVD_5_RO
		| XBEIMAGE_MEDIA_TYPE_DVD_9_RO
		| XBEIMAGE_MEDIA_TYPE_DVD_5_RW
		| XBEIMAGE_MEDIA_TYPE_DVD_9_RW
		;
	// Patch the XBE Header to allow running on all regions
	g_pCertificate->dwGameRegion = 0
		| XBEIMAGE_GAME_REGION_MANUFACTURING
		| XBEIMAGE_GAME_REGION_NA
		| XBEIMAGE_GAME_REGION_JAPAN
		| XBEIMAGE_GAME_REGION_RESTOFWORLD
		;
	// Patch the XBE Security Flag
	// This field is only present if the Xbe Size is >= than our Certificate Structure
	// This works as our structure is large enough to fit the newer certificate size, 
	// while dwSize is the actual size of the certificate in the Xbe.
	// Source: Various Hacked Kernels
	if (g_pCertificate->dwSize >= sizeof(Xbe::Certificate)) {
		g_pCertificate->dwSecurityFlags &= ~1;
	}
}

void SetupPerTitleKeys()
{
	// Generate per-title keys from the XBE Certificate
	UCHAR Digest[20] = {};

	// Set the LAN Key
	xboxkrnl::XcHMAC(xboxkrnl::XboxCertificateKey, xboxkrnl::XBOX_KEY_LENGTH, g_pCertificate->bzLanKey, xboxkrnl::XBOX_KEY_LENGTH, NULL, 0, Digest);
	memcpy(xboxkrnl::XboxLANKey, Digest, xboxkrnl::XBOX_KEY_LENGTH);

	// Signature Key
	xboxkrnl::XcHMAC(xboxkrnl::XboxCertificateKey, xboxkrnl::XBOX_KEY_LENGTH, g_pCertificate->bzSignatureKey, xboxkrnl::XBOX_KEY_LENGTH, NULL, 0, Digest);
	memcpy(xboxkrnl::XboxSignatureKey, Digest, xboxkrnl::XBOX_KEY_LENGTH);

	// Alternate Signature Keys
	for (int i = 0; i < xboxkrnl::ALTERNATE_SIGNATURE_COUNT; i++) {
		xboxkrnl::XcHMAC(xboxkrnl::XboxCertificateKey, xboxkrnl::XBOX_KEY_LENGTH, g_pCertificate->bzTitleAlternateSignatureKey[i], xboxkrnl::XBOX_KEY_LENGTH, NULL, 0, Digest);
		memcpy(xboxkrnl::XboxAlternateSignatureKeys[i], Digest, xboxkrnl::XBOX_KEY_LENGTH);
	}

}

void CxbxLaunchXbe(void(*Entry)())
{
	__try
	{
		Entry();
	}
	__except (EmuException(GetExceptionInformation()))
	{
		EmuLog(LOG_LEVEL::WARNING, "Problem with ExceptionFilter");
	}
}

// Entry point address XOR keys per Xbe type (Retail, Debug or Chihiro) :
const DWORD XOR_EP_KEY[3] = { XOR_EP_RETAIL, XOR_EP_DEBUG, XOR_EP_CHIHIRO };
// Kernel thunk address XOR keys per Xbe type (Retail, Debug or Chihiro) :
const DWORD XOR_KT_KEY[3] = { XOR_KT_RETAIL, XOR_KT_DEBUG, XOR_KT_CHIHIRO };

// Executable image header pointers (it's contents can be switched between
// Exe-compatibility and Xbe-identical mode, using RestoreExeImageHeader
// vs RestoreXbeImageHeader) :
const PIMAGE_DOS_HEADER ExeDosHeader = (PIMAGE_DOS_HEADER)XBE_IMAGE_BASE;
PIMAGE_NT_HEADERS ExeNtHeader = nullptr;
PIMAGE_OPTIONAL_HEADER ExeOptionalHeader = nullptr;

// Copy of original executable image headers, used both as backup and valid replacement structure :
PIMAGE_DOS_HEADER NewDosHeader = nullptr;
PIMAGE_NT_HEADERS NewNtHeader = nullptr;
PIMAGE_OPTIONAL_HEADER NewOptionalHeader = nullptr;

// Xbe backup values. RestoreXbeImageHeader place these into ExeHeader to restore loaded Xbe contents.
WORD Xbe_magic = 0;
LONG Xbe_lfanew = 0;
IMAGE_DATA_DIRECTORY Xbe_TLS = { };

// Remember the current XBE contents of the executable image
// header fields that RestoreExeImageHeader needs to restore.
void StoreXbeImageHeader()
{
	Xbe_magic = ExeDosHeader->e_magic; // Normally 0x4258 = 'XB'; (...'EH')
	Xbe_lfanew = ExeDosHeader->e_lfanew;
	Xbe_TLS = ExeOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
}

// Restore memory to the exact contents as loaded from the current XBE.
// Avoid threadswitches and calling Windows API's while this in effect
// because those can fail. Hence, RestoreExeImageHeader quickly again!
void RestoreXbeImageHeader()
{
	ExeDosHeader->e_magic = Xbe_magic; // Sets XbeHeader.dwMagic
	ExeDosHeader->e_lfanew = Xbe_lfanew; // Sets part of XbeHeader.pbDigitalSignature
	ExeOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS] = Xbe_TLS;
}

// Restore memory to the exact contents loaded from the running EXE.
// This is required to keep thread-switching and Windows API's working.
void RestoreExeImageHeader()
{
	ExeDosHeader->e_magic = NewDosHeader->e_magic; // = 0x5A4D = 'MZ'; Overwrites XbeHeader.dwMagic
	ExeDosHeader->e_lfanew = NewDosHeader->e_lfanew; // Overwrites part of XbeHeader.pbDigitalSignature
	ExeOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS] = NewOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
}

typedef const char* (CDECL *LPFN_WINEGETVERSION)(void);
LPFN_WINEGETVERSION wine_get_version;

// Forward declaration to avoid moving the definition of LoadXboxKeys
void LoadXboxKeys(std::string path);

// Returns the Win32 error in string format. Returns an empty string if there is no error.
std::string CxbxGetErrorCodeAsString(DWORD errorCode)
{
	std::string result;
	LPSTR lpMessageBuffer = nullptr;
	DWORD dwLength = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, // lpSource
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&lpMessageBuffer,
		0, // nSize
		NULL); // Arguments
	if (dwLength > 0) {
		result = std::string(lpMessageBuffer, dwLength);
	}

	LocalFree(lpMessageBuffer);
	return result;
}

// Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string CxbxGetLastErrorString(char * lpszFunction)
{
	DWORD errorCode = ::GetLastError(); // Do this first, before any following code changes it
	std::string result = "No error";
	if (errorCode > 0) {
		std::ostringstream stringStream;
		stringStream << lpszFunction << " failed with error " << errorCode << ": " << CxbxGetErrorCodeAsString(errorCode);
		result = stringStream.str();
	}

	return result;
}

#pragma optimize("", off)

void PrintCurrentConfigurationLog()
{
	if (g_bIsWine) {
		EmuLogInit(LOG_LEVEL::INFO, "Running under Wine Version %s", wine_get_version());
	}

	// HACK: For API TRace..
	// bLLE_GPU = true;

	// Print current LLE configuration
	{
		EmuLogInit(LOG_LEVEL::INFO, "---------------------------- LLE CONFIG ----------------------------");
		EmuLogInit(LOG_LEVEL::INFO, "LLE for APU is %s", bLLE_APU ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "LLE for GPU is %s", bLLE_GPU ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "LLE for USB is %s", bLLE_USB ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "LLE for JIT is %s", bLLE_JIT ? "enabled" : "disabled");
	}

	// Print current video configuration (DirectX/HLE)
	if (!bLLE_GPU) {
		Settings::s_video XBVideoConf;
		g_EmuShared->GetVideoSettings(&XBVideoConf);

		EmuLogInit(LOG_LEVEL::INFO, "--------------------------- VIDEO CONFIG ---------------------------");
		EmuLogInit(LOG_LEVEL::INFO, "Direct3D Device: %s", XBVideoConf.direct3DDevice == 0 ? "Direct3D HAL (Hardware Accelerated)" : "Direct3D REF (Software)");
		EmuLogInit(LOG_LEVEL::INFO, "Video Resolution: %s", XBVideoConf.szVideoResolution);
		EmuLogInit(LOG_LEVEL::INFO, "Force VSync is %s", XBVideoConf.bVSync ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "Fullscreen is %s", XBVideoConf.bFullScreen ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "Hardware YUV is %s", XBVideoConf.bHardwareYUV ? "enabled" : "disabled");
	}

	// Print current audio configuration
	{
		Settings::s_audio XBAudioConf;
		g_EmuShared->GetAudioSettings(&XBAudioConf);

		EmuLogInit(LOG_LEVEL::INFO, "--------------------------- AUDIO CONFIG ---------------------------");
		EmuLogInit(LOG_LEVEL::INFO, "Audio Adapter: %s", XBAudioConf.adapterGUID.Data1 == 0 ? "Primary Audio Device" : "Secondary Audio Device");
		EmuLogInit(LOG_LEVEL::INFO, "PCM is %s", XBAudioConf.codec_pcm ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "XADPCM is %s", XBAudioConf.codec_xadpcm ? "enabled" : "disabled");
		EmuLogInit(LOG_LEVEL::INFO, "Unknown Codec is %s", XBAudioConf.codec_unknown ? "enabled" : "disabled");
	}

	// Print current network configuration
	{
		Settings::s_network XBNetworkConf;
		g_EmuShared->GetNetworkSettings(&XBNetworkConf);

		EmuLogInit(LOG_LEVEL::INFO, "--------------------------- NETWORK CONFIG -------------------------");
		EmuLogInit(LOG_LEVEL::INFO, "Network Adapter Name: %s", strlen(XBNetworkConf.adapter_name) == 0 ? "Not Configured" : XBNetworkConf.adapter_name);
	}

	// Print Enabled Hacks
	{
		EmuLogInit(LOG_LEVEL::INFO, "--------------------------- HACKS CONFIG ---------------------------");
		EmuLogInit(LOG_LEVEL::INFO, "Disable Pixel Shaders: %s", g_DisablePixelShaders == 1 ? "On" : "Off (Default)");
		EmuLogInit(LOG_LEVEL::INFO, "Run Xbox threads on all cores: %s", g_UseAllCores == 1 ? "On" : "Off (Default)");
		EmuLogInit(LOG_LEVEL::INFO, "Skip RDTSC Patching: %s", g_SkipRdtscPatching == 1 ? "On" : "Off (Default)");
	}

	EmuLogInit(LOG_LEVEL::INFO, "------------------------- END OF CONFIG LOG ------------------------");
	
}

#if 0
BOOLEAN ApcInterrupt
(
	IN struct _KINTERRUPT *Interrupt,
	IN PVOID ServiceContext
)
{

}

BOOLEAN DispatchInterrupt
(
	IN struct _KINTERRUPT *Interrupt,
	IN PVOID ServiceContext
)
{
	ExecuteDpcQueue();
}

void InitSoftwareInterrupts()
{
	// Init software interrupt 1 (for APC dispatching)
	xboxkrnl::KINTERRUPT SoftwareInterrupt_1;
	SoftwareInterrupt_1.BusInterruptLevel = 1;
	SoftwareInterrupt_1.ServiceRoutine = ApcInterrupt;
	xboxkrnl::KeConnectInterrupt(&SoftwareInterrupt_1);

	// Init software interrupt 2 (for DPC dispatching)
	xboxkrnl::KINTERRUPT SoftwareInterrupt_2;
	SoftwareInterrupt_2.BusInterruptLevel = 2;
	SoftwareInterrupt_2.ServiceRoutine = DispatchInterrupt;
	xboxkrnl::KeConnectInterrupt(&SoftwareInterrupt_2);
}
#endif

void TriggerPendingConnectedInterrupts()
{
	for (int i = 0; i < MAX_BUS_INTERRUPT_LEVEL; i++) {
		// If the interrupt is pending and connected, process it
		if (HalSystemInterrupts[i].IsPending() && EmuInterruptList[i] && EmuInterruptList[i]->Connected) {
			HalSystemInterrupts[i].Trigger(EmuInterruptList[i]);
		}
		SwitchToThread();
	}
}

static unsigned int WINAPI CxbxKrnlInterruptThread(PVOID param)
{
	CxbxSetThreadName("CxbxKrnl Interrupts");

	// Make sure Xbox1 code runs on one core :
	InitXboxThread(g_CPUXbox);

#if 0
	InitSoftwareInterrupts();
#endif

	while (true) {
		if (g_bEnableAllInterrupts) {
			TriggerPendingConnectedInterrupts();
		}
		Sleep(1);
	}

	return 0;
}

static void CxbxKrnlClockThread(void* pVoid)
{
	LARGE_INTEGER CurrentTicks;
	uint64_t Delta;
	uint64_t Microseconds;
	unsigned int IncrementScaling;
	static uint64_t LastTicks = 0;
	static uint64_t Error = 0;
	static uint64_t UnaccountedMicroseconds = 0;

	// This keeps track of how many us have elapsed between two cycles, so that the xbox clocks are updated
	// with the proper increment (instead of blindly adding a single increment at every step)

	if (LastTicks == 0) {
		QueryPerformanceCounter(&CurrentTicks);
		LastTicks = CurrentTicks.QuadPart;
		CurrentTicks.QuadPart = 0;
	}

	QueryPerformanceCounter(&CurrentTicks);
	Delta = CurrentTicks.QuadPart - LastTicks;
	LastTicks = CurrentTicks.QuadPart;

	Error += (Delta * SCALE_S_IN_US);
	Microseconds = Error / HostClockFrequency;
	Error -= (Microseconds * HostClockFrequency);

	UnaccountedMicroseconds += Microseconds;
	IncrementScaling = (unsigned int)(UnaccountedMicroseconds / 1000); // -> 1 ms = 1000us -> time between two xbox clock interrupts
	UnaccountedMicroseconds -= (IncrementScaling * 1000);

	xboxkrnl::KiClockIsr(IncrementScaling);
}

std::vector<xbaddr> g_RdtscPatches;

#define OPCODE_PATCH_RDTSC 0x90EF  // OUT DX, EAX; NOP

bool IsRdtscInstruction(xbaddr addr)
{
	// First the fastest check - does addr contain exact patch from PatchRdtsc?
	// Second check - is addr on the rdtsc patch list?
	return (*(uint16_t*)addr == OPCODE_PATCH_RDTSC)
		// Note : It's not needed to check for g_SkipRdtscPatching,
		// as when that's set, the g_RdtscPatches vector will be empty
		// anyway, failing this lookup :
		&& (std::find(g_RdtscPatches.begin(), g_RdtscPatches.end(), addr) != g_RdtscPatches.end());
}

void PatchRdtsc(xbaddr addr)
{
	// Patch away rdtsc with an opcode we can intercept
	// We use a privilaged instruction rather than int 3 for debugging
	// When using int 3, attached debuggers trap and rdtsc is used often enough
	// that it makes Cxbx-Reloaded unusable
	// A privilaged instruction (like OUT) does not suffer from this
	EmuLogInit(LOG_LEVEL::DEBUG, "Patching rdtsc opcode at 0x%.8X", (DWORD)addr);
	*(uint16_t*)addr = OPCODE_PATCH_RDTSC;
	g_RdtscPatches.push_back(addr);
}

const uint8_t rdtsc_pattern[] = {
	0x89,//{ 0x0F,0x31,0x89 },
	0xC3,//{ 0x0F,0x31,0xC3 },
	0x8B,//{ 0x0F,0x31,0x8B },   //one false positive in Sonic Rider .text 88 5C 0F 31
	0xB9,//{ 0x0F,0x31,0xB9 },
	0xC7,//{ 0x0F,0x31,0xC7 },
	0x8D,//{ 0x0F,0x31,0x8D },
	0x68,//{ 0x0F,0x31,0x68 },
	0x5A,//{ 0x0F,0x31,0x5A },
	0x29,//{ 0x0F,0x31,0x29 },
	0xF3,//{ 0x0F,0x31,0xF3 },
	0xE9,//{ 0x0F,0x31,0xE9 },
	0x2B,//{ 0x0F,0x31,0x2B },
	0x50,//{ 0x0F,0x31,0x50 },	// 0x50 only used in ExaSkeleton .text , but encounter false positive in RalliSport .text 83 E2 0F 31
	0x0F,//{ 0x0F,0x31,0x0F },
	0x3B,//{ 0x0F,0x31,0x3B },
	0xD9,//{ 0x0F,0x31,0xD9 },
	0x57,//{ 0x0F,0x31,0x57 },
	0xB9,//{ 0x0F,0x31,0xB9 },
	0x85,//{ 0x0F,0x31,0x85 },
	0x83,//{ 0x0F,0x31,0x83 },
	0x33,//{ 0x0F,0x31,0x33 },
	0xF7,//{ 0x0F,0x31,0xF7 },
	0x8A,//{ 0x0F,0x31,0x8A }, // 8A and 56 only apears in RalliSport 2 .text , need to watch whether any future false positive.
	0x56,//{ 0x0F,0x31,0x56 }
    0x6A,                      // 6A, 39, EB, F6, A1, 01 only appear in Unreal Championship, 01 is at WMVDEC section
    0x39,
    0xEB,
    0xF6,
    0xA1,
    0x01
};
const int sizeof_rdtsc_pattern = sizeof(rdtsc_pattern);

void PatchRdtscInstructions()
{
	uint8_t rdtsc[2] = { 0x0F, 0x31 };
	DWORD sizeOfImage = CxbxKrnl_XbeHeader->dwSizeofImage;

	// Iterate through each CODE section
	for (uint32_t sectionIndex = 0; sectionIndex < CxbxKrnl_Xbe->m_Header.dwSections; sectionIndex++) {
		if (!CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwFlags.bExecutable) {
			continue;
		}

		// Skip some segments known to never contain rdtsc (to avoid false positives)
		if (std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == "DSOUND"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == "XGRPH"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == ".data"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == ".rdata"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == "XMV"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == "XONLINE"
			|| std::string(CxbxKrnl_Xbe->m_szSectionName[sectionIndex]) == "MDLPL") {
			continue;
		}

		EmuLogInit(LOG_LEVEL::INFO, "Searching for rdtsc in section %s", CxbxKrnl_Xbe->m_szSectionName[sectionIndex]);
		xbaddr startAddr = CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwVirtualAddr;
		//rdtsc is two bytes instruction, it needs at least one opcode byte after it to finish a function, so the endAddr need to substract 3 bytes.
		xbaddr endAddr = startAddr + CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwSizeofRaw-3;
		for (xbaddr addr = startAddr; addr <= endAddr; addr++) 
		{
			if (memcmp((void*)addr, rdtsc, 2) == 0) 
			{
				uint8_t next_byte = *(uint8_t*)(addr + 2);
				// If the following byte matches the known pattern.
				int i = 0;
				for (i = 0; i<sizeof_rdtsc_pattern; i++) 
				{
					if (next_byte == rdtsc_pattern[i]) 
					{
						if (next_byte == 0x8B)
						{
							if (*(uint8_t*)(addr - 2) == 0x88 && *(uint8_t*)(addr - 1) == 0x5C)
							{
								EmuLogInit(LOG_LEVEL::INFO, "Skipped false positive: rdtsc pattern  0x%.2X, @ 0x%.8X", next_byte, (DWORD)addr);
								continue;
							}

						}
						if (next_byte == 0x50)
						{
							if (*(uint8_t*)(addr - 2) == 0x83 && *(uint8_t*)(addr - 1) == 0xE2)
							{
								EmuLogInit(LOG_LEVEL::INFO, "Skipped false positive: rdtsc pattern  0x%.2X, @ 0x%.8X", next_byte, (DWORD)addr);
								continue;
							}

						}
						PatchRdtsc(addr);
						//the first for loop already increment addr per loop. we only increment one more time so the addr will point to the byte next to the found rdtsc instruction. this is important since there is at least one case that two rdtsc instructions are next to each other.
						addr += 1;
						//if a match found, break the pattern matching loop and keep looping addr for next rdtsc.
						break;
					}
				}
				if (i>= sizeof_rdtsc_pattern)
				{
					//no pattern matched, keep record for detections we treat as non-rdtsc for future debugging.
					EmuLogInit(LOG_LEVEL::INFO, "Skipped potential rdtsc: Unknown opcode pattern  0x%.2X, @ 0x%.8X", next_byte, (DWORD)addr);
				}
			}
		}
	}

	EmuLogInit(LOG_LEVEL::INFO, "Done patching rdtsc, total %d rdtsc instructions patched", g_RdtscPatches.size());
}

void MapThunkTable(uint32_t* kt, const uint32_t* pThunkTable, const hook *HookTable)
{
#if 0
    const bool SendDebugReports = (pThunkTable == CxbxKrnl_KernelThunkTable) && CxbxDebugger::CanReport();
#endif
	uint32_t kt_tbl = *reinterpret_cast<uint32_t *>(XBOX_MEM_READ(reinterpret_cast<xbaddr>(kt), 4).data());
	int i = 0;
	while (kt_tbl != 0) {
		int ordinal = kt_tbl & 0x7FFFFFFF;
		XBOX_MEM_WRITE(reinterpret_cast<xbaddr>(kt + i), 4, &pThunkTable[ordinal]);
#if 0
        if (SendDebugReports) {
            // TODO: Update CxbxKrnl_KernelThunkTable to include symbol names
            std::string importName = "KernelImport_" + std::to_string(ordinal);
            CxbxDebugger::ReportKernelPatch(importName.c_str(), kt_tbl);
        }
#endif
		if (HookTable[ordinal].info.addr != nullptr) {
			g_CPU->InstallHook(pThunkTable[ordinal], &HookTable[ordinal]);
		}
		else {
			switch (ordinal)
			{
			case 16:
				xboxkrnl::ExEventObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ExEventObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[0], &xboxkrnl::ExEventObjectType);
				break;

			case 22:
				xboxkrnl::ExMutantObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ExMutantObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[1], &xboxkrnl::ExMutantObjectType);
				break;

			case 30:
				xboxkrnl::ExSemaphoreObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ExSemaphoreObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[2], &xboxkrnl::ExSemaphoreObjectType);
				break;

			case 31:
				xboxkrnl::ExTimerObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ExTimerObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[3], &xboxkrnl::ExTimerObjectType);
				break;

			case 40:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[4], &xboxkrnl::HalDiskCachePartitionCount);
				break;

			case 41:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[5], &xboxkrnl::HalDiskModelNumber);
				break;

			case 42:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[6], &xboxkrnl::HalDiskSerialNumber);
				break;

			case 64:
				xboxkrnl::IoCompletionObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::IoCompletionObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[7], &xboxkrnl::IoCompletionObjectType);
				break;

			case 70:
				xboxkrnl::IoDeviceObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::IoDeviceObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[8], &xboxkrnl::IoDeviceObjectType);
				break;

			case 71:
				xboxkrnl::IoFileObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::IoFileObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[9], &xboxkrnl::IoFileObjectType);
				break;

			case 88:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[10], &xboxkrnl::KdDebuggerEnabled);
				break;

			case 89:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[11], &xboxkrnl::KdDebuggerNotPresent);
				break;

			case 102:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[12], xboxkrnl::MmGlobalData);
				break;

			case 120:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[13], &xboxkrnl::KeInterruptTime);
				break;

			case 154:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[14], &xboxkrnl::KeSystemTime);
				break;

			case 156:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[15], &xboxkrnl::KeTickCount);
				break;

			case 157:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[16], &xboxkrnl::KeTimeIncrement);
				break;

			case 162:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[17], xboxkrnl::KiBugCheckData);
				break;

			case 164:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[18], &xboxkrnl::LaunchDataPage);
				break;

			case 240:
				xboxkrnl::ObDirectoryObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ObDirectoryObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[19], &xboxkrnl::ObDirectoryObjectType);
				break;

			case 245:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[20], &xboxkrnl::ObpObjectHandleTable);
				break;

			case 249:
				xboxkrnl::ObSymbolicLinkObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::ObSymbolicLinkObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[21], &xboxkrnl::ObSymbolicLinkObjectType);
				break;

			case 259:
				xboxkrnl::PsThreadObjectType.AllocateProcedure = reinterpret_cast<xboxkrnl::OB_ALLOCATE_METHOD>(pThunkTable[15]);
				xboxkrnl::PsThreadObjectType.FreeProcedure = reinterpret_cast<xboxkrnl::OB_FREE_METHOD>(pThunkTable[17]);
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[22], &xboxkrnl::PsThreadObjectType);
				break;

			case 321:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[23], &xboxkrnl::XboxEEPROMKey);
				break;

			case 322:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[24], &xboxkrnl::XboxHardwareInfo);
				break;

			case 323:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[25], &xboxkrnl::XboxHDKey);
				break;

			case 324:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[26], &xboxkrnl::XboxKrnlVersion);
				break;

			case 325:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[27], &xboxkrnl::XboxSignatureKey);
				break;

			case 326:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[28], &xboxkrnl::XeImageFileName);
				break;

			case 353:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[29], &xboxkrnl::XboxLANKey);
				break;

			case 354:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[30], xboxkrnl::XboxAlternateSignatureKeys);
				break;

			case 355:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[31], xboxkrnl::XePublicKeyData);
				break;

			case 356:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[32], &xboxkrnl::HalBootSMCVideoMode);
				break;

			case 357:
				XBOX_MEM_WRITE(pThunkTable[ordinal], KernelVarsSize[33], &xboxkrnl::IdexChannelObject);
				break;

			default:
				assert(0);
			}
		}
		i++;
		kt_tbl = *reinterpret_cast<uint32_t *>(XBOX_MEM_READ(reinterpret_cast<xbaddr>(kt + i), 4).data());
	}
}

typedef struct {
	xbaddr ThunkAddr;
	xbaddr LibNameAddr;
} XbeImportEntry;

void ImportLibraries(XbeImportEntry *pImportDirectory)
{
	// assert(pImportDirectory);

	while (pImportDirectory->LibNameAddr && pImportDirectory->ThunkAddr) {
		std::wstring LibName = std::wstring((wchar_t*)pImportDirectory->LibNameAddr);

		if (LibName == L"xbdm.dll") {
			MapThunkTable((uint32_t *)pImportDirectory->ThunkAddr, Cxbx_LibXbdmThunkTable, nullptr); // TODO: hook table for xbdm
		}
		else {
			// TODO: replace wprintf to EmuLogInit, how?
			wprintf(L"LOAD : Skipping unrecognized import library : %s\n", LibName.c_str());
		}

		pImportDirectory++;
	}
}

bool CreateSettings()
{
	g_Settings = new Settings();
	if (g_Settings == nullptr) {
		PopupError(nullptr, szSettings_alloc_error);
		return false;
	}

	if (!g_Settings->Init()) {
		return false;
	}

	log_get_settings();
	return true;
}

bool HandleFirstLaunch()
{
	bool bFirstLaunch;
	g_EmuShared->GetIsFirstLaunch(&bFirstLaunch);

	/* check if process is launch with elevated access then prompt for continue on or not. */
	if (!bFirstLaunch) {
		if (!CreateSettings()) {
			return false;
		}

		bool bElevated = CxbxIsElevated();
		if (bElevated && !g_Settings->m_core.allowAdminPrivilege) {
			PopupReturn ret = PopupWarningEx(nullptr, PopupButtons::YesNo, PopupReturn::No,
				"Cxbx-Reloaded has detected that it has been launched with Administrator rights.\n"
				"\nThis is dangerous, as a maliciously modified Xbox titles could take control of your system.\n"
				"\nAre you sure you want to continue?");
			if (ret != PopupReturn::Yes) {
				return false;
			}
		}

		g_EmuShared->SetIsFirstLaunch(true);
	}

	return true;
}

void CxbxKrnlEmulate(unsigned int reserved_systems, blocks_reserved_t blocks_reserved)
{
	std::string tempStr;

	// NOTE: This is designated for standalone kernel mode launch without GUI
	if (g_Settings != nullptr) {

		// Reset to default
		g_EmuShared->Reset();

		g_Settings->Verify();
		g_Settings->SyncToEmulator();

		// We don't need to keep Settings open plus allow emulator to use unused memory.
		delete g_Settings;
		g_Settings = nullptr;

		// Perform identical to what GUI will do to certain EmuShared's variable before launch.
		g_EmuShared->SetIsEmulating(true);

		// NOTE: This setting the ready status is optional. Internal kernel process is checking if GUI is running.
		// Except if enforce check, then we need to re-set ready status every time for non-GUI.
		//g_EmuShared->SetIsReady(true);
	}

	/* Initialize popup message management from kernel side. */
	log_init_popup_msg();

	/* Initialize Cxbx File Paths */
	CxbxInitFilePaths();

	// Skip '/load' switch
	// Get XBE Name :
	std::string xbePath;
	cli_config::GetValue(cli_config::load, &xbePath);
	xbePath = std::filesystem::absolute(std::filesystem::path(xbePath)).string();

	// Get DCHandle :
	// We must save this handle now to keep the child window working in the case we need to display the UEM
	HWND hWnd = nullptr;
	if (cli_config::GetValue(cli_config::hwnd, &tempStr)) {
		hWnd = (HWND)std::atoi(tempStr.c_str());
	}
	CxbxKrnl_hEmuParent = IsWindow(hWnd) ? hWnd : nullptr;

	// Get KernelDebugMode :
	DebugMode DbgMode = DebugMode::DM_NONE;
	if (cli_config::GetValue(cli_config::debug_mode, &tempStr)) {
		DbgMode = (DebugMode)std::atoi(tempStr.c_str());
	}

	// Get KernelDebugFileName :
	std::string DebugFileName = "";
	if (cli_config::GetValue(cli_config::debug_file, &tempStr)) {
		DebugFileName = tempStr;
	}

	int BootFlags;
	g_EmuShared->GetBootFlags(&BootFlags);

	g_CurrentProcessHandle = GetCurrentProcess(); // OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());

	// Set up the logging variables for the kernel process during initialization.
	log_sync_config();

	// When a reboot occur, we need to keep persistent memory buffer open before emulation process shutdown.
	if ((BootFlags & BOOT_QUICK_REBOOT) != 0) {
		g_VMManager.GetPersistentMemory();
	}

	if (CxbxKrnl_hEmuParent != NULL) {
		ipc_send_gui_update(IPC_UPDATE_GUI::KRNL_IS_READY, static_cast<UINT>(GetCurrentProcessId()));

		// Force wait until GUI process is ready
		do {
			int waitCounter = 10;
			bool isReady = false;

			while (waitCounter > 0) {
				g_EmuShared->GetIsReady(&isReady);
				if (isReady) {
					break;
				}
				waitCounter--;
				Sleep(100);
			}
			if (!isReady) {
				EmuLog(LOG_LEVEL::WARNING, "GUI process is not ready!");
				PopupReturn mbRet = PopupWarningEx(nullptr, PopupButtons::RetryCancel, PopupReturn::Cancel,
					"GUI process is not ready, do you wish to retry?");
				if (mbRet == PopupReturn::Retry) {
					continue;
				}
				CxbxKrnlShutDown();
			}
			break;
		} while (true);
	}

	g_EmuShared->SetIsReady(false);

	UINT prevKrnlProcID = 0;
	DWORD dwExitCode = EXIT_SUCCESS;
	g_EmuShared->GetKrnlProcID(&prevKrnlProcID);

	// Save current kernel proccess id for next reboot if will occur in the future.
	// And to tell previous kernel process we had take over. This allow reboot's shared memory buffer to survive.
	g_EmuShared->SetKrnlProcID(GetCurrentProcessId());

	// Force wait until previous kernel process is closed.
	if (prevKrnlProcID != 0) {
		HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, prevKrnlProcID);
		// If we do receive valid handle, let's do the next step.
		if (hProcess != NULL) {

			WaitForSingleObject(hProcess, INFINITE);

			GetExitCodeProcess(hProcess, &dwExitCode);
			CloseHandle(hProcess);
		}
	}

	if (dwExitCode != EXIT_SUCCESS) {// Stop emulation
		CxbxKrnlShutDown();
	}

	/* Must be called after CxbxInitFilePaths and previous kernel process shutdown. */
	if (!CxbxLockFilePath()) {
		return;
	}

	FILE* krnlLog = nullptr;
	// debug console allocation (if configured)
	if (DbgMode == DM_CONSOLE)
	{
		if (AllocConsole())
		{
			HANDLE StdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
			// Maximise the console scroll buffer height :
			CONSOLE_SCREEN_BUFFER_INFO coninfo;
			GetConsoleScreenBufferInfo(StdHandle, &coninfo);
			coninfo.dwSize.Y = SHRT_MAX - 1; // = 32767-1 = 32766 = maximum value that works
			SetConsoleScreenBufferSize(StdHandle, coninfo.dwSize);
			(void)freopen("CONOUT$", "wt", stdout);
			(void)freopen("CONIN$", "rt", stdin);
			SetConsoleTitle("Cxbx-Reloaded : Kernel Debug Console");
			SetConsoleTextAttribute(StdHandle, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
		}
	}
	else
	{
		FreeConsole();
		if (DbgMode == DM_FILE) {
			// Peform clean write to kernel log for first boot. Unless multi-xbe boot occur then perform append to existing log.
			krnlLog = freopen(DebugFileName.c_str(), ((BootFlags == DebugMode::DM_NONE) ? "wt" : "at"), stdout);
			// Append separator for better readability after reboot.
			if (BootFlags != DebugMode::DM_NONE) {
				std::cout << "\n------REBOOT------REBOOT------REBOOT------REBOOT------REBOOT------\n" << std::endl;
			}
		}
		else {
			char buffer[16];
			if (GetConsoleTitle(buffer, 16) != NULL)
				(void)freopen("nul", "w", stdout);
		}
	}

	bool isLogEnabled;
	g_EmuShared->GetIsKrnlLogEnabled(&isLogEnabled);
	g_bPrintfOn = isLogEnabled;

	g_EmuShared->ResetKrnl();

	// Write a header to the log
	{
		EmuLogInit(LOG_LEVEL::INFO, "Cxbx-Reloaded Version %s", CxbxVersionStr);

		time_t startTime = time(nullptr);
		struct tm* tm_info = localtime(&startTime);
		char timeString[26];
		strftime(timeString, 26, "%F %T", tm_info);
		EmuLogInit(LOG_LEVEL::INFO, "Log started at %s", timeString);

#ifdef _DEBUG_TRACE
		EmuLogInit(LOG_LEVEL::INFO, "Debug Trace Enabled.");
#else
		EmuLogInit(LOG_LEVEL::INFO, "Debug Trace Disabled.");
#endif
	}

	// Log once, since multi-xbe boot is appending to file instead of overwrite.
	if (BootFlags == BOOT_NONE) {
		log_generate_active_filter_output(CXBXR_MODULE::INIT);
	}

	// Detect Wine
	g_bIsWine = false;
	HMODULE hNtDll = GetModuleHandle("ntdll.dll");

	if (hNtDll != nullptr) {
		wine_get_version = (LPFN_WINEGETVERSION)GetProcAddress(hNtDll, "wine_get_version");
		if (wine_get_version) {
			g_bIsWine = true;
		}
	}
#ifndef LLE_CPU
	// Now we got the arguments, start by initializing the Xbox memory map :
	// PrepareXBoxMemoryMap()
	{
		// Our executable DOS image header must be loaded at 0x00010000
		// Assert(ExeDosHeader == XBE_IMAGE_BASE);

		// Determine EXE's header locations & size :
		ExeNtHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)ExeDosHeader + ExeDosHeader->e_lfanew); // = + 0x138
		ExeOptionalHeader = (PIMAGE_OPTIONAL_HEADER)&(ExeNtHeader->OptionalHeader);

		// verify base of code of our executable is 0x00001000
		if (ExeNtHeader->OptionalHeader.BaseOfCode != CXBX_BASE_OF_CODE)
		{
			PopupFatal(nullptr, "Cxbx-Reloaded executuable requires it's base of code to be 0x00001000");
			return; // TODO : Halt(0); 
		}

#ifndef CXBXR_EMU
		// verify virtual_memory_placeholder is located at 0x00011000
		if ((UINT_PTR)(&(virtual_memory_placeholder[0])) != (XBE_IMAGE_BASE + CXBX_BASE_OF_CODE))
		{
			PopupFatal(nullptr, "virtual_memory_placeholder is not loaded to base address 0x00011000 (which is a requirement for Xbox emulation)");
			return; // TODO : Halt(0); 
		}
#endif

		// Create a safe copy of the complete EXE header:
		DWORD ExeHeaderSize = ExeOptionalHeader->SizeOfHeaders; // Should end up as 0x400
		NewDosHeader = (PIMAGE_DOS_HEADER)VirtualAlloc(nullptr, ExeHeaderSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		memcpy(NewDosHeader, ExeDosHeader, ExeHeaderSize);

		// Determine NewOptionalHeader, required by RestoreExeImageHeader
		NewNtHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)NewDosHeader + ExeDosHeader->e_lfanew);
		NewOptionalHeader = (PIMAGE_OPTIONAL_HEADER)&(NewNtHeader->OptionalHeader);

		// Make sure the new DOS header points to the new relative NtHeader location:
		NewDosHeader->e_lfanew = (ULONG_PTR)NewNtHeader - XBE_IMAGE_BASE;

		// Note : NewOptionalHeader->ImageBase can stay at ExeOptionalHeader->ImageBase = 0x00010000

		// Note : Since virtual_memory_placeholder prevents overlap between reserved xbox memory
		// and Cxbx.exe sections, section headers don't have to be patched up.

		// Mark the virtual memory range completely accessible
		DWORD OldProtection;
		if (0 == VirtualProtect((void*)XBE_IMAGE_BASE, XBE_MAX_VA - XBE_IMAGE_BASE, PAGE_EXECUTE_READWRITE, &OldProtection)) {
			DWORD err = GetLastError();

			// Translate ErrorCode to String.
			LPTSTR Error = 0;
			if (::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,
				err,
				0,
				(LPTSTR)&Error,
				0,
				NULL) == 0) {
				// Failed in translating.
			}

			// Free the buffer.
			if (Error) {
				::LocalFree(Error);
				Error = 0;
			}
		}

		// Clear out the virtual memory range
		memset((void*)XBE_IMAGE_BASE, 0, XBE_MAX_VA - XBE_IMAGE_BASE);

		// Restore enough of the executable image headers to keep WinAPI's working :
		RestoreExeImageHeader();
	}
#endif
	// Load Per-Xbe Keys from the Cxbx-Reloaded AppData directory
	LoadXboxKeys(szFolder_CxbxReloadedData);

	EEPROM = CxbxRestoreEEPROM(szFilePath_EEPROM_bin);
	if (EEPROM == nullptr)
	{
		PopupFatal(nullptr, "Couldn't init EEPROM!");
		return; // TODO : Halt(0); 
	}

	// TODO : Instead of loading an Xbe here, initialize the kernel so that it will launch the Xbe on itself.
	// using XeLoadImage from LaunchDataPage->Header.szLaunchPath

	// Now we can load and run the XBE :
	// MapAndRunXBE(XbePath, DCHandle);
	XbeType xbeType = XbeType::xtRetail;
	{
		// NOTE: This is a safety to clean the file path for any malicious file path attempt.
		// Might want to move this into a utility function.
		size_t n, i;
		// Remove useless slashes before and after semicolon.
		std::string semicolon_search[] = { "\\;", ";\\", "/;", ";/" };
		std::string semicolon_str = ";";
		for (n = 0, i = 0; i < semicolon_search->size(); i++, n = 0) {
			while ((n = xbePath.find(semicolon_search[i], n)) != std::string::npos) {
				xbePath.replace(n, semicolon_search[i].size(), semicolon_str);
				n += semicolon_str.size();
			}
		}
		// Remove extra slashes.
		std::string slash_search[] = { "\\\\", "//" };
		std::string slash_str = "/";
		for (n = 0, i = 0; i < slash_search->size(); i++, n = 0) {
			while ((n = xbePath.find(slash_search[i], n)) != std::string::npos) {
				xbePath.replace(n, slash_search[i].size(), slash_str);
				n += slash_str.size();
			}
		}

		// Once clean up process is done, proceed set to global variable string.
		strncpy(szFilePath_Xbe, xbePath.c_str(), MAX_PATH - 1);
		std::replace(xbePath.begin(), xbePath.end(), ';', '/');
		// Load Xbe (this one will reside above WinMain's virtual_memory_placeholder)
		CxbxKrnl_Xbe = new Xbe(xbePath.c_str(), false); // TODO : Instead of using the Xbe class, port Dxbx _ReadXbeBlock()

		if (CxbxKrnl_Xbe->HasFatalError()) {
			CxbxKrnlCleanup(CxbxKrnl_Xbe->GetError().c_str());
			return;
		}

		// Check the signature of the xbe
		if (CxbxKrnl_Xbe->CheckSignature()) {
			EmuLogInit(LOG_LEVEL::INFO, "Valid xbe signature. Xbe is legit");
		}
		else {
			EmuLogInit(LOG_LEVEL::WARNING, "Invalid xbe signature. Homebrew, tampered or pirated xbe?");
		}

		// Check the integrity of the xbe sections
		for (uint32_t sectionIndex = 0; sectionIndex < CxbxKrnl_Xbe->m_Header.dwSections; sectionIndex++) {
			if (CxbxKrnl_Xbe->CheckSectionIntegrity(sectionIndex)) {
				EmuLogInit(LOG_LEVEL::INFO, "SHA hash check of section %s successful", CxbxKrnl_Xbe->m_szSectionName[sectionIndex]);
			}
			else {
				EmuLogInit(LOG_LEVEL::WARNING, "SHA hash of section %s doesn't match, section is corrupted", CxbxKrnl_Xbe->m_szSectionName[sectionIndex]);
			}
		}

		// If CLI has given console type, then enforce it.
		if (cli_config::hasKey(cli_config::system_chihiro)) {
			EmuLogInit(LOG_LEVEL::INFO, "Auto detect is disabled, running as chihiro.");
			xbeType = XbeType::xtChihiro;
		}
		else if (cli_config::hasKey(cli_config::system_devkit)) {
			EmuLogInit(LOG_LEVEL::INFO, "Auto detect is disabled, running as devkit.");
			xbeType = XbeType::xtDebug;
		}
		else if (cli_config::hasKey(cli_config::system_retail)) {
			EmuLogInit(LOG_LEVEL::INFO, "Auto detect is disabled, running as retail.");
			xbeType = XbeType::xtRetail;
		}
		// Otherwise, use auto detect method.
		else {
			// Detect XBE type :
			xbeType = CxbxKrnl_Xbe->GetXbeType();
			EmuLogInit(LOG_LEVEL::INFO, "Auto detect: XbeType = %s", GetXbeTypeToStr(xbeType));
		}

		EmuLogInit(LOG_LEVEL::INFO, "Host's compatible system types: %2X", reserved_systems);
		unsigned int emulate_system = 0;
		size_t ramsize;
		// Set reserved_systems which system we will about to emulate.
		if (isSystemFlagSupport(reserved_systems, SYSTEM_CHIHIRO) && xbeType == XbeType::xtChihiro) {
			emulate_system = SYSTEM_CHIHIRO;
			ramsize = CHIHIRO_MEMORY_SIZE;
		}
		else if (isSystemFlagSupport(reserved_systems, SYSTEM_DEVKIT) && xbeType == XbeType::xtDebug) {
			emulate_system = SYSTEM_DEVKIT;
			ramsize = CHIHIRO_MEMORY_SIZE;
		}
		else if (isSystemFlagSupport(reserved_systems, SYSTEM_XBOX) && xbeType == XbeType::xtRetail) {
			emulate_system = SYSTEM_XBOX;
			ramsize = XBOX_MEMORY_SIZE;
		}
		// If none of system type requested to emulate isn't supported on host's end. Then enforce failure.
		else {
			CxbxKrnlCleanup("Unable to emulate system type due to host is not able to reserve required memory ranges.");
			return;
		}
		// Clear emulation system from reserved systems to be free.
		reserved_systems &= ~emulate_system;

		// Once we have determine which system type to run as, enforce it in future reboots.
		if ((BootFlags & BOOT_QUICK_REBOOT) == 0) {
			const char* system_str = GetSystemTypeToStr(emulate_system);
			cli_config::SetSystemType(system_str);
		}

		// Register if we're running an Chihiro executable or a debug xbe, otherwise it's an Xbox retail executable
		g_bIsChihiro = (xbeType == XbeType::xtChihiro);
		g_bIsDebug = (xbeType == XbeType::xtDebug);
		g_bIsRetail = (xbeType == XbeType::xtRetail);

		// Disabled: The media board rom fails to run because it REQUIRES LLE USB, which is not yet enabled.
		// Chihiro games can be ran directly for now. 
		// This just means that you cannot access the Chihiro test menus and related stuff, games should still be okay
#if 0   
		// If the Xbe is Chihiro, and we were not launched by SEGABOOT, we need to load SEGABOOT from the Chihiro Media Board rom instead!
		// TODO: We also need to store the path of the loaded game, and mount it as the mediaboard filesystem
		// TODO: How to we detect who launched us, to prevent a reboot-loop
		if (g_bIsChihiro) {
			std::string chihiroMediaBoardRom = std::string(szFolder_CxbxReloadedData) + std::string("/EmuDisk/") + MediaBoardRomFile;
			if (!std::filesystem::exists(chihiroMediaBoardRom)) {
				CxbxKrnlCleanup("Chihiro Media Board ROM (fpr21042_m29w160et.bin) could not be found");
			}

			delete CxbxKrnl_Xbe;
			CxbxKrnl_Xbe = new Xbe(chihiroMediaBoardRom.c_str(), false);
		}
#endif

		// init xbox cpu
		g_CPU = new Cpu;
		if (g_CPU == nullptr) {
			CxbxKrnlCleanup("Failed to construct cpu object!\n");
		}
		g_CPU->Init(ramsize);

#ifndef LLE_CPU
#ifndef CXBXR_EMU
		// Only for GUI executable with emulation code.
		blocks_reserved_t blocks_reserved_gui = { 0 };
		// Reserve console system's memory ranges before start initialize.
		if (!ReserveAddressRanges(emulate_system, blocks_reserved_gui)) {
			CxbxKrnlCleanup("Failed to reserve required memory ranges!", GetLastError());
		}
		// Initialize the memory manager
		g_VMManager.Initialize(emulate_system, BootFlags, blocks_reserved_gui);
#else
		// Release unnecessary memory ranges to allow console/host to use those memory ranges.
		FreeAddressRanges(emulate_system, reserved_systems, blocks_reserved);
		// Initialize the memory manager
		g_VMManager.Initialize(emulate_system, BootFlags, blocks_reserved);
#endif
#endif
		// Initialize the memory manager
		g_VMManager.Initialize(emulate_system, BootFlags);

		// Reserve the xbe image memory
		VAddr xbe_base = CxbxKrnl_Xbe->m_Header.dwBaseAddr;
		size_t size = CxbxKrnl_Xbe->m_Header.dwSizeofImage;
		g_VMManager.XbAllocateVirtualMemory(&xbe_base, 0, &size, XBOX_MEM_RESERVE, XBOX_PAGE_READWRITE);

		// Commit the memory used by the xbe header
		xbe_base = CxbxKrnl_Xbe->m_Header.dwBaseAddr;
		size = CxbxKrnl_Xbe->m_Header.dwSizeofHeaders;
		g_VMManager.XbAllocateVirtualMemory(&xbe_base, 0, &size, XBOX_MEM_COMMIT, XBOX_PAGE_READWRITE);

		// Copy over loaded Xbe Headers to specified base address
		XBOX_MEM_WRITE(CxbxKrnl_Xbe->m_Header.dwBaseAddr, sizeof(Xbe::Header), &CxbxKrnl_Xbe->m_Header);
		XBOX_MEM_WRITE(CxbxKrnl_Xbe->m_Header.dwBaseAddr + sizeof(Xbe::Header), CxbxKrnl_Xbe->m_ExSize, CxbxKrnl_Xbe->m_HeaderEx);

		// Load all sections marked as preload using the in-memory copy of the xbe header
		xboxkrnl::PXBEIMAGE_SECTION sectionHeaders = (xboxkrnl::PXBEIMAGE_SECTION)CxbxKrnl_Xbe->m_Header.dwSectionHeadersAddr;
		for (uint32_t i = 0; i < CxbxKrnl_Xbe->m_Header.dwSections; i++) {
			if ((CxbxKrnl_Xbe->m_SectionHeader[i].dwFlags_value & XBEIMAGE_SECTION_PRELOAD) != 0) {
				xboxkrnl::NTSTATUS result = xboxkrnl::XeLoadSection(sectionHeaders);
				if (FAILED(result)) {
					EmuLogInit(LOG_LEVEL::WARNING, "Failed to preload XBE section: %s", CxbxKrnl_Xbe->m_szSectionName[i]);
				}
			}
			sectionHeaders++;
		}
#ifndef LLE_CPU
		// We need to remember a few XbeHeader fields, so we can switch between a valid ExeHeader and XbeHeader :
		StoreXbeImageHeader();

		// Restore enough of the executable image headers to keep WinAPI's working :
		RestoreExeImageHeader();

		// HACK: Attempt to patch out XBE header reads
		// This works by searching for the XBEH signature and replacing it with what appears in host address space instead
		// Test case: Half Life 2
		// Iterate through each CODE section
		for (uint32_t sectionIndex = 0; sectionIndex < CxbxKrnl_Xbe->m_Header.dwSections; sectionIndex++) {
			if (!CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwFlags.bExecutable) {
				continue;
			}

			EmuLogInit(LOG_LEVEL::INFO, "Searching for XBEH in section %s", CxbxKrnl_Xbe->m_szSectionName[sectionIndex]);
			xbaddr startAddr = CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwVirtualAddr;
			xbaddr endAddr = startAddr + CxbxKrnl_Xbe->m_SectionHeader[sectionIndex].dwSizeofRaw;
			for (xbaddr addr = startAddr; addr < endAddr; addr++) {
				if (*(uint32_t*)addr == 0x48454258) {
					EmuLogInit(LOG_LEVEL::INFO, "Patching XBEH at 0x%08X", addr);
					*((uint32_t*)addr) = *(uint32_t*)XBE_IMAGE_BASE;
				}
			}
		}
#endif
	}

	// Decode kernel thunk table address :
	uint32_t kt = CxbxKrnl_Xbe->m_Header.dwKernelImageThunkAddr;
	kt ^= XOR_KT_KEY[to_underlying(xbeType)];

	// Process the Kernel thunk table to map Kernel function calls to their actual address :
	MapThunkTable((uint32_t*)kt, CxbxKrnl_KernelThunkTable, CxbxKrnl_KernelHookTable);
#if 0
	// Does this xbe import any other libraries?
	if (CxbxKrnl_Xbe->m_Header.dwNonKernelImportDirAddr) {
		ImportLibraries((XbeImportEntry*)CxbxKrnl_Xbe->m_Header.dwNonKernelImportDirAddr);
	}
#endif
	// Launch the XBE :
	{
		// Load TLS
		Xbe::TLS* XbeTls = (Xbe::TLS*)CxbxKrnl_Xbe->m_Header.dwTLSAddr;
		void* XbeTlsData = (XbeTls != nullptr) ? (void*)CxbxKrnl_Xbe->m_TLS->dwDataStartAddr : nullptr;
		// Decode Entry Point
		xbaddr EntryPoint = CxbxKrnl_Xbe->m_Header.dwEntryAddr;
		EntryPoint ^= XOR_EP_KEY[to_underlying(xbeType)];
		// Launch XBE
		CxbxKrnlInit(
			XbeTlsData, 
			XbeTls, 
			CxbxKrnl_Xbe->m_LibraryVersion, 
			DbgMode,
			DebugFileName.c_str(),
			(Xbe::Header*)CxbxKrnl_Xbe->m_Header.dwBaseAddr,
			CxbxKrnl_Xbe->m_Header.dwSizeofHeaders,
			(void(*)())EntryPoint,
 			BootFlags
		);
	}

	if (!krnlLog) {
		(void)fclose(krnlLog);
	}
}
#pragma optimize("", on)

// Loads a keys.bin file as generated by dump-xbox
// See https://github.com/JayFoxRox/xqemu-tools/blob/master/dump-xbox.c
void LoadXboxKeys(std::string path)
{
	std::string keys_path = path + "\\keys.bin";

	// Attempt to open Keys.bin
	FILE* fp = fopen(keys_path.c_str(), "rb");

	if (fp != nullptr) {
		// Determine size of Keys.bin
		xboxkrnl::XBOX_KEY_DATA keys[2];
		fseek(fp, 0, SEEK_END);
		long size = ftell(fp);
		rewind(fp);

		// If the size of Keys.bin is correct (two keys), read it
		if (size == xboxkrnl::XBOX_KEY_LENGTH * 2) {
			fread(keys, xboxkrnl::XBOX_KEY_LENGTH, 2, fp);

			memcpy(xboxkrnl::XboxEEPROMKey, &keys[0], xboxkrnl::XBOX_KEY_LENGTH);
			memcpy(xboxkrnl::XboxCertificateKey, &keys[1], xboxkrnl::XBOX_KEY_LENGTH);
		}
		else {
			EmuLog(LOG_LEVEL::WARNING, "Keys.bin has an incorrect filesize. Should be %d bytes", xboxkrnl::XBOX_KEY_LENGTH * 2);
		}

		fclose(fp);
		return;
	}

	// If we didn't already exit the function, keys.bin could not be loaded
	EmuLog(LOG_LEVEL::WARNING, "Failed to load Keys.bin. Cxbx-Reloaded will be unable to read Save Data from a real Xbox");
}

void CxbxKrnlInit
(
	void                   *pTLSData,
	Xbe::TLS               *pTLS,
	Xbe::LibraryVersion    *pLibraryVersion,
	DebugMode               DbgMode,
	const char             *szDebugFilename,
	Xbe::Header            *pXbeHeader,
	uint32_t                dwXbeHeaderSize,
	void(*Entry)(),
	int BootFlags)
{
    // Set windows timer period to 1ms
    // Windows will automatically restore this value back to original on program exit
    // But with this, we can replace some busy loops with sleeps.
    timeBeginPeriod(1);

    xboxkrnl::InitializeFscCacheEvent();

	// update caches
	CxbxKrnl_TLS = pTLS;
	CxbxKrnl_TLSData = pTLSData;
	CxbxKrnl_XbeHeader = pXbeHeader;
	CxbxKrnl_DebugMode = DbgMode;
	CxbxKrnl_DebugFileName = (char*)szDebugFilename;

	// A patch to dwCertificateAddr is a requirement due to Windows TLS is overwriting dwGameRegion data address.
	// By using unalternated certificate data, it should no longer cause any problem with titles running and Cxbx's log as well.
	CxbxKrnl_XbeHeader->dwCertificateAddr = (uint32_t)&CxbxKrnl_Xbe->m_Certificate;
	g_pCertificate = &CxbxKrnl_Xbe->m_Certificate;

	// Initialize timer subsystem
	Timer_Init();
	// for unicode conversions
	setlocale(LC_ALL, "English");
	// Initialize time-related variables for the kernel and the timers
	CxbxInitPerformanceCounters();
#ifdef _DEBUG
//	PopupCustom(LOG_LEVEL::INFO, "Attach a Debugger");
//  Debug child processes using https://marketplace.visualstudio.com/items?itemName=GreggMiskelly.MicrosoftChildProcessDebuggingPowerTool
#endif

	// debug trace
	{
#ifdef _DEBUG_TRACE
		EmuLogInit(LOG_LEVEL::INFO, "Debug Trace Enabled.");
		EmuLogInit(LOG_LEVEL::INFO, "CxbxKrnlInit\n"
			"(\n"
			"   hwndParent          : 0x%.08p\n"
			"   pTLSData            : 0x%.08p\n"
			"   pTLS                : 0x%.08p\n"
			"   pLibraryVersion     : 0x%.08p\n"
			"   DebugConsole        : 0x%.08X\n"
			"   DebugFilename       : \"%s\"\n"
			"   pXBEHeader          : 0x%.08p\n"
			"   dwXBEHeaderSize     : 0x%.08X\n"
			"   Entry               : 0x%.08p\n"
			");",
			CxbxKrnl_hEmuParent, pTLSData, pTLS, pLibraryVersion, DbgMode, szDebugFilename, pXbeHeader, dwXbeHeaderSize, Entry);
#else
		EmuLogInit(LOG_LEVEL::INFO, "Debug Trace Disabled.");
#endif
	}

#ifdef _DEBUG_TRACE
	// VerifyHLEDataBase();
#endif
	// TODO : The following seems to cause a crash when booting the game "Forza Motorsport",
	// according to https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/issues/101#issuecomment-277230140
	{
		// Create a fake kernel header for XapiRestrictCodeSelectorLimit
		// Thanks advancingdragon / DirtBox
		PDUMMY_KERNEL DummyKernel = (PDUMMY_KERNEL)XBOX_KERNEL_BASE;
		memset(DummyKernel, 0, sizeof(DUMMY_KERNEL));

		// XapiRestrictCodeSelectorLimit only checks these fields.
		DummyKernel->DosHeader.e_lfanew = sizeof(IMAGE_DOS_HEADER); // RVA of NtHeaders
		DummyKernel->FileHeader.SizeOfOptionalHeader = 0;
		DummyKernel->FileHeader.NumberOfSections = 1;
		// as long as this doesn't start with "INIT"
		strncpy_s((PSTR)DummyKernel->SectionHeader.Name, 8, "DONGS", 8);
		EmuLogInit(LOG_LEVEL::INFO, "Initialized dummy kernel image header.");
	}

	// Read which components need to be LLE'ed per user request
	{
		unsigned int CxbxLLE_Flags;
		g_EmuShared->GetFlagsLLE(&CxbxLLE_Flags);
		bLLE_APU = (CxbxLLE_Flags & LLE_APU) > 0;
		bLLE_GPU = (CxbxLLE_Flags & LLE_GPU) > 0;
		//bLLE_USB = (CxbxLLE_Flags & LLE_USB) > 0; // Reenable this when LLE USB actually works
		bLLE_JIT = (CxbxLLE_Flags & LLE_JIT) > 0;
	}

	// Process Hacks
	{
		int HackEnabled = 0;
		g_EmuShared->GetDisablePixelShaders(&HackEnabled);
		g_DisablePixelShaders = !!HackEnabled;
		g_EmuShared->GetUseAllCores(&HackEnabled);
		g_UseAllCores = !!HackEnabled;
		g_EmuShared->GetSkipRdtscPatching(&HackEnabled);
		g_SkipRdtscPatching = !!HackEnabled;
	}

#ifdef _DEBUG_PRINT_CURRENT_CONF
	PrintCurrentConfigurationLog();
#endif
	
	// Initialize devices :
	char szBuffer[sizeof(szFilePath_Xbe)];
	g_EmuShared->GetStorageLocation(szBuffer);

	CxbxBasePath = std::string(szBuffer) + "\\EmuDisk\\";

	// Determine XBE Path
	strncpy(szBuffer, szFilePath_Xbe, sizeof(szBuffer)-1);
	szBuffer[sizeof(szBuffer) - 1] = '\0'; // Safely null terminate at the end.

	std::string xbePath(szBuffer);
	std::replace(xbePath.begin(), xbePath.end(), ';', '/');
	std::string xbeDirectory(szBuffer);
	size_t lastFind = xbeDirectory.find(';');
	// First find if there is a semicolon when dashboard or title disc (such as demo disc) has it.
	// Then we must obey the current directory it asked for.
	if (lastFind != std::string::npos) {
		if (xbeDirectory.find(';', lastFind + 1) != std::string::npos) {
			CxbxKrnlCleanupEx(LOG_PREFIX_INIT, "Cannot contain multiple of ; symbol.");
		}
		xbeDirectory = xbeDirectory.substr(0, lastFind);
	}
	else {
		xbeDirectory = xbeDirectory.substr(0, xbeDirectory.find_last_of("\\/"));
	}
	CxbxBasePathHandle = CreateFile(CxbxBasePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	memset(szBuffer, 0, sizeof(szBuffer));
	// Games may assume they are running from CdRom :
	CxbxDefaultXbeDriveIndex = CxbxRegisterDeviceHostPath(DeviceCdrom0, xbeDirectory);
	// Partition 0 contains configuration data, and is accessed as a native file, instead as a folder :
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition0, CxbxBasePath + "Partition0", /*IsFile=*/true);
	// The first two partitions are for Data and Shell files, respectively :
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition1, CxbxBasePath + "Partition1");
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition2, CxbxBasePath + "Partition2");
	// The following partitions are for caching purposes - for now we allocate up to 7 (as xbmp needs that many) :
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition3, CxbxBasePath + "Partition3");
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition4, CxbxBasePath + "Partition4");
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition5, CxbxBasePath + "Partition5");
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition6, CxbxBasePath + "Partition6");
	CxbxRegisterDeviceHostPath(DeviceHarddisk0Partition7, CxbxBasePath + "Partition7");

	// Create default symbolic links :
	EmuLogInit(LOG_LEVEL::DEBUG, "Creating default symbolic links.");
	{
		// TODO: DriveD should always point to the Xbe Path
		// This is the only symbolic link the Xbox Kernel sets, the rest are set by the application, usually via XAPI.
		// If the Xbe is located outside of the emulated HDD, mounting it as DeviceCdrom0 is correct
		// If the Xbe is located inside the emulated HDD, the full path should be used, eg: "\\Harddisk0\\partition2\\xboxdash.xbe"
		CxbxCreateSymbolicLink(DriveD, DeviceCdrom0);
		// Arrange that the Xbe path can reside outside the partitions, and put it to g_hCurDir :
		EmuNtSymbolicLinkObject* xbePathSymbolicLinkObject = FindNtSymbolicLinkObjectByDriveLetter(CxbxDefaultXbeDriveLetter);
		g_hCurDir = xbePathSymbolicLinkObject->RootDirectoryHandle;
	}

	// Determine Xbox path to XBE and place it in XeImageFileName
	{
		std::string fileName(xbePath);
		// Strip out the path, leaving only the XBE file name
		// NOTE: we assume that the XBE is always on the root of the D: drive
		// This is a safe assumption as the Xbox kernel ALWAYS mounts D: as the Xbe Path
		if (fileName.rfind('\\') != std::string::npos)
			fileName = fileName.substr(fileName.rfind('\\') + 1);

		if (xboxkrnl::XeImageFileName.Buffer != NULL)
			free(xboxkrnl::XeImageFileName.Buffer);

		// Assign the running Xbe path, so it can be accessed via the kernel thunk 'XeImageFileName' :
		xboxkrnl::XeImageFileName.MaximumLength = MAX_PATH;
		xboxkrnl::XeImageFileName.Buffer = (PCHAR)g_VMManager.Allocate(MAX_PATH);
		sprintf(xboxkrnl::XeImageFileName.Buffer, "%c:\\%s", CxbxDefaultXbeDriveLetter, fileName.c_str());
		xboxkrnl::XeImageFileName.Length = (USHORT)strlen(xboxkrnl::XeImageFileName.Buffer);
		EmuLogInit(LOG_LEVEL::INFO, "XeImageFileName = %s", xboxkrnl::XeImageFileName.Buffer);
	}

	// Dump Xbe information
	{
		if (CxbxKrnl_Xbe != nullptr) {
			EmuLogInit(LOG_LEVEL::INFO, "Title : %s", CxbxKrnl_Xbe->m_szAsciiTitle);
		}

		// Dump Xbe certificate
		if (g_pCertificate != NULL) {
			std::stringstream titleIdHex;
			titleIdHex << std::hex << g_pCertificate->dwTitleId;

			EmuLogInit(LOG_LEVEL::INFO, "XBE TitleID : %s", FormatTitleId(g_pCertificate->dwTitleId).c_str());
			EmuLogInit(LOG_LEVEL::INFO, "XBE TitleID (Hex) : 0x%s", titleIdHex.str().c_str());
			EmuLogInit(LOG_LEVEL::INFO, "XBE Version : 1.%02d", g_pCertificate->dwVersion);
			EmuLogInit(LOG_LEVEL::INFO, "XBE TitleName : %ls", g_pCertificate->wszTitleName);
			EmuLogInit(LOG_LEVEL::INFO, "XBE Region : %s", CxbxKrnl_Xbe->GameRegionToString());
		}

		// Dump Xbe library build numbers
		Xbe::LibraryVersion* libVersionInfo = pLibraryVersion;// (LibraryVersion *)(CxbxKrnl_XbeHeader->dwLibraryVersionsAddr);
		if (libVersionInfo != NULL) {
			for (uint32_t v = 0; v < CxbxKrnl_XbeHeader->dwLibraryVersions; v++) {
				EmuLogInit(LOG_LEVEL::INFO, "XBE Library %u : %.8s (version %d)", v, libVersionInfo->szName, libVersionInfo->wBuildVersion);
				libVersionInfo++;
			}
		}
	}

	CxbxKrnlRegisterThread(GetCurrentThread());

	// Make sure the Xbox1 code runs on one core (as the box itself has only 1 CPU,
	// this will better aproximate the environment with regard to multi-threading) :
	EmuLogInit(LOG_LEVEL::DEBUG, "Determining CPU affinity.");
	{
		if (!GetProcessAffinityMask(g_CurrentProcessHandle, &g_CPUXbox, &g_CPUOthers))
			CxbxKrnlCleanupEx(LOG_PREFIX_INIT, "GetProcessAffinityMask failed.");

		// For the other threads, remove one bit from the processor mask:
		g_CPUOthers = ((g_CPUXbox - 1) & g_CPUXbox);

		// Test if there are any other cores available :
		if (g_CPUOthers > 0) {
			// If so, make sure the Xbox threads run on the core NOT running Xbox code :
			g_CPUXbox = g_CPUXbox & (~g_CPUOthers);
		} else {
			// Else the other threads must run on the same core as the Xbox code :
			g_CPUOthers = g_CPUXbox;
		}
	}

	// initialize graphics
	EmuLogInit(LOG_LEVEL::DEBUG, "Initializing render window.");
	CxbxInitWindow(true);

	// Now process the boot flags to see if there are any special conditions to handle
	if (BootFlags & BOOT_EJECT_PENDING) {} // TODO
	if (BootFlags & BOOT_FATAL_ERROR)
	{
		// If we are here it means we have been rebooted to display the fatal error screen. The error code is set
		// to 0x15 and the led flashes with the sequence green, red, red, red

		SetLEDSequence(0xE1);
		CxbxKrnlPrintUEM(FATAL_ERROR_REBOOT_ROUTINE); // won't return
	}
	if (BootFlags & BOOT_SKIP_ANIMATION) {} // TODO
	if (BootFlags & BOOT_RUN_DASHBOARD) {} // TODO

    CxbxInitAudio();

	EmuHLEIntercept(pXbeHeader);

	if (!bLLE_USB) {
		SetupXboxDeviceTypes();
	}

	InitXboxHardware(HardwareModel::Revision1_5); // TODO : Make configurable

	// Read Xbox video mode from the SMC, store it in HalBootSMCVideoMode
	xboxkrnl::HalReadSMBusValue(SMBUS_ADDRESS_SYSTEM_MICRO_CONTROLLER, SMC_COMMAND_AV_PACK, FALSE, &xboxkrnl::HalBootSMCVideoMode);

	g_InputDeviceManager.Initialize(false);

	// Now the hardware devices exist, couple the EEPROM buffer to it's device
	g_EEPROM->SetEEPROM((uint8_t*)EEPROM);

	if (!bLLE_GPU)
	{
		EmuLogInit(LOG_LEVEL::DEBUG, "Initializing Direct3D.");
		EmuD3DInit();
	}
	
	if (CxbxDebugger::CanReport())
	{
		CxbxDebugger::ReportDebuggerInit(CxbxKrnl_Xbe->m_szAsciiTitle);
	}

	// Apply Media Patches to bypass Anti-Piracy checks
	// Required until we perfect emulation of X2 DVD Authentication
	// See: https://multimedia.cx/eggs/xbox-sphinx-protocol/
	ApplyMediaPatches();

	// Chihiro games require more patches
	// The chihiro BIOS does this to bypass XAPI cache init
	if (g_bIsChihiro) {
		CxbxKrnl_XbeHeader->dwInitFlags.bDontSetupHarddisk = true;
	}

	if(!g_SkipRdtscPatching)
	{ 
		PatchRdtscInstructions();
	}

	// Setup per-title encryption keys
	SetupPerTitleKeys();

	EmuInitFS();

	InitXboxThread(g_CPUXbox);
	xboxkrnl::ObInitSystem();
	xboxkrnl::KiInitSystem();

	EmuX86_Init();
	// Create the interrupt processing thread
	DWORD dwThreadId;
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, NULL, CxbxKrnlInterruptThread, NULL, NULL, (unsigned int*)&dwThreadId);
	// Start the kernel clock thread
	TimerObject* KernelClockThr = Timer_Create(CxbxKrnlClockThread, nullptr, "Kernel clock thread", &g_CPUOthers);
	Timer_Start(KernelClockThr, SCALE_MS_IN_NS);

	EmuLogInit(LOG_LEVEL::DEBUG, "Calling XBE entry point...");
	CxbxLaunchXbe(Entry);

	// FIXME: Wait for Cxbx to exit or error fatally
	Sleep(INFINITE);

	EmuLogInit(LOG_LEVEL::DEBUG, "XBE entry point returned");
	fflush(stdout);

	CxbxUnlockFilePath();

	//	EmuShared::Cleanup();   FIXME: commenting this line is a bad workaround for issue #617 (https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/issues/617)
    CxbxKrnlTerminateThread();
}

void CxbxInitFilePaths()
{
	if (g_Settings) {
		std::string dataLoc = g_Settings->GetDataLocation();
		std::strncpy(szFolder_CxbxReloadedData, dataLoc.c_str(), dataLoc.length() + 1);
	}
	else {
		g_EmuShared->GetStorageLocation(szFolder_CxbxReloadedData);
	}

	// Make sure our data folder exists :
	bool result = std::filesystem::exists(szFolder_CxbxReloadedData);
	if (!result && !std::filesystem::create_directory(szFolder_CxbxReloadedData)) {
		CxbxKrnlCleanup("%s : Couldn't create Cxbx-Reloaded's data folder!", __func__);
	}

	// Make sure the EmuDisk folder exists
	std::string emuDisk = std::string(szFolder_CxbxReloadedData) + std::string("\\EmuDisk");
	result = std::filesystem::exists(emuDisk);
	if (!result && !std::filesystem::create_directory(emuDisk)) {
		CxbxKrnlCleanup("%s : Couldn't create Cxbx-Reloaded EmuDisk folder!", __func__);
	}

	snprintf(szFilePath_EEPROM_bin, MAX_PATH, "%s\\EEPROM.bin", szFolder_CxbxReloadedData);

	GetModuleFileName(GetModuleHandle(nullptr), szFilePath_CxbxReloaded_Exe, MAX_PATH);
}

HANDLE hMapDataHash = nullptr;

bool CxbxLockFilePath()
{
    std::stringstream filePathHash("Local\\");
    uint64_t hashValue = XXH3_64bits(szFolder_CxbxReloadedData, strlen(szFolder_CxbxReloadedData) + 1);
    if (!hashValue) {
        CxbxKrnlCleanup("%s : Couldn't generate Cxbx-Reloaded's data folder hash!", __func__);
    }

    filePathHash << std::hex << hashValue;

    hMapDataHash = CreateFileMapping
    (
        INVALID_HANDLE_VALUE,       // Paging file
        nullptr,                    // default security attributes
        PAGE_READONLY,              // readonly access
        0,                          // size: high 32 bits
        /*Dummy size*/4,            // size: low 32 bits
        filePathHash.str().c_str()  // name of map object
    );

    if (hMapDataHash == nullptr) {
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        PopupError(nullptr, "Data path directory is currently in used.\nUse different data path directory or stop emulation from another process.");
        CloseHandle(hMapDataHash);
        return false;
    }

    return true;
}

void CxbxUnlockFilePath()
{
    // Close opened file path lockdown shared memory.
    if (hMapDataHash) {
        CloseHandle(hMapDataHash);
        hMapDataHash = nullptr;
    }
}

// REMARK: the following is useless, but PatrickvL has asked to keep it for documentation purposes
/*xboxkrnl::LAUNCH_DATA_PAGE DefaultLaunchDataPage =
{
	{   // header
		2,  // 2: dashboard, 0: title
		0,
		"D:\\default.xbe",
		0
	}
};*/

void CxbxKrnlCleanupEx(CXBXR_MODULE cxbxr_module, const char *szErrorMessage, ...)
{
    g_bEmuException = true;

    CxbxKrnlResume();

    // print out error message (if exists)
    if(szErrorMessage != NULL)
    {
        char szBuffer2[1024];
        va_list argp;

        va_start(argp, szErrorMessage);
        vsprintf(szBuffer2, szErrorMessage, argp);
        va_end(argp);

		(void)PopupCustomEx(nullptr, cxbxr_module, LOG_LEVEL::FATAL, PopupIcon::Error, PopupButtons::Ok, PopupReturn::Ok, "Received Fatal Message:\n\n* %s\n", szBuffer2); // Will also EmuLogEx
    }

	EmuLogInit(LOG_LEVEL::INFO, "MAIN: Terminating Process");
    fflush(stdout);

    // cleanup debug output
    {
        FreeConsole();

        char buffer[16];

        if(GetConsoleTitle(buffer, 16) != NULL)
            freopen("nul", "w", stdout);
    }

	CxbxKrnlShutDown();
}

void CxbxKrnlRegisterThread(HANDLE hThread)
{
	// we must duplicate this handle in order to retain Suspend/Resume thread rights from a remote thread
	{
		HANDLE hDupHandle = NULL;

		if (DuplicateHandle(g_CurrentProcessHandle, hThread, g_CurrentProcessHandle, &hDupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			hThread = hDupHandle; // Thread handle was duplicated, continue registration with the duplicate
		}
		else {
			auto message = CxbxGetLastErrorString("DuplicateHandle");
			EmuLog(LOG_LEVEL::WARNING, message.c_str());
		}
	}

	g_hThreads.push_back(hThread);
}

void CxbxKrnlSuspend()
{
    if(g_bEmuSuspended || g_bEmuException)
        return;

    for (auto it = g_hThreads.begin(); it != g_hThreads.end(); ++it)
    {
        DWORD dwExitCode;

        if(GetExitCodeThread(*it, &dwExitCode) && dwExitCode == STILL_ACTIVE) {
            // suspend thread if it is active
            SuspendThread(*it);
        } else {
            // remove thread from thread list if it is dead
			g_hThreads.erase(it);
        }
    }

    // append 'paused' to rendering window caption text
    {
        char szBuffer[256];

        HWND hWnd = GET_FRONT_WINDOW_HANDLE;

        GetWindowText(hWnd, szBuffer, 255 - 10);

        strcat(szBuffer, " (paused)");
        SetWindowText(hWnd, szBuffer);
    }

    g_bEmuSuspended = true;
}

void CxbxKrnlResume()
{
    if(!g_bEmuSuspended)
        return;

    // remove 'paused' from rendering window caption text
    {
        char szBuffer[256];

        HWND hWnd = GET_FRONT_WINDOW_HANDLE;

        GetWindowText(hWnd, szBuffer, 255);

        szBuffer[strlen(szBuffer)-9] = '\0';

        SetWindowText(hWnd, szBuffer);
    }

	for (auto it = g_hThreads.begin(); it != g_hThreads.end(); ++it)
	{
		DWORD dwExitCode;

		if (GetExitCodeThread(*it, &dwExitCode) && dwExitCode == STILL_ACTIVE) {
			// resume thread if it is active
			ResumeThread(*it);
		}
		else {
			// remove thread from thread list if it is dead
			g_hThreads.erase(it);
		}
	}

    g_bEmuSuspended = false;
}

void CxbxKrnlShutDown()
{
	// Clear all kernel boot flags. These (together with the shared memory) persist until Cxbx-Reloaded is closed otherwise.
	int BootFlags = 0;
	g_EmuShared->SetBootFlags(&BootFlags);

	// NOTE: This causes a hang when exiting while NV2A is processing
	// This is okay for now: It won't leak memory or resources since TerminateProcess will free everything
	// delete g_NV2A; // TODO : g_pXbox

	// Shutdown the input device manager
	g_InputDeviceManager.Shutdown();

	// Shutdown the memory manager
	g_VMManager.Shutdown();

	CxbxUnlockFilePath();

	if (CxbxKrnl_hEmuParent != NULL) {
		SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);
	}

	EmuShared::Cleanup();
	TerminateProcess(g_CurrentProcessHandle, 0);
}

void CxbxKrnlPrintUEM(ULONG ErrorCode)
{
	ULONG Type;
	xboxkrnl::XBOX_EEPROM Eeprom;
	ULONG ResultSize;

	xboxkrnl::NTSTATUS status = xboxkrnl::ExQueryNonVolatileSetting(xboxkrnl::XC_MAX_ALL, &Type, &Eeprom, sizeof(Eeprom), &ResultSize);

	if (status == STATUS_SUCCESS)
	{
		xboxkrnl::XBOX_UEM_INFO* UEMInfo = (xboxkrnl::XBOX_UEM_INFO*)&(Eeprom.UEMInfo[0]);

		if (UEMInfo->ErrorCode == FATAL_ERROR_NONE)
		{
			// ergo720: the Xbox sets the error code and displays the UEM only for non-manufacturing xbe's (it power cycles
			// otherwise). Considering that this flag can be easily tampered with in the xbe and the typical end user of cxbx
			// can't fix the cause of the fatal error, I decided to always display it anyway.

			UEMInfo->ErrorCode = (UCHAR)ErrorCode;
			UEMInfo->History |= (1 << (ErrorCode - 5));
		}
		else {
			UEMInfo->ErrorCode = FATAL_ERROR_NONE;
		}
		xboxkrnl::ExSaveNonVolatileSetting(xboxkrnl::XC_MAX_ALL, Type, &Eeprom, sizeof(Eeprom));
	}
	else {
		CxbxKrnlCleanup("Could not display the fatal error screen");
	}

	if (g_bIsChihiro)
	{
		// The Chihiro doesn't display the UEM
		CxbxKrnlCleanup("The running Chihiro xbe has encountered a fatal error and needs to close");
	}

	g_CxbxFatalErrorCode = ErrorCode;
	g_CxbxPrintUEM = true; // print the UEM

	CxbxPrintUEMInfo(ErrorCode);

	// Sleep forever to prevent continuing the initialization
	Sleep(INFINITE);
}

void CxbxPrintUEMInfo(ULONG ErrorCode)
{
	// See here for a description of the error codes and their meanings:
	// https://www.reddit.com/r/originalxbox/wiki/error_codes

	std::map<int, std::string> UEMErrorTable;

	UEMErrorTable.emplace(FATAL_ERROR_CORE_DIGITAL, "General motherboard issue");
	UEMErrorTable.emplace(FATAL_ERROR_BAD_EEPROM, "General EEPROM issue");
	UEMErrorTable.emplace(FATAL_ERROR_BAD_RAM, "RAM failure");
	UEMErrorTable.emplace(FATAL_ERROR_HDD_NOT_LOCKED, "HDD is not locked");
	UEMErrorTable.emplace(FATAL_ERROR_HDD_CANNOT_UNLOCK, "Unable to unlock HDD (bad password?)");
	UEMErrorTable.emplace(FATAL_ERROR_HDD_TIMEOUT, "HDD failed to respond");
	UEMErrorTable.emplace(FATAL_ERROR_HDD_NOT_FOUND, "Missing HDD");
	UEMErrorTable.emplace(FATAL_ERROR_HDD_BAD_CONFIG, "Invalid / missing HDD parameter(s)");
	UEMErrorTable.emplace(FATAL_ERROR_DVD_TIMEOUT, "DVD drive failed to respond");
	UEMErrorTable.emplace(FATAL_ERROR_DVD_NOT_FOUND, "Missing DVD drive");
	UEMErrorTable.emplace(FATAL_ERROR_DVD_BAD_CONFIG, "Invalid / missing DVD drive parameter(s)");
	UEMErrorTable.emplace(FATAL_ERROR_XBE_DASH_GENERIC, "Generic MS dashboard issue (dashboard not installed?)");
	UEMErrorTable.emplace(FATAL_ERROR_XBE_DASH_ERROR, "General MS dashboard issue");
	UEMErrorTable.emplace(FATAL_ERROR_XBE_DASH_SETTINGS, "MS dashboard issue: cannot reset console clock");
	UEMErrorTable.emplace(FATAL_ERROR_XBE_DASH_X2_PASS, "General MS dashboard issue, DVD drive authentication was successfull");
	UEMErrorTable.emplace(FATAL_ERROR_REBOOT_ROUTINE, "The console was instructed to reboot to this error screen");

	auto it = UEMErrorTable.find(ErrorCode);
	if (it != UEMErrorTable.end())
	{
		std::string ErrorMessage = "Fatal error. " + it->second + ". This error screen will persist indefinitely. Stop the emulation to close it.";
		PopupFatal(nullptr, ErrorMessage.c_str());
	}
	else
	{
		PopupFatal(nullptr, "Unknown fatal error. This error screen will persist indefinitely. Stop the emulation to close it.");
	}
}

void CxbxKrnlTerminateThread()
{
    TerminateThread(GetCurrentThread(), 0);
}

void CxbxKrnlPanic()
{
    CxbxKrnlCleanup("Kernel Panic!");
}

static clock_t						g_DeltaTime = 0;			 // Used for benchmarking/fps count
static unsigned int					g_Frames = 0;

// ******************************************************************
// * update the current milliseconds per frame
// ******************************************************************
static void UpdateCurrentMSpFAndFPS() {
	if (g_EmuShared) {
		static float currentFPSVal = 30;

		currentFPSVal = (float)(g_Frames*0.5 + currentFPSVal * 0.5);
		g_EmuShared->SetCurrentFPS(&currentFPSVal);
	}
}

void UpdateFPSCounter()
{
	static clock_t lastDrawFunctionCallTime = 0;
	clock_t currentDrawFunctionCallTime = clock();

	g_DeltaTime += currentDrawFunctionCallTime - lastDrawFunctionCallTime;
	lastDrawFunctionCallTime = currentDrawFunctionCallTime;
	g_Frames++;

	if (g_DeltaTime >= CLOCKS_PER_SEC) {
		UpdateCurrentMSpFAndFPS();
		g_Frames = 0;
		g_DeltaTime -= CLOCKS_PER_SEC;
	}
}
