// SPDX-License-Identifier: MIT
#include "superpos_schema.h"
#include "u64_bits.h"
#include "core/crypto/crypto_core.h"
#include "core/object/class_db.h"
#include "superpos/schema.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include "private/allocation_probe.h"

namespace {
bool append(std::span<uint8_t> bytes, size_t &position, uint64_t value, int width) {
    if (position > bytes.size() || size_t(width) > bytes.size() - position) { return false; }
    for (int i = 0; i < width; ++i) { bytes[position++] = uint8_t(value >> (i * 8)); }
    return true;
}
}
Dictionary SuperposSchema::bake() const {
    Dictionary result;
    result["error"] = ERR_INVALID_DATA;
    if (!schema_id || !revision || fields.is_empty() || fields.size() > int(superpos::Schema::maximum_fields)) { return result; }
    const TypedArray<SuperposField> owned_fields = fields;
    std::array<SuperposField *, superpos::Schema::maximum_fields> sorted{};
    const size_t count = size_t(fields.size());
    size_t manifest_size = 28;
    for (size_t i = 0; i < count; ++i) {
        auto *field = Object::cast_to<SuperposField>(static_cast<Object *>(owned_fields[int(i)]));
        if (!field || !field->get_field_id() || !field->get_codec_id() || field->get_codec_id() > 10 || field->get_audience() > 2 || !field->get_max_bytes() || field->get_max_bytes() > superpos::Schema::maximum_state_bytes) { return result; }
        String name = field->get_field_name();
        if (name.is_empty() || name.length() > 64) { return result; }
        for (int c = 0; c < name.length(); ++c) {
            if (!(name[c] >= 'a' && name[c] <= 'z') && !(name[c] >= 'A' && name[c] <= 'Z') && !(name[c] >= '0' && name[c] <= '9') && name[c] != '_') { return result; }
        }
        manifest_size += 44 + size_t(name.length());
        sorted[i] = field;
    }
    std::sort(sorted.begin(), sorted.begin() + count, [](const SuperposField *a, const SuperposField *b) { return a->get_field_id() < b->get_field_id(); });
    // Optional typed RPC block (manifest version 2). Without RPCs the manifest
    // keeps version 1, so existing schema fingerprints are unchanged.
    if (rpcs.size() > int(superpos::Schema::maximum_rpcs)) { return result; }
    const TypedArray<SuperposRpc> owned_rpcs = rpcs;
    std::array<SuperposRpc *, superpos::Schema::maximum_rpcs> sorted_rpcs{};
    const size_t rpc_count = size_t(rpcs.size());
    for (size_t i = 0; i < rpc_count; ++i) {
        auto *rpc = Object::cast_to<SuperposRpc>(static_cast<Object *>(owned_rpcs[int(i)]));
        if (!rpc || !rpc->get_rpc_id() || rpc->get_permission() > 2 || !rpc->get_maximum_payload() || rpc->get_maximum_payload() > 4096) { return result; }
        sorted_rpcs[i] = rpc;
    }
    std::sort(sorted_rpcs.begin(), sorted_rpcs.begin() + rpc_count, [](const SuperposRpc *a, const SuperposRpc *b) { return a->get_rpc_id() < b->get_rpc_id(); });
    std::array<superpos::RpcDescriptor, superpos::Schema::maximum_rpcs> rpc_descriptors{};
    for (size_t i = 0; i < rpc_count; ++i) {
        if (i && sorted_rpcs[i]->get_rpc_id() == sorted_rpcs[i - 1]->get_rpc_id()) { return result; }
        rpc_descriptors[i] = {sorted_rpcs[i]->get_rpc_id(), static_cast<superpos::RpcPermission>(sorted_rpcs[i]->get_permission()), sorted_rpcs[i]->get_maximum_payload()};
    }
    if (rpc_count) { manifest_size += 4 + 16 * rpc_count; }
    uint64_t previous = 0, payload_limit = 0;
    std::array<superpos::FieldDescriptor, superpos::Schema::maximum_fields> descriptors{};
    for (size_t i = 0; i < count; ++i) {
        const auto &field = sorted[i];
        if (field->get_field_id() == previous) { return result; }
        previous = field->get_field_id();
        auto &descriptor = descriptors[i];
        descriptor.id = field->get_field_id();
        descriptor.kind = static_cast<superpos::FieldKind>(field->get_codec_id() - 1);
        descriptor.offset = uint32_t(payload_limit);
        descriptor.size = field->get_max_bytes();
        descriptor.audience = static_cast<superpos::FieldAudience>(field->get_audience());
        descriptor.minimum = field->get_minimum();
        descriptor.maximum = field->get_maximum();
        descriptor.quantization_levels = field->get_quantization_levels();
        payload_limit += field->get_max_bytes();
        if (payload_limit > superpos::Schema::maximum_state_bytes) { return result; }
    }
    auto schema = superpos::Schema::create(schema_id, std::span(descriptors).first(count), std::span(rpc_descriptors).first(rpc_count));
    if (!schema) { return result; }
    // One bounded, checked engine allocation. Failed allocation publishes no
    // manifest/fingerprint and cannot advance a Session registry.
    PackedByteArray manifest;
    const int requested = superpos_egp::consume_allocation_failure(superpos_egp::AllocationPoint::SchemaManifest) ? -1 : int(manifest_size);
    Error resized = manifest.resize(requested);
    if (resized != OK) { result["error"] = resized; return result; }
    std::span<uint8_t> encoding{manifest.ptrw(), manifest_size};
    encoding[0] = 'S'; encoding[1] = 'P'; encoding[2] = 'G'; encoding[3] = 'S';
    size_t position = 4;
    if (!append(encoding, position, rpc_count ? 2 : 1, 4) || !append(encoding, position, schema_id, 8) ||
            !append(encoding, position, revision, 8) || !append(encoding, position, count, 4)) { return result; }
    for (size_t i = 0; i < count; ++i) {
        const auto &field = sorted[i];
        String name = field->get_field_name(); // Validated ASCII; no UTF8 buffer allocation.
        if (!append(encoding, position, field->get_field_id(), 8) ||
                !append(encoding, position, field->get_codec_id(), 4) ||
                !append(encoding, position, field->get_max_bytes(), 4) ||
                !append(encoding, position, field->get_audience(), 4) ||
                !append(encoding, position, std::bit_cast<uint64_t>(field->get_minimum()), 8) ||
                !append(encoding, position, std::bit_cast<uint64_t>(field->get_maximum()), 8) ||
                !append(encoding, position, field->get_quantization_levels(), 4) ||
                !append(encoding, position, name.length(), 4) ||
                position > encoding.size() || size_t(name.length()) > encoding.size() - position) { return result; }
        for (int c = 0; c < name.length(); ++c) { encoding[position++] = uint8_t(name[c]); }
    }
    if (rpc_count) {
        if (!append(encoding, position, rpc_count, 4)) { return result; }
        for (size_t i = 0; i < rpc_count; ++i) {
            if (!append(encoding, position, rpc_descriptors[i].id, 8) || !append(encoding, position, uint64_t(rpc_descriptors[i].permission), 4) ||
                    !append(encoding, position, rpc_descriptors[i].maximum_payload, 4)) { return result; }
        }
    }
    if (position != manifest_size) { return result; }
    unsigned char hash[32];
    Error error = CryptoCore::sha256(manifest.ptr(), manifest.size(), hash);
    if (error != OK) { result["error"] = error; return result; }
    result["error"] = OK;
    result["manifest"] = manifest;
    result["fingerprint"] = String::hex_encode_buffer(hash, 32);
    result["schema_id"] = superpos_egp::signed_bits(schema_id);
    result["revision"] = superpos_egp::signed_bits(revision);
    result["state_bytes"] = int64_t(schema->state_bytes());
    result["rpcs"] = int64_t(rpc_count);
    return result;
}
String SuperposSchema::get_fingerprint() const { return bake().get("fingerprint", String()); }

