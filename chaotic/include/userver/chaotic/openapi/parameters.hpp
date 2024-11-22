#pragma once

#include <string_view>
#include <vector>

USERVER_NAMESPACE_BEGIN

namespace chaotic::openapi {

enum class In {
    kHeader,
    kCookie,
    kPath,
    kQuery,
    kQueryExplode,
};

using Name = const char* const;

enum class ParameterType {
    kTrivial,
    kArray,
    // kObject,
};

template <In In_, const Name& Name_, typename RawType_, typename UserType_ = RawType_>
struct TrivialParameter {
    static constexpr auto kIn = In_;
    static constexpr auto& kName = Name_;

    using RawType = RawType_;
    using UserType = UserType_;
    static constexpr ParameterType Type = ParameterType::kTrivial;
};

template <In In_, const Name& Name_, char Delimiter_, typename RawItemType_, typename UserItemType_ = RawItemType_>
struct ArrayParameter {
    static constexpr auto kIn = In_;
    static constexpr auto& kName = Name_;
    static constexpr char kDelimiter = Delimiter_;

    using RawType = std::vector<std::string>;
    using RawItemType = RawItemType_;
    using UserType = std::vector<UserItemType_>;
    using UserItemType = UserItemType_;
    static constexpr ParameterType Type = ParameterType::kArray;
};

}  // namespace chaotic::openapi

USERVER_NAMESPACE_END
