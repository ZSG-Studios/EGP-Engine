#pragma once
#include "allocator.hpp"
#include "types.hpp"
namespace superpos {
// Physical routing is independent of logical reliability and control reserves.
// A single carrier reports Single; a paired provider must report the actual
// association used, after authenticating its immutable pairing transcript.
enum class CarrierLane : std::uint8_t { Single, Control, State };
struct CarrierFrame { std::size_t bytes{}; CarrierLane lane{CarrierLane::Single}; };
// Split providers progress both associations and report independent write
// ownership/backpressure. Busy as an error means no lane can accept a write.
struct CarrierProgress { bool control_writable{true},state_writable{true}; };
struct TransportCapabilities {
 bool authenticated{}, encrypted{}, datagram{}, browser{};
 std::size_t maximum_frame{};
 // Complete encrypted UDP payload overhead for the qualified fixed cipher.
 // Zero means unknown, and cannot establish native packet byte accounting.
 std::uint16_t encrypted_overhead_bytes{};
 bool split_carriers{};
 // The provider coalesces frames accepted during a pump into shared datagrams
 // and transmits them at flush(). Session offers such a provider up to 32 frames
 // per pump; any other provider receives at most four, because an accepting
 // carrier (for example a browser data channel) may only queue them, and its
 // receiver drains a bounded ring per quantum.
 bool coalescing{};
 // Increases each time the provider moves its traffic to a newly validated
 // peer address. Path-dependent state above it (path MTU) restarts on change.
 std::uint64_t path_generation{};
};
class Clock { public: virtual ~Clock()=default; virtual std::uint64_t now_ms() noexcept=0; };
class AuthProvider {
public:
 virtual ~AuthProvider()=default;
 // Return a peer-specific admission key through trusted provisioning. No key goes on wire.
 virtual Status admission_key(PeerId,std::span<std::byte,32>) noexcept=0;
};
// Path of the datagram most recently returned by DatagramIO::receive. The
// current path is the validated (or handshake) address; a candidate is one
// newer source address, named by a generation that is never reused.
struct DatagramPath { std::uint64_t generation{}; bool candidate{}; };
class DatagramIO {
public:
 virtual ~DatagramIO()=default;
 virtual Result<std::size_t> send(std::span<const std::byte>) noexcept=0;
 virtual Result<std::size_t> receive(std::span<std::byte>) noexcept=0;
 // Path-aware IO (a single-port server's per-association port). The defaults
 // describe a fixed, connected path: one remote address, nothing to validate.
 virtual bool path_aware() const noexcept { return false; }
 virtual DatagramPath received_path() const noexcept { return {}; }
 // Canonical bytes of the current remote address; empty when unknown or fixed.
 virtual std::span<const std::byte> path_identity() const noexcept { return {}; }
 // Send to a candidate path. CapacityExceeded: refused (amplification limit).
 virtual Result<std::size_t> send_candidate(std::uint64_t,std::span<const std::byte>) noexcept { return fail(Error::Unsupported); }
 // The candidate answered an authenticated challenge: it becomes current.
 virtual Status promote_candidate(std::uint64_t) noexcept { return fail(Error::Unsupported); }
 // The peer authenticated over the current path (handshake complete).
 virtual void path_authenticated() noexcept {}
 // Refresh queued input without consuming it, so path_identity() reflects a
 // datagram that has arrived but has not been read (shared-socket polling).
 virtual Status poll() noexcept { return {}; }
};
class TransportProvider {
public:
 virtual ~TransportProvider()=default;
 virtual TransportCapabilities capabilities() const noexcept=0;
 virtual Status advance() noexcept=0;
 virtual bool ready() const noexcept=0;
 // Busy preserves provider ownership of the exact write buffer; caller retries
 // advance rather than substituting another message.
 virtual Status send(std::span<const std::byte>) noexcept=0;
 virtual Result<std::size_t> receive(std::span<std::byte>) noexcept=0;
 // End of an owner pump: a coalescing provider transmits frames accepted by
 // send/send_frame during this pump. Busy means paced work remains queued and
 // owned; it is retried by the next advance or flush. Default: nothing queued.
 virtual Status flush() noexcept { return {}; }
 // Busy owns the exact payload AND lane until advance completes. The default
 // preserves existing single-carrier providers. Advertising split_carriers
 // without implementing all three frame methods fails explicitly.
 virtual Result<CarrierProgress> advance_frames() noexcept {
  if(capabilities().split_carriers)return fail(Error::Unsupported);
  auto result=advance();if(!result)return fail(result.error());return CarrierProgress{};
 }
 virtual Status send_frame(std::span<const std::byte> bytes,CarrierLane lane) noexcept {
  if(static_cast<unsigned>(lane)>static_cast<unsigned>(CarrierLane::State))return fail(Error::InvalidArgument);
  auto caps=capabilities();if(caps.split_carriers)return fail(Error::Unsupported);
  if(bytes.size()>caps.maximum_frame)return fail(Error::CapacityExceeded);
  return send(bytes);
 }
 virtual Result<CarrierFrame> receive_frame(std::span<std::byte> bytes) noexcept {
  if(capabilities().split_carriers)return fail(Error::Unsupported);
  auto received=receive(bytes);if(!received)return fail(received.error());
  if(*received>bytes.size())return fail(Error::ProtocolViolation);
  return CarrierFrame{*received,CarrierLane::Single};
 }
};
class ConnectivityProvider { public: virtual ~ConnectivityProvider()=default; virtual Status can_route(PeerId,TransportCapabilities) noexcept=0; };
}
