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

#include <cstdint>
#include <cstddef>
#include <climits>
#include <cuchar>
#include <concepts>


namespace xbox
{
	// ******************************************************************
	// * Calling conventions
	// ******************************************************************
#ifndef _WIN64
	#define XBOXAPI             __stdcall
	#define XCALLBACK           XBOXAPI
	#define XCDECL              __cdecl
	#define XFASTCALL           __fastcall
#else
	#define XBOXAPI
	#define XCALLBACK
	#define XCDECL
	#define XFASTCALL
#endif

	// ******************************************************************
	// * Basic types
	// ******************************************************************
	using void_xt = void;
	using char_xt = char;
	using cchar_xt = char;
	using wchar_xt = char16_t;
	using short_xt = std::int16_t;
	using cshort_xt = std::int16_t;
	using long_xt = std::int32_t;
	using uchar_xt = std::uint8_t;
	using byte_xt = std::uint8_t;
	using boolean_xt = std::uint8_t;
	using ushort_xt = std::uint16_t;
	using word_xt = std::uint16_t;
	using ulong_xt = std::uint32_t;
	using dword_xt = std::uint32_t;
	using size_xt = ulong_xt;
	using access_mask_xt = ulong_xt;
	using physical_address_xt = ulong_xt;
	using uint_xt = std::uint32_t;
	using int_xt = std::int32_t;
	using int_ptr_xt = int_xt;
	using long_ptr_xt = long_xt;
	using ulong_ptr_xt = ulong_xt;
	using longlong_xt = std::int64_t;
	using ulonglong_xt = std::uint64_t;
	using quad_xt = std::uint64_t; // 8 byte aligned 8 byte long
	using bool_xt = std::int32_t;
	using hresult_xt = long_xt;
	using ntstatus_xt = long_xt;
	using float_xt = float;
	/*! addr is the type of a 32bit address */
	using addr_xt = std::uint32_t;
	/*! zero is the type of null address or value */
	inline constexpr addr_xt zero = 0;
	/*! zeroptr is the type of null pointer address */
	using zeroptr_xt = std::nullptr_t;
	inline constexpr zeroptr_xt zeroptr = nullptr;

	// ******************************************************************
	// * Pointer types
	// ******************************************************************
	template<typename T, typename U>
	concept CanComparePtr = requires(T *t, U *u)
	{
		t == u; // ptr comparison only allowed when the comparison of the corresponding native ptrs would be allowed too
	};

	template<typename T>
	concept IsXboxPtr = std::is_class_v<T> && requires(T t) // must have class type
	{
		typename T::PT;     // there must exists a type member named PT
		{ t.m_ptr };        // there must exists a data member named m_ptr
	};

	/*
	The ptr_xt class behaves as if it were a native ptr, with the addition that it will transparently access guest memory via memory handlers.
	The latter is only really useful with cpu emulation, because with direct exec xbox ptr == native ptr.
	*/
	template<typename T>
	class ptr_xt
	{
	public:
		using PT = T;

		ptr_xt() = default;

		ptr_xt(const addr_xt val) { m_ptr = val; }

		ptr_xt(const T *val) { m_ptr = reinterpret_cast<addr_xt>(val); }

		std::add_lvalue_reference_t<T> operator[](const std::uint32_t idx) const requires (!std::is_void_v<T>)
		{
			return *reinterpret_cast<T *>(m_ptr + sizeof(T) * idx);
		}

		std::add_lvalue_reference_t<ptr_xt> operator++() requires (!std::is_void_v<T>) { m_ptr += sizeof(T); return *this; }

		std::add_lvalue_reference_t<ptr_xt> operator--() requires (!std::is_void_v<T>) { m_ptr -= sizeof(T); return *this; }

		ptr_xt operator++(int) requires (!std::is_void_v<T>) { ptr_xt ret(*this); ++(*this); return ret; }

		ptr_xt operator--(int) requires (!std::is_void_v<T>) { ptr_xt ret(*this); --(*this); return ret; }

		ptr_xt operator+(const std::uint32_t n) const requires (!std::is_void_v<T>) { return m_ptr + sizeof(T) * n; }

		ptr_xt operator-(const std::uint32_t n) const requires (!std::is_void_v<T>) { return m_ptr - sizeof(T) * n; }

		ptr_xt operator-(const ptr_xt &val) const requires (!std::is_void_v<T>) { return (m_ptr - val.m_ptr) / sizeof(T); }

		std::add_lvalue_reference_t<ptr_xt> operator+=(const std::uint32_t n) requires (!std::is_void_v<T>) { *this = *this + n; return *this; }

