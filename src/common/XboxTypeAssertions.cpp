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
// *  (c) 2022 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************

#include "xbox_types.h"
#include "core/kernel/common/types.h"
#include <tuple>


using namespace xbox;
using all_xptr_types = std::tuple<
    // basic pointers
    pchar_xt,
    puchar_xt,
    pvoid_xt,
    ppvoid_xt,
    // function pointers
    pob_allocate_method_xt,
    pob_free_method_xt,
    pob_close_method_xt,
    pob_delete_method_xt,
    pob_parse_method_xt
>;

template<std::size_t idx>
consteval bool assert_xptr_requirements()
{
    using xptr_t = typename std::tuple_element_t<idx, all_xptr_types>;
    constexpr bool assert_size_and_pod = (sizeof(xptr_t) == 4) &&  // ptr_xt must have the same size of an x86 pointer 
        std::is_standard_layout_v<xptr_t> &&                       // &ptr_xt == &ptr_xt::m_ptr
        std::is_trivial_v<xptr_t>;                                 // must be a POD type (is_standard_layout_v + is_trivial_v)

    if constexpr (std::is_function_v<std::remove_pointer_t<xptr_t::PT>>) {
        // Function pointers are allowed in the ptr_xt template specializations
        return assert_size_and_pod;
    }
    else {
        // All other pointers and references are not allowed
        return assert_size_and_pod &&
            !std::is_pointer_v<xptr_t::PT> &&    // T must not be a pointer
            !std::is_reference_v<xptr_t::PT>;    // T must not be a reference
    }
}

template<std::size_t idx>
struct assert_xptr
{
    static const bool value = (assert_xptr<idx - 1>::value) && assert_xptr_requirements<idx>();
};

template<>
struct assert_xptr<0>
{
    static const bool value = assert_xptr_requirements<0>();
};

consteval bool assert_all_xptr_requirements()
{
    return assert_xptr<std::tuple_size_v<all_xptr_types> - 1>::value;
}

static_assert(assert_all_xptr_requirements());
static_assert(CHAR_BIT == 8);
static_assert(sizeof(char16_t) == 2);
