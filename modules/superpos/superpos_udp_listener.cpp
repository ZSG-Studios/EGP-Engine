// SPDX-License-Identifier: MIT
#include "superpos_udp_listener.h"
#include "core/object/class_db.h"
#include "private/module_memory.hpp"
#include "private/captured_owner.hpp"
#ifdef SUPERPOS_HAS_DTLS
#include "private/udp_listener_access.hpp"
#include <cstddef>
#include <optional>
#endif

struct SuperposUdpListener::Impl {
#ifdef SUPERPOS_HAS_DTLS
    std::optional<superpos::UdpMux> mux;
#endif
    int64_t port = 0;
};

namespace {
bool listener_value(const Dictionary &p_configuration, const char *p_key, uint64_t p_fallback, uint64_t p_minimum, uint64_t p_maximum, uint64_t &r_value) {
    if (!p_configuration.has(p_key)) { r_value = p_fallback; return true; }
    const Variant value = p_configuration[p_key];
    if (value.get_type() != Variant::INT || int64_t(value) < 0) { return false; }
    r_value = uint64_t(int64_t(value));
    return r_value >= p_minimum && r_value <= p_maximum;
}
#ifdef SUPERPOS_HAS_DTLS
Error listener_error(superpos::Error p_error) {
    switch (p_error) {
        case superpos::Error::None: return OK;
        case superpos::Error::OutOfMemory: case superpos::Error::CapacityExceeded: return ERR_OUT_OF_MEMORY;
        case superpos::Error::InvalidArgument: return ERR_INVALID_PARAMETER;
        case superpos::Error::Busy: return ERR_ALREADY_IN_USE;
        case superpos::Error::Io: return ERR_CANT_CREATE;
        case superpos::Error::Unsupported: return ERR_UNAVAILABLE;
        case superpos::Error::PermissionDenied: return ERR_UNAUTHORIZED;
        default: return ERR_CANT_CREATE;
    }
}
#endif
}

SuperposUdpListener::SuperposUdpListener() {
    auto created = superpos_egp::captured_create<Impl>(superpos_egp::module_backing(), superpos::MemoryDomain::Backend);
    if (created) { impl = created->release(); } else { last_error = ERR_OUT_OF_MEMORY; }
}

SuperposUdpListener::~SuperposUdpListener() {
    // Every attached Session pins this listener, so no port outlives the mux.
    if (impl) {
        std::destroy_at(impl);
        superpos_egp::module_backing().deallocate(impl);
        impl = nullptr;
    }
}

Error SuperposUdpListener::bind(const String &p_address, uint32_t p_port, const Dictionary &p_configuration) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!Thread::is_main_thread()) { return last_error = ERR_UNAVAILABLE; }
    if (!impl) { return last_error = ERR_OUT_OF_MEMORY; }
#if !defined(SUPERPOS_HAS_DTLS) || defined(WEB_ENABLED)
    return last_error = ERR_UNAVAILABLE;
#else
    if (impl->mux) { return last_error = ERR_ALREADY_IN_USE; }
    for (int i = 0; i < p_configuration.size(); ++i) {
        const Variant key = p_configuration.get_key_at_index(i);
        const String name = key;
        if ((key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) ||
                (name != "maximum_associations" && name != "queue_datagrams" && name != "poll_quantum" &&
                 name != "amplification_factor" && name != "receive_buffer_bytes")) { return last_error = ERR_INVALID_PARAMETER; }
    }
    uint64_t associations = 0, queue = 0, quantum = 0, amplification = 0, buffer = 0;
    if (!listener_value(p_configuration, "maximum_associations", 256, 1, 4096, associations) ||
            !listener_value(p_configuration, "queue_datagrams", 32, 1, 256, queue) ||
            !listener_value(p_configuration, "poll_quantum", 256, 1, 4096, quantum) ||
            !listener_value(p_configuration, "amplification_factor", 3, 1, 10, amplification) ||
            !listener_value(p_configuration, "receive_buffer_bytes", 0, 0, superpos::UdpOptions::maximum_receive_buffer_bytes, buffer) ||
            p_port > 65535 || p_address.is_empty() || p_address.length() > 45) { return last_error = ERR_INVALID_PARAMETER; }
    for (int i = 0; i < p_address.length(); ++i) {
        const char32_t c = p_address[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.')) { return last_error = ERR_INVALID_PARAMETER; }
    }
    const CharString text = p_address.utf8();
    auto local = superpos::IpEndpoint::parse({text.get_data(), size_t(text.length())}, uint16_t(p_port));
    if (!local) { return last_error = ERR_INVALID_PARAMETER; }
    superpos::UdpMuxConfig config;
    config.maximum_associations = uint32_t(associations);
    config.queue_datagrams = uint16_t(queue);
    config.poll_quantum = uint16_t(quantum);
    config.amplification_factor = uint8_t(amplification);
    config.receive_buffer_bytes = uint32_t(buffer);
    auto bound = superpos::UdpMux::bind(superpos_egp::module_backing(), std::span<const superpos::IpEndpoint>(&*local, 1), config);
    if (!bound) { return last_error = listener_error(bound.error()); }
    auto endpoint = bound->local_endpoint(0);
    if (!endpoint) { return last_error = listener_error(endpoint.error()); }
    impl->mux.emplace(std::move(*bound));
    // IpEndpoint holds a native sockaddr_in/sockaddr_in6; both keep the
    // network-order port at byte offset 2.
    if (endpoint->length < 4) { impl->mux.reset(); return last_error = ERR_CANT_CREATE; }
    impl->port = int64_t((std::to_integer<uint32_t>(endpoint->address[2]) << 8) | std::to_integer<uint32_t>(endpoint->address[3]));
    return last_error = OK;
#endif
}

