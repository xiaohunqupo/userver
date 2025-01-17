#include <userver/ugrpc/field_mask.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/field_mask.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/port.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/port.h>
#include <google/protobuf/util/field_mask_util.h>
#include <grpcpp/support/config.h>

#include <ugrpc/impl/protobuf_utils.hpp>
#include <userver/crypto/base64.hpp>
#include <userver/ugrpc/protobuf_visit.hpp>
#include <userver/utils/assert.hpp>
#include <userver/utils/text_light.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc {

namespace {

struct Part {
    std::string_view part;
    std::size_t used_symbols;
};

Part GetRoot(std::string_view path) {
    if (path.empty()) {
        throw FieldMask::BadPathError("The path may not be empty");
    }

    if (path[0] == '`') {
        const std::size_t end = path.find('`', 1);
        if (end == std::string_view::npos) {
            throw FieldMask::BadPathError("Every backtick must be closed");
        }
        if (end != path.size() - 1 && path[end + 1] != '.') {
            throw FieldMask::BadPathError("A closing backtick must be followed by a dot");
        }

        const std::string_view part = path.substr(1, end - 1);
        if (part.empty()) {
            throw FieldMask::BadPathError("The path may not have empty parts");
        }

        return {part, std::min(end + 2, path.size())};
    }

    const std::size_t end = std::min(path.find('.'), path.size());
    const std::string_view part = path.substr(0, end);
    if (part.empty()) {
        throw FieldMask::BadPathError("The path may not have empty parts");
    }
    return {part, std::min(end + 1, path.size())};
}

std::string GetMapKeyAsString(const google::protobuf::Message& entry) {
    const google::protobuf::Descriptor* descriptor = entry.GetDescriptor();
    UINVARIANT(descriptor, "descriptor is nullptr");

    const google::protobuf::FieldDescriptor* map_key = descriptor->map_key();
    UINVARIANT(map_key, "map_key is nullptr");

    const google::protobuf::Reflection* reflection = entry.GetReflection();
    UINVARIANT(reflection, "reflection is nullptr");

    switch (map_key->cpp_type()) {
        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            return std::to_string(reflection->GetInt32(entry, map_key));
        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            return std::to_string(reflection->GetInt64(entry, map_key));
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            return std::to_string(reflection->GetUInt32(entry, map_key));
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            return std::to_string(reflection->GetUInt64(entry, map_key));
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            return reflection->GetString(entry, map_key);
        default:
            UINVARIANT(false, "Invalid map_key type");
    }
}

google::protobuf::FieldMask RawFromString(std::string_view string, FieldMask::Encoding encoding) {
    google::protobuf::FieldMask out;
    std::string base64_tmp;
    switch (encoding) {
        case FieldMask::Encoding::kWebSafeBase64:
            base64_tmp = crypto::base64::Base64UrlDecode(string);
            string = base64_tmp;
            [[fallthrough]];
        case FieldMask::Encoding::kCommaSeparated:
#if GOOGLE_PROTOBUF_VERSION >= 3022000
            google::protobuf::util::FieldMaskUtil::FromString(string, &out);
#else
            google::protobuf::util::FieldMaskUtil::FromString(std::string(string), &out);
#endif
            break;
        default:
            UINVARIANT(false, "Unknown encoding");
    }
    return out;
}

std::string RawToString(const google::protobuf::FieldMask& field_mask, FieldMask::Encoding encoding) {
    auto comma_separated = google::protobuf::util::FieldMaskUtil::ToString(field_mask);
    if (encoding == FieldMask::Encoding::kCommaSeparated) return comma_separated;
    UINVARIANT(encoding == FieldMask::Encoding::kWebSafeBase64, "Unknown encoding");
    return crypto::base64::Base64UrlEncode(comma_separated, crypto::base64::Pad::kWith);
}

}  // namespace

FieldMask::FieldMask(const google::protobuf::FieldMask& field_mask) {
    for (const auto& path : field_mask.paths()) {
        AddPath(path);
    }
}

FieldMask::FieldMask(std::string_view string, Encoding encoding) : FieldMask(RawFromString(string, encoding)) {}

std::string FieldMask::ToString() const { return RawToString(ToRawMask(), Encoding::kCommaSeparated); }

std::string FieldMask::ToWebSafeBase64() const { return RawToString(ToRawMask(), Encoding::kWebSafeBase64); }

void FieldMask::AddPath(std::string_view path) {
    if (path.empty() || is_leaf_) {
        // Everything inside is already in the field mask
        children_->clear();
        is_leaf_ = true;
        return;
    }

    Part root = GetRoot(path);
    const auto it = children_->emplace(std::move(root.part), FieldMask());
    it.first->second.AddPath(path.substr(root.used_symbols));
}

