
#pragma once

#include <__config.hpp>
#include <stddef.hpp>
#include <type_traits.hpp>
#include <reverse_iterator.hpp>
#include <array.hpp>
#include <__iterator/iterator_traits.hpp>

namespace kstd {

inline constexpr size_t dynamic_extent = static_cast<size_t>(-1);

template <class T, size_t Extent = dynamic_extent>
class span;

_CTAD_SUPPORTED_FOR_TYPE(span);

template <class T, size_t Extent>
class span {
public:
    using element_type = T;
    using value_type = remove_cv_t<T>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = kstd::reverse_iterator<iterator>;
    using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

    static constexpr size_type extent = Extent;

    constexpr span() noexcept requires (Extent == 0) : _data(nullptr) {}

    constexpr span(pointer ptr, size_type count) : _data(ptr) {
        (void)count;
    }

    constexpr span(pointer first, pointer last) : _data(first) {
        (void)(last - first);
    }

    template <size_t N>
    constexpr span(T (&arr)[N]) noexcept requires (Extent == N)
        : _data(arr) {}

    template <class U, size_t N>
    constexpr span(array<U, N>& arr) noexcept requires (Extent == N && is_convertible_v<U(*)[], T(*)[]>)
        : _data(arr.data()) {}

    template <class U, size_t N>
    constexpr span(const array<U, N>& arr) noexcept requires (Extent == N && is_convertible_v<const U(*)[], T(*)[]>)
        : _data(arr.data()) {}

    template <class U, size_t OtherExtent>
    constexpr span(const span<U, OtherExtent>& other) noexcept
        requires (OtherExtent == Extent && is_convertible_v<U(*)[], T(*)[]>)
        : _data(other.data()) {}

    // Observers
    [[nodiscard]] constexpr pointer data() const noexcept { return _data; }
    [[nodiscard]] constexpr size_type size() const noexcept { return Extent; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return Extent * sizeof(T); }
    [[nodiscard]] constexpr bool empty() const noexcept { return Extent == 0; }

    // Element access
    constexpr reference operator[](size_type idx) const { return _data[idx]; }
    constexpr reference front() const { return _data[0]; }
    constexpr reference back() const { return _data[Extent - 1]; }

    // Iterators
    constexpr iterator begin() const noexcept { return _data; }
    constexpr iterator end() const noexcept { return _data + Extent; }
    constexpr const_iterator cbegin() const noexcept { return _data; }
    constexpr const_iterator cend() const noexcept { return _data + Extent; }

    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
    constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    // Subviews
    template <size_t Count>
    constexpr span<T, Count> first() const requires (Count <= Extent) {
        return span<T, Count>(data(), Count);
    }

    template <size_t Count>
    constexpr span<T, Count> last() const requires (Count <= Extent) {
        return span<T, Count>(data() + (Extent - Count), Count);
    }

    template <size_t Offset, size_t Count = dynamic_extent>
    constexpr auto subspan() const requires (Offset <= Extent && (Count == dynamic_extent || Count <= Extent - Offset)) {
        constexpr size_t E = (Count != dynamic_extent) ? Count : (Extent - Offset);
        return span<T, E>(data() + Offset, E);
    }

    constexpr span<T, dynamic_extent> first(size_type count) const {
        return {data(), count};
    }

    constexpr span<T, dynamic_extent> last(size_type count) const {
        return {data() + (size() - count), count};
    }

    constexpr span<T, dynamic_extent> subspan(size_type offset, size_type count = dynamic_extent) const {
        return {data() + offset, (count == dynamic_extent) ? (size() - offset) : count};
    }

private:
    pointer _data;
};

// Dynamic Extent specialization
template <class T>
class span<T, dynamic_extent> {
public:
    using element_type = T;
    using value_type = remove_cv_t<T>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = kstd::reverse_iterator<iterator>;
    using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

    static constexpr size_type extent = dynamic_extent;

    constexpr span() noexcept : _data(nullptr), _size(0) {}
    constexpr span(pointer ptr, size_type count) : _data(ptr), _size(count) {}
    constexpr span(pointer first, pointer last) : _data(first), _size(last - first) {}

    template <size_t N>
    constexpr span(T (&arr)[N]) noexcept : _data(arr), _size(N) {}

    template <class U, size_t N>
    constexpr span(array<U, N>& arr) noexcept requires (is_convertible_v<U(*)[], T(*)[]>)
        : _data(arr.data()), _size(N) {}

    template <class U, size_t N>
    constexpr span(const array<U, N>& arr) noexcept requires (is_convertible_v<const U(*)[], T(*)[]>)
        : _data(arr.data()), _size(N) {}

    template <class U, size_t OtherExtent>
    constexpr span(const span<U, OtherExtent>& other) noexcept
        requires (is_convertible_v<U(*)[], T(*)[]>)
        : _data(other.data()), _size(other.size()) {}

    // Observers
    [[nodiscard]] constexpr pointer data() const noexcept { return _data; }
    [[nodiscard]] constexpr size_type size() const noexcept { return _size; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return _size * sizeof(T); }
    [[nodiscard]] constexpr bool empty() const noexcept { return _size == 0; }

    // Element access
    constexpr reference operator[](size_type idx) const { return _data[idx]; }
    constexpr reference operator[](int idx) const { return _data[idx]; }
    constexpr reference front() const { return _data[0]; }
    constexpr reference back() const { return _data[_size - 1]; }

    // Iterators
    constexpr iterator begin() const noexcept { return _data; }
    constexpr iterator end() const noexcept { return _data + _size; }
    constexpr const_iterator cbegin() const noexcept { return _data; }
    constexpr const_iterator cend() const noexcept { return _data + _size; }

    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
    constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    // Subviews
    template <size_t Count>
    constexpr span<T, Count> first() const {
        return span<T, Count>(data(), Count);
    }

    template <size_t Count>
    constexpr span<T, Count> last() const {
        return span<T, Count>(data() + (size() - Count), Count);
    }

    template <size_t Offset, size_t Count = dynamic_extent>
    constexpr auto subspan() const {
        return span<T, Count>(data() + Offset, (Count == dynamic_extent) ? (size() - Offset) : Count);
    }

    constexpr span<T, dynamic_extent> first(size_type count) const {
        return {data(), count};
    }

    constexpr span<T, dynamic_extent> last(size_type count) const {
        return {data() + (size() - count), count};
    }

    constexpr span<T, dynamic_extent> subspan(size_type offset, size_type count = dynamic_extent) const {
        return {data() + offset, (count == dynamic_extent) ? (size() - offset) : count};
    }

private:
    pointer _data;
    size_type _size;
};

// Deduction guides for kstd::span
template <class T, size_t N>
span(T (&)[N]) -> span<T, N>;

template <class T, size_t N>
span(array<T, N>&) -> span<T, N>;

template <class T, size_t N>
span(const array<T, N>&) -> span<const T, N>;

template <class It, class EndOrSize>
span(It, EndOrSize) -> span<remove_reference_t<iter_reference_t<It>>, dynamic_extent>;

} // namespace kstd
