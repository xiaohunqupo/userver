#include <userver/yaml_config/schema.hpp>

#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/count.hpp>

#include <fmt/ranges.h>

#include <userver/formats/parse/common_containers.hpp>
#include <userver/formats/serialize/common_containers.hpp>
#include <userver/formats/yaml/serialize.hpp>
#include <userver/formats/yaml/value_builder.hpp>
#include <userver/utils/assert.hpp>
#include <userver/utils/trivial_map.hpp>

USERVER_NAMESPACE_BEGIN

namespace yaml_config {

namespace {

void CheckFieldsNames(const formats::yaml::Value& yaml_schema) {
    static constexpr utils::TrivialSet kFieldNames = [](auto selector) {
        return selector()
            .Case("type")
            .Case("description")
            .Case("defaultDescription")
            .Case("additionalProperties")
            .Case("properties")
            .Case("items")
            .Case("enum")
            .Case("minimum")
            .Case("maximum")
            .Case("minItems")
            .Case("maxItems");
    };

    for (const auto& [name, value] : Items(yaml_schema)) {
        const auto found = kFieldNames.Contains(name);

        if (!found) {
            throw std::runtime_error(fmt::format(
                "Schema field name must be one of [{}], but '{}' was "
                "given. Schema path: '{}'",
                kFieldNames.Describe(),
                name,
                yaml_schema.GetPath()
            ));
        }
    }
}

template <typename Field>
void CheckTypeSupportsField(
    const Schema& schema,
    std::string_view field_name,
    const std::optional<Field>& optional_field,
    std::initializer_list<FieldType> allowed_types
) {
    if (optional_field.has_value() && boost::count(allowed_types, schema.type) == 0) {
        const auto allowed_type_strings = boost::adaptors::transform(allowed_types, [](FieldType type) {
            return fmt::format("'{}'", ToString(type));
        });
        throw std::runtime_error(fmt::format(
            "Schema field '{}' of type '{}' can not have field '{}', "
            "because its type is not {}",
            schema.path,
            ToString(schema.type),
            field_name,
            fmt::join(allowed_type_strings, " or ")
        ));
    }
}

void CheckSchemaStructure(const Schema& schema) {
    CheckTypeSupportsField(schema, "items", schema.items, {FieldType::kArray});
    CheckTypeSupportsField(schema, "properties", schema.properties, {FieldType::kObject});
    CheckTypeSupportsField(schema, "additionalProperties", schema.additional_properties, {FieldType::kObject});
    CheckTypeSupportsField(schema, "enum", schema.enum_values, {FieldType::kString});
    CheckTypeSupportsField(schema, "minimum", schema.minimum, {FieldType::kInteger, FieldType::kNumber});
    CheckTypeSupportsField(schema, "maximum", schema.maximum, {FieldType::kInteger, FieldType::kNumber});
    CheckTypeSupportsField(schema, "minItems", schema.min_items, {FieldType::kArray});
    CheckTypeSupportsField(schema, "maxItems", schema.max_items, {FieldType::kArray});

    if (schema.type == FieldType::kObject) {
        if (!schema.properties.has_value()) {
            throw std::runtime_error(fmt::format(
                "Schema field '{}' of type 'object' "
                "must have field 'properties'",
                schema.path
            ));
        }
        if (!schema.additional_properties.has_value()) {
            throw std::runtime_error(fmt::format(
                "Schema field '{}' of type 'object' must have field "
                "'additionalProperties'",
                schema.path
            ));
        }
    } else if (schema.type == FieldType::kArray) {
        if (!schema.items.has_value()) {
            throw std::runtime_error(
                fmt::format("Schema field '{}' of type 'array' must have field 'items'", schema.path)
            );
        }
    }
}

constexpr utils::TrivialBiMap kFieldTypes = [](auto selector) {
    return selector()
        .Case("boolean", FieldType::kBool)
        .Case("integer", FieldType::kInteger)
        .Case("number", FieldType::kNumber)
        .Case("string", FieldType::kString)
        .Case("object", FieldType::kObject)
        .Case("array", FieldType::kArray);
};

auto TieSchema(const Schema& schema) {
    return std::tie(
        // `path` is ignored, because it serves a purely diagnostic purpose.
        schema.type,
        schema.description,
        schema.default_description,
        schema.additional_properties,
        schema.properties,
        schema.items,
        schema.enum_values,
        schema.minimum,
        schema.maximum,
        schema.min_items,
        schema.max_items
    );
}

}  // namespace

std::string ToString(FieldType type) {
    auto value = kFieldTypes.TryFind(type);
    UINVARIANT(value, "Incorrect field type");
    return std::string{*value};
}

FieldType Parse(const formats::yaml::Value& type, formats::parse::To<FieldType>) {
    const std::string as_string = type.As<std::string>();
    auto value = kFieldTypes.TryFind(as_string);

    if (value) {
        return *value;
    }

    throw std::runtime_error(fmt::format(
        "Schema field 'type' must be one of [{}]), but '{}' was given", kFieldTypes.DescribeFirst(), as_string
    ));
}

SchemaPtr Parse(const formats::yaml::Value& schema, formats::parse::To<SchemaPtr>) {
    return SchemaPtr(schema.As<Schema>());
}

formats::yaml::Value Serialize(const SchemaPtr& schema, formats::serialize::To<formats::yaml::Value> to) {
    return Serialize(*schema, to);
}

SchemaPtr::SchemaPtr(Schema&& schema) : schema_(std::make_unique<Schema>(std::move(schema))) {}

SchemaPtr::SchemaPtr(const SchemaPtr& other) : SchemaPtr(Schema{*other}) {}

SchemaPtr& SchemaPtr::operator=(const SchemaPtr& other) {
    *this = SchemaPtr{other};
    return *this;
}

const Schema& SchemaPtr::operator*() const {
    UASSERT(schema_);
    return *schema_;
}

Schema& SchemaPtr::operator*() {
    UASSERT(schema_);
    return *schema_;
}

bool SchemaPtr::operator==(const SchemaPtr& other) const { return *schema_ == *other.schema_; }

std::variant<bool, SchemaPtr>
Parse(const formats::yaml::Value& value, formats::parse::To<std::variant<bool, SchemaPtr>>) {
    if (value.IsBool()) {
        return value.As<bool>();
    } else {
        return value.As<SchemaPtr>();
    }
}

Schema Parse(const formats::yaml::Value& schema, formats::parse::To<Schema>) {
    Schema result;
    result.path = schema.GetPath();
    result.type = schema["type"].As<FieldType>();
    result.description = schema["description"].As<std::string>();

    result.additional_properties = schema["additionalProperties"].As<std::optional<std::variant<bool, SchemaPtr>>>();
    result.default_description = schema["defaultDescription"].As<std::optional<std::string>>();
    result.properties = schema["properties"].As<std::optional<std::unordered_map<std::string, SchemaPtr>>>();
    result.items = schema["items"].As<std::optional<SchemaPtr>>();
    result.enum_values = schema["enum"].As<std::optional<std::unordered_set<std::string>>>();
    result.minimum = schema["minimum"].As<std::optional<double>>();
    result.maximum = schema["maximum"].As<std::optional<double>>();

    result.min_items = schema["minItems"].As<std::optional<std::size_t>>();
    result.max_items = schema["maxItems"].As<std::optional<std::size_t>>();

    CheckFieldsNames(schema);

    CheckSchemaStructure(result);

    return result;
}

formats::yaml::Value Serialize(const Schema& schema, formats::serialize::To<formats::yaml::Value>) {
    formats::yaml::ValueBuilder builder;

    builder["type"] = ToString(schema.type);
    builder["description"] = schema.description;

    if (schema.default_description) builder["defaultDescription"] = *schema.default_description;

    if (schema.additional_properties)
        builder["additionalProperties"] =
            std::visit([](const auto& x) { return formats::yaml::ValueBuilder(x); }, *schema.additional_properties);
    if (schema.properties) builder["properties"] = *schema.properties;
    if (schema.items) builder["items"] = *schema.items;
    if (schema.enum_values) builder["enum"] = *schema.enum_values;
    if (schema.minimum) builder["minimum"] = *schema.minimum;
    if (schema.maximum) builder["maximum"] = *schema.maximum;
    if (schema.min_items) builder["minItems"] = *schema.min_items;
    if (schema.max_items) builder["maxItems"] = *schema.max_items;
    return builder.ExtractValue();
}

void Schema::UpdateDescription(std::string new_description) { description = std::move(new_description); }

Schema Schema::EmptyObject() {
    return impl::SchemaFromString(R"(
  type: object
  properties: {}
  additionalProperties: false
  description: TODO
)");
}

bool Schema::operator==(const Schema& other) const { return TieSchema(*this) == TieSchema(other); }

Schema impl::SchemaFromString(const std::string& yaml_string) {
    return formats::yaml::FromString(yaml_string).As<Schema>();
}

}  //  namespace yaml_config

USERVER_NAMESPACE_END
