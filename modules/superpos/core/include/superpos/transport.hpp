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
};
class Clock { public: virtual ~Clock()=default; virtual std::uint64_t now_ms() noexcept=0; };
class AuthProvider {
public:
 virtual ~AuthProvider()=default;
 // Return a peer-specific admission key through trusted provisioning. No key goes on wire.
 virtual Status admission_key(PeerId,std::span<std::byte,32>) noexcept=0;
};
class DatagramIO {
public:
 virtual ~DatagramIO()=default;
 virtual Result<std::size_t> send(std::span<const std::byte>) noexcept=0;
 virtual Result<std::size_t> receive(std::span<std::byte>) noexcept=0;
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
