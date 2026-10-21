
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp>
inline const bool __is_unqualified_v = __is_same(_Tp, __remove_cvref(_Tp));

}
