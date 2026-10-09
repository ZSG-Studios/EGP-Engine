// SPDX-License-Identifier: MIT
#include "superpos/canonical_checkpoint.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace superpos::canonical {
namespace {
constexpr std::uint64_t magic = 0x535043414e4f4e31ULL;
bool less(Identity a, Identity b) noexcept {
    return a.kind < b.kind || (a.kind == b.kind && a.simulation < b.simulation);
}
bool valid(Identity id) noexcept { return id.kind && id.simulation && id.generation; }
bool null(Identity id) noexcept { return !id.kind && !id.simulation && !id.generation; }
const RecordSpec *spec(const Schema &s, std::uint32_t kind) noexcept {
    auto it = std::lower_bound(s.records.begin(), s.records.end(), kind, [](const auto &r, auto k) { return r.kind < k; });
    return it != s.records.end() && it->kind == kind ? &*it : nullptr;
}
Status validate_schema(const Schema &s) noexcept {
    if (!known_fingerprint(s.fingerprint) || s.records.empty() || s.records.size() > 256 ||
        !s.limits.records || !s.limits.fields || !s.limits.atoms || !s.limits.encoded_bytes || !s.limits.decoded_bytes)
        return fail(Error::InvalidArgument);
    std::uint32_t prior = 0;
    for (const auto &r : s.records) {
        if (!r.kind || r.kind <= prior || r.minimum_records > r.maximum_records || r.maximum_records > s.limits.records ||
            r.fields.size() > 512) return fail(Error::InvalidArgument);
        prior = r.kind; std::uint32_t field_prior = 0;
        for (const auto &f : r.fields) {
            if (!f.id || f.id <= field_prior || unsigned(f.type) > unsigned(AtomType::Reference) ||
                f.minimum_atoms > f.maximum_atoms || f.maximum_atoms > s.limits.atoms ||
                (f.type == AtomType::Reference ? !f.reference_kind : (f.reference_kind || f.nullable_reference)))
                return fail(Error::InvalidArgument);
            field_prior = f.id;
        }
    }
    for (const auto &r : s.records) for (const auto &f : r.fields)
        if (f.type == AtomType::Reference && !spec(s, f.reference_kind)) return fail(Error::InvalidArgument);
    return {};
}
Status atom_valid(const FieldSpec &f, const Atom &a) noexcept {
    if (f.type == AtomType::Reference) {
        if (a.bits || (!valid(a.identity) && !(f.nullable_reference && null(a.identity))) ||
            (valid(a.identity) && a.identity.kind != f.reference_kind)) return fail(Error::InvalidArgument);
    } else {
        if (!null(a.identity)) return fail(Error::InvalidArgument);
        if (f.type == AtomType::Boolean && a.bits > 1) return fail(Error::NonCanonical);
        if (f.type == AtomType::Float32 && (a.bits > UINT32_MAX || !std::isfinite(std::bit_cast<float>(std::uint32_t(a.bits))))) return fail(Error::NonCanonical);
        if (f.type == AtomType::Float64 && !std::isfinite(std::bit_cast<double>(a.bits))) return fail(Error::NonCanonical);
    }
    return {};
}
Status references(std::span<const Record> records, const Schema &s) noexcept {
    for (const auto &r : records) {
        const auto *definition = spec(s, r.identity.kind);
        for (size_t i = 0; i < r.fields.size(); ++i) if (definition->fields[i].type == AtomType::Reference)
            for (const auto &a : r.fields[i].atoms) if (!null(a.identity)) {
                auto target = std::lower_bound(records.begin(), records.end(), a.identity,
                    [](const Record &r, Identity id) { return less(r.identity, id); });
                if (target == records.end() || target->identity != a.identity) return fail(Error::StaleGeneration);
            }
    }
    return {};
}
struct Range { std::uintptr_t start{}; size_t size{}; };
template<class T> Range range(std::span<T> values) noexcept {
    return {reinterpret_cast<std::uintptr_t>(values.data()), values.size_bytes()};
}
bool overlap(Range a, Range b) noexcept {
    if (!a.size || !b.size) return false;
    return a.start <= b.start ? b.start - a.start < a.size : a.start - b.start < b.size;
}
bool schema_overlap(const Schema &s, Range bytes) noexcept {
    if (overlap(range(std::span<const Schema>(&s,1)),bytes) || overlap(range(s.records),bytes)) return true;
    for (const auto &r:s.records) if(overlap(range(r.fields),bytes)) return true;
    return false;
}
struct Counter {
    size_t count = 0, limit;
    Error error = Error::Truncated;
    Status raw(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() > limit - count) { error=Error::CapacityExceeded; return fail(error); }
        count += bytes.size(); return {};
    }
    Status u64(std::uint64_t) noexcept { std::array<std::byte,8> bytes{}; return raw(bytes); }
    Status varuint(std::uint64_t v) noexcept {
        size_t n = 1; while (v >>= 7) ++n;
        if (n > limit - count) { error=Error::CapacityExceeded; return fail(error); } count += n; return {};
    }
};
template<class Output> Status write(Output &out, const Schema &s, const Checkpoint &c) noexcept {
    if (!out.u64(magic) || !out.varuint(1) || !out.raw(s.fingerprint) || !out.u64(c.header.epoch) || !out.u64(c.header.tick) ||
        !out.raw(c.header.simulation) || !out.raw(c.header.geometry) || !out.varuint(c.records.size())) return fail(Error::Truncated);
    for (const auto &r : c.records) {
        if (!out.varuint(r.identity.kind) || !out.u64(r.identity.simulation) || !out.u64(r.identity.generation) || !out.varuint(r.fields.size())) return fail(Error::Truncated);
        const auto *definition = spec(s, r.identity.kind);
        for (size_t i = 0; i < r.fields.size(); ++i) {
            const auto &f = r.fields[i]; const auto type = definition->fields[i].type;
            if (!out.varuint(f.id) || !out.varuint(f.atoms.size())) return fail(Error::Truncated);
            for (const auto &a : f.atoms) {
                if (type == AtomType::Reference) {
                    if (!out.varuint(null(a.identity) ? 0 : 1)) return fail(Error::Truncated);
                    if (!null(a.identity) && (!out.varuint(a.identity.kind) || !out.u64(a.identity.simulation) || !out.u64(a.identity.generation))) return fail(Error::Truncated);
                } else if (type == AtomType::Unsigned || type == AtomType::Boolean) {
                    if (!out.varuint(a.bits)) return fail(Error::Truncated);
                } else if (type == AtomType::Signed) {
                    if (!out.varuint((a.bits << 1) ^ (0 - (a.bits >> 63)))) return fail(Error::Truncated);
                } else if (type == AtomType::Float32) {
                    std::array<std::byte,4> bytes{}; for (unsigned k = 0; k < 4; ++k) bytes[3-k] = std::byte(a.bits >> (8*k));
                    if (!out.raw(bytes)) return fail(Error::Truncated);
                } else if (!out.u64(a.bits)) return fail(Error::Truncated);
            }
        }
    }
    return {};
}
bool decoded_capacity(size_t records, size_t fields, size_t atoms, size_t limit) noexcept {
    if (records > limit / sizeof(Record)) return false; limit -= records * sizeof(Record);
    if (fields > limit / sizeof(Field)) return false; limit -= fields * sizeof(Field);
    return atoms <= limit / sizeof(Atom);
}
}
Result<size_t> measure(const Schema &s, const Checkpoint &c) noexcept {
    if (auto v = validate_schema(s); !v) return fail(v.error());
    if (!c.header.epoch || !known_fingerprint(c.header.simulation) || !known_fingerprint(c.header.geometry) || c.records.size() > s.limits.records) return fail(Error::InvalidArgument);
    size_t fields = 0, atoms = 0; std::array<std::uint32_t,256> counts{};
    for (size_t i = 0; i < c.records.size(); ++i) {
        const auto &r = c.records[i]; const auto *definition = spec(s, r.identity.kind);
        if (!valid(r.identity) || (i && !less(c.records[i-1].identity, r.identity)) || !definition || r.fields.size() != definition->fields.size()) return fail(Error::IncompatibleSchema);
        if (++counts[definition - s.records.data()] > definition->maximum_records || r.fields.size() > s.limits.fields - fields) return fail(Error::CapacityExceeded);
        fields += r.fields.size();
        for (size_t k = 0; k < r.fields.size(); ++k) {
            const auto &field = r.fields[k]; const auto &f = definition->fields[k];
            if (field.id != f.id || field.atoms.size() < f.minimum_atoms || field.atoms.size() > f.maximum_atoms) return fail(Error::IncompatibleSchema);
            if (field.atoms.size() > s.limits.atoms - atoms) return fail(Error::CapacityExceeded); atoms += field.atoms.size();
            for (const auto &a : field.atoms) if (auto v = atom_valid(f,a); !v) return fail(v.error());
        }
    }
    for (size_t i = 0; i < s.records.size(); ++i) if (counts[i] < s.records[i].minimum_records) return fail(Error::RecoveryUnavailable);
    if (!decoded_capacity(c.records.size(),fields,atoms,s.limits.decoded_bytes)) return fail(Error::CapacityExceeded);
    if (auto v = references(c.records,s); !v) return fail(v.error());
    Counter counter{0,s.limits.encoded_bytes}; if (auto v = write(counter,s,c); !v) return fail(counter.error); return counter.count;
}
Result<size_t> encode(const Schema &s, const Checkpoint &c, std::span<std::byte> output) noexcept {
    const auto length = measure(s,c); if (!length) return fail(length.error());
    if (output.size() < *length) return fail(Error::Truncated);
    const auto destination=range(output.first(*length));
    if(schema_overlap(s,destination) || overlap(range(std::span<const Checkpoint>(&c,1)),destination) ||
        overlap(range(c.records),destination)) return fail(Error::InvalidArgument);
    for(const auto &r:c.records) {
        if(overlap(range(r.fields),destination)) return fail(Error::InvalidArgument);
        for(const auto &f:r.fields) if(overlap(range(f.atoms),destination)) return fail(Error::InvalidArgument);
    }
    // The owner excludes concurrent mutation throughout admission and emission.
    Writer writer(output); if (auto v = write(writer,s,c); !v) return fail(v.error()); return writer.size();
}
Result<Checkpoint> decode(const Schema &s, std::span<const std::byte> bytes, DecodeStorage storage) noexcept {
    if (auto v = validate_schema(s); !v) return fail(v.error());
    if (bytes.size() > s.limits.encoded_bytes) return fail(Error::CapacityExceeded);
    const auto rr=range(storage.records), fr=range(storage.fields), ar=range(storage.atoms), input=range(bytes);
    if(overlap(rr,fr) || overlap(rr,ar) || overlap(fr,ar) || overlap(input,rr) || overlap(input,fr) || overlap(input,ar) ||
        schema_overlap(s,rr) || schema_overlap(s,fr) || schema_overlap(s,ar)) return fail(Error::InvalidArgument);
    Reader reader(bytes); auto m=reader.u64(),version=reader.varuint(); auto fingerprint=reader.raw(32);
    auto epoch=reader.u64(),tick=reader.u64(); auto simulation=reader.raw(32),geometry=reader.raw(32);auto count=reader.varuint();
    if (!m || !version || !fingerprint || !epoch || !tick || !simulation || !geometry || !count) return fail(Error::Truncated);
    if (*m != magic || *version != 1 || !std::equal(fingerprint->begin(),fingerprint->end(),s.fingerprint.begin())) return fail(Error::IncompatibleSchema);
    if (*count > s.limits.records || *count > storage.records.size()) return fail(Error::CapacityExceeded);
    if(!decoded_capacity(size_t(*count),0,0,s.limits.decoded_bytes)) return fail(Error::CapacityExceeded);
    Header header{*epoch,*tick};std::copy(simulation->begin(),simulation->end(),header.simulation.begin());std::copy(geometry->begin(),geometry->end(),header.geometry.begin());
    size_t field_cursor=0,atom_cursor=0;
    for (size_t i=0;i<*count;++i) {
        auto kind=reader.varuint(),id=reader.u64(),generation=reader.u64(),field_count=reader.varuint();
        if (!kind || !id || !generation || !field_count) return fail(Error::Truncated);
        if (*kind > UINT32_MAX) return fail(Error::Overflow);const auto *definition=spec(s,std::uint32_t(*kind));
        if (!definition || *field_count != definition->fields.size()) return fail(Error::IncompatibleSchema);
        if (*field_count > storage.fields.size()-field_cursor || *field_count > s.limits.fields-field_cursor) return fail(Error::CapacityExceeded);
        if(!decoded_capacity(size_t(*count),field_cursor+size_t(*field_count),atom_cursor,s.limits.decoded_bytes)) return fail(Error::CapacityExceeded);
        const auto field_start=field_cursor;
        for (size_t k=0;k<*field_count;++k) {
            const auto &f=definition->fields[k];auto field_id=reader.varuint(),n=reader.varuint();
            if (!field_id || !n) return fail(Error::Truncated);
            if (*field_id!=f.id || *n<f.minimum_atoms || *n>f.maximum_atoms) return fail(Error::IncompatibleSchema);
            if (*n > storage.atoms.size()-atom_cursor || *n > s.limits.atoms-atom_cursor) return fail(Error::CapacityExceeded);
            if(!decoded_capacity(size_t(*count),field_cursor+size_t(*field_count)-k,atom_cursor+size_t(*n),s.limits.decoded_bytes)) return fail(Error::CapacityExceeded);
            const auto atom_start=atom_cursor;
            for (size_t j=0;j<*n;++j) {
                Atom a{};
                if (f.type==AtomType::Reference) {
                    auto present=reader.varuint();if(!present)return fail(present.error());if(*present>1)return fail(Error::NonCanonical);
                    if(*present) {auto rk=reader.varuint(),ri=reader.u64(),rg=reader.u64();if(!rk || !ri || !rg)return fail(Error::Truncated);if(*rk>UINT32_MAX)return fail(Error::Overflow);a.identity={std::uint32_t(*rk),*ri,*rg};}
                } else if(f.type==AtomType::Float32) {
                    auto bits=reader.raw(4);if(!bits)return fail(bits.error());for(auto b:*bits)a.bits=(a.bits<<8)|std::to_integer<unsigned>(b);
                } else if(f.type==AtomType::Float64) {auto bits=reader.u64();if(!bits)return fail(bits.error());a.bits=*bits;}
                else {auto bits=reader.varuint();if(!bits)return fail(bits.error());a.bits=f.type==AtomType::Signed?((*bits>>1)^(0-(*bits&1))):*bits;}
                if(auto v=atom_valid(f,a);!v)return fail(v.error());storage.atoms[atom_cursor++]=a;
            }
            storage.fields[field_cursor++]={f.id,storage.atoms.subspan(atom_start,size_t(*n))};
        }
        storage.records[i]={{std::uint32_t(*kind),*id,*generation},storage.fields.subspan(field_start,size_t(*field_count))};
    }
    if(!reader.empty())return fail(Error::NonCanonical);
    Checkpoint checkpoint{header,storage.records.first(size_t(*count))};if(auto v=measure(s,checkpoint);!v)return fail(v.error());return checkpoint;
}
}
