#include "rtc_process.hpp"
#include "../private/module_memory.hpp"
#include "core/os/thread.h"
#include "scene/main/scene_tree.h"
#include <cstdlib>
#include <new>

namespace superpos_egp {
namespace {
// Intentionally no static destructor: only SCENE shutdown may destroy owner.
// This backing parent exists before host construction and survives its drain.
superpos::BudgetAllocator& process_backing=module_backing();
RtcOwner* host{};
bool registered{}, observed_idle{}, stopping{}, finished{};
bool draining{};
std::chrono::steady_clock::time_point drain_deadline{};
constexpr std::uint64_t process_epoch=1; // Installation is single-shot.
superpos::Result<RtcOwner*> running_host() noexcept {
    if(!Thread::is_main_thread())return superpos::fail(superpos::Error::PermissionDenied);
    if(!host||stopping||finished)return superpos::fail(superpos::Error::NotReady);
    return host;
}
void drain_step() noexcept {
    if(!host)return;
    if(!draining){
        auto begun=host->begin_stop();
        if(!begun)std::abort();
        drain_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        draining=true;
    }
    auto step=host->stop_step(drain_deadline);
    if(step){
        auto* retired=host;host=nullptr;
        std::destroy_at(retired);
        process_backing.deallocate(retired);
        finished=true;return;
    }
    if(step.error()!=superpos::Error::Busy||std::chrono::steady_clock::now()>=drain_deadline)std::abort();
}
void idle() noexcept {
    if(!Thread::is_main_thread())std::abort();
    observed_idle=true;
    if(host && !stopping){
        auto status=host->pump();
        if(!status && status.error()!=superpos::Error::Busy)stopping=true;
    }
    if(stopping)drain_step();
}
}
void rtc_process_register_idle() noexcept {
    if(!Thread::is_main_thread())std::abort();
    if(!registered){SceneTree::add_idle_callback(idle);registered=true;}
}
superpos::Status rtc_process_install(
    superpos::AllocatedOwner<superpos::service::admission::CredentialProvider> credentials,
    superpos::AllocatedOwner<AttachmentPolicy> policy, pairing::NativeIceConfig config) noexcept {
    using namespace superpos;
    if(!Thread::is_main_thread())return fail(superpos::Error::PermissionDenied);
    if(!registered||!observed_idle||finished||stopping)return fail(superpos::Error::NotReady);
    if(host)return fail(superpos::Error::Busy);
    if(!credentials||!policy)return fail(superpos::Error::InvalidArgument);
    if(credentials.backing_allocator()!=&module_backing()||policy.backing_allocator()!=&module_backing())return fail(superpos::Error::InvalidArgument);
    auto bounded=MemoryPlan{}.validate(size_t(1280)<<20);
    if(!bounded)return fail(bounded.error());
    auto configured=module_memory_configuration();
    if(!configured)return fail(configured.error());
    void* backing=process_backing.allocate(sizeof(RtcOwner),alignof(RtcOwner),MemoryDomain::Backend);
    if(!backing)return fail(superpos::Error::OutOfMemory);
    host=std::construct_at(static_cast<RtcOwner*>(backing),process_backing,std::move(credentials),std::move(policy));
    auto status=host->initialize(config);
    if(!status)stopping=true; // Retain failed initialization until ordered drain.
    return status;
}
superpos::Status rtc_process_revalidate(std::uint64_t generation,std::uint64_t uncertainty_us) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->revalidate(generation,uncertainty_us);
}
superpos::Result<superpos::Fingerprint> rtc_process_local_fingerprint() noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->local_fingerprint();
}
superpos::Result<pairing::Offer> rtc_process_issue(const pairing::Scope& scope,const pairing::Certificates& certificates) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->issue(scope,certificates);
}
superpos::Status rtc_process_start(pairing::Token token,pairing::Negotiation mode) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->start(token,mode);
}
superpos::Status rtc_process_submit_remote(pairing::Token token,superpos::CarrierLane lane,pairing::Negotiation mode,std::span<const char> description) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->submit_remote(token,lane,mode,description);
}
superpos::Result<pairing::DescriptionText> rtc_process_local_description(pairing::Token token,superpos::CarrierLane lane) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->local_description(token,lane);
}
superpos::Status rtc_process_admit(pairing::Token token,superpos::CarrierLane lane,const superpos::service::admission::Request& request) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->admit(token,lane,request);
}
superpos::Status rtc_process_cancel(pairing::Token token) noexcept {
    auto current=running_host();if(!current)return superpos::fail(current.error());
    return (*current)->cancel(token);
}
superpos::Result<superpos::SessionConfig> rtc_process_prepare(pairing::Token token,superpos::SessionConfig config) noexcept {
    using namespace superpos;
    if(!Thread::is_main_thread())return fail(superpos::Error::PermissionDenied);
    if(!host||stopping||finished)return fail(superpos::Error::NotReady);
    return host->prepare_session(token,config);
}
superpos::Result<ProcessHandle> rtc_process_attach(pairing::Token token,const superpos::SessionConfig& config) noexcept {
    using namespace superpos;
    if(!Thread::is_main_thread())return fail(superpos::Error::PermissionDenied);
    if(!host||stopping||finished)return fail(superpos::Error::NotReady);
    auto attached=host->attach(token,config);
    if(!attached)return fail(attached.error());
    return ProcessHandle{process_epoch,*attached};
}
superpos::Result<RtcOwner::SessionLease> rtc_process_borrow(ProcessHandle handle) noexcept {
    using namespace superpos;
    if(!Thread::is_main_thread())return fail(superpos::Error::PermissionDenied);
    if(!host||stopping||finished||handle.epoch!=process_epoch)return fail(superpos::Error::NotReady);
    return host->borrow_session(handle.connection);
}
superpos::Status rtc_process_retire(ProcessHandle handle) noexcept {
    using namespace superpos;
    if(!Thread::is_main_thread())return fail(superpos::Error::PermissionDenied);
    // Module shutdown has already retired every claim before destroying host.
    if(finished)return {};
    if(!host||handle.epoch!=process_epoch)return fail(superpos::Error::StaleGeneration);
    return host->retire(handle.connection);
}
void rtc_process_shutdown() noexcept {
    if(!Thread::is_main_thread())std::abort();
    stopping=true;
    // Failure aborts within drain_step, without unwinding/finalizing PSA or
    // abandoning allocations. The deadline also covers an earlier idle drain.
    while(host){drain_step();if(host)std::this_thread::yield();}
    finished=true;
}
}

// Private qualification observation only, not a script or provisioned API.
#ifdef EGP_RTC_AGGREGATE_FIXTURE
namespace superpos_egp { std::size_t rtc_process_aggregate_bytes() noexcept { return process_backing.total(); } }
#endif
