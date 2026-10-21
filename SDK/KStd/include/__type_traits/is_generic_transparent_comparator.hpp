
#pragma once


#include <__config.hpp>

namespace kstd {

// This trait returns true if the given _Comparator is known to accept any two types for comparison. This is separate
// from `__is_transparent_v`, since that only enables overloads of specific functions, but doesn't give any semantic
// guarantees. This trait guarantess that the comparator simply calls the appropriate comparison functions for any two
// types.

template <class _Comparator>
inline const bool __is_generic_transparent_comparator_v = false;

}
