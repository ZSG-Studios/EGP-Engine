// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/codec.hpp>
#include <superpos/capability.hpp>
#include <cstdint>
#include <span>
namespace superpos::canonical {
// IDs belong to a canonical simulation namespace, never native pool indices,
// pointers, RID values, SCTP IDs or local engine handles. Generation is explicit.
struct Identity {
    std::uint32_t kind{};
    std::uint64_t simulation{}, generation{};
    bool operator==(const Identity &) const noexcept = default;
};
enum class AtomType : std::uint8_t { Unsigned, Signed, Boolean, Float32, Float64, Reference };
struct Atom {
    // Explicit unsigned integer or IEEE bits; signed values use bit_cast.
    // Reference atoms use only identity. Unused members must be zero.
    std::uint64_t bits{};
    Identity identity{};
};
struct FieldSpec {
    std::uint32_t id{};
    AtomType type{};
    std::uint32_t reference_kind{};
    std::uint32_t minimum_atoms{}, maximum_atoms{};
    bool nullable_reference{};
};
struct RecordSpec {
    std::uint32_t kind{};
    std::uint32_t minimum_records{}, maximum_records{};
    std::span<const FieldSpec> fields;
};
struct Limits {
    std::uint32_t records = 100000, fields = 1000000, atoms = 2000000;
    std::size_t encoded_bytes = 48u << 20, decoded_bytes = 64u << 20;
};
struct Schema {
    Fingerprint fingerprint{};
    // All kinds and fields strictly ascending, exact and frozen. Zero minimum
    // permits empty collections; a missing required kind fails admission.
    std::span<const RecordSpec> records;
    Limits limits;
};
struct Header {
    Epoch epoch{};
    Tick tick{};
    Fingerprint simulation{}, geometry{};
};
struct Field { std::uint32_t id{}; std::span<const Atom> atoms; };
struct Record { Identity identity; std::span<const Field> fields; };
struct Checkpoint { Header header; std::span<const Record> records; };
struct DecodeStorage {
    // Caller-reserved private staging; never published until decode succeeds.
    // On rejection these spans may contain partial staging. All backing charges
    // and capacity belong to the caller's validated recovery MemoryPlan.
    std::span<Record> records;
    std::span<Field> fields;
    std::span<Atom> atoms;
};
Result<std::size_t> measure(const Schema &, const Checkpoint &) noexcept;
Result<std::size_t> encode(const Schema &, const Checkpoint &, std::span<std::byte>) noexcept;
Result<Checkpoint> decode(const Schema &, std::span<const std::byte>, DecodeStorage) noexcept;
// This codec supplies a portable field representation, not physics admission.
// A complete native bridge, registered gameplay participants and qualification
// must prove semantic coverage before PortableRestart/ExactResume is advertised.
}
