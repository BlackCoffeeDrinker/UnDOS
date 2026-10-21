
#pragma once

#include <__config.hpp>

namespace kstd {
template<template<class...> class _Func, class... _Args>
struct _Lazy : _Func<_Args...> {};

}// namespace kstd
