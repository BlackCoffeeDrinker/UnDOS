
#pragma once

#include <__config.hpp>
#include <string_view.hpp>

namespace kstd {

class error_category;
class error_condition;
class error_code;
[[nodiscard]] const error_category& generic_category() noexcept;
[[nodiscard]] const error_category& system_category() noexcept;

class error_category {
public:
    constexpr error_category() noexcept = default;
    error_category(const error_category&) = delete;
    error_category& operator=(const error_category&) = delete;
    virtual ~error_category() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual string_view message(int __ev) const noexcept {
        (void)__ev;
        return "unknown";
    }

    [[nodiscard]] virtual error_condition default_error_condition(int __ev) const noexcept;
    [[nodiscard]] virtual bool equivalent(int __code, const error_condition& __condition) const noexcept;
    [[nodiscard]] virtual bool equivalent(const error_code& __code, int __condition) const noexcept;

    [[nodiscard]] bool operator==(const error_category& __rhs) const noexcept { return this == &__rhs; }
    [[nodiscard]] bool operator!=(const error_category& __rhs) const noexcept { return !(*this == __rhs); }
};

class error_condition {
public:
    using value_type = int;

    error_condition() noexcept : __val_(0), __cat_(&generic_category()) {}
    constexpr error_condition(int __val, const error_category& __cat) noexcept : __val_(__val), __cat_(&__cat) {}

    [[nodiscard]] constexpr int value() const noexcept { return __val_; }
    [[nodiscard]] constexpr const error_category& category() const noexcept { return *__cat_; }
    [[nodiscard]] string_view message() const noexcept { return __cat_->message(__val_); }

    void clear() noexcept {
        __val_ = 0;
        __cat_ = &generic_category();
    }

    explicit constexpr operator bool() const noexcept { return __val_ != 0; }

private:
    int __val_;
    const error_category* __cat_;
};

class error_code {
public:
    using value_type = int;

    error_code() noexcept : __val_(0), __cat_(&system_category()) {}
    constexpr error_code(int __val, const error_category& __cat) noexcept : __val_(__val), __cat_(&__cat) {}

    [[nodiscard]] constexpr int value() const noexcept { return __val_; }
    [[nodiscard]] constexpr const error_category& category() const noexcept { return *__cat_; }
    [[nodiscard]] error_condition default_error_condition() const noexcept {
        return __cat_->default_error_condition(__val_);
    }
    [[nodiscard]] string_view message() const noexcept { return __cat_->message(__val_); }

    void clear() noexcept {
        __val_ = 0;
        __cat_ = &system_category();
    }

    explicit constexpr operator bool() const noexcept { return __val_ != 0; }

private:
    int __val_;
    const error_category* __cat_;
};

class __generic_error_category final : public error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "generic"; }
};

class __system_error_category final : public error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "system"; }
};

inline const error_category& generic_category() noexcept {
    static __generic_error_category __cat;
    return __cat;
}

inline const error_category& system_category() noexcept {
    static __system_error_category __cat;
    return __cat;
}

inline error_condition error_category::default_error_condition(int __ev) const noexcept {
    return error_condition(__ev, *this);
}

inline bool error_category::equivalent(int __code, const error_condition& __condition) const noexcept {
    return default_error_condition(__code).value() == __condition.value() && default_error_condition(__code).category() == __condition.category();
}

inline bool error_category::equivalent(const error_code& __code, int __condition) const noexcept {
    return *this == __code.category() && __code.value() == __condition;
}

inline bool operator==(const error_code& __lhs, const error_code& __rhs) noexcept {
    return __lhs.category() == __rhs.category() && __lhs.value() == __rhs.value();
}

inline bool operator==(const error_condition& __lhs, const error_condition& __rhs) noexcept {
    return __lhs.category() == __rhs.category() && __lhs.value() == __rhs.value();
}

inline bool operator==(const error_code& __lhs, const error_condition& __rhs) noexcept {
    return __lhs.category().equivalent(__lhs.value(), __rhs) || __rhs.category().equivalent(__lhs, __rhs.value());
}

inline bool operator==(const error_condition& __lhs, const error_code& __rhs) noexcept {
    return __rhs == __lhs;
}

inline bool operator!=(const error_code& __lhs, const error_code& __rhs) noexcept {
    return !(__lhs == __rhs);
}

inline bool operator!=(const error_condition& __lhs, const error_condition& __rhs) noexcept {
    return !(__lhs == __rhs);
}

inline bool operator!=(const error_code& __lhs, const error_condition& __rhs) noexcept {
    return !(__lhs == __rhs);
}

inline bool operator!=(const error_condition& __lhs, const error_code& __rhs) noexcept {
    return !(__lhs == __rhs);
}

}// namespace kstd
