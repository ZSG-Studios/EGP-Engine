// SPDX-License-Identifier: MIT
#pragma once
#include "core/io/resource.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class SuperposField : public Resource {
    GDCLASS(SuperposField, Resource);
    uint64_t field_id = 1;
    uint32_t codec_id = 3;
    uint32_t max_bytes = 8;
    StringName field_name = "value";
    uint32_t audience = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    uint32_t quantization_levels = 0;
protected:
    static void _bind_methods();
public:
    void set_field_id(uint64_t p_value) { field_id = p_value; emit_changed(); }
    uint64_t get_field_id() const { return field_id; }
    void set_codec_id(uint32_t p_value) { codec_id = p_value; emit_changed(); }
    uint32_t get_codec_id() const { return codec_id; }
    void set_max_bytes(uint32_t p_value) { max_bytes = p_value; emit_changed(); }
    uint32_t get_max_bytes() const { return max_bytes; }
    void set_field_name(const StringName &p_value) { field_name = p_value; emit_changed(); }
    StringName get_field_name() const { return field_name; }
    void set_audience(uint32_t p_value) { audience = p_value; emit_changed(); }
    uint32_t get_audience() const { return audience; }
    void set_minimum(double p_value) { minimum = p_value; emit_changed(); }
    double get_minimum() const { return minimum; }
    void set_maximum(double p_value) { maximum = p_value; emit_changed(); }
    double get_maximum() const { return maximum; }
    void set_quantization_levels(uint32_t p_value) { quantization_levels = p_value; emit_changed(); }
    uint32_t get_quantization_levels() const { return quantization_levels; }
};

class SuperposSchema : public Resource {
    GDCLASS(SuperposSchema, Resource);
    uint64_t schema_id = 1;
    uint64_t revision = 1;
    TypedArray<SuperposField> fields;
protected:
    static void _bind_methods();
public:
    void set_schema_id(uint64_t p_value) { schema_id = p_value; emit_changed(); }
    uint64_t get_schema_id() const { return schema_id; }
    void set_revision(uint64_t p_value) { revision = p_value; emit_changed(); }
    uint64_t get_revision() const { return revision; }
    void set_fields(const TypedArray<SuperposField> &p_fields) { fields = p_fields; emit_changed(); }
    TypedArray<SuperposField> get_fields() const { return fields; }
    // Native author declarations only: no Object::get or script callbacks.
    Dictionary bake() const;
    String get_fingerprint() const;
};
