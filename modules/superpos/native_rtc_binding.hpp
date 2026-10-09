// Private native binding; no Godot method, wire credential or C++17 SDK API.
#pragma once
#include "core/error/error_list.h"
#include "rtc_process.hpp"
class SuperposSession;
struct SuperposRtcBindingAccess {
    // Token already belongs to this process host and is admitted by both lanes.
    // Identity fields come from the host AttachmentPolicy, never caller values.
    static Error bind(SuperposSession&, superpos_egp::pairing::Token) noexcept;
};
