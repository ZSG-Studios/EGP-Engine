// SPDX-License-Identifier: MIT
#pragma once
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "net_core.h"
class EGPNetSession : public RefCounted {
    GDCLASS(EGPNetSession, RefCounted);
    std::unique_ptr<egp::net::Session> session;
    Thread::ID owner_thread = Thread::get_caller_id();
protected:
    static void _bind_methods();
public:
    Error configure(const Dictionary &p_options = Dictionary());
    Error listen(int p_port = 10515, const String &p_binding = "0.0.0.0");
    Error connect_to_server(const String &p_address, int p_port = 10515);
    Error connect_token(int64_t p_client_id, const PackedByteArray &p_token, const String &p_binding = "0.0.0.0");
    Dictionary issue_token(int64_t p_client_id, const String &p_public_address);
    Error poll();
    void stop();
    void close();
    String get_state() const;
    String get_fingerprint() const;
    Dictionary get_statistics() const;
    Error send_application(int64_t p_peer, const PackedByteArray &p_payload);
    Variant command(const StringName &p_operation, const Dictionary &p_arguments = Dictionary());
};
