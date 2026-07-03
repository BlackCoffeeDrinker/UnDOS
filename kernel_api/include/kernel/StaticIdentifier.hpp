#pragma once

#include <stdint.h>
#include <string_view.hpp>

namespace kernel {

/**
 * @brief Generic FNV-1a 64-bit hasher.
 */
struct fnv1a64 final {
    static constexpr uint64_t offset_basis = 14695981039346656037ull;
    static constexpr uint64_t prime = 1099511628211ull;

    static consteval uint64_t hash(const char* s, size_t n, uint64_t seed = offset_basis) noexcept {
        uint64_t h = seed;
        for (size_t i = 0; i < n; ++i) {
            h ^= static_cast<uint8_t>(s[i]);
            h *= prime;
        }
        return h;
    }
};

/**
 * @brief A type-safe static identifier based on FNV-1a hashing.
 * 
 * @tparam Tag A tag type used to categorize identifiers and provide a unique seed.
 */
template<typename Tag>
struct StaticIdentifier final {
    uint64_t value{0};

    constexpr StaticIdentifier() noexcept = default;
    constexpr explicit StaticIdentifier(uint64_t v) noexcept : value(v) {}

    constexpr bool operator==(const StaticIdentifier& other) const noexcept { return value == other.value; }
    constexpr bool operator!=(const StaticIdentifier& other) const noexcept { return value != other.value; }
    constexpr bool operator<(const StaticIdentifier& other) const noexcept { return value < other.value; }
    constexpr bool operator<=(const StaticIdentifier& other) const noexcept { return value <= other.value; }
    constexpr bool operator>(const StaticIdentifier& other) const noexcept { return value > other.value; }
    constexpr bool operator>=(const StaticIdentifier& other) const noexcept { return value >= other.value; }

    template<size_t N>
    static consteval StaticIdentifier from_literal(const char (&s)[N]) noexcept {
        return StaticIdentifier{hash(s, N - 1)};
    }

    static consteval StaticIdentifier from_string(kstd::string_view sv) noexcept {
        return StaticIdentifier{hash(sv.data(), sv.size())};
    }

private:
    static consteval uint64_t hash(const char* s, size_t n) noexcept {
        uint64_t seed = fnv1a64::offset_basis;
        if constexpr (requires { Tag::seed; }) {
            seed = Tag::seed;
        }
        return fnv1a64::hash(s, n, seed);
    }
};

} // namespace kernel
