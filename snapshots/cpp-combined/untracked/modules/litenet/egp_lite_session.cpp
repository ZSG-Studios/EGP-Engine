// SPDX-License-Identifier: MIT
#include "egp_lite_session.h"
#include "core/object/class_db.h"

Variant EGPLiteSession::dispatch(const StringName &p_operation, const Dictionary &p_arguments) {
	ERR_FAIL_COND_V_MSG(Thread::get_caller_id() != owner_thread, int64_t(ERR_BUSY), "LiteNet session operations must run on their owner thread.");
	ERR_FAIL_COND_V_MSG(!runtime.is_valid(), int64_t(ERR_UNCONFIGURED), "Attach an EGPLiteRuntime from C# before using this session.");
	Variant operation = p_operation;
	Variant arguments = p_arguments;
	const Variant *args[] = { &operation, &arguments };
	Variant result;
	Callable::CallError error;
	runtime.callp(args, 2, result, error);
	ERR_FAIL_COND_V(error.error != Callable::CallError::CALL_OK, int64_t(FAILED));
	return result;
}

Error EGPLiteSession::attach_runtime(const Callable &p_runtime) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(runtime.is_valid(), ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(!p_runtime.is_valid(), ERR_INVALID_PARAMETER);
	runtime = p_runtime;
	return OK;
}

void EGPLiteSession::detach_runtime() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	if (runtime.is_valid()) {
		dispatch("stop");
	}
	runtime = Callable();
}

bool EGPLiteSession::has_runtime() const { return runtime.is_valid(); }
Error EGPLiteSession::listen(int p_port, const String &p_bind_address) {
	Dictionary args;
	args["port"] = p_port;
	args["bind_address"] = p_bind_address;
	return Error(int64_t(dispatch("listen", args)));
}
Error EGPLiteSession::connect_to_server(const String &p_host, int p_port) {
	Dictionary args;
	args["host"] = p_host;
	args["port"] = p_port;
	return Error(int64_t(dispatch("connect", args)));
}
Error EGPLiteSession::poll() { return Error(int64_t(dispatch("poll"))); }
void EGPLiteSession::stop() { if (runtime.is_valid()) { dispatch("stop"); } }
Dictionary EGPLiteSession::get_statistics() {
	Variant result = dispatch("statistics");
	return result.get_type() == Variant::DICTIONARY ? Dictionary(result) : Dictionary();
}
String EGPLiteSession::get_state() {
	Variant result = dispatch("state");
	return result.get_type() == Variant::STRING ? String(result) : "Unconfigured";
}
Error EGPLiteSession::send_application(int p_peer_id, const PackedByteArray &p_payload) {
	Dictionary args;
	args["peer"] = p_peer_id;
	args["payload"] = p_payload;
	return Error(int64_t(dispatch("send_application", args)));
}

void EGPLiteSession::_bind_methods() {
	ClassDB::bind_method(D_METHOD("attach_runtime", "runtime"), &EGPLiteSession::attach_runtime);
	ClassDB::bind_method(D_METHOD("detach_runtime"), &EGPLiteSession::detach_runtime);
	ClassDB::bind_method(D_METHOD("has_runtime"), &EGPLiteSession::has_runtime);
	ClassDB::bind_method(D_METHOD("listen", "port", "bind_address"), &EGPLiteSession::listen, DEFVAL(10515), DEFVAL("0.0.0.0"));
	ClassDB::bind_method(D_METHOD("connect_to_server", "host", "port"), &EGPLiteSession::connect_to_server, DEFVAL(10515));
	ClassDB::bind_method(D_METHOD("poll"), &EGPLiteSession::poll);
	ClassDB::bind_method(D_METHOD("stop"), &EGPLiteSession::stop);
	ClassDB::bind_method(D_METHOD("get_statistics"), &EGPLiteSession::get_statistics);
	ClassDB::bind_method(D_METHOD("get_state"), &EGPLiteSession::get_state);
	ClassDB::bind_method(D_METHOD("send_application", "peer_id", "payload"), &EGPLiteSession::send_application);
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::STRING, "state")));
	ADD_SIGNAL(MethodInfo("player_joined", PropertyInfo(Variant::INT, "player_id")));
	ADD_SIGNAL(MethodInfo("player_left", PropertyInfo(Variant::INT, "player_id")));
	ADD_SIGNAL(MethodInfo("application_received", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload")));
	ADD_SIGNAL(MethodInfo("diagnostic", PropertyInfo(Variant::STRING, "message")));
}
