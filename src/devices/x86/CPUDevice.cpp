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

#include "CPUDevice.h"

#define LOG_PREFIX CXBXR_MODULE::X86

Cpu *g_CPU = nullptr;


void Cpu::Init(size_t ramsize)
{
	LIB86CPU_EXPECTED(m_cpu = cpu_new(ramsize).value();, CxbxKrnlCleanup("Failed to initialize lib86cpu!\n");)

	if (!LIB86CPU_CHECK_SUCCESS(memory_init_region_ram(m_cpu, 0, ramsize, 1))) {
		CxbxKrnlCleanup("Failed to initialize ram region!\n");
	}

	if (!LIB86CPU_CHECK_SUCCESS(memory_init_region_ram(m_cpu, CONTIGUOUS_MEMORY_BASE, ramsize, 1))) {
		CxbxKrnlCleanup("Failed to initialize contiguous region!\n");
	}

	if (!LIB86CPU_CHECK_SUCCESS(memory_init_region_ram(m_cpu, TILED_MEMORY_BASE, TILED_MEMORY_SIZE, 1))) {
		CxbxKrnlCleanup("Failed to initialize tiled region!\n");
	}

	// Activate protection, write-protect and the native numeric error support
	// We don't enable pagination now because we have yet to setup the page tables, which is done later by the VMManager
	XBOX_REG_WRITE(REG_CR0, (1 << 16) | (1 << 5) | 1);

	// Setup cr3 to point to the page directory
	XBOX_REG_WRITE(REG_CR3, PAGE_DIRECTORY_PHYSICAL_ADDRESS);

	// Activate PSE, OSFXSR and OSXMMEXCPT support
	XBOX_REG_WRITE(REG_CR4, (1 << 10) | (1 << 9) | (1 << 4));

	cpu_sync_state(m_cpu);
	m_ramsize = ramsize;
}

std::vector<uint8_t> Cpu::RamPhysRead(xbaddr addr, size_t size)
{
	// NOTE: contiguous addresses should have the highest bit masked
	assert((addr + size <= m_ramsize));

	std::vector<uint8_t> buffer(size);
	std::memcpy(buffer.data(), &m_cpu->cpu_ctx.ram[addr], size);
	return buffer;
}

void Cpu::RamPhysWrite(xbaddr addr, size_t size, void *buffer)
{
	// NOTE: contiguous addresses should have the highest bit masked
	assert((addr + size <= m_ramsize));

	std::memcpy(&m_cpu->cpu_ctx.ram[addr], buffer, size);
}

void Cpu::RamPhysZero(xbaddr addr, size_t size)
{
	// NOTE: contiguous addresses should have the highest bit masked
	assert((addr + size <= m_ramsize));

	std::memset(&m_cpu->cpu_ctx.ram[addr], 0, size);
}

std::vector<uint8_t> Cpu::MemRead(xbaddr addr, size_t size)
{
	try {
		return mem_read_block(m_cpu, addr, size).value();
	}
	catch (const tl::bad_expected_access<lc86_status> &e) {
		CxbxKrnlCleanup("Page fault while trying to read at address %#010x with size %zu\n", addr, size);
	}
}

void Cpu::MemWrite(xbaddr addr, size_t size, void *buffer)
{
	if (!LIB86CPU_CHECK_SUCCESS(mem_write_block(m_cpu, addr, size, buffer))) {
		CxbxKrnlCleanup("Page fault while trying to write at address %#010x with size %zu\n", addr, size);
	}
}

void Cpu::TlbFlush(xbaddr addr_s, xbaddr addr_e)
{
	tlb_invalidate(m_cpu, addr_s, addr_e);
}
