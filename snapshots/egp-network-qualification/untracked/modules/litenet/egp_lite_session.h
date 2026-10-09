// SPDX-License-Identifier: MIT
#pragma once
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

// The managed runtime owns sockets, replication and ticks. Native/GDScript users
// share this facade; it does not introduce a second networking protocol.
class EGPLiteSession : public RefCounted {
	GDCLASS(EGPLiteSession, RefCounted);
	Callable runtime;
	Thread::ID owner_thread = Thread::get_caller_id();
	Variant dispatch(const StringName &p_operation, const Dictionary &p_arguments = Dictionary());

protected:
	static void _bind_methods();

public:
	Error attach_runtime(const Callable &p_runtime);
	void detach_runtime();
	bool has_runtime() const;
	Error listen(int p_port = 10515, const String &p_bind_address = "0.0.0.0");
	Error connect_to_server(const String &p_host, int p_port = 10515);
	Error poll();
	void stop();
	Dictionary get_statistics();
	String get_state();
	Error send_application(int p_peer_id, const PackedByteArray &p_payload);
};
