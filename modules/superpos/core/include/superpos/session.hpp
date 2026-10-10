#pragma once
#include "delivery.hpp"
#include "transport.hpp"
#include "capability.hpp"
namespace superpos {
enum class Outcome : std::uint8_t { Accepted, Received, Applied, Committed };
enum class ChannelPurpose : std::uint8_t { Application,Control };
struct DeliveryTicket { Epoch epoch{}; std::uint64_t message{}; std::uint8_t channel{}; DeliveryMode mode{}; };
// Process-local instance is preserved by moves and never reused by creation;
// it is not a wire identity or an admission credential.
struct SessionIdentity { std::uint64_t session{}; Epoch connection_epoch{}; PeerId local_peer{},remote_peer{}; Fingerprint schemas{},simulation{}; std::uint64_t instance{}; bool operator==(const SessionIdentity&) const noexcept=default; };
struct SessionConfig {
 std::uint64_t session_id{};Epoch epoch{1};PeerId local_peer{},remote_peer{};
 // A nonzero compatibility fingerprint must equal capabilities.schemas. A zero
 // value is derived from that mandatory validated manifest at creation.
 std::array<std::byte,32> schema_fingerprint{};
 std::uint8_t logical_channels{32};
 DeliveryLimits limits{};
 std::array<DeliveryMode,32> channel_modes{};
 // Role and effective byte caps are immutable HELLO admission. Control gets a
 // separate shared eight-slot/32KiB reserve, ReliableOrdered delivery, and an enforced 4KiB record ceiling;
 // reliable ordered Application remains eligible for 64KiB logical messages.
 std::array<ChannelPurpose,32> channel_purposes{};
 // Physical association routing is independent of ChannelPurpose. Unreliable
 // inputs may use Control without claiming reliable Control reserve semantics.
 // Control-purpose channels normalize to Control; other channels default State.
 std::array<CarrierLane,32> channel_carriers=[]() noexcept {std::array<CarrierLane,32> routes{};routes.fill(CarrierLane::State);return routes;}();
 std::array<std::uint32_t,32> channel_message_bytes=[]() noexcept {std::array<std::uint32_t,32> caps{};caps.fill(65536);return caps;}();
 CapabilityManifest capabilities{};
 AdmissionRequirements admission{};
};
// Pre-provisioned authenticated datagram path, up to 32 negotiated logical
// channels. Each direction shares eight Application reservations/256 KiB and
// eight reserved Control reservations/32 KiB across channels of each purpose.
// Application receipt metadata has 64 fixed rows plus eight reserved Control
// rows, emitted first. All fixed metadata and reserves count against the Session
// ledger. UnreliableLatest supersedes the entire channel; per-object
// coalescing belongs to replication. All methods belong to the owner thread.
// Admission requires qualified schema/simulation declarations and local policy;
// there is no unregistered/manual-handshake bypass. Manifests are immutable for
// both HELLO directions and retries. Only ready sessions expose common bounds.
// A terminal delivery/transport error currently terminates this whole Session.
// Carrier segmentation counters (path MTU below a complete session frame).
struct SessionSegmentStatistics {
 std::uint64_t segmented_frames{},segments_sent{},segments_received{},reassembled_frames{};
 std::uint64_t evicted_frames{},expired_frames{},abandoned_frames{};
 unsigned queued_segments{};
};
class Session {
 struct Impl;Impl* impl_{};Allocator* allocator_{};
 Status pump_frames(Tick) noexcept;
public:
 // Version 4 adds carrier segment frames below logical fragments.
 static constexpr std::uint64_t hello_wire_version=4;
 Session() noexcept=default;~Session();
 Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 Session(Session&&) noexcept;Session&operator=(Session&&) noexcept;
 static Result<Session> create(Allocator&,TransportProvider&,SessionConfig) noexcept;
 Status pump(Tick) noexcept;
 // Raises every channel's retransmission interval (in pump ticks) to the
 // carrier's measured retransmission timeout; never below limits.retry_ticks.
 Status set_retry_ticks(Tick) noexcept;
 bool ready() const noexcept;
 // Negotiated, immutable carrier roles are visible only after admission.
 Result<std::uint8_t> channel_count() const noexcept;
 Result<DeliveryMode> channel_mode(std::uint8_t channel) const noexcept;
 Result<SessionIdentity> identity() const noexcept;
 // Callback-free owner-thread local creation token, including during admission.
 // This conveys neither readiness nor authority and is not a wire credential.
 Result<std::uint64_t> instance_identity() const noexcept;
    Result<DeliveryLimits> delivery_limits() const noexcept;
 Result<ChannelPurpose> channel_purpose(std::uint8_t channel) const noexcept;
 Result<CarrierLane> channel_carrier(std::uint8_t channel) const noexcept;
 Result<std::uint32_t> channel_message_bytes(std::uint8_t channel) const noexcept;
    // Owner-thread alias preflight for the handle, implementation and owned
    // send/receive pools. Provider-owned memory remains a caller lifetime duty.
    Result<bool> storage_overlaps(std::span<const std::byte>) const noexcept;
 Result<AdmittedCapabilities> capabilities() const noexcept;
 Result<SessionSegmentStatistics> segment_statistics() const noexcept;
 Result<DeliveryTicket> send(std::span<const std::byte>,Tick,std::uint8_t channel=0) noexcept;
 Result<DeliveryView> receive(std::uint8_t channel=0) const noexcept;
 Status applied(std::uint64_t message,std::uint8_t channel=0) noexcept;
 Result<Outcome> outcome(DeliveryTicket) const noexcept;
 Status retire(DeliveryTicket) noexcept;
};
class Runtime {
 BudgetAllocator allocator_;
public:
 explicit Runtime(MemoryPlan memory={}) noexcept:allocator_(memory){}
 Allocator& allocator() noexcept{return allocator_;}
 std::size_t owned_bytes() const noexcept{return allocator_.total();}
 Result<Session> create_session(TransportProvider&p,SessionConfig c) noexcept{return Session::create(allocator_,p,c);}
};
}