void FieldMask::CheckValidity(const google::protobuf::Descriptor* descriptor) const {
    // A leaf mask is surely valid
    if (IsLeaf()) return;

    // It is not possible to mask nothing
    if (!descriptor) {
        throw BadPathError("Trying to mask a NULL descriptor");
    }

    for (const auto& [field_name, nested_mask] : *children_) {
        const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName(field_name);

        // Check that the field exists
        if (!field) {
            throw BadPathError(fmt::format("{} was not found on message {}", field_name, descriptor->full_name()));
        }

        // If the nested mask is a leaf, it is surely valid
        if (nested_mask.IsLeaf()) continue;

        if (field->is_map()) {
            // The children of nested_mask are map keys

            const google::protobuf::Descriptor* entry = field->message_type();
            UINVARIANT(entry, "entry is nullptr");

            const google::protobuf::FieldDescriptor* map_key = entry->map_key();
            UINVARIANT(map_key, "map_key is nullptr");

            using Fd = google::protobuf::FieldDescriptor;
            if (map_key->cpp_type() != Fd::CPPTYPE_INT32 && map_key->cpp_type() != Fd::CPPTYPE_INT64 &&
                map_key->cpp_type() != Fd::CPPTYPE_UINT32 && map_key->cpp_type() != Fd::CPPTYPE_UINT64 &&
                map_key->cpp_type() != Fd::CPPTYPE_STRING) {
                throw BadPathError(fmt::format(
                    "Trying to mask a map with non-integer and non-strings "
                    "keys at field {} of message {}",
                    field->name(),
                    descriptor->full_name()
                ));
            }

            const google::protobuf::FieldDescriptor* map_value = entry->map_value();
            UINVARIANT(map_value, "map_value is nullptr");

            for (const auto& [key, value_mask] : *nested_mask.children_) {
                // If the mask is a leaf, it is surely valid
                if (value_mask.IsLeaf()) continue;

                // It is not possible to mask a non-message type.
                if (!impl::IsMessage(*map_value)) {
                    throw BadPathError(fmt::format(
                        "Trying to mask a primitive map value at key {} in "
                        "field {} of message {}",
                        key,
                        field->name(),
                        descriptor->full_name()
                    ));
                }

                value_mask.CheckValidity(map_value->message_type());
            }
        } else if (field->is_repeated()) {
            if (nested_mask.children_->size() != 1 || nested_mask.children_->begin()->first != "*") {
                throw BadPathError(fmt::format(
                    "A non-leaf mask for a repeated field {} in message {} "
                    "contains something except *",
                    field->name(),
                    descriptor->full_name()
                ));
            }

            const FieldMask& star_mask = nested_mask.children_->begin()->second;

            // If the mask is a leaf, it is surely valid
            if (star_mask.IsLeaf()) continue;

            if (!impl::IsMessage(*field)) {
                throw BadPathError(fmt::format(
                    "Trying to mask a repeated of primitives in field {} of message {}",
                    field->name(),
                    descriptor->full_name()
                ));
            }

            star_mask.CheckValidity(field->message_type());
        } else if (!impl::IsMessage(*field)) {
            // It is not possible to mask a non-message type.
            throw BadPathError(
                fmt::format("Trying to mask a primitive field {} of message {}", field->name(), descriptor->full_name())
            );
        } else {
            nested_mask.CheckValidity(field->message_type());
        }
    }
}

bool FieldMask::IsPathFullyIn(std::string_view path) const {
    if (path.empty() || IsLeaf()) return IsLeaf();
    const Part root = GetRoot(path);
    const utils::OptionalRef<const ugrpc::FieldMask> child = GetMaskForField(root.part);
    return child.has_value() ? child->IsPathFullyIn(path.substr(root.used_symbols)) : false;
}

bool FieldMask::IsPathPartiallyIn(std::string_view path) const {
    if (path.empty() || IsLeaf()) return true;
    const Part root = GetRoot(path);
    const utils::OptionalRef<const ugrpc::FieldMask> child = GetMaskForField(root.part);
    return child.has_value() ? child->IsPathPartiallyIn(path.substr(root.used_symbols)) : false;
}

void FieldMask::Trim(google::protobuf::Message& message) const {
    CheckValidity(message.GetDescriptor());
    TrimNoValidate(message);
}

