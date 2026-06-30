
#pragma once
#include <__config.hpp>
#include <stddef.hpp>

#include <__new/new_handler.hpp>

// Placement new/delete
inline void *operator new(size_t, void *__p) noexcept { return __p; }
inline void *operator new[](size_t, void *__p) noexcept { return __p; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}

// Global new/delete declarations (implemented in kernel)
void *operator new(size_t size);
void *operator new[](size_t size);
void operator delete(void *ptr) noexcept;
void operator delete[](void *ptr) noexcept;
void operator delete(void *ptr, size_t) noexcept;
void operator delete[](void *ptr, size_t) noexcept;
