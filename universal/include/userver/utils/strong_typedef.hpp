#pragma once

/// @file userver/utils/strong_typedef.hpp
/// @brief @copybrief utils::StrongTypedef

#include <functional>
#include <iosfwd>
#include <string>
#include <type_traits>
#include <utility>

#include <fmt/format.h>
#include <userver/utils/fmt_compat.hpp>

#include <boost/functional/hash_fwd.hpp>

#include <userver/formats/common/meta.hpp>
#include <userver/utils/meta.hpp>
#include <userver/utils/underlying_value.hpp>
#include <userver/utils/void_t.hpp>

namespace testing {

template <typename T>
std::string PrintToString(const T& value);

}  // namespace testing

USERVER_NAMESPACE_BEGIN

namespace logging {
class LogHelper;  // Forward declaration
}

namespace utils {

enum class StrongTypedefOps {
    kNoCompare = 0,  /// Forbid all comparisons for StrongTypedef

    kCompareStrong = 1,           /// Allow comparing two StrongTypedef<Tag, T>
    kCompareTransparentOnly = 2,  /// Allow comparing StrongTypedef<Tag, T> and T
    kCompareTransparent = 3,      /// Allow both of the above

    kNonLoggable = 4,  /// Forbid logging and serializing for StrongTypedef
};

constexpr bool operator&(StrongTypedefOps op, StrongTypedefOps mask) noexcept {
    return utils::UnderlyingValue(op) & utils::UnderlyingValue(mask);
}

constexpr auto operator|(StrongTypedefOps op1, StrongTypedefOps op2) noexcept {
    return StrongTypedefOps{utils::UnderlyingValue(op1) | utils::UnderlyingValue(op2)};
}

/// @ingroup userver_universal userver_containers
///
/// @brief Strong typedef for a type T.
///
/// Typical usage:
/// @code
///   using MyString = utils::StrongTypedef<class MyStringTag, std::string>;
/// @endcode
///
/// Or:
/// @code
///   struct MyString final : utils::StrongTypedef<MyString, std::string> {
///     using StrongTypedef::StrongTypedef;
///   };
/// @endcode
///
/// Has all the:
/// * comparison (see "Operators" below)
/// * hashing
/// * streaming operators
/// * optimizaed logging for LOG_XXX()
///
/// If used with container-like type also has common STL functions:
/// * begin()
/// * end()
/// * cbegin()
/// * cend()
/// * size()
/// * empty()
/// * clear()
/// * operator[]
///
/// Operators:
///   You can customize the operators that are available by passing the third
///   argument of type StrongTypedefOps. See its docs for more info.
template <class Tag, class T, StrongTypedefOps Ops = StrongTypedefOps::kCompareStrong, class /*Enable*/ = void>
class StrongTypedef;  // Forward declaration

// Helpers
namespace impl::strong_typedef {

template <class T, class /*Enable*/ = void_t<>>
struct InitializerListImpl {
    struct DoNotMatch;
    using type = DoNotMatch;
};

template <class T>
struct InitializerListImpl<T, void_t<typename T::value_type>> {
    using type = std::initializer_list<typename T::value_type>;
};

// We have to invent this alias to avoid hard error in StrongTypedef<Tag, T> for
// types T that do not have T::value_type.
template <class T>
using InitializerList = typename InitializerListImpl<T>::type;

template <class T, class U>
constexpr void CheckStrongCompare() {
    static_assert(
        std::is_same_v<typename T::UnderlyingType, typename U::UnderlyingType> &&
            std::is_same_v<typename T::TagType, typename U::TagType> && T::kOps == U::kOps &&
            (T::kOps & StrongTypedefOps::kCompareStrong),
        "Comparing those StrongTypedefs is forbidden"
    );
}

template <class Typedef>
constexpr void CheckTransparentCompare() {
    static_assert(
        Typedef::kOps & StrongTypedefOps::kCompareTransparentOnly,
        "Comparing this StrongTypedef to a raw value is forbidden"
    );
}

struct StrongTypedefTag {};

template <typename T>
using IsStrongTypedef =
    std::conjunction<std::is_base_of<StrongTypedefTag, T>, std::is_convertible<T&, StrongTypedefTag&>>;

template <class T>
const auto& UnwrapIfStrongTypedef(const T& value) {
    if constexpr (IsStrongTypedef<T>{}) {
        return value.GetUnderlying();
    } else {
        return value;
    }
}

// For 'std::string', begin-end methods are not forwarded, because otherwise
// it might get serialized as an array.
template <typename T, typename Void>
using EnableIfRange = std::enable_if_t<
    std::is_void_v<Void> && meta::kIsRange<T> && !meta::kIsInstantiationOf<std::basic_string, std::remove_const_t<T>>>;

template <typename T, typename Void>
using EnableIfSizeable = std::enable_if_t<std::is_void_v<Void> && meta::kIsSizable<T>>;

template <typename T>
constexpr void CheckIfAllowsLogging() {
    static_assert(IsStrongTypedef<T>::value);

    if constexpr (T::kOps & StrongTypedefOps::kNonLoggable) {
        static_assert(!sizeof(T), "Trying to print a non-loggable StrongTypedef");
    }
}

template <class To, class... Args>
constexpr bool IsStrongToStrongConversion() noexcept {
    static_assert(IsStrongTypedef<To>::value);

    if constexpr (sizeof...(Args) == 1) {
        using FromDecayed = std::decay_t<decltype((std::declval<Args>(), ...))>;
        if constexpr (IsStrongTypedef<FromDecayed>::value) {
            // Required to make `MyVariant v{MySpecialInt{10}};` compile.
            return !std::is_same_v<FromDecayed, To> &&
                   (std::is_same_v<typename FromDecayed::UnderlyingType, typename To::UnderlyingType> ||
                    std::is_arithmetic_v<typename To::UnderlyingType>);
        }
    }

    return false;
}

}  // namespace impl::strong_typedef

using impl::strong_typedef::IsStrongTypedef;

template <class Tag, class T, StrongTypedefOps Ops, class /*Enable*/>
class StrongTypedef : public impl::strong_typedef::StrongTypedefTag {
    static_assert(!std::is_reference<T>::value);
    static_assert(!std::is_pointer<T>::value);

