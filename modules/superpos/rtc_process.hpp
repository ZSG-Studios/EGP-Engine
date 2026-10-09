// PRIVATE C++23 integration candidate; never include in generated SDK headers.
#pragma once
#include "rtc_owner.hpp"

namespace superpos_egp {
struct ProcessHandle { std::uint64_t epoch{}; RtcOwner::Handle connection{}; };
// Call only from SCENE module initialization/uninitialization, on main thread.
void rtc_process_register_idle() noexcept;
void rtc_process_shutdown() noexcept;
// Native trusted provisioning only. No script bindings or wire authority.
superpos::Status rtc_process_install(
    superpos::AllocatedOwner<superpos::service::admission::CredentialProvider>,
    superpos::AllocatedOwner<AttachmentPolicy>, pairing::NativeIceConfig = {}) noexcept;
// Trusted native bridge only. Remote descriptions remain untrusted bytes;
// admit obtains the actual native certificate identity inside RtcOwner.
// revalidate requires independently calibrated continuity, never wire time.
superpos::Status rtc_process_revalidate(std::uint64_t generation,std::uint64_t uncertainty_us) noexcept;
superpos::Result<superpos::Fingerprint> rtc_process_local_fingerprint() noexcept;
superpos::Result<pairing::Offer> rtc_process_issue(const pairing::Scope&,const pairing::Certificates&) noexcept;
superpos::Status rtc_process_start(pairing::Token,pairing::Negotiation) noexcept;
superpos::Status rtc_process_submit_remote(pairing::Token,superpos::CarrierLane,pairing::Negotiation,std::span<const char>) noexcept;
superpos::Result<pairing::DescriptionText> rtc_process_local_description(pairing::Token,superpos::CarrierLane) noexcept;
superpos::Status rtc_process_admit(pairing::Token,superpos::CarrierLane,const superpos::service::admission::Request&) noexcept;
superpos::Status rtc_process_cancel(pairing::Token) noexcept;
superpos::Result<superpos::SessionConfig> rtc_process_prepare(pairing::Token,superpos::SessionConfig) noexcept;
superpos::Result<ProcessHandle> rtc_process_attach(pairing::Token,const superpos::SessionConfig&) noexcept;
superpos::Result<RtcOwner::SessionLease> rtc_process_borrow(ProcessHandle) noexcept;
superpos::Status rtc_process_retire(ProcessHandle) noexcept;
}
