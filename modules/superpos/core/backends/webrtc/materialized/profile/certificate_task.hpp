// SPDX-License-Identifier: MIT
// Fixed task + completion admission, before provider ownership transfer.
#pragma once
#include "completion.hpp"
#include "task_admission.hpp"
#include <concepts>
#include <new>

namespace superpos::certificate_completion {
inline Error task_error(task_admission::Admission status) noexcept {
    using A = task_admission::Admission;
    switch (status) {
        case A::Full: return Error::Full;
        case A::ByteLimit: return Error::ByteLimit;
        case A::Contended: return Error::Contended;
        case A::Closed: return Error::Closed;
        case A::Exhausted: return Error::Exhausted;
        default: return Error::Unsupported;
    }
}
template<class Results, class Provider> struct CertificateJob {
    typename Results::Producer producer;
    Provider provider;
    void operator()() noexcept {
        try {
            typename Results::value_type value = provider();
            (void)producer.complete(value);
        } catch (const std::bad_alloc&) {
            (void)producer.fail(Error::OutOfMemory);
        } catch (...) {
            (void)producer.fail(Error::ProviderFailure);
        }
    }
};

// Task/producer/ticket all borrow their runtimes. No blocking get, hidden
// packaged_task, implicit future allocation or accepted job without a result.
// Charge values describe retained ownership and need real provider accounting.
template<class Tasks, class Results, class Provider>
Result<typename Results::Ticket> try_submit(
    Tasks& tasks, task_admission::Owner owner, Results& results, Provider& provider,
    std::size_t result_charge, std::size_t task_charge = 0) noexcept {
    using Job = CertificateJob<Results, Provider>;
    using Task = typename Tasks::Task;
    if constexpr (!std::is_nothrow_move_constructible_v<Provider> ||
                  !std::is_nothrow_destructible_v<Provider> ||
                  !std::is_invocable_r_v<typename Results::value_type, Provider&> ||
                  !Task::template supported<Job>) {
        return std::unexpected(Error::Unsupported);
    } else {
        auto completion = results.try_reserve(result_charge);
        if (!completion) return std::unexpected(completion.error());
        auto task = tasks.try_reserve(owner, task_admission::TaskClass::Data, {task_charge});
        if (task.status != task_admission::Admission::Accepted)
            return std::unexpected(task_error(task.status));
        // All fixed credits exist before moving caller-owned provider state.
        Job job{std::move(completion->producer), std::move(provider)};
        Task callable;
        // These operations are guaranteed after supported<Job>/Reserved proof.
        // A failed invariant is an implementation/lifetime misuse, never a
        // false nonacceptance result after caller ownership has been consumed.
        if (callable.assign(job) != task_admission::Admission::Accepted) std::terminate();
        if (task.ticket.commit(callable) != task_admission::Admission::Accepted) std::terminate();
        return std::move(completion->ticket);
    }
}
} // namespace superpos::certificate_completion
