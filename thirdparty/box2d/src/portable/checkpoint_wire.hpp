// SPDX-License-Identifier: MIT
#pragma once
// Fixture-only byte form of a complete WorldCheckpoint for cross-process
// restore. Layout (little-endian, no padding):
//   header  magic "SPB2WCK1", format version, position bits, total length,
//           embedded source complete-state digest, record count
//   body    every record group in a fixed order; each record is its
//           identity, field count, and per field its id, atom count and atoms
//           (bits plus reference identity); then symbolic bindings (ascending
//           symbols only, never addresses), retired lifetimes and slot layout
//   trailer SHA-256 over every preceding byte
// Decoding is bounded: every count is checked against the remaining bytes and
// fixed limits before storage grows. It rejects a wrong magic, version or
// position width, length mismatch, trailer mismatch, truncation, oversize
// counts, records out of canonical order, unknown symbols and trailing bytes,
// and finally requires that re-encoding reproduces the input exactly.
// Semantic admission is separate: the caller restores the checkpoint and
// requires the candidate digest to equal the embedded source digest.
#include "checkpoint_pipeline.hpp"
#include <cstddef>
#include <vector>
namespace superpos::box2d_portable::fixture {
inline constexpr uint32_t checkpoint_wire_version=1;
inline constexpr size_t checkpoint_wire_max_bytes=size_t(64)<<20;
// Position width compiled into this binary (32 or 64). Restores across
// widths are unsupported and rejected.
uint32_t checkpoint_position_bits()noexcept;
// Requires symbolic bindings ({binding_kind, symbol, 1}); returns empty on failure.
std::vector<std::byte> encode_checkpoint(const WorldCheckpoint&);
// Fills `out` (which must be empty) and resolves every symbol through `symbols`.
Status decode_checkpoint(std::span<const std::byte>,const SymbolRegistry&symbols,WorldCheckpoint&out);
// Helpers for negative tests: rewrite the trailer after editing the bytes.
void reseal_checkpoint(std::vector<std::byte>&);
}
