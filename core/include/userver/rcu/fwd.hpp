#pragma once

/// @file userver/rcu/fwd.hpp
/// @brief Forward declarations for rcu::Variable and rcu::RcuMap

// TODO remove extra includes
#include <functional>
#include <unordered_map>

USERVER_NAMESPACE_BEGIN

namespace rcu {

struct DefaultRcuTraits;
struct SyncRcuTraits;
struct BlockingRcuTraits;

template <typename Key>
struct DefaultRcuMapTraits;

template <typename T, typename RcuTraits = DefaultRcuTraits>
class Variable;

template <typename T, typename RcuTraits = DefaultRcuTraits>
class ReadablePtr;

template <typename T, typename RcuTraits = DefaultRcuTraits>
class WritablePtr;

template <typename Key, typename Value, typename RcuMapTraits = DefaultRcuMapTraits<Key>>
class RcuMap;

}  // namespace rcu

USERVER_NAMESPACE_END