void SuperposField::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_field_id", "value"), &SuperposField::set_field_id);
    ClassDB::bind_method(D_METHOD("get_field_id"), &SuperposField::get_field_id);
    ClassDB::bind_method(D_METHOD("set_codec_id", "value"), &SuperposField::set_codec_id);
    ClassDB::bind_method(D_METHOD("get_codec_id"), &SuperposField::get_codec_id);
    ClassDB::bind_method(D_METHOD("set_max_bytes", "value"), &SuperposField::set_max_bytes);
    ClassDB::bind_method(D_METHOD("get_max_bytes"), &SuperposField::get_max_bytes);
    ClassDB::bind_method(D_METHOD("set_field_name", "value"), &SuperposField::set_field_name);
    ClassDB::bind_method(D_METHOD("get_field_name"), &SuperposField::get_field_name);
    ClassDB::bind_method(D_METHOD("set_audience", "value"), &SuperposField::set_audience);
    ClassDB::bind_method(D_METHOD("get_audience"), &SuperposField::get_audience);
    ClassDB::bind_method(D_METHOD("set_minimum", "value"), &SuperposField::set_minimum);
    ClassDB::bind_method(D_METHOD("get_minimum"), &SuperposField::get_minimum);
    ClassDB::bind_method(D_METHOD("set_maximum", "value"), &SuperposField::set_maximum);
    ClassDB::bind_method(D_METHOD("get_maximum"), &SuperposField::get_maximum);
    ClassDB::bind_method(D_METHOD("set_quantization_levels", "value"), &SuperposField::set_quantization_levels);
    ClassDB::bind_method(D_METHOD("get_quantization_levels"), &SuperposField::get_quantization_levels);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "field_id"), "set_field_id", "get_field_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "codec_id", PROPERTY_HINT_ENUM, "Boolean:1,U32:2,U64:3,I32:4,I64:5,F32:6,F64:7,Bytes:8,ObjectReference:9,QuantizedF32:10"), "set_codec_id", "get_codec_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_bytes"), "set_max_bytes", "get_max_bytes");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "field_name"), "set_field_name", "get_field_name");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "audience", PROPERTY_HINT_ENUM, "Everyone,Owner,Authority"), "set_audience", "get_audience");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "minimum"), "set_minimum", "get_minimum");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "maximum"), "set_maximum", "get_maximum");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quantization_levels"), "set_quantization_levels", "get_quantization_levels");
}
void SuperposRpc::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_rpc_id", "value"), &SuperposRpc::set_rpc_id);
    ClassDB::bind_method(D_METHOD("get_rpc_id"), &SuperposRpc::get_rpc_id);
    ClassDB::bind_method(D_METHOD("set_permission", "value"), &SuperposRpc::set_permission);
    ClassDB::bind_method(D_METHOD("get_permission"), &SuperposRpc::get_permission);
    ClassDB::bind_method(D_METHOD("set_maximum_payload", "value"), &SuperposRpc::set_maximum_payload);
    ClassDB::bind_method(D_METHOD("get_maximum_payload"), &SuperposRpc::get_maximum_payload);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "rpc_id"), "set_rpc_id", "get_rpc_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "permission", PROPERTY_HINT_ENUM, "Authority,Owner,AdmittedPeer"), "set_permission", "get_permission");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "maximum_payload", PROPERTY_HINT_RANGE, "1,4096,1"), "set_maximum_payload", "get_maximum_payload");
    BIND_ENUM_CONSTANT(PERMISSION_AUTHORITY);
    BIND_ENUM_CONSTANT(PERMISSION_OWNER);
    BIND_ENUM_CONSTANT(PERMISSION_ADMITTED_PEER);
}
void SuperposSchema::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_schema_id", "value"), &SuperposSchema::set_schema_id);
    ClassDB::bind_method(D_METHOD("get_schema_id"), &SuperposSchema::get_schema_id);
    ClassDB::bind_method(D_METHOD("set_revision", "value"), &SuperposSchema::set_revision);
    ClassDB::bind_method(D_METHOD("get_revision"), &SuperposSchema::get_revision);
    ClassDB::bind_method(D_METHOD("set_fields", "fields"), &SuperposSchema::set_fields);
    ClassDB::bind_method(D_METHOD("get_fields"), &SuperposSchema::get_fields);
    ClassDB::bind_method(D_METHOD("set_rpcs", "rpcs"), &SuperposSchema::set_rpcs);
    ClassDB::bind_method(D_METHOD("get_rpcs"), &SuperposSchema::get_rpcs);
    ClassDB::bind_method(D_METHOD("bake"), &SuperposSchema::bake);
    ClassDB::bind_method(D_METHOD("get_fingerprint"), &SuperposSchema::get_fingerprint);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "schema_id"), "set_schema_id", "get_schema_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "revision"), "set_revision", "get_revision");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "fields", PROPERTY_HINT_ARRAY_TYPE, "SuperposField"), "set_fields", "get_fields");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "rpcs", PROPERTY_HINT_ARRAY_TYPE, "SuperposRpc"), "set_rpcs", "get_rpcs");
}
