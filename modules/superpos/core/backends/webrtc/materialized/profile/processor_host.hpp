// Host owns this runtime; it is never captured as its own lifetime pin.
#pragma once
#include "processor_profile.hpp"
#include "superpos/allocator.hpp"
#include <array>
#include <memory>
namespace rtc {class PeerConnection;}
namespace rtc::impl {
class ProcessorHost {
    struct Storage;
    superpos::Allocator& allocator_;
    Storage* storage_{};
    const std::thread::id owner_=std::this_thread::get_id();
    std::size_t cursor_{};bool draining_{},closed_{};
public:
    enum class Release { Released, NotRetained, NotClosed, WrongThread, HostClosed };
    struct Poll {std::size_t scanned{},failed{};bool wrong_thread{};pa::Admission status{pa::Admission::Accepted};};
    explicit ProcessorHost(superpos::Allocator&);~ProcessorHost();
    static std::size_t metadata_bytes()noexcept;
    retirement::Domain::Reserved reserveRetirement()noexcept;
    ProcessorHost(const ProcessorHost&)=delete;ProcessorHost& operator=(const ProcessorHost&)=delete;
    pa::Admission retain(const std::shared_ptr<rtc::PeerConnection>&)noexcept;
    // Release only after provider close and capture detachment have completed.
    // External owners must independently respect their callback lifetime.
    Release release(const std::shared_ptr<rtc::PeerConnection>&)noexcept;
    Poll poll(std::size_t quantum=8)noexcept;
    // Caller closes associations/providers and stops callbacks before shutdown.
    pa::Wait shutdown(std::chrono::steady_clock::time_point deadline)noexcept;
    ProcessorPool::Metrics metrics()noexcept;
};
}
