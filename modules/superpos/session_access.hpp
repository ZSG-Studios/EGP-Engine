// PRIVATE C++23 common Session access. UDP keeps its original optional Session.
#pragma once
#include "superpos/session.hpp"
#include <optional>
#ifdef SUPERPOS_HAS_RTC
#include "rtc_process.hpp"
#endif
namespace superpos_egp {
class SessionAccess {
#ifdef SUPERPOS_HAS_RTC
    std::optional<RtcOwner::SessionLease> lease_;
#endif
    superpos::Session* session_{};
public:
    explicit SessionAccess(superpos::Session& local) noexcept : session_(&local) {}
#ifdef SUPERPOS_HAS_RTC
    explicit SessionAccess(RtcOwner::SessionLease&& lease) noexcept : lease_(std::move(lease)),session_(lease_->operator->()) {}
#endif
    SessionAccess(SessionAccess&&)=default;
    SessionAccess(const SessionAccess&)=delete;
    superpos::Session* operator->() const noexcept { return session_; }
};
#ifdef SUPERPOS_HAS_RTC
inline superpos::Result<SessionAccess> session_access(std::optional<superpos::Session>& udp,
        const std::optional<ProcessHandle>& rtc) noexcept {
    if(udp&&rtc)return superpos::fail(superpos::Error::InvalidArgument);
    if(udp)return SessionAccess(*udp);
    if(rtc){
        auto lease=rtc_process_borrow(*rtc);
        if(!lease)return superpos::fail(lease.error());
        return SessionAccess(std::move(*lease));
    }
    return superpos::fail(superpos::Error::NotReady);
}
#else
inline superpos::Result<SessionAccess> session_access(std::optional<superpos::Session>& udp) noexcept {
    if(udp)return SessionAccess(*udp);
    return superpos::fail(superpos::Error::NotReady);
}
#endif
}
