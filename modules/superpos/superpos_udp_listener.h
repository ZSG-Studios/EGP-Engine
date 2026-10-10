// SPDX-License-Identifier: MIT
#pragma once
// Public C++17 engine/SDK declaration; the core UdpMux stays private.
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

// One bound UDP socket shared by every server-side Session association
// (single-port server). Clients prefix each datagram with the connection ID
// they were admitted with; the ID routes and never authenticates. Unknown
// traffic is dropped without a reply or allocation, and bytes sent to an
// unauthenticated address are amplification-limited. Owner thread only.
class SuperposUdpListener : public RefCounted {
    GDCLASS(SuperposUdpListener, RefCounted);
    friend struct SuperposUdpListenerAccess;
    struct Impl;
    Impl *impl = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
    Error last_error = OK;
protected:
    static void _bind_methods();
public:
    Error bind(const String &p_address, uint32_t p_port, const Dictionary &p_configuration = Dictionary());
    // Refused with ERR_BUSY while any Session association is attached.
    Error close();
    bool is_bound() const;
    int64_t get_local_port() const;
    Dictionary read_status() const;
    Error get_last_error() const;
    SuperposUdpListener();
    ~SuperposUdpListener();
};
