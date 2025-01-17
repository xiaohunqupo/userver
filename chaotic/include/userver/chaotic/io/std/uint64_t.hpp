#pragma once

#include <cstddef>

#include <fmt/format.h>

#include <userver/chaotic/convert/to.hpp>
#include <userver/utils/numeric_cast.hpp>

USERVER_NAMESPACE_BEGIN

namespace chaotic::convert {

template <typename T>
std::enable_if_t<std::is_integral_v<T>, T> Convert(const std::uint64_t& value, To<T>) {
    return utils::numeric_cast<T>(value);
}

template <typename T>
std::enable_if_t<std::is_integral_v<T>, std::uint64_t> Convert(const T& value, To<std::uint64_t>) {
    return utils::numeric_cast<std::uint64_t>(value);
}

}  // namespace chaotic::convert

USERVER_NAMESPACE_END
