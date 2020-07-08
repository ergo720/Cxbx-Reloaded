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
// *  (c) 2020 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************

#pragma once

#include "lib86cpu.h"
#include "core\kernel\init\CxbxKrnl.h"


inline constexpr int REG_EAX = 0;
inline constexpr int REG_ECX = 1;
inline constexpr int REG_EDX = 2;
inline constexpr int REG_EBX = 3;
inline constexpr int REG_ESP = 4;
inline constexpr int REG_EBP = 5;
inline constexpr int REG_ESI = 6;
inline constexpr int REG_EDI = 7;
inline constexpr int REG_ES = 8;
inline constexpr int REG_CS = 9;
inline constexpr int REG_SS = 10;
inline constexpr int REG_DS = 11;
inline constexpr int REG_FS = 12;
inline constexpr int REG_GS = 13;
inline constexpr int REG_CR0 = 14;
inline constexpr int REG_CR1 = 15;
inline constexpr int REG_CR2 = 16;
inline constexpr int REG_CR3 = 17;
inline constexpr int REG_CR4 = 18;
inline constexpr int REG_DR0 = 19;
inline constexpr int REG_DR1 = 20;
inline constexpr int REG_DR2 = 21;
inline constexpr int REG_DR3 = 22;
inline constexpr int REG_DR4 = 23;
inline constexpr int REG_DR5 = 24;
inline constexpr int REG_DR6 = 25;
inline constexpr int REG_DR7 = 26;
inline constexpr int REG_EFLAGS = 27;
inline constexpr int REG_EIP = 28;
inline constexpr int REG_IDTR = 29;
inline constexpr int REG_GDTR = 30;
inline constexpr int REG_LDTR = 31;
inline constexpr int REG_TR = 32;

inline constexpr int SEG_SEL = 0;
inline constexpr int SEG_BASE = 1;
inline constexpr int SEG_LIMIT = 2;
inline constexpr int SEG_FLG = 3;


class Cpu {
public:
	void Init(size_t ramsize);
	std::vector<uint8_t> RamPhysRead(xbaddr addr, size_t size);
	void RamPhysWrite(xbaddr addr, size_t size, void *buffer);
	void RamPhysZero(xbaddr addr, size_t size);
	std::vector<uint8_t> MemRead(xbaddr addr, size_t size);
	void MemWrite(xbaddr addr, size_t size, void *buffer);
	template <typename T> T IoRead(xbport port);
	template <typename T> void IoWrite(xbport port, T value);
	template <auto reg, auto sel = SEG_SEL> uint32_t ReadReg32();
	template <auto reg, auto sel = SEG_SEL> void WriteReg32(uint32_t value);
	void TlbFlush(xbaddr addr_s, xbaddr addr_e);

private:
	cpu_t *m_cpu;
	size_t m_ramsize;
};

extern Cpu *g_CPU;

template <typename T>
T Cpu::IoRead(xbport port)
{
	switch (sizeof(T))
	{
	case 8:
		return io_read_8(m_cpu, port);

	case 16:
		return io_read_16(m_cpu, port);

	case 32:
		return io_read_32(m_cpu, port);

	default:
		CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid size %zu specified to %s while trying to read at port %#06hx\n", sizeof(T), __func__, port);
	}
}

template <typename T>
void Cpu::IoWrite(xbport port, T value)
{
	switch (sizeof(T))
	{
	case 8:
		io_write_8(m_cpu, port, value);
		break;

	case 16:
		io_write_16(m_cpu, port, value);
		break;

	case 32:
		io_write_32(m_cpu, port, value);
		break;

	default:
		CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid size %zu specified to %s while trying to write at port %#06hx\n", sizeof(T), __func__, port);
	}
}

