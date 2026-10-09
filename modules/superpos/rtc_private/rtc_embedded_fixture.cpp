// SPDX-License-Identifier: MIT
// Compile only into the native EGP fixture profile; no extension or script bridge.
#ifdef SUPERPOS_RTC_EMBEDDED_FIXTURE
#ifndef SUPERPOS_HAS_RTC
#error Embedded RTC fixture requires the engine-owned RTC feature.
#endif
#include "modules/superpos/native_rtc_binding.hpp"
#include "modules/superpos/superpos_session.h"
#include "modules/superpos/private/module_memory.hpp"
#include "scene/main/scene_tree.h"
#include "core/os/os.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <thread>

namespace superpos_egp::embedded_fixture {
using namespace superpos;
namespace admission = service::admission;
unsigned checks{}, reentries{};
[[noreturn]] void fail_at(unsigned line) noexcept {
    std::printf("EGP_RTC_EMBEDDED_FAIL line=%u checks=%u\n",line,checks);
    std::fflush(stdout);std::_Exit(3); // Failure evidence without interactive CRT dialogs.
}
#define RTC_CHECK(...) do {++checks;if(!(__VA_ARGS__))fail_at(__LINE__);}while(false)
pairing::Scope scope(unsigned i) noexcept {return {{119,207,31,400+i,51,UINT64_MAX,1},800+i};}
struct Credentials final:admission::CredentialProvider {
    std::array<std::byte,32> key{};
    Credentials() noexcept {key.fill(std::byte{0x6d});}
    ~Credentials(){key.fill(std::byte{});}
    Status resolve(const admission::Request& request,std::span<std::byte,32> out,admission::CredentialScope& selected) noexcept override {
        RTC_CHECK(rtc_process_revalidate(1,0).error()==superpos::Error::Busy);++reentries;
        for(unsigned i=1;i<=2;++i){const auto p=scope(i).principal;if(request.actor==p.actor){
            selected={p.match,p.session,p.authority_epoch,p.actor,p.actor_epoch,p.permissions};
            std::copy(key.begin(),key.end(),out.begin());return {};}}
        return fail(superpos::Error::PermissionDenied);
    }
};
struct Policy final:AttachmentPolicy {
    Result<std::uint64_t> policy_revision() const noexcept override {return 1;}
    Status check(const pairing::Scope& observed) noexcept override {
        const auto& x=observed.principal;
        for(unsigned i=1;i<=2;++i){const auto expected=scope(i);const auto& y=expected.principal;
            if(observed.peer==expected.peer&&x.match==y.match&&x.session==y.session&&x.authority_epoch==y.authority_epoch&&
               x.connection_incarnation==y.connection_incarnation&&x.actor==y.actor&&x.actor_epoch==y.actor_epoch&&x.permissions==y.permissions)return {};}
        return fail(superpos::Error::PermissionDenied);
    }
    Result<SessionBinding> session_binding(const pairing::Scope& observed) noexcept override {
        if(auto status=check(observed);!status)return fail(status.error());
        return SessionBinding{observed.peer==scope(1).peer?scope(2).peer:scope(1).peer,31};
    }
};
struct Fixture {
    enum class Stage {Fingerprint,Negotiation,Ready,Delivery,Applied};
    Stage stage=Stage::Fingerprint;
    const std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    Credentials* credentials{};
    std::array<pairing::Offer,2> offers{};
    std::array<admission::Request,2> proofs{};
    std::array<bool,4> submitted{},admitted{};
    std::array<Ref<SuperposSession>,2> sessions;
    std::array<PackedByteArray,2> payloads;
    std::array<Dictionary,2> tickets;
    std::array<bool,2> received{};
    std::size_t baseline{};
    explicit Fixture(std::size_t before) noexcept:baseline(before){}
    void initialize() noexcept {
        auto c=AllocatedOwner<admission::CredentialProvider>::create<Credentials>(module_backing(),MemoryDomain::Backend);
        auto p=AllocatedOwner<AttachmentPolicy>::create<Policy>(module_backing(),MemoryDomain::Backend);RTC_CHECK(c&&p);
        credentials=static_cast<Credentials*>(c->get());RTC_CHECK(rtc_process_install(std::move(*c),std::move(*p)));
        superpos::Error wrong{};std::thread worker([&]{wrong=rtc_process_revalidate(1,0).error();});worker.join();
        RTC_CHECK(wrong==superpos::Error::PermissionDenied);
    }
    bool step() noexcept {
        RTC_CHECK(std::chrono::steady_clock::now()<deadline);
        if(stage==Stage::Fingerprint){
            auto fp=rtc_process_local_fingerprint();if(!fp){RTC_CHECK(fp.error()==superpos::Error::NotReady);return false;}
            RTC_CHECK(rtc_process_revalidate(1,0));
            BorrowedPsaCrypto signer(*credentials);pairing::Certificates certificates{*fp,*fp,*fp,*fp};
            for(unsigned i=0;i<2;++i){auto offer=rtc_process_issue(scope(i+1),certificates);RTC_CHECK(offer);offers[i]=*offer;
                auto proof=signer.sign(offer->request,credentials->key);RTC_CHECK(proof);proofs[i]=*proof;
                auto started=rtc_process_start(offer->pair,i?pairing::Negotiation::Answerer:pairing::Negotiation::Offerer);
                if(!started)std::printf("EGP_RTC_EMBEDDED_START peer=%u error=%u\n",i,unsigned(started.error()));
                RTC_CHECK(started);}
            stage=Stage::Negotiation;
        }
        if(stage==Stage::Negotiation){
            for(unsigned peer=0;peer<2;++peer)for(unsigned lane=0;lane<2;++lane){const unsigned n=peer*2+lane;
                const auto carrier=lane?CarrierLane::State:CarrierLane::Control;
                if(!submitted[n]){auto d=rtc_process_local_description(offers[peer].pair,carrier);
                    if(!d){RTC_CHECK(d.error()==superpos::Error::NotReady);continue;}
                    RTC_CHECK(rtc_process_submit_remote(offers[1-peer].pair,carrier,d->sender,std::span(d->bytes).first(d->size)));submitted[n]=true;}}
            if(!std::ranges::all_of(submitted,[](bool b){return b;}))return false;
            for(unsigned peer=0;peer<2;++peer)for(unsigned lane=0;lane<2;++lane){const unsigned n=peer*2+lane;if(admitted[n])continue;
                auto status=rtc_process_admit(offers[peer].pair,lane?CarrierLane::State:CarrierLane::Control,proofs[peer]);
                if(!status){RTC_CHECK(status.error()==superpos::Error::NotReady);continue;}admitted[n]=true;}
            if(!std::ranges::all_of(admitted,[](bool b){return b;}))return false;
            Ref<SuperposField> field;field.instantiate();Ref<SuperposSchema> schema;schema.instantiate();
            TypedArray<SuperposField> fields;fields.push_back(field);schema->set_fields(fields);
            TypedArray<SuperposSchema> schemas;schemas.push_back(schema);
            for(unsigned i=0;i<2;++i){sessions[i].instantiate();RTC_CHECK(sessions[i]->configure(schemas,4,31,0,4096)==OK);
                RTC_CHECK(SuperposRtcBindingAccess::bind(*sessions[i].ptr(),offers[i].pair)==OK);
                RTC_CHECK(SuperposRtcBindingAccess::bind(*sessions[i].ptr(),offers[i].pair)==ERR_ALREADY_IN_USE);}
            RTC_CHECK(reentries>=4);stage=Stage::Ready;
        }
        for(auto& session:sessions){auto advanced=session->advance_tick();RTC_CHECK(advanced==OK||advanced==ERR_BUSY);}
        if(stage==Stage::Ready){
            if(sessions[0]->get_state()!=String("NetworkReady")||sessions[1]->get_state()!=String("NetworkReady"))return false;
            RTC_CHECK(payloads[0].resize(65536)==OK&&payloads[1].resize(4096)==OK);
            std::fill_n(payloads[0].ptrw(),65536,0x52);std::fill_n(payloads[1].ptrw(),4096,0xc1);
            for(unsigned i=0;i<2;++i){tickets[i]=sessions[i]->enqueue_packet(payloads[i],0);RTC_CHECK(int(tickets[i]["error"])==OK);}
            stage=Stage::Delivery;
        }
        if(stage==Stage::Delivery){
            for(unsigned i=0;i<2;++i){if(received[i])continue;auto incoming=sessions[1-i]->read_packet(0);
                if(int(incoming["error"])!=OK){RTC_CHECK(int(incoming["error"])==ERR_UNCONFIGURED);continue;}
                PackedByteArray actual=incoming["payload"];RTC_CHECK(actual==payloads[i]);
                RTC_CHECK(sessions[1-i]->acknowledge_packet(uint64_t(int64_t(incoming["message"])),uint64_t(int64_t(incoming["binding_generation"])),0)==OK);received[i]=true;}
            if(!received[0]||!received[1])return false;stage=Stage::Applied;
        }
        if(stage==Stage::Applied){
            for(unsigned i=0;i<2;++i){auto outcome=sessions[i]->get_packet_outcome(uint64_t(int64_t(tickets[i]["message"])),uint64_t(int64_t(tickets[i]["binding_generation"])),0);
                RTC_CHECK(int(outcome["error"])==OK);if(String(outcome["outcome"])!="Applied")return false;}
            for(unsigned i=0;i<2;++i){RTC_CHECK(sessions[i]->retire_packet(uint64_t(int64_t(tickets[i]["message"])),uint64_t(int64_t(tickets[i]["binding_generation"])),0)==OK);
                RTC_CHECK(sessions[i]->close_checked()==OK);sessions[i].unref();}
            // Host owns and drains claims after wrappers have stopped retaining Sessions.
            rtc_process_shutdown();
            // The engine's PSA runtime must remain usable after the borrower drains.
            Credentials verification;BorrowedPsaCrypto after(verification);std::array<std::byte,32> nonce{};RTC_CHECK(after.fresh(nonce));RTC_CHECK(after.stop());
            return true;
        }
        return false;
    }
};
Fixture* fixture{};bool done{};
void idle() noexcept {
    if(done)return;
    if(!fixture){const auto baseline=module_backing().total();void* storage=module_backing().allocate(sizeof(Fixture),alignof(Fixture),MemoryDomain::Backend);
        RTC_CHECK(storage);fixture=std::construct_at(static_cast<Fixture*>(storage),baseline);fixture->initialize();}
    if(!fixture->step())return;
    const auto baseline=fixture->baseline;std::destroy_at(fixture);module_backing().deallocate(fixture);fixture=nullptr;done=true;
    RTC_CHECK(module_backing().total()==baseline);
    std::printf("EGP_RTC_EMBEDDED_OK checks=%u associations=4 bindings=2 applied=2 callback_reentry_denied=%u parent_restored=1 engine_psa_survived=1\n",checks,reentries);
    std::fflush(stdout);SceneTree::get_singleton()->quit(0);
}
}
void superpos_egp_register_embedded_fixture() noexcept {
    // API generation also runs this editor. Only the explicit fixture process
    // may install the networking workload or request a successful exit.
    for(const String& argument:OS::get_singleton()->get_cmdline_user_args()) {
        if(argument=="--superpos-rtc-fixture") {
            SceneTree::add_idle_callback(superpos_egp::embedded_fixture::idle);
            return;
        }
    }
}
#endif