    static_assert(!std::is_reference<Tag>::value);
    static_assert(!std::is_pointer<Tag>::value);

public:
    using UnderlyingType = T;
    using TagType = Tag;
    static constexpr StrongTypedefOps kOps = Ops;

    StrongTypedef() = default;
    StrongTypedef(const StrongTypedef&) = default;
    StrongTypedef(StrongTypedef&&) noexcept = default;
    StrongTypedef& operator=(const StrongTypedef&) = default;
    StrongTypedef& operator=(StrongTypedef&&) noexcept = default;

    constexpr StrongTypedef(impl::strong_typedef::InitializerList<T> lst) : data_(lst) {}

    template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<T, Args...>>>
    explicit constexpr StrongTypedef(Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...)))
        : data_(std::forward<Args>(args)...) {
        using impl::strong_typedef::IsStrongToStrongConversion;
        static_assert(
            !IsStrongToStrongConversion<StrongTypedef, Args...>(),
            "Attempt to convert one StrongTypedef to another. Use "
            "utils::StrongCast to do that"
        );
    }

    explicit constexpr operator const T&() const& noexcept { return data_; }
    explicit constexpr operator T() && noexcept { return std::move(data_); }
    explicit constexpr operator T&() & noexcept { return data_; }

    constexpr const T& GetUnderlying() const& noexcept { return data_; }
    constexpr T GetUnderlying() && noexcept { return std::move(data_); }
    constexpr T& GetUnderlying() & noexcept { return data_; }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<T, Void>>
    auto begin() {
        return std::begin(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<T, Void>>
    auto end() {
        return std::end(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<const T, Void>>
    auto begin() const {
        return std::begin(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<const T, Void>>
    auto end() const {
        return std::end(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<const T, Void>>
    auto cbegin() const {
        return std::cbegin(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfRange<const T, Void>>
    auto cend() const {
        return std::cend(data_);
    }

    template <typename Void = void, typename = impl::strong_typedef::EnableIfSizeable<const T, Void>>
    auto size() const {
        return std::size(data_);
    }

    auto empty() const { return data_.empty(); }

    auto clear() { return data_.clear(); }

    template <class Arg>
    decltype(auto) operator[](Arg&& i) {
        return data_[std::forward<Arg>(i)];
    }
    template <class Arg>
    decltype(auto) operator[](Arg&& i) const {
        return data_[std::forward<Arg>(i)];
    }

private:
    T data_{};
};

// Relational operators

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UTILS_STRONG_TYPEDEF_REL_OP(OPERATOR)                                                         \
    template <                                                                                        \
        class T,                                                                                      \
        class U,                                                                                      \
        std::enable_if_t<                                                                             \
            impl::strong_typedef::IsStrongTypedef<T>{} || impl::strong_typedef::IsStrongTypedef<U>{}, \
            int> /*Enable*/                                                                           \
        = 0>                                                                                          \
    constexpr auto operator OPERATOR(const T& lhs, const U& rhs)                                      \
        ->decltype(impl::strong_typedef::UnwrapIfStrongTypedef(lhs)                                   \
                       OPERATOR impl::strong_typedef::UnwrapIfStrongTypedef(rhs)) {                   \
        if constexpr (impl::strong_typedef::IsStrongTypedef<T>{}) {                                   \
            if constexpr (impl::strong_typedef::IsStrongTypedef<U>{}) {                               \
                impl::strong_typedef::CheckStrongCompare<T, U>();                                     \
                return lhs.GetUnderlying() OPERATOR rhs.GetUnderlying();                              \
            } else {                                                                                  \
                impl::strong_typedef::CheckTransparentCompare<T>();                                   \
                return lhs.GetUnderlying() OPERATOR rhs;                                              \
            }                                                                                         \
        } else {                                                                                      \
            impl::strong_typedef::CheckTransparentCompare<U>();                                       \
            return lhs OPERATOR rhs.GetUnderlying();                                                  \
        }                                                                                             \
    }

UTILS_STRONG_TYPEDEF_REL_OP(==)
UTILS_STRONG_TYPEDEF_REL_OP(!=)
UTILS_STRONG_TYPEDEF_REL_OP(<)
UTILS_STRONG_TYPEDEF_REL_OP(>)
UTILS_STRONG_TYPEDEF_REL_OP(<=)
UTILS_STRONG_TYPEDEF_REL_OP(>=)

#if __cpp_lib_three_way_comparison >= 201711L || defined(ARCADIA_ROOT)
UTILS_STRONG_TYPEDEF_REL_OP(<=>)
#endif

#undef UTILS_STRONG_TYPEDEF_REL_OP

/// Ostreams and Logging

template <class Tag, class T, StrongTypedefOps Ops>
std::ostream& operator<<(std::ostream& os, const StrongTypedef<Tag, T, Ops>& v) {
    impl::strong_typedef::CheckIfAllowsLogging<StrongTypedef<Tag, T, Ops>>();
    return os << v.GetUnderlying();
}

template <class Tag, class T, StrongTypedefOps Ops>
logging::LogHelper& operator<<(logging::LogHelper& os, const StrongTypedef<Tag, T, Ops>& v) {
    impl::strong_typedef::CheckIfAllowsLogging<StrongTypedef<Tag, T, Ops>>();
    return os << v.GetUnderlying();
}

// UnderlyingValue
template <class Tag, class T, StrongTypedefOps Ops>
constexpr decltype(auto) UnderlyingValue(const StrongTypedef<Tag, T, Ops>& v) noexcept {
    return v.GetUnderlying();
}

template <class Tag, class T, StrongTypedefOps Ops>
constexpr T UnderlyingValue(StrongTypedef<Tag, T, Ops>&& v) noexcept {
    return std::move(v).GetUnderlying();
}

constexpr bool IsStrongTypedefLoggable(StrongTypedefOps Ops) { return !(Ops & StrongTypedefOps::kNonLoggable); }

// Serialization

template <typename T, typename Value>
std::enable_if_t<formats::common::kIsFormatValue<Value> && IsStrongTypedef<T>{}, T>
Parse(const Value& source, formats::parse::To<T>) {
    return T{source.template As<typename T::UnderlyingType>()};
}

template <typename T, typename Value>
std::enable_if_t<IsStrongTypedef<T>{}, Value> Serialize(const T& object, formats::serialize::To<Value>) {
    impl::strong_typedef::CheckIfAllowsLogging<T>();
    return typename Value::Builder(object.GetUnderlying()).ExtractValue();
}

template <typename T, typename StringBuilder>
std::enable_if_t<IsStrongTypedef<T>{}> WriteToStream(const T& object, StringBuilder& sw) {
    impl::strong_typedef::CheckIfAllowsLogging<T>();
    WriteToStream(object.GetUnderlying(), sw);
}

template <typename Tag, StrongTypedefOps Ops>
std::string ToString(const StrongTypedef<Tag, std::string, Ops>& object) {
    impl::strong_typedef::CheckIfAllowsLogging<StrongTypedef<Tag, std::string, Ops>>();
    return object.GetUnderlying();
}

template <typename Tag, typename T, StrongTypedefOps Ops, std::enable_if_t<meta::kIsInteger<T>, bool> = true>
std::string ToString(const StrongTypedef<Tag, T, Ops>& object) {
    impl::strong_typedef::CheckIfAllowsLogging<StrongTypedef<Tag, std::string, Ops>>();
    return std::to_string(object.GetUnderlying());
}

template <typename Tag, typename T, StrongTypedefOps Ops, std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
std::string ToString(const StrongTypedef<Tag, T, Ops>& object) {
    impl::strong_typedef::CheckIfAllowsLogging<StrongTypedef<Tag, std::string, Ops>>();
    return fmt::to_string(object.GetUnderlying());
}

// Explicit casting

/// Explicitly cast from one strong typedef to another, to replace constructions
/// `SomeStrongTydef{utils::UnderlyingValue(another_strong_val)}` with
/// `utils::StrongCast<SomeStrongTydef>(another_strong_val)`
template <typename Target, typename Tag, typename T, StrongTypedefOps Ops, typename Enable>
constexpr Target StrongCast(const StrongTypedef<Tag, T, Ops, Enable>& src) {
    static_assert(IsStrongTypedef<Target>{}, "Expected strong typedef as target type");
    static_assert(
        std::is_convertible_v<T, typename Target::UnderlyingType>,
        "Source strong typedef underlying type must be convertible to "
        "target's underlying type"
    );
    return Target{src.GetUnderlying()};
}

template <typename Target, typename Tag, typename T, StrongTypedefOps Ops, typename Enable>
constexpr Target StrongCast(StrongTypedef<Tag, T, Ops, Enable>&& src) {
    static_assert(IsStrongTypedef<Target>{}, "Expected strong typedef as target type");
    static_assert(
        std::is_convertible_v<T, typename Target::UnderlyingType>,
        "Source strong typedef underlying type must be convertible to "
        "target's underlying type"
    );
    return Target{std::move(src).GetUnderlying()};
}

template <class Tag, class T, StrongTypedefOps Ops>
std::size_t hash_value(const StrongTypedef<Tag, T, Ops>& v) {
    return boost::hash<T>{}(v.GetUnderlying());
}

/// gtest formatter for utils::StrongTypedef
template <class Tag, class T, StrongTypedefOps Ops>
void PrintTo(const StrongTypedef<Tag, T, Ops>& v, std::ostream* os) {
    *os << testing::PrintToString(v.GetUnderlying());
}

/// A StrongTypedef for data that MUST NOT be logged or outputted in some other
/// way. Also prevents the data from appearing in backtrace prints of debugger.
///
/// @snippet storages/secdist/secdist_test.cpp UserPasswords
template <class Tag, class T>
using NonLoggable = StrongTypedef<Tag, T, StrongTypedefOps::kCompareStrong | StrongTypedefOps::kNonLoggable>;

}  // namespace utils

USERVER_NAMESPACE_END

// std::hash support
template <class Tag, class T, USERVER_NAMESPACE::utils::StrongTypedefOps Ops>
struct std::hash<USERVER_NAMESPACE::utils::StrongTypedef<Tag, T, Ops>> : std::hash<T> {
    std::size_t operator()(const USERVER_NAMESPACE::utils::StrongTypedef<Tag, T, Ops>& v) const
        noexcept(noexcept(std::declval<const std::hash<T>>()(std::declval<const T&>()))) {
        return std::hash<T>::operator()(v.GetUnderlying());
    }
};

// fmt::format support
template <class T, class Char>
struct fmt::formatter<T, Char, std::enable_if_t<USERVER_NAMESPACE::utils::IsStrongTypedef<T>{}>>
    : fmt::formatter<typename T::UnderlyingType, Char> {
    template <typename FormatContext>
    auto format(const T& v, FormatContext& ctx) USERVER_FMT_CONST {
        USERVER_NAMESPACE::utils::impl::strong_typedef::CheckIfAllowsLogging<T>();
        return fmt::formatter<typename T::UnderlyingType, Char>::format(v.GetUnderlying(), ctx);
    }
};
