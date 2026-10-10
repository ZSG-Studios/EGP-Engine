// SPDX-License-Identifier: MIT
#pragma once
// Private helper shared by scene capture and projection. Paths come from local
// authoring (mappings, schema field names, catalogs), never from the network.
#include "core/error/error_list.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace superpos_egp {
// "name" or "name:sub[:sub]" (Object::get_indexed/set_indexed), at most four
// segments of at most 64 characters each and 128 characters in total.
inline Error parse_property_path(const String &p_text, Vector<StringName> &r_path) {
    if (p_text.is_empty() || p_text.length() > 128) { return ERR_INVALID_PARAMETER; }
    const Vector<String> parts = p_text.split(":");
    if (parts.is_empty() || parts.size() > 4) { return ERR_INVALID_PARAMETER; }
    Vector<StringName> path;
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].is_empty() || parts[i].length() > 64) { return ERR_INVALID_PARAMETER; }
        path.push_back(StringName(parts[i]));
    }
    r_path = path;
    return OK;
}
// True when one path equals or contains the other, so both would write one value.
inline bool property_paths_overlap(const Vector<StringName> &p_a, const Vector<StringName> &p_b) {
    const int common = p_a.size() < p_b.size() ? p_a.size() : p_b.size();
    for (int i = 0; i < common; ++i) {
        if (p_a[i] != p_b[i]) { return false; }
    }
    return true;
}
} // namespace superpos_egp
