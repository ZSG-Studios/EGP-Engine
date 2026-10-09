// SPDX-License-Identifier: MIT
#include "superpos_uint64.h"
#include "u64_bits.h"
#include "core/object/class_db.h"

int SuperposUInt64::compare(uint64_t p_a, uint64_t p_b) { return superpos_egp::compare(p_a, p_b); }
String SuperposUInt64::to_decimal(uint64_t p_value) { return String(std::to_string(p_value).c_str()); }
Dictionary SuperposUInt64::from_decimal(const String &p_text) {
    uint64_t value = 0;
    bool ok = !p_text.is_empty() && p_text.length() <= 20;
    for (int i = 0; ok && i < p_text.length(); ++i) { ok = p_text[i] >= '0' && p_text[i] <= '9'; }
    if (ok) {
        CharString text = p_text.utf8();
        ok = superpos_egp::parse_decimal(std::string(text.get_data(), size_t(text.length())), value);
    }
    Dictionary result;
    result["error"] = ok ? OK : ERR_INVALID_PARAMETER;
    result["value"] = superpos_egp::signed_bits(value);
    return result;
}
Dictionary SuperposUInt64::checked_add(uint64_t p_a, uint64_t p_b) {
    uint64_t value = 0;
    bool ok = superpos_egp::checked_add(p_a, p_b, value);
    Dictionary result;
    result["error"] = ok ? OK : ERR_PARAMETER_RANGE_ERROR;
    result["value"] = superpos_egp::signed_bits(value);
    return result;
}
Dictionary SuperposUInt64::bounded_difference(uint64_t p_later, uint64_t p_earlier, uint64_t p_limit) {
    bool ok = p_later >= p_earlier && p_later - p_earlier <= p_limit;
    Dictionary result;
    result["error"] = ok ? OK : ERR_PARAMETER_RANGE_ERROR;
    result["value"] = superpos_egp::signed_bits(ok ? p_later - p_earlier : 0);
    return result;
}
PackedByteArray SuperposUInt64::to_bytes(uint64_t p_value) {
    PackedByteArray bytes;
    bytes.resize(8);
    for (int i = 0; i < 8; ++i) { bytes.set(i, uint8_t(p_value >> (i * 8))); }
    return bytes;
}
Dictionary SuperposUInt64::from_bytes(const PackedByteArray &p_bytes) {
    uint64_t value = 0;
    bool ok = p_bytes.size() == 8;
    if (ok) { for (int i = 0; i < 8; ++i) { value |= uint64_t(p_bytes[i]) << (i * 8); } }
    Dictionary result;
    result["error"] = ok ? OK : ERR_INVALID_DATA;
    result["value"] = superpos_egp::signed_bits(value);
    return result;
}
void SuperposUInt64::_bind_methods() {
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("compare", "a", "b"), &SuperposUInt64::compare);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("to_decimal", "value"), &SuperposUInt64::to_decimal);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("from_decimal", "text"), &SuperposUInt64::from_decimal);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("checked_add", "a", "b"), &SuperposUInt64::checked_add);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("bounded_difference", "later", "earlier", "limit"), &SuperposUInt64::bounded_difference);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("to_bytes", "value"), &SuperposUInt64::to_bytes);
    ClassDB::bind_static_method("SuperposUInt64", D_METHOD("from_bytes", "bytes"), &SuperposUInt64::from_bytes);
}
