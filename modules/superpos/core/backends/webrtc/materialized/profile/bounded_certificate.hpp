// SPDX-License-Identifier: MIT
// Experimental actual RTC certificate profile; not the full backend scheduler.
#pragma once
#include "certificate.hpp"
#include "completion.hpp"
#include "superpos/allocator.hpp"
#include <chrono>
#include <memory>

namespace rtc::impl {
namespace bounded_certificate {
namespace cc = ::superpos::certificate_completion;
using Results = cc::Pool<certificate_ptr,16,2*1024*1024,UINT64_MAX,1025>;
class Failure final : public std::exception {
    cc::Error error_;
public:
    explicit Failure(cc::Error error) noexcept : error_(error) {}
    cc::Error code() const noexcept { return error_; }
    const char* what() const noexcept override { return "BOUNDED_CERTIFICATE_UNAVAILABLE"; }
};
class Ticket {
    Results::Ticket token_;
    friend class Runtime;
    explicit Ticket(Results::Ticket token) noexcept : token_(std::move(token)) {}
public:
    Ticket() noexcept = default;
    Ticket(Ticket&&) noexcept = default;
    Ticket& operator=(Ticket&&) noexcept = default;
    Ticket(const Ticket&)=delete;
    Ticket& operator=(const Ticket&)=delete;
    // No wait, promise, future, allocation, user callback or lock acquisition.
    certificate_ptr get() const { auto result=token_.poll(); if(!result) throw Failure(result.error()); return std::move(*result); }
};
class Runtime {
    struct State;
    superpos::Allocator& backing_;
    State* state_{};
public:
    struct Metrics { std::size_t live_retirements{}, deleted{}, generated{}, wrong_thread_deletions{}; };
    explicit Runtime(superpos::Allocator& backing);
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    cc::Result<void> prewarm(CertificateType type=CertificateType::Default) noexcept;
    cc::Result<Ticket> acquire(CertificateType type=CertificateType::Default) noexcept;
    cc::Result<void> shutdown(std::chrono::steady_clock::time_point deadline) noexcept;
    Metrics metrics() const noexcept;
    static cc::Result<Ticket> acquire_active(CertificateType type) noexcept;
};
} // namespace bounded_certificate
} // namespace rtc::impl
