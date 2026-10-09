#include "superpos/schema.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace superpos {
namespace {
std::uint64_t load(std::span<const std::byte> bytes) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0; i < bytes.size(); ++i) value |= std::uint64_t(std::to_integer<unsigned char>(bytes[i])) << (8 * i);
    return value;
}
void store(std::span<std::byte> bytes, std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte((value >> (8 * i)) & 255);
}
bool overlaps(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    if (a.empty() || b.empty()) return false;
    auto x = reinterpret_cast<std::uintptr_t>(a.data());
    auto y = reinterpret_cast<std::uintptr_t>(b.data());
    return x <= y ? y - x < a.size() : x - y < b.size();
}
std::size_t scalar_size(FieldKind kind) noexcept {
    switch (kind) {
    case FieldKind::Boolean: return 1;
    case FieldKind::U32: case FieldKind::I32: case FieldKind::F32: case FieldKind::QuantizedF32: return 4;
    case FieldKind::U64: case FieldKind::I64: case FieldKind::F64: case FieldKind::ObjectReference: return 8;
    case FieldKind::Bytes: return 0;
    }
    return 0;
}
}

Result<Schema> Schema::create(SchemaId id, std::span<const FieldDescriptor> fields,
    std::span<const RpcDescriptor> rpcs) noexcept {
    if (!id || fields.empty() || fields.size() > maximum_fields || rpcs.size() > maximum_rpcs)
        return fail(Error::InvalidArgument);
    std::size_t offset{};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        if (!f.id || f.offset != offset || f.size > maximum_state_bytes - offset ||
            static_cast<unsigned>(f.kind) > static_cast<unsigned>(FieldKind::QuantizedF32) ||
            static_cast<unsigned>(f.audience) > static_cast<unsigned>(FieldAudience::Authority))
            return fail(Error::InvalidArgument);
        if (f.kind == FieldKind::Bytes ? f.size < 4 : f.size != scalar_size(f.kind)) return fail(Error::InvalidArgument);
        if (f.kind == FieldKind::QuantizedF32) {
            if (!std::isfinite(f.minimum) || !std::isfinite(f.maximum) || f.minimum >= f.maximum ||
                f.minimum < -std::numeric_limits<float>::max() || f.maximum > std::numeric_limits<float>::max() ||
                !std::isfinite(f.maximum - f.minimum) || !f.quantization_levels || f.quantization_levels > 0xffffff)
                return fail(Error::InvalidArgument);
        } else if (f.minimum != 0 || f.maximum != 0 || f.quantization_levels != 0) return fail(Error::InvalidArgument);
        for (std::size_t j = 0; j < i; ++j) if (fields[j].id == f.id) return fail(Error::InvalidArgument);
        offset += f.size;
    }
    for (std::size_t i = 0; i < rpcs.size(); ++i) {
        const auto& r = rpcs[i];
        if (!r.id || r.maximum_payload > 4096 || static_cast<unsigned>(r.permission) > static_cast<unsigned>(RpcPermission::AdmittedPeer))
            return fail(Error::InvalidArgument);
        for (std::size_t j = 0; j < i; ++j) if (rpcs[j].id == r.id) return fail(Error::InvalidArgument);
    }
    Schema schema;
    schema.id_ = id; schema.fields_ = fields; schema.rpcs_ = rpcs; schema.bytes_ = offset;
    return schema;
}
const FieldDescriptor* Schema::field(FieldId id) const noexcept {
    for (const auto& f : fields_) if (f.id == id) return &f;
    return nullptr;
}
const RpcDescriptor* Schema::rpc(RpcId id) const noexcept {
    for (const auto& r : rpcs_) if (r.id == id) return &r;
    return nullptr;
}
bool Schema::compatible_with(const Schema& other) const noexcept {
    if (id_ != other.id_ || bytes_ != other.bytes_ || fields_.size() != other.fields_.size() || rpcs_.size() != other.rpcs_.size()) return false;
    for (std::size_t i = 0; i < fields_.size(); ++i) if (!(fields_[i] == other.fields_[i])) return false;
    for (std::size_t i = 0; i < rpcs_.size(); ++i) if (!(rpcs_[i] == other.rpcs_[i])) return false;
    return true;
}
Status Schema::validate_field(const FieldDescriptor& f, std::span<const std::byte> bytes) const noexcept {
    if (static_cast<unsigned>(f.kind) > static_cast<unsigned>(FieldKind::QuantizedF32) ||
        f.size > maximum_state_bytes || (f.kind == FieldKind::Bytes ? f.size < 4 : f.size != scalar_size(f.kind)))
        return fail(Error::IncompatibleSchema);
    if (bytes.size() != f.size) return fail(Error::Truncated);
    switch (f.kind) {
    case FieldKind::Boolean:
        if (std::to_integer<unsigned>(bytes[0]) > 1) return fail(Error::NonCanonical);
        break;
    case FieldKind::F32: {
        auto bits = static_cast<std::uint32_t>(load(bytes)); auto value = std::bit_cast<float>(bits);
        if (!std::isfinite(value) || (value == 0 && bits != 0)) return fail(Error::NonCanonical);
        break;
    }
    case FieldKind::F64: {
        auto bits = load(bytes); auto value = std::bit_cast<double>(bits);
        if (!std::isfinite(value) || (value == 0 && bits != 0)) return fail(Error::NonCanonical);
        break;
    }
    case FieldKind::QuantizedF32:
        if (load(bytes) > f.quantization_levels) return fail(Error::NonCanonical);
        break;
    case FieldKind::ObjectReference:
        if (auto value = load(bytes); value && !ObjectHandle{value}) return fail(Error::NonCanonical);
        break;
    case FieldKind::Bytes: {
        auto size = static_cast<std::size_t>(load(bytes.first(4)));
        if (size > bytes.size() - 4) return fail(Error::NonCanonical);
        for (auto b : bytes.subspan(4 + size)) if (b != std::byte{}) return fail(Error::NonCanonical);
        break;
    }
    case FieldKind::U32: case FieldKind::U64: case FieldKind::I32: case FieldKind::I64: break;
    }
    return {};
}
Status Schema::validate(std::span<const std::byte> state) const noexcept {
    if (!id_ || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    for (const auto& f : fields_) if (auto s = validate_field(f, state.subspan(f.offset, f.size)); !s) return s;
    return {};
}
Result<std::uint64_t> Schema::read_scalar(FieldId id, FieldKind kind, std::span<const std::byte> state) const noexcept {
    auto f = field(id);
    if (!f || f->kind != kind || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    auto bytes = state.subspan(f->offset, f->size);
    if (auto s = validate_field(*f, bytes); !s) return fail(s.error());
    return load(bytes);
}
Status Schema::write_scalar(FieldId id, FieldKind kind, std::span<std::byte> state, std::uint64_t bits) const noexcept {
    auto f = field(id);
    if (!f || f->kind != kind || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    if (kind == FieldKind::F32 && std::bit_cast<float>(static_cast<std::uint32_t>(bits)) == 0) bits = 0;
    if (kind == FieldKind::F64 && std::bit_cast<double>(bits) == 0) bits = 0;
    std::array<std::byte, 8> temporary{};
    store(std::span(temporary).first(f->size), bits);
    if (auto s = validate_field(*f, std::span(temporary).first(f->size)); !s) return s;
    std::memcpy(state.data() + f->offset, temporary.data(), f->size);
    return {};
}
Status Schema::write(Field<QuantizedFloat> field_id, std::span<std::byte> state, QuantizedFloat value) const noexcept {
    auto f = field(field_id.id);
    if (!f || f->kind != FieldKind::QuantizedF32 || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    if (!std::isfinite(value.value) || value.value < f->minimum || value.value > f->maximum) return fail(Error::InvalidArgument);
    auto scaled = (double(value.value) - f->minimum) / (f->maximum - f->minimum) * f->quantization_levels;
    // Explicit nearest, ties upward; independent of the process rounding mode.
    auto quantized = static_cast<std::uint32_t>(std::floor(scaled + 0.5));
    store(state.subspan(f->offset, 4), quantized);
    return {};
}
Result<QuantizedFloat> Schema::read(Field<QuantizedFloat> field_id, std::span<const std::byte> state) const noexcept {
    auto f = field(field_id.id);
    if (!f || f->kind != FieldKind::QuantizedF32 || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    auto bytes = state.subspan(f->offset, f->size);
    if (auto s = validate_field(*f, bytes); !s) return fail(s.error());
    auto value = f->minimum + (f->maximum - f->minimum) * double(load(bytes)) / f->quantization_levels;
    return QuantizedFloat{static_cast<float>(value)};
}
Status Schema::write_bytes(FieldId id, std::span<std::byte> state, std::span<const std::byte> value) const noexcept {
    auto f = field(id);
    if (!f || f->kind != FieldKind::Bytes || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    if (value.size() > f->size - 4 || overlaps(state, value)) return fail(Error::InvalidArgument);
    auto target = state.subspan(f->offset, f->size);
    store(target.first(4), value.size());
    if (!value.empty()) std::memcpy(target.data() + 4, value.data(), value.size());
    std::fill(target.begin() + static_cast<std::ptrdiff_t>(4 + value.size()), target.end(), std::byte{});
    return {};
}
Result<std::span<const std::byte>> Schema::read_bytes(FieldId id, std::span<const std::byte> state) const noexcept {
    auto f = field(id);
    if (!f || f->kind != FieldKind::Bytes || state.size() != bytes_) return fail(Error::IncompatibleSchema);
    auto bytes = state.subspan(f->offset, f->size);
    if (auto s = validate_field(*f, bytes); !s) return fail(s.error());
    return bytes.subspan(4, static_cast<std::size_t>(load(bytes.first(4))));
}
Status Schema::project(std::span<const std::byte> source, bool owner, bool authority, std::span<std::byte> output) const noexcept {
    if (auto s = validate(source); !s) return s;
    if (output.size() != bytes_) return fail(Error::InvalidArgument);
    std::memmove(output.data(), source.data(), bytes_);
    for (const auto& f : fields_) {
        if ((f.audience == FieldAudience::Owner && !owner && !authority) || (f.audience == FieldAudience::Authority && !authority))
            std::fill(output.begin() + f.offset, output.begin() + f.offset + f.size, std::byte{});
    }
    return {};
}

Result<std::size_t> encode_delta(const Schema& schema, std::span<const std::byte> baseline,
    std::span<const std::byte> replacement, std::span<std::byte> output) noexcept {
    if (auto s = schema.validate(baseline); !s) return fail(s.error());
    if (auto s = schema.validate(replacement); !s) return fail(s.error());
    std::uint64_t mask{}; std::size_t size = 8; std::size_t index{};
    for (const auto& f : schema.fields()) {
        if (std::memcmp(baseline.data() + f.offset, replacement.data() + f.offset, f.size) != 0) { mask |= std::uint64_t{1} << index; size += f.size; }
        ++index;
    }
    if (size > output.size()) return fail(Error::CapacityExceeded);
    if (overlaps(output, baseline) || overlaps(output, replacement)) return fail(Error::InvalidArgument);
    store(output.first(8), mask); std::size_t offset = 8; index = 0;
    for (const auto& f : schema.fields()) {
        if (mask & (std::uint64_t{1} << index)) { std::memcpy(output.data() + offset, replacement.data() + f.offset, f.size); offset += f.size; }
        ++index;
    }
    return size;
}
Status validate_delta(const Schema& schema, std::span<const std::byte> baseline, std::span<const std::byte> delta) noexcept {
    if (auto s = schema.validate(baseline); !s) return s;
    if (delta.size() < 8) return fail(Error::Truncated);
    auto mask = load(delta.first(8));
    if (schema.fields().size() < 64 && (mask >> schema.fields().size()) != 0) return fail(Error::NonCanonical);
    std::size_t offset = 8; std::size_t index{};
    for (const auto& f : schema.fields()) {
        if (mask & (std::uint64_t{1} << index)) {
            if (f.size > delta.size() - offset) return fail(Error::Truncated);
            if (auto s = schema.validate_field(f, delta.subspan(offset, f.size)); !s) return s;
            offset += f.size;
        }
        ++index;
    }
    if (offset != delta.size()) return fail(Error::NonCanonical);
    return {};
}
Status apply_delta(const Schema& schema, std::span<const std::byte> baseline,
    std::span<const std::byte> delta, std::span<std::byte> output) noexcept {
    if (output.size() != schema.state_bytes() || overlaps(output, delta)) return fail(Error::InvalidArgument);
    if (auto s = validate_delta(schema, baseline, delta); !s) return s;
    std::memmove(output.data(), baseline.data(), baseline.size());
    auto mask = load(delta.first(8)); std::size_t offset = 8; std::size_t index{};
    for (const auto& f : schema.fields()) {
        if (mask & (std::uint64_t{1} << index)) { std::memcpy(output.data() + f.offset, delta.data() + offset, f.size); offset += f.size; }
        ++index;
    }
    return {};
}
}
