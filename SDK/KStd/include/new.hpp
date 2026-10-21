#pragma once

#include <__config.hpp>
#include <__new/new_handler.hpp>
#include <__new/launder.hpp>

#if __STDC_HOSTED__ == 0
// Placement new/delete
inline constexpr void *operator new(size_t, void *__p) noexcept { return __p; }
inline constexpr void *operator new[](size_t, void *__p) noexcept { return __p; }
inline constexpr void operator delete(void *, void *) noexcept {}
inline constexpr void operator delete[](void *, void *) noexcept {}

// Global new/delete declarations (implemented in kernel)
void *operator new(size_t size);
void *operator new[](size_t size);
void operator delete(void *ptr) noexcept;
void operator delete[](void *ptr) noexcept;
void operator delete(void *ptr, size_t) noexcept;
void operator delete[](void *ptr, size_t) noexcept;
#endif
