// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/rtc/pairing.hpp"
#include "superpos/service/rtc/ice_config.hpp"
#include <chrono>

namespace superpos::service::pairing {
inline constexpr std::size_t native_association_capacity=528;
inline constexpr std::size_t signaling_bytes=4096;
enum class NativePhase { Created, Negotiating, Ready, Bound, Closing, Reclaiming };
enum class Negotiation { Offerer, Answerer };
struct NativeInfo {
    std::uint64_t identity{};
    Token pair{};
    CarrierLane role{};
    NativePhase phase{};
    Error failure{};
    bool local_description_ready{},remote_description_submitted{};
};
struct DescriptionText {
    std::array<char,signaling_bytes> bytes{};
    std::size_t size{};
    Negotiation sender{};
};
struct NativeCounts {std::size_t live{},peak{},metadata_bytes{},host_metadata_bytes{};bool prewarm_submitted{},stopping{};};
struct NativePoll {std::size_t scanned{},progressed{},closed{};};

// Exclusive owner of one backend ProcessorHost and certificate runtime. All
// methods and destruction run on this creating thread. No raw host, peer or
// channel escapes. External native PeerConnection construction while this
// adapter lives violates its exclusive ownership contract.
//
// initialize may leave owned runtime state if later initialization fails:
// shutdown must finish before destruction whenever allocated() is true.
// create publishes custody only. Later channel/signaling failure is observed
// through info() and closed asynchronously, retaining its original identity.
class NativeAdapter final:public NativeAssociations {
    struct Storage;
    Allocator& allocator_;
    ClockSource& clock_;
    ContinuityGuard& continuity_;
    const std::thread::id owner_=std::this_thread::get_id();
    Storage* storage_{};
    bool busy_{},stopping_{},closed_{};
    std::uint64_t first_identity_{},last_identity_{};
    Status own()const noexcept;
    Result<std::size_t> find(std::uint64_t)const noexcept;
    Status begin_close(std::size_t,Error)noexcept;
    bool advance_close(std::size_t)noexcept;
public:
    NativeAdapter(Allocator& a,ClockSource& c,ContinuityGuard& g)noexcept:allocator_(a),clock_(c),continuity_(g){}
    ~NativeAdapter();
    NativeAdapter(const NativeAdapter&)=delete;NativeAdapter& operator=(const NativeAdapter&)=delete;
    NativeAdapter(NativeAdapter&&)=delete;NativeAdapter& operator=(NativeAdapter&&)=delete;
    Result<bool> allocated()const noexcept;
    Status initialize(NativeIceConfig={})noexcept;
    Result<NativePathInfo> selected_path(std::uint64_t)noexcept;
    // Actual selected local certificate, available once prewarming completes.
    // This is not evidence about any remote certificate or admitted peer.
    Result<Fingerprint> local_fingerprint()noexcept;
    Result<std::uint64_t> create(const Offer&,CarrierLane)noexcept override;
    Status start(std::uint64_t,Negotiation)noexcept;
    Status submit_remote(std::uint64_t,Negotiation,std::span<const char>)noexcept;
    Result<DescriptionText> local_description(std::uint64_t)const noexcept;
    Result<ObservedAssociation> observed(std::uint64_t)const noexcept;
    // Bound-only whole-frame IO. true means admitted (including SCTP-buffered);
    // false means only typed pre-admission Full/Contended, safe to retry unchanged.
    // Other send failures close the association and may have unknown delivery.
    Result<bool> try_send(std::uint64_t,std::span<const std::byte>)noexcept;
    // A short output retains the whole frame in this owner's fixed 960-byte slot.
    // Only binary 1..960-byte messages are returned, without a partial prefix.
    Result<std::size_t> receive_frame(std::uint64_t,std::span<std::byte>)noexcept;
    // Registry must already own the transferred pair. Validates both records
    // together before removing their handshake deadline (never extends it).
    Status activate(const Registry&,OwnershipToken)noexcept;
    Result<NativeInfo> info(std::uint64_t)const noexcept;
    Result<NativeCounts> counts()const noexcept;
    Result<NativePoll> poll(std::size_t quantum=8)noexcept;
    Status close_and_quiesce(std::uint64_t)noexcept override;
    Status shutdown(std::chrono::steady_clock::time_point deadline)noexcept;
};
}
