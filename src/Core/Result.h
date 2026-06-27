#pragma once
#include <variant>
#include <utility>
#include <type_traits>
#include <stdexcept>
#include "Errors.h"

namespace InputBridge
{

struct OkTag {};
struct ErrTag {};

inline constexpr OkTag OkTag_v{};
inline constexpr ErrTag ErrTag_v{};

template<typename T>
struct OkValue
{
    T value;
};

template<typename E>
struct ErrValue
{
    E error;
};

template<typename T, typename E>
class Result
{
public:
    using ValueType = T;
    using ErrorType = E;

private:
    std::variant<OkValue<T>, ErrValue<E>> m_data;

public:

    Result(OkTag, T value)
        : m_data(OkValue<T>{std::move(value)}) {}

    Result(ErrTag, E error)
        : m_data(ErrValue<E>{std::move(error)}) {}

    static Result Ok(T value)
    {
        return Result(OkTag{}, std::move(value));
    }

    static Result Err(E error)
    {
        return Result(ErrTag{}, std::move(error));
    }

    bool IsOk() const
    {
        return std::holds_alternative<OkValue<T>>(m_data);
    }

    bool IsErr() const
    {
        return std::holds_alternative<ErrValue<E>>(m_data);
    }

    explicit operator bool() const
    {
        return IsOk();
    }

    // ----- Value access -----

    T& Value()
    {
        if (!IsOk())
            throw std::runtime_error("Attempted to access Value() on Err result");

        return std::get<OkValue<T>>(m_data).value;
    }

    const T& Value() const
    {
        if (!IsOk())
            throw std::runtime_error("Attempted to access Value() on Err result");

        return std::get<OkValue<T>>(m_data).value;
    }

    // ----- Error access -----

    E& Error()
    {
        if (!IsErr())
            throw std::runtime_error("Attempted to access Error() on Ok result");

        return std::get<ErrValue<E>>(m_data).error;
    }

    const E& Error() const
    {
        if (!IsErr())
            throw std::runtime_error("Attempted to access Error() on Ok result");

        return std::get<ErrValue<E>>(m_data).error;
    }

    // ----- Convenience -----

    T ValueOr(T fallback) const
    {
        if (IsOk())
            return Value();

        return fallback;
    }

    template<typename F>
    auto Map(F&& f) const
    {
        using U = std::invoke_result_t<F, const T&>;

        if (IsOk())
            return Result<U,E>(OkTag{}, f(Value()));

        return Result<U,E>(ErrTag{}, Error());
    }

    template<typename F>
    auto MapError(F&& f) const
    {
        using E2 = std::invoke_result_t<F, const E&>;

        if (IsErr())
            return Result<T,E2>(ErrTag{}, f(Error()));

        return Result<T,E2>(OkTag{}, Value());
    }

    template<typename F>
    auto AndThen(F&& f) const
    {
        using Ret = std::invoke_result_t<F, const T&>;

        if (IsOk())
            return f(Value());

        return Ret(ErrTag{}, Error());
    }
};

} // namespace InputBridge
