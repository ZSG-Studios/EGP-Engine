// SPDX-License-Identifier: MIT
// Private implementation included after Impl. Public engine headers stay C++17.
namespace {
superpos::Status write_field_uint(superpos::Writer &writer, uint64_t value, size_t width) noexcept {
    std::array<std::byte, 8> bytes{};
    for (size_t i = 0; i < width; ++i) bytes[i] = std::byte(value >> (8 * i));
    return writer.raw(std::span(bytes).first(width));
}
superpos::Status encode_engine_field(const superpos::Schema &schema,
        const superpos::FieldDescriptor &field, std::span<std::byte> state,
        const Variant &value) noexcept {
    using superpos::Field;
    using superpos::FieldKind;
    const auto type = value.get_type();
    switch (field.kind) {
        case FieldKind::Boolean:
            if (type == Variant::BOOL) return schema.write(Field<bool>{field.id}, state, bool(value));
            break;
        case FieldKind::U32:
            if (type == Variant::INT) {
                const int64_t number = value;
                if (number >= 0 && uint64_t(number) <= UINT32_MAX)
                    return schema.write(Field<uint32_t>{field.id}, state, uint32_t(number));
            }
            break;
        case FieldKind::U64:
            if (type == Variant::INT)
                return schema.write(Field<uint64_t>{field.id}, state, uint64_t(int64_t(value)));
            break;
        case FieldKind::I32:
            if (type == Variant::INT) {
                const int64_t number = value;
                if (number >= INT32_MIN && number <= INT32_MAX)
                    return schema.write(Field<int32_t>{field.id}, state, int32_t(number));
            }
            break;
        case FieldKind::I64:
            if (type == Variant::INT)
                return schema.write(Field<int64_t>{field.id}, state, int64_t(value));
            break;
        case FieldKind::F32:
            if (type == Variant::FLOAT)
                return schema.write(Field<float>{field.id}, state, float(double(value)));
            break;
        case FieldKind::F64:
            if (type == Variant::FLOAT)
                return schema.write(Field<double>{field.id}, state, double(value));
            break;
        case FieldKind::Bytes:
            if (type == Variant::PACKED_BYTE_ARRAY) {
                const PackedByteArray bytes = value;
                return schema.write_bytes(field.id, state, byte_view(bytes));
            }
            break;
        case FieldKind::ObjectReference:
            if (type == Variant::INT)
                return schema.write(Field<superpos::ObjectHandle>{field.id}, state,
                    superpos::ObjectHandle{uint64_t(int64_t(value))});
            break;
        case FieldKind::QuantizedF32:
            if (type == Variant::FLOAT)
                return schema.write(Field<superpos::QuantizedFloat>{field.id}, state,
                    superpos::QuantizedFloat{float(double(value))});
            break;
    }
    return superpos::fail(superpos::Error::InvalidArgument);
}

Error decode_engine_field(const superpos::Schema &schema,
        const superpos::FieldDescriptor &field, std::span<const std::byte> state,
        Variant &output) {
    using superpos::Field;
    using superpos::FieldKind;
    // The complete frozen object was validated on publication. Still use checked
    // field reads; no native structure layout or unaligned loads enter Variant.
#define SUPERPOS_READ_FIELD(kind, native_type, expression) \
    case FieldKind::kind: { \
        const auto value = schema.read(Field<native_type>{field.id}, state); \
        if (!value) return translate(value.error()); \
        output = (expression); return OK; \
    }
    switch (field.kind) {
        SUPERPOS_READ_FIELD(Boolean, bool, *value)
        SUPERPOS_READ_FIELD(U32, uint32_t, int64_t(*value))
        SUPERPOS_READ_FIELD(U64, uint64_t, superpos_egp::signed_bits(*value))
        SUPERPOS_READ_FIELD(I32, int32_t, int64_t(*value))
        SUPERPOS_READ_FIELD(I64, int64_t, *value)
        SUPERPOS_READ_FIELD(F32, float, double(*value))
        SUPERPOS_READ_FIELD(F64, double, *value)
        SUPERPOS_READ_FIELD(ObjectReference, superpos::ObjectHandle,
            superpos_egp::signed_bits(value->value))
        case FieldKind::QuantizedF32: {
            const auto value = schema.read(Field<superpos::QuantizedFloat>{field.id}, state);
            if (!value) return translate(value.error());
            output = double(value->value); return OK;
        }
        case FieldKind::Bytes: {
            const auto value = schema.read_bytes(field.id, state);
            if (!value) return translate(value.error());
            PackedByteArray bytes;
            const auto error = bytes.resize(int(value->size()));
            if (error != OK) return error;
            if (!bytes.is_empty()) memcpy(bytes.ptrw(), value->data(), value->size());
            output = bytes; return OK;
        }
    }
#undef SUPERPOS_READ_FIELD
    return ERR_INVALID_DATA;
}
}

