// SPDX-License-Identifier: MIT
#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class SuperposUInt64 : public RefCounted {
    GDCLASS(SuperposUInt64, RefCounted);
protected:
    static void _bind_methods();
public:
    static int compare(uint64_t p_a, uint64_t p_b);
    static String to_decimal(uint64_t p_value);
    static Dictionary from_decimal(const String &p_text);
    static Dictionary checked_add(uint64_t p_a, uint64_t p_b);
    static Dictionary bounded_difference(uint64_t p_later, uint64_t p_earlier, uint64_t p_limit);
    static PackedByteArray to_bytes(uint64_t p_value);
    static Dictionary from_bytes(const PackedByteArray &p_bytes);
};