Error SuperposUdpListener::close() {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
#ifdef SUPERPOS_HAS_DTLS
    if (!impl || !impl->mux) { return last_error = OK; }
    auto statistics = impl->mux->statistics();
    if (!statistics || statistics->attached) { return last_error = ERR_BUSY; }
    impl->mux.reset();
    impl->port = 0;
#endif
    return last_error = OK;
}

bool SuperposUdpListener::is_bound() const {
#ifdef SUPERPOS_HAS_DTLS
    return Thread::get_caller_id() == owner_thread && impl && impl->mux.has_value();
#else
    return false;
#endif
}

int64_t SuperposUdpListener::get_local_port() const {
    return is_bound() ? impl->port : 0;
}

Error SuperposUdpListener::get_last_error() const {
    return Thread::get_caller_id() != owner_thread ? ERR_BUSY : last_error;
}

Dictionary SuperposUdpListener::read_status() const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
#ifdef SUPERPOS_HAS_DTLS
    if (!impl || !impl->mux) { return result; }
    auto statistics = impl->mux->statistics();
    if (!statistics) { result["error"] = listener_error(statistics.error()); return result; }
    result["error"] = OK;
    result["port"] = impl->port;
    result["attached"] = int64_t(statistics->attached);
    result["datagrams_received"] = int64_t(statistics->datagrams_received);
    result["datagrams_routed"] = int64_t(statistics->datagrams_routed);
    result["datagrams_sent"] = int64_t(statistics->datagrams_sent);
    result["malformed"] = int64_t(statistics->malformed);
    result["unknown_connection"] = int64_t(statistics->unknown_connection);
    result["not_handshake"] = int64_t(statistics->not_handshake);
    result["foreign_during_handshake"] = int64_t(statistics->foreign_during_handshake);
    result["queue_overflow"] = int64_t(statistics->queue_overflow);
    result["amplification_limited"] = int64_t(statistics->amplification_limited);
    result["candidate_paths"] = int64_t(statistics->candidate_paths);
    result["path_promotions"] = int64_t(statistics->path_promotions);
#endif
    return result;
}

#ifdef SUPERPOS_HAS_DTLS
superpos::Result<superpos::UdpMuxPort> SuperposUdpListenerAccess::attach(SuperposUdpListener &listener, std::uint64_t connection_id) noexcept {
    if (Thread::get_caller_id() != listener.owner_thread) { return superpos::fail(superpos::Error::PermissionDenied); }
    if (!listener.impl || !listener.impl->mux) { return superpos::fail(superpos::Error::NotReady); }
    return listener.impl->mux->attach(connection_id);
}
superpos::Result<superpos::UdpMuxStatistics> SuperposUdpListenerAccess::statistics(const SuperposUdpListener &listener) noexcept {
    if (Thread::get_caller_id() != listener.owner_thread) { return superpos::fail(superpos::Error::PermissionDenied); }
    if (!listener.impl || !listener.impl->mux) { return superpos::fail(superpos::Error::NotReady); }
    return listener.impl->mux->statistics();
}
#endif

void SuperposUdpListener::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind", "address", "port", "configuration"), &SuperposUdpListener::bind, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("close"), &SuperposUdpListener::close);
    ClassDB::bind_method(D_METHOD("is_bound"), &SuperposUdpListener::is_bound);
    ClassDB::bind_method(D_METHOD("get_local_port"), &SuperposUdpListener::get_local_port);
    ClassDB::bind_method(D_METHOD("read_status"), &SuperposUdpListener::read_status);
    ClassDB::bind_method(D_METHOD("get_last_error"), &SuperposUdpListener::get_last_error);
}