void FieldMask::TrimNoValidate(google::protobuf::Message& message) const {
    if (IsLeaf()) {
        // The message is either explicitly in the mask, or the mask is empty
        return;
    }

    const google::protobuf::Reflection* reflection = message.GetReflection();
    UINVARIANT(reflection, "reflection is nullptr");

    VisitFields(message, [&](google::protobuf::Message&, const google::protobuf::FieldDescriptor& field) {
        const utils::OptionalRef<const ugrpc::FieldMask> nested_mask_ref = GetMaskForField(field.name());
        if (!nested_mask_ref.has_value()) {
            // The field is not in the field mask. Remove it.
            return reflection->ClearField(&message, &field);
        }

        const FieldMask& nested_mask = nested_mask_ref.value();
        if (nested_mask.IsLeaf()) {
            // The field must not be masked
            return;
        }

        if (field.is_map()) {
            const google::protobuf::MutableRepeatedFieldRef<google::protobuf::Message> map =
                reflection->GetMutableRepeatedFieldRef<google::protobuf::Message>(&message, &field);
            for (int i = 0; i < map.size(); ++i) {
                google::protobuf::Message* entry = reflection->MutableRepeatedMessage(&message, &field, i);
                UINVARIANT(entry, "entry is nullptr");

                const std::string key = GetMapKeyAsString(*entry);
                const utils::OptionalRef<const ugrpc::FieldMask> value_mask_ref = nested_mask.GetMaskForField(key);

                if (!value_mask_ref.has_value()) {
                    // The map key is not in the field mask.
                    // Remove the record by putting it to the back of the array.
                    //
                    // The protocol does not guarantee the order of elements in the
                    // map, so this should be safe.
                    map.SwapElements(i--, map.size() - 1);
                    map.RemoveLast();
                    continue;
                }

                if (value_mask_ref->IsLeaf()) continue;

                // The map key has a mask for the value
                const google::protobuf::Descriptor* entry_desc = field.message_type();
                UINVARIANT(entry_desc, "entry_desc is nullptr");

                const google::protobuf::FieldDescriptor* map_value = entry_desc->map_value();
                UINVARIANT(map_value, "map_value is nullptr");
                UINVARIANT(impl::IsMessage(*map_value), "non-leaf mask may only apply to messages");

                const google::protobuf::Reflection* entry_reflection = entry->GetReflection();
                UINVARIANT(entry_reflection, "entry_reflection is nullptr");

                google::protobuf::Message* value_msg = entry_reflection->MutableMessage(entry, map_value);
                UINVARIANT(value_msg, "value_msg is nullptr");
                value_mask_ref->TrimNoValidate(*value_msg);
            }
        } else if (field.is_repeated()) {
            constexpr std::string_view kBadRepeatedMask = "A non-leaf field mask for an array can contain only *";
            UINVARIANT(nested_mask.children_->size() == 1, kBadRepeatedMask);
            UINVARIANT(nested_mask.children_->begin()->first == "*", kBadRepeatedMask);

            const FieldMask& star_mask = nested_mask.children_->begin()->second;
            if (star_mask.IsLeaf()) return;

            UINVARIANT(impl::IsMessage(field), "non-leaf mask may only apply to messages");

            const int repeated_size = reflection->FieldSize(message, &field);
            for (int i = 0; i < repeated_size; ++i) {
                google::protobuf::Message* repeated_msg = reflection->MutableRepeatedMessage(&message, &field, i);
                UINVARIANT(repeated_msg, "repeated_msg is nullptr");
                star_mask.TrimNoValidate(*repeated_msg);
            }
        } else {
            UINVARIANT(impl::IsMessage(field), "non-leaf mask may only apply to messages");
            google::protobuf::Message* nested_msg = reflection->MutableMessage(&message, &field);
            UINVARIANT(nested_msg, "nested_msg is nullptr");
            nested_mask.TrimNoValidate(*nested_msg);
        }
    });
}

bool FieldMask::IsLeaf() const { return is_leaf_ || children_->empty(); }

std::vector<std::string_view> FieldMask::GetFieldNamesList() const {
    const auto view = GetFieldNames();
    return std::vector<std::string_view>(view.cbegin(), view.cend());
}

bool FieldMask::HasFieldName(std::string_view field) const { return GetMaskForField(field).has_value(); }

utils::OptionalRef<const FieldMask> FieldMask::GetMaskForField(std::string_view field) const {
    if (IsLeaf()) {
        // Everything inside is included in the field mask
        static const FieldMask kEmptyFieldMask;
        return kEmptyFieldMask;
    }
    auto it = utils::impl::FindTransparent(*children_, field);
    if (it == children_->end()) {
        // '*' applies to all fields if there is no more specific rule
        it = children_->find("*");
        if (it == children_->end()) return std::nullopt;
    }
    return it->second;
}

google::protobuf::FieldMask FieldMask::ToRawMask() const {
    google::protobuf::FieldMask out;
    std::vector<std::string> stack;
    ToRawMaskImpl(stack, out);
    return out;
}

void FieldMask::ToRawMaskImpl(std::vector<std::string>& stack, google::protobuf::FieldMask& out) const {
    if (IsLeaf()) {
        const std::string path = fmt::format("{}", fmt::join(stack, "."));
        out.add_paths(path);
        return;
    }
    for (const auto& [field_name, nested_mask] : *children_) {
        if (field_name.find('.') == std::string::npos) {
            stack.push_back(field_name);
        } else {
            stack.push_back(fmt::format("`{}`", field_name));
        }
        nested_mask.ToRawMaskImpl(stack, out);
        stack.pop_back();
    }
}

}  // namespace ugrpc

USERVER_NAMESPACE_END
