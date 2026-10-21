
#pragma once
#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_trivially_copyable.hpp>

namespace kstd {
template<class _Tp, class = void>
struct is_replaceable : is_trivially_copyable<_Tp> {};

template<class _Tp>
struct is_replaceable<_Tp, enable_if_t<is_same<_Tp, typename _Tp::__replaceable>::value>> : true_type {};

template<class _Tp>
inline const bool is_replaceable_v = is_replaceable<_Tp>::value;

// Determines whether an allocator member of a container is replaceable.
//
// First, we require the allocator type to be considered replaceable. If not, then something fishy might be
// happening. Assuming the allocator type is replaceable, we conclude replaceability of the allocator as a
// member of the container if the allocator always compares equal (in which case propagation doesn't matter),
// or if the allocator always propagates on assignment, which is required in order for move construction and
// assignment to be equivalent.
template<class _AllocatorTraits>
struct __container_allocator_is_replaceable
    : integral_constant<bool,
                        is_replaceable_v<typename _AllocatorTraits::allocator_type> &&
                            (_AllocatorTraits::is_always_equal::value ||
                             (_AllocatorTraits::propagate_on_container_move_assignment::value &&
                              _AllocatorTraits::propagate_on_container_copy_assignment::value))> {};

}// namespace kstd
