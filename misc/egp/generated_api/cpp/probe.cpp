// SPDX-License-Identifier: MIT
// Compile-only direct Superpos API coverage against the matching C++17 SDK.
#include <godot_cpp/classes/superpos_session.hpp>
#include <godot_cpp/classes/superpos_schema.hpp>
#include <godot_cpp/classes/superpos_field.hpp>
#include <godot_cpp/classes/superpos_u_int64.hpp>
#include <godot_cpp/variant/typed_array.hpp>
using namespace godot;
void verify_generated_api() {
    Ref<SuperposField> field; field.instantiate(); field->set_field_id(1);
    Ref<SuperposSchema> schema; schema.instantiate(); schema->set_schema_id(73);
    TypedArray<SuperposField> fields; fields.push_back(field); schema->set_fields(fields);
    TypedArray<SuperposSchema> schemas; schemas.push_back(schema);
    Ref<SuperposSession> session; session.instantiate();
    const Error configured = session->configure(schemas, 4, 1, 0, 4096);
    const uint64_t object = session->spawn_object(73, 0, SuperposUInt64::to_bytes(1));
    const Dictionary observed = session->read_object(object);
    PackedInt64Array ids; ids.push_back(1);
    const Dictionary typed = session->read_fields(object, ids);
    Dictionary values; values[int64_t(1)] = int64_t(-1);
    const Error published = session->publish_fields(object, UINT64_MAX, values);
    (void)typed; (void)published;
    const Dictionary tick = session->read_tick();
    session->close();
    (void)configured; (void)observed; (void)tick;
}