		std::add_lvalue_reference_t<ptr_xt> operator-=(const std::uint32_t n) requires (!std::is_void_v<T>) { *this = *this - n; return *this; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator==(const ptr_xt<U> &val) const { return m_ptr == val.m_ptr; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator!=(const ptr_xt<U> &val) const { return m_ptr != val.m_ptr; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator>=(const ptr_xt<U> &val) const { return m_ptr >= val.m_ptr; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator<=(const ptr_xt<U> &val) const { return m_ptr <= val.m_ptr; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator>(const ptr_xt<U> &val) const { return m_ptr > val.m_ptr; }

		template<typename U> requires CanComparePtr<T, U>
		bool operator<(const ptr_xt<U> &val) const { return m_ptr < val.m_ptr; }

		// Don't allow the logical AND and OR overloaded operators, because they don't support short-circuit evaluation,
		// which can lead to unexpected results in code such as ptr && ptr->
		bool operator&&(const ptr_xt &) const = delete;
		bool operator||(const ptr_xt &) const = delete;

		std::add_lvalue_reference_t<T> operator*() const requires (!std::is_void_v<T>) { return *reinterpret_cast<T *>(m_ptr); }

		T *operator->() const requires (!std::is_void_v<T>) { return reinterpret_cast<T *>(m_ptr); }

		template<typename U> operator ptr_xt<U>() const { return m_ptr; }

		explicit operator bool() const { return m_ptr != zero; }

		T *cast() const { return reinterpret_cast<T *>(m_ptr); }

		T *get_native_ptr() const { return cast(); }

		addr_xt m_ptr;
	};

	// Template specializations for xbox function pointers
	template<typename RT, typename... Args>
	class ptr_xt<RT(XCDECL *)(Args...)>
	{
	public:
		using FT = RT(XCDECL *)(Args...);
		using PT = FT;

		ptr_xt() = default;

		ptr_xt(FT val) { m_ptr = reinterpret_cast<addr_xt>(val); };

		RT operator()(Args... args) const { return reinterpret_cast<FT>(m_ptr)(std::forward<Args>(args)...); }

		explicit operator bool() const { return m_ptr != zero; }

		addr_xt m_ptr;
	};

	// On x64, cdecl, fastcall and stdcall are ignored and instead use the default x64 calling convention, so disable the below specializations
	// to avoid "duplicated templates" compiler errors
#ifndef _WIN64
	template<typename RT, typename... Args>
	class ptr_xt<RT(XBOXAPI *)(Args...)>
	{
	public:
		using FT = RT(XBOXAPI *)(Args...);
		using PT = FT;

		ptr_xt() = default;

		ptr_xt(FT val) { m_ptr = reinterpret_cast<addr_xt>(val); };

		RT operator()(Args... args) const { return reinterpret_cast<FT>(m_ptr)(std::forward<Args>(args)...); }

		explicit operator bool() const { return m_ptr != zero; }

		addr_xt m_ptr;
	};

	template<typename RT, typename... Args>
	class ptr_xt<RT(XFASTCALL *)(Args...)>
	{
	public:
		using FT = RT(XFASTCALL *)(Args...);
		using PT = FT;

		ptr_xt() = default;

		ptr_xt(FT val) { m_ptr = reinterpret_cast<addr_xt>(val); };

		RT operator()(Args... args) const { return reinterpret_cast<FT>(m_ptr)(std::forward<Args>(args)...); }

		explicit operator bool() const { return m_ptr != zero; }

		addr_xt m_ptr;
	};
#endif

	using pvoid_xt = ptr_xt<void_xt>;
	using ppvoid_xt = ptr_xt<pvoid_xt>;
	using pchar_xt = ptr_xt<char_xt>;
	using puchar_xt = ptr_xt<uchar_xt>;
	typedef char_xt *PSZ;
	typedef const char_xt *PCSZ;
	typedef byte_xt *PBYTE;
	typedef boolean_xt *PBOOLEAN;
	typedef ushort_xt *PUSHORT;
	typedef uint_xt *PUINT;
	typedef ulong_xt *PULONG;
	typedef dword_xt *PDWORD, *LPDWORD;
	typedef long_xt *PLONG;
	typedef int_ptr_xt *PINT_PTR;
	typedef void_xt *HANDLE;
	typedef HANDLE *PHANDLE;
	typedef size_xt *PSIZE_T;
	typedef access_mask_xt *PACCESS_MASK;
	typedef longlong_xt *PLONGLONG;
	typedef quad_xt *PQUAD;

	// Native pointer types
	using PVOID = void_xt *;
	using LPVOID = void_xt *;
	using PCHAR = char_xt *;
	using PUCHAR = uchar_xt *;

	// ******************************************************************
	// ANSI (Multi-byte Character) types
	// ******************************************************************
	typedef char_xt *LPCH, *PCH;
	typedef const char_xt *LPCCH, *PCCH;
	typedef wchar_xt *LPWSTR, *PWSTR;
	typedef /*_Null_terminated_*/ const wchar_xt *LPCWSTR, *PCWSTR;

	// ******************************************************************
	// Misc
	// ******************************************************************
	typedef struct _XD3DVECTOR {
		float_xt x;
		float_xt y;
		float_xt z;
	} D3DVECTOR;

	template<typename A, typename B>
	inline void CopyD3DVector(A& a, const B& b)
	{
		a.x = b.x;
		a.y = b.y;
		a.z = b.z;
	}

	// ******************************************************************
	// Defines
	// ******************************************************************
	constexpr uint_xt max_path{ 260 }; // Xbox file path max limitation
}
