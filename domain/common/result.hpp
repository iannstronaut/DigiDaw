#pragma once

#include "error_code.hpp"
#include <variant>
#include <utility>
#include <type_traits>
#include <string>

namespace digidaw::domain {

struct Error {
    ErrorCode code{ErrorCode::Success};
    std::string detail{};

    constexpr Error() noexcept = default;
    constexpr explicit Error(ErrorCode c) noexcept : code(c) {}
    Error(ErrorCode c, std::string d) : code(c), detail(std::move(d)) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return code == ErrorCode::Success;
    }

    [[nodiscard]] constexpr std::string_view message() const noexcept {
        return get_error_message(code);
    }

    [[nodiscard]] constexpr std::string_view identifier() const noexcept {
        return get_error_identifier(code);
    }
};

template <typename T>
class Result {
public:
    Result(const T& val) : data_(val) {}
    Result(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>) : data_(std::move(val)) {}
    Result(Error err) noexcept : data_(std::move(err)) {}
    Result(ErrorCode code) noexcept : data_(Error{code}) {}

    [[nodiscard]] bool is_ok() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] bool is_error() const noexcept {
        return !is_ok();
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(data_);
    }

    [[nodiscard]] T& value() {
        return std::get<T>(data_);
    }

    [[nodiscard]] T value_or(T default_val) const {
        if (is_ok()) {
            return std::get<T>(data_);
        }
        return default_val;
    }

    [[nodiscard]] const Error& error() const noexcept {
        return std::get<Error>(data_);
    }

    [[nodiscard]] ErrorCode error_code() const noexcept {
        if (is_error()) {
            return std::get<Error>(data_).code;
        }
        return ErrorCode::Success;
    }

private:
    std::variant<T, Error> data_;
};

// Specialization for void Result
template <>
class Result<void> {
public:
    constexpr Result() noexcept : error_(ErrorCode::Success) {}
    Result(Error err) noexcept : error_(std::move(err)) {}
    constexpr Result(ErrorCode code) noexcept : error_(code) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return error_.is_ok();
    }

    [[nodiscard]] constexpr bool is_error() const noexcept {
        return !is_ok();
    }

    [[nodiscard]] const Error& error() const noexcept {
        return error_;
    }

    [[nodiscard]] constexpr ErrorCode error_code() const noexcept {
        return error_.code;
    }

    static Result<void> ok() noexcept {
        return Result<void>{};
    }

private:
    Error error_;
};

} // namespace digidaw::domain