Dictionary SuperposSession::read_fields(uint64_t p_handle, const PackedInt64Array &p_fields) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired || !impl || !impl->world) return result;
    if (managed_reload_paused || closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (p_fields.is_empty() || p_fields.size() > int(superpos::Schema::maximum_fields)) {
        result["error"] = ERR_INVALID_PARAMETER; return result;
    }
    const auto object = impl->world->world().view({p_handle});
    if (!object) { result["error"] = translate(object.error()); return result; }
    Dictionary values;
    for (int i = 0; i < p_fields.size(); ++i) {
        const uint64_t id = uint64_t(p_fields[i]);
        const auto *field = object->schema->field(id);
        if (!field || values.has(p_fields[i])) { result["error"] = ERR_INVALID_PARAMETER; return result; }
        Variant value;
        const auto error = decode_engine_field(*object->schema, *field, object->canonical, value);
        if (error != OK) { result["error"] = error; return result; }
        values[p_fields[i]] = value;
    }
    result["error"] = OK;
    result["values"] = values;
    result["revision"] = superpos_egp::signed_bits(object->revision);
    result["binding_generation"] = superpos_egp::signed_bits(impl->binding_generation);
    return result;
}

Error SuperposSession::publish_fields(uint64_t p_handle, uint64_t p_expected_revision,
        const Dictionary &p_values) {
    if (Thread::get_caller_id() != owner_thread) return ERR_BUSY;
    if (owner_retired || !impl || !impl->world) return ERR_UNCONFIGURED;
    if (managed_reload_paused || closing || simulation_in_flight) return ERR_BUSY;
    if (p_values.is_empty() || p_values.size() > int(superpos::Schema::maximum_fields))
        return ERR_INVALID_PARAMETER;
    PackedByteArray envelope;
    {
        const auto object = impl->world->world().view({p_handle});
        if (!object) return translate(object.error());
        if (object->revision != p_expected_revision) return ERR_INVALID_DATA;
        // Bounded fallible staging is charged to the native Session quota.
        superpos::Buffer staging{impl->allocator, superpos::MemoryDomain::Session};
        const auto reserved = staging.resize(object->canonical.size());
        if (!reserved) return translate(reserved.error());
        memcpy(staging.bytes().data(), object->canonical.data(), staging.size());
        for (int i = 0; i < p_values.size(); ++i) {
            const Variant key = p_values.get_key_at_index(i);
            if (key.get_type() != Variant::INT) return ERR_INVALID_PARAMETER;
            const auto *field = object->schema->field(uint64_t(int64_t(key)));
            if (!field) return ERR_INVALID_PARAMETER;
            const auto encoded = encode_engine_field(*object->schema, *field,
                staging.bytes(), p_values.get_value_at_index(i));
            if (!encoded) return translate(encoded.error());
        }
        const auto error = envelope.resize(int(32 + staging.size()));
        if (error != OK) return error;
        superpos::Writer writer({reinterpret_cast<std::byte *>(envelope.ptrw()), size_t(envelope.size())});
        static constexpr std::byte magic[]{std::byte{'S'}, std::byte{'P'}, std::byte{'G'}, std::byte{'O'}};
        if (!writer.raw(magic) || !write_field_uint(writer, 1, 4) || !write_field_uint(writer, 1, 4) ||
                !write_field_uint(writer, p_handle, 8) || !write_field_uint(writer, p_expected_revision, 8) ||
                !write_field_uint(writer, staging.size(), 4) || !writer.raw(staging.bytes())) return ERR_INVALID_DATA;
    }
    // Release every borrowed allocator/view before canonical_published can run
    // arbitrary callbacks, close this world, or dispose a managed wrapper.
    return publish_packed(envelope);
}

Dictionary SuperposSession::read_field_changes(uint64_t p_handle, const Dictionary &p_values) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired || !impl || !impl->world) return result;
    if (managed_reload_paused || closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (p_values.is_empty() || p_values.size() > int(superpos::Schema::maximum_fields)) {
        result["error"] = ERR_INVALID_PARAMETER; return result;
    }
    const auto object = impl->world->world().view({p_handle});
    if (!object) { result["error"] = translate(object.error()); return result; }
    // The same encoder as publication, into charged staging; nothing is
    // published and no callback runs. Comparing encoded bytes means a
    // quantized field changes only when its quantized level changes.
    superpos::Buffer staging{impl->allocator, superpos::MemoryDomain::Session};
    const auto reserved = staging.resize(object->canonical.size());
    if (!reserved) { result["error"] = translate(reserved.error()); return result; }
    memcpy(staging.bytes().data(), object->canonical.data(), staging.size());
    PackedInt64Array changed;
    for (int i = 0; i < p_values.size(); ++i) {
        const Variant key = p_values.get_key_at_index(i);
        if (key.get_type() != Variant::INT) { result["error"] = ERR_INVALID_PARAMETER; return result; }
        const auto *field = object->schema->field(uint64_t(int64_t(key)));
        if (!field) { result["error"] = ERR_INVALID_PARAMETER; return result; }
        const auto encoded = encode_engine_field(*object->schema, *field, staging.bytes(), p_values.get_value_at_index(i));
        if (!encoded) { result["error"] = translate(encoded.error()); return result; }
        if (memcmp(staging.bytes().data() + field->offset, object->canonical.data() + field->offset, field->size) != 0) {
            changed.push_back(int64_t(key));
        }
    }
    result["error"] = OK;
    result["changed"] = changed;
    result["revision"] = superpos_egp::signed_bits(object->revision);
    return result;
}
