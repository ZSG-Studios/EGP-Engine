#pragma once

#include "types.hpp"
#include <bit>
#include <concepts>
#include <span>
#include <type_traits>

namespace superpos {

using SchemaId = std::uint64_t;
using FieldId = std::uint64_t;
using RpcId = std::uint64_t;

enum class FieldKind : std::uint8_t { Boolean, U32, U64, I32, I64, F32, F64, Bytes, ObjectReference, QuantizedF32 };
enum class FieldAudience : std::uint8_t { Everyone, Owner, Authority };
enum class RpcPermission : std::uint8_t { Authority, Owner, AdmittedPeer };

struct FieldDescriptor {
    FieldId id{};
    FieldKind kind{};
    std::uint32_t offset{};
    // Bytes fields contain a little-endian length followed by zero-padded storage.
    std::uint32_t size{};
    FieldAudience audience{FieldAudience::Everyone};
    double minimum{};
    double maximum{};
    std::uint32_t quantization_levels{};
    bool operator==(const FieldDescriptor&) const noexcept = default;
};

struct RpcDescriptor {
    RpcId id{};
    RpcPermission permission{RpcPermission::Owner};
    std::uint32_t maximum_payload{4096};
    bool operator==(const RpcDescriptor&) const noexcept = default;
};

template<class T> struct Field { FieldId id{}; };
struct QuantizedFloat { float value{}; };

template<class T>
concept SchemaScalar = std::same_as<T, bool> || std::same_as<T, std::uint32_t> ||
    std::same_as<T, std::uint64_t> || std::same_as<T, std::int32_t> ||
    std::same_as<T, std::int64_t> || std::same_as<T, float> ||
    std::same_as<T, double> || std::same_as<T, ObjectHandle>;

template<SchemaScalar T> constexpr FieldKind field_kind() noexcept {
    if constexpr (std::same_as<T, bool>) return FieldKind::Boolean;
    else if constexpr (std::same_as<T, std::uint32_t>) return FieldKind::U32;
    else if constexpr (std::same_as<T, std::uint64_t>) return FieldKind::U64;
    else if constexpr (std::same_as<T, std::int32_t>) return FieldKind::I32;
    else if constexpr (std::same_as<T, std::int64_t>) return FieldKind::I64;
    else if constexpr (std::same_as<T, float>) return FieldKind::F32;
    else if constexpr (std::same_as<T, double>) return FieldKind::F64;
    else return FieldKind::ObjectReference;
}

// Immutable, allocation-free view. Descriptor storage must outlive this view and
// every world/replica which registers it. No native structure layout goes on wire.
class Schema {
    SchemaId id_{};
    std::span<const FieldDescriptor> fields_;
    std::span<const RpcDescriptor> rpcs_;
    std::size_t bytes_{};
    Result<std::uint64_t> read_scalar(FieldId, FieldKind, std::span<const std::byte>) const noexcept;
    Status write_scalar(FieldId, FieldKind, std::span<std::byte>, std::uint64_t) const noexcept;
public:
    static constexpr std::size_t maximum_fields = 64;
    static constexpr std::size_t maximum_rpcs = 32;
    static constexpr std::size_t maximum_state_bytes = 16 * 1024;
    static Result<Schema> create(SchemaId, std::span<const FieldDescriptor>,
        std::span<const RpcDescriptor> = {}) noexcept;
    SchemaId id() const noexcept { return id_; }
    std::size_t state_bytes() const noexcept { return bytes_; }
    std::span<const FieldDescriptor> fields() const noexcept { return fields_; }
    std::span<const RpcDescriptor> rpcs() const noexcept { return rpcs_; }
    const FieldDescriptor* field(FieldId) const noexcept;
    const RpcDescriptor* rpc(RpcId) const noexcept;
    bool compatible_with(const Schema&) const noexcept;
    Status validate(std::span<const std::byte>) const noexcept;
    Status validate_field(const FieldDescriptor&, std::span<const std::byte>) const noexcept;
    Status project(std::span<const std::byte>, bool owner, bool authority,
        std::span<std::byte>) const noexcept;

    template<SchemaScalar T>
    Status write(Field<T> f, std::span<std::byte> state, T value) const noexcept {
        std::uint64_t bits{};
        if constexpr (std::same_as<T, ObjectHandle>) bits = value.value;
        else if constexpr (std::same_as<T, float>) bits = std::bit_cast<std::uint32_t>(value);
        else if constexpr (std::same_as<T, double> || std::same_as<T, std::int64_t>) bits = std::bit_cast<std::uint64_t>(value);
        else if constexpr (std::same_as<T, std::int32_t>) bits = std::bit_cast<std::uint32_t>(value);
        else bits = static_cast<std::uint64_t>(value);
        return write_scalar(f.id, field_kind<T>(), state, bits);
    }
    template<SchemaScalar T>
    Result<T> read(Field<T> f, std::span<const std::byte> state) const noexcept {
        auto result = read_scalar(f.id, field_kind<T>(), state);
        if (!result) return fail(result.error());
        if constexpr (std::same_as<T, ObjectHandle>) return ObjectHandle{*result};
        else if constexpr (std::same_as<T, float>) return std::bit_cast<float>(static_cast<std::uint32_t>(*result));
        else if constexpr (std::same_as<T, double> || std::same_as<T, std::int64_t>) return std::bit_cast<T>(*result);
        else if constexpr (std::same_as<T, std::int32_t>) return std::bit_cast<T>(static_cast<std::uint32_t>(*result));
        else return static_cast<T>(*result);
    }
    Status write(Field<QuantizedFloat>, std::span<std::byte>, QuantizedFloat) const noexcept;
    Result<QuantizedFloat> read(Field<QuantizedFloat>, std::span<const std::byte>) const noexcept;
    Status write_bytes(FieldId, std::span<std::byte>, std::span<const std::byte>) const noexcept;
    Result<std::span<const std::byte>> read_bytes(FieldId, std::span<const std::byte>) const noexcept;
};

// These fixed-layout delta primitives are a bounded foundation, not the final
// session wire protocol. A delta reconstructs an entire replacement against its
// named immutable baseline. Validation precedes all destination writes.
Result<std::size_t> encode_delta(const Schema&, std::span<const std::byte> baseline,
    std::span<const std::byte> replacement, std::span<std::byte> output) noexcept;
Status validate_delta(const Schema&, std::span<const std::byte> baseline,
    std::span<const std::byte> delta) noexcept;
Status apply_delta(const Schema&, std::span<const std::byte> baseline,
    std::span<const std::byte> delta, std::span<std::byte> output) noexcept;

}