template <auto reg, auto sel>
uint32_t Cpu::ReadReg32()
{
	switch (reg)
	{
	case REG_EAX:
		return m_cpu->cpu_ctx.regs.eax;

	case REG_ECX:
		return m_cpu->cpu_ctx.regs.ecx;

	case REG_EDX:
		return m_cpu->cpu_ctx.regs.edx;

	case REG_EBX:
		return m_cpu->cpu_ctx.regs.ebx;

	case REG_ESP:
		return m_cpu->cpu_ctx.regs.esp;

	case REG_EBP:
		return m_cpu->cpu_ctx.regs.ebp;

	case REG_ESI:
		return m_cpu->cpu_ctx.regs.esi;

	case REG_EDI:
		return m_cpu->cpu_ctx.regs.edi;

	case REG_ES:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.es;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.es_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.es_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.es_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_CS:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.cs;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.cs_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.cs_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.cs_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_SS:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.ss;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.ss_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.ss_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.ss_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_DS:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.ds;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.ds_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.ds_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.ds_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_FS:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.fs;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.fs_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.fs_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.fs_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_GS:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.gs;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.gs_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.gs_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.gs_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_CR0:
		return m_cpu->cpu_ctx.regs.cr0;

	case REG_CR1:
		return m_cpu->cpu_ctx.regs.cr1;

	case REG_CR2:
		return m_cpu->cpu_ctx.regs.cr2;

	case REG_CR3:
		return m_cpu->cpu_ctx.regs.cr3;

	case REG_CR4:
		return m_cpu->cpu_ctx.regs.cr4;

	case REG_DR0:
		return m_cpu->cpu_ctx.regs.dr0;

	case REG_DR1:
		return m_cpu->cpu_ctx.regs.dr1;

	case REG_DR2:
		return m_cpu->cpu_ctx.regs.dr2;

	case REG_DR3:
		return m_cpu->cpu_ctx.regs.dr3;

	case REG_DR4:
		return m_cpu->cpu_ctx.regs.dr4;

	case REG_DR5:
		return m_cpu->cpu_ctx.regs.dr5;

	case REG_DR6:
		return m_cpu->cpu_ctx.regs.dr6;

	case REG_DR7:
		return m_cpu->cpu_ctx.regs.dr7;

	case REG_EFLAGS:
		return m_cpu->cpu_ctx.regs.eflags;

	case REG_EIP:
		return m_cpu->cpu_ctx.regs.eip;

	case REG_IDTR:
		switch (sel)
		{
		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.idtr_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.idtr_hidden.limit;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_GDTR:
		switch (sel)
		{
		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.gdtr_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.gdtr_hidden.limit;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_LDTR:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.ldtr;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.ldtr_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.ldtr_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.ldtr_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_TR:
		switch (sel)
		{
		case SEG_SEL:
			return m_cpu->cpu_ctx.regs.tr;

		case SEG_BASE:
			return m_cpu->cpu_ctx.regs.tr_hidden.base;

		case SEG_LIMIT:
			return m_cpu->cpu_ctx.regs.tr_hidden.limit;

		case SEG_FLG:
			return m_cpu->cpu_ctx.regs.tr_hidden.flags;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	default:
		CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
	}
}

template <auto reg, auto sel>
void Cpu::WriteReg32(uint32_t value)
{
	switch (reg)
	{
	case REG_EAX:
		m_cpu->cpu_ctx.regs.eax = value;
		break;

	case REG_ECX:
		m_cpu->cpu_ctx.regs.ecx = value;
		break;

	case REG_EDX:
		m_cpu->cpu_ctx.regs.edx = value;
		break;

	case REG_EBX:
		m_cpu->cpu_ctx.regs.ebx = value;
		break;

	case REG_ESP:
		m_cpu->cpu_ctx.regs.esp = value;
		break;

	case REG_EBP:
		m_cpu->cpu_ctx.regs.ebp = value;
		break;

	case REG_ESI:
		m_cpu->cpu_ctx.regs.esi = value;
		break;

	case REG_EDI:
		m_cpu->cpu_ctx.regs.edi = value;
		break;

	case REG_ES:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.es = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.es_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.es_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.es_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_CS:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.cs = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.cs_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.cs_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.cs_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_SS:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.ss = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.ss_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.ss_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.ss_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_DS:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.ds = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.ds_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.ds_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.ds_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_FS:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.fs = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.fs_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.fs_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.fs_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_GS:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.gs = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.gs_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.gs_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.gs_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_CR0:
		m_cpu->cpu_ctx.regs.cr0 = value;
		break;

	case REG_CR1:
		m_cpu->cpu_ctx.regs.cr1 = value;
		break;

	case REG_CR2:
		m_cpu->cpu_ctx.regs.cr2 = value;
		break;

	case REG_CR3:
		m_cpu->cpu_ctx.regs.cr3 = value;
		break;

	case REG_CR4:
		m_cpu->cpu_ctx.regs.cr4 = value;
		break;

	case REG_DR0:
		m_cpu->cpu_ctx.regs.dr0 = value;
		break;

	case REG_DR1:
		m_cpu->cpu_ctx.regs.dr1 = value;
		break;

	case REG_DR2:
		m_cpu->cpu_ctx.regs.dr2 = value;
		break;

	case REG_DR3:
		m_cpu->cpu_ctx.regs.dr3 = value;
		break;

	case REG_DR4:
		m_cpu->cpu_ctx.regs.dr4 = value;
		break;

	case REG_DR5:
		m_cpu->cpu_ctx.regs.dr5 = value;
		break;

	case REG_DR6:
		m_cpu->cpu_ctx.regs.dr6 = value;
		break;

	case REG_DR7:
		m_cpu->cpu_ctx.regs.dr7 = value;
		break;

	case REG_EFLAGS:
		m_cpu->cpu_ctx.regs.eflags = value;
		break;

	case REG_EIP:
		m_cpu->cpu_ctx.regs.eip = value;
		break;

	case REG_IDTR:
		switch (sel)
		{
		case SEG_BASE:
			m_cpu->cpu_ctx.regs.idtr_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.idtr_hidden.limit = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_GDTR:
		switch (sel)
		{
		case SEG_BASE:
			m_cpu->cpu_ctx.regs.gdtr_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.gdtr_hidden.limit = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_LDTR:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.ldtr = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.ldtr_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.ldtr_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.ldtr_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	case REG_TR:
		switch (sel)
		{
		case SEG_SEL:
			m_cpu->cpu_ctx.regs.tr = value & 0xFFFF;
			break;

		case SEG_BASE:
			m_cpu->cpu_ctx.regs.tr_hidden.base = value;
			break;

		case SEG_LIMIT:
			m_cpu->cpu_ctx.regs.tr_hidden.limit = value;
			break;

		case SEG_FLG:
			m_cpu->cpu_ctx.regs.tr_hidden.flags = value;
			break;

		default:
			CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
		}
		break;

	default:
		CxbxKrnlCleanupEx(CXBXR_MODULE::X86, "Invalid reg index specified; reg = %d, sel = %d \n", reg, sel);
	}
}


#define XBOX_RAM_READ(addr, size) g_CPU->RamPhysRead(addr, size)
#define XBOX_RAM_WRITE(addr, size, buffer_ptr) g_CPU->RamPhysWrite(addr, size, buffer_ptr)
#define XBOX_RAM_ZERO(addr, size) g_CPU->RamPhysZero(addr, size)
#define XBOX_MEM_READ(addr, size) g_CPU->MemRead(addr, size)
#define XBOX_MEM_WRITE(addr, size, buffer_ptr) g_CPU->MemWrite(addr, size, buffer_ptr)
#define XBOX_PORT_READ(port, size_type) g_CPU->IoRead<size_type>(port)
#define XBOX_PORT_WRITE(port, size_type, value) g_CPU->IoWrite<size_type>(port, value)
#define XBOX_REG_READ(reg) g_CPU->ReadReg32<reg>()
#define XBOX_REG_WRITE(reg, value) g_CPU->WriteReg32<reg>(value)
#define XBOX_SEL_READ(reg, sel) g_CPU->ReadReg32<reg, sel>()
#define XBOX_SEL_WRITE(reg, sel, value) g_CPU->WriteReg32<reg, sel>(value)
#define XBOX_TLB_FLUSH(addr_s, addr_e) g_CPU->TlbFlush(addr_s, addr_e)
