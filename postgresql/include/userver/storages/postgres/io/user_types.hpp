#pragma once

/// @file userver/storages/postgres/io/user_types.hpp
/// @brief User types I/O support
/// @ingroup userver_postgres_parse_and_format

#include <unordered_map>
#include <unordered_set>

#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/pg_types.hpp>
#include <userver/storages/postgres/io/type_mapping.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

/// @brief PostgreSQL composite type description
class CompositeTypeDescription {
public:
    using CompositeFieldDefs = std::vector<CompositeFieldDef>;
    CompositeTypeDescription(CompositeFieldDefs::const_iterator begin, CompositeFieldDefs::const_iterator end)
        : attributes_{begin, end} {}
    std::size_t Size() const { return attributes_.size(); }
    bool Empty() const { return attributes_.empty(); }
    const CompositeFieldDef& operator[](std::size_t index) const {
        if (index >= Size()) {
            throw FieldIndexOutOfBounds{index};
        }
        return attributes_[index];
    }

private:
    CompositeFieldDefs attributes_;
};

/// @brief Container for connection-specific user data types.
class UserTypes {
public:
    using CompositeFieldDefs = std::vector<CompositeFieldDef>;

    UserTypes() = default;
    UserTypes(const UserTypes&) = delete;
    UserTypes(UserTypes&&) noexcept = default;

    UserTypes& operator=(const UserTypes&) = delete;
    UserTypes& operator=(UserTypes&&) noexcept = default;

    void Reset();

    Oid FindOid(DBTypeName) const;
    Oid FindArrayOid(DBTypeName) const;
    /// Find element type oid for an array type.
    /// Returns invalid oid if the type is not an array or the type is not found
    Oid FindElementOid(Oid) const;
    DBTypeName FindName(Oid) const;
    /// Find name of the base type for a domain or element type for an array.
    /// For the rest of types returns the name for the oid if found.
    DBTypeName FindBaseName(Oid) const;
    /// Find base oid for a domain or element type for an array.
    /// For the rest of types returns the oid itself.
    Oid FindBaseOid(Oid) const;
    Oid FindBaseOid(DBTypeName) const;
    /// Find base oid for a domain.
    /// For the rest of types returns the oid itself.
    Oid FindDomainBaseOid(Oid) const;

    bool HasParser(Oid) const;
    io::BufferCategory GetBufferCategory(Oid) const;
    const io::TypeBufferCategory& GetTypeBufferCategories() const;

    void AddType(DBTypeDescription&& desc);
    void AddCompositeFields(CompositeFieldDefs&& defs);

    const CompositeTypeDescription& GetCompositeDescription(Oid) const;
    /// Get type description by oid.
    /// May return nullptr if the type was not loaded from the database
    const DBTypeDescription* GetTypeDescription(Oid) const;

    /// @throws UserTypeError if not all registered cpp types are added
    void CheckRegisteredTypes() const;

private:
    using DescriptionSet =
        std::unordered_set<DBTypeDescription, DBTypeDescription::NameHash, DBTypeDescription::NamesEqual>;
    using DescriptionIterator = DescriptionSet::const_iterator;
    using MapByOid = std::unordered_map<Oid, DescriptionIterator>;
    using MapByName = std::unordered_map<DBTypeName, DescriptionIterator>;
    using CompositeTypes = std::unordered_map<Oid, CompositeTypeDescription>;

    DescriptionSet types_;
    MapByOid by_oid_;
    MapByName by_name_;
    io::TypeBufferCategory buffer_categories_;
    CompositeTypes composite_types_;
};

namespace io::detail {

template <typename T>
inline constexpr DBTypeName kPgUserTypeName = CppToUserPg<T>::postgres_name;

template <typename T>
struct CppToUserPgImpl {
    static_assert(io::traits::CheckParser<T>());

    using Type = T;
    using Mapping = CppToUserPg<T>;
    static constexpr DBTypeName postgres_name = kPgUserTypeName<T>;
    static const detail::RegisterUserTypeParser init_;
    static Oid GetOid(const UserTypes& user_types) {
        // TODO Handle oid not found
        return user_types.FindOid(init_.postgres_name);
    }
    static Oid GetArrayOid(const UserTypes& user_types) {
        // TODO Handle oid not found
        return user_types.FindArrayOid(init_.postgres_name);
    }
};

template <typename T>
const RegisterUserTypeParser CppToUserPgImpl<T>::init_ =
    RegisterUserTypeParser::Register(kPgUserTypeName<T>, std::string{compiler::GetTypeName<T>()});

}  // namespace io::detail

void LogRegisteredTypesOnce();

}  // namespace storages::postgres

USERVER_NAMESPACE_END
