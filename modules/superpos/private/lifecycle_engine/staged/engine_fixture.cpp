// SPDX-License-Identifier: MIT
#ifdef SUPERPOS_LIFECYCLE_FIXTURE
#include "native_receiver_access.hpp"
#include "superpos_session.h"
#include "private/module_memory.hpp"
#include "superpos_schema.h"
#include "core/os/os.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <thread>

namespace superpos_egp::lifecycle_engine::fixture {
unsigned checks{};
#define VERIFY(...) do{++checks;if(!(__VA_ARGS__)){std::printf("NATIVE_LIFECYCLE_FAIL line=%u checks=%u expression=%s\n",__LINE__,checks,#__VA_ARGS__);std::fflush(stdout);failed=true;SceneTree::get_singleton()->quit(1);return false;}}while(false)
class Replica final:public RefCounted {
    GDCLASS(Replica,RefCounted);
protected: static void _bind_methods(){}
public: std::array<std::byte,8> current{},staged{};bool live{};
};
class Factory final:public SuperposNativeFactory {
    GDCLASS(Factory,SuperposNativeFactory);
protected:
    static void _bind_methods(){}
    bool accepts_native(const Object& object) const noexcept override{return Object::cast_to<Replica>(&object)!=nullptr;}
    superpos::Status prepare_native(Object& object,superpos::ReplicaChangeKind kind,const superpos::CanonicalReplica& state) noexcept override {
        auto* value=Object::cast_to<Replica>(&object);if(!value || state.canonical.size()!=8)return superpos::fail(superpos::Error::InvalidArgument);
        std::copy(state.canonical.begin(),state.canonical.end(),value->staged.begin());pending_destroy=kind==superpos::ReplicaChangeKind::Destroy;return {};
    }
    void commit_native(Object& object,superpos::ReplicaChangeKind) noexcept override {auto* value=Object::cast_to<Replica>(&object);value->current=value->staged;value->live=!pending_destroy;++commits;}
    void abort_native(Object&,superpos::ReplicaChangeKind) noexcept override{}
    void destroy_native(Object& object) noexcept override{Object::cast_to<Replica>(&object)->live=false;++destroyed;}
public:
    Ref<Replica> replica;bool delayed{},pending_destroy{};unsigned started{},cancelled{},destroyed{},commits{},quiesced{};
    ObjectID close_other;Error close_result=OK;
    superpos::Status begin(const lifecycle::Construction&) noexcept override{++started;return {};}
    superpos::Result<lifecycle::Instance> poll(const lifecycle::Construction&) noexcept override {
        if(delayed)return superpos::fail(superpos::Error::NotReady);return lifecycle::Instance{uint64_t(replica->get_instance_id())};
    }
    superpos::Status cancel(const lifecycle::Construction&) noexcept override {
        ++cancelled;if(auto* other=Object::cast_to<SuperposSession>(ObjectDB::get_instance(close_other)))close_result=other->close_checked();return {};
    }
    superpos::Status quiesce_native() noexcept override {++quiesced;delayed=false;return {};}
};
// Native schema destruction is real Session cleanup user code. It drops the
// last external Session reference while _finish_retirement still uses its Impl.
class DropSchema final:public SuperposSchema {
    GDCLASS(DropSchema,SuperposSchema);
protected: static void _bind_methods(){}
public:
    Ref<SuperposSession>* recipient{};unsigned* drops{};bool* remained_live{};
    ~DropSchema(){if(recipient){const auto id=(*recipient)->get_instance_id();recipient->unref();++*drops;*remained_live=ObjectDB::get_instance(id)!=nullptr;}}
};
struct Packet{std::array<std::byte,960> bytes{};std::size_t size{};};
// Explicit fake authenticated carrier for engine ownership tests. DTLS/RTC
// authentication is separately qualified; no cryptography claim is made here.
struct Carrier final:superpos::TransportProvider {
    Carrier* remote{};std::array<Packet,64> packets{};std::size_t head{},count{};
    superpos::TransportCapabilities capabilities() const noexcept override{return {true,true,true,false,960};}
    bool ready() const noexcept override{return true;}
    superpos::Status advance() noexcept override{return {};}
    superpos::Status send(std::span<const std::byte> bytes) noexcept override {
        if(bytes.size()>960 || remote->count==64)return superpos::fail(superpos::Error::CapacityExceeded);
        auto& p=remote->packets[(remote->head+remote->count)%64];std::copy(bytes.begin(),bytes.end(),p.bytes.begin());p.size=bytes.size();++remote->count;return {};
    }
    superpos::Result<std::size_t> receive(std::span<std::byte> bytes) noexcept override {
        if(!count)return superpos::fail(superpos::Error::Busy);auto& p=packets[head];if(bytes.size()<p.size)return superpos::fail(superpos::Error::Truncated);
        std::copy_n(p.bytes.begin(),p.size,bytes.begin());const auto size=p.size;head=(head+1)%64;--count;return size;
    }
};
struct Run {
    std::array<Carrier,2> carriers;
    std::array<Ref<SuperposSession>,2> sessions;
    Ref<Factory> factory,other_factory;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    unsigned scenario{},stage{},ticks{},cleanup_drops{};bool failed{},cleanup_remained_live{};DropSchema* drop_schema{};ObjectID departed;std::size_t baseline{};
    bool create(bool delay){
        carriers[0].remote=&carriers[1];carriers[1].remote=&carriers[0];
        Ref<SuperposField> field;field.instantiate();field->set_field_id(1);field->set_codec_id(3);field->set_max_bytes(8);
        Ref<SuperposSchema> schema;if(scenario==5){Ref<DropSchema> custom;custom.instantiate();drop_schema=custom.ptr();schema=custom;}else schema.instantiate();schema->set_schema_id(19);TypedArray<SuperposField> fields;fields.append(field);schema->set_fields(fields);
        TypedArray<SuperposSchema> schemas;schemas.append(schema);
        for(unsigned i=0;i<2;++i){sessions[i].instantiate();VERIFY(sessions[i]->configure(schemas,4,7,1,1<<20)==OK);
            superpos::SessionConfig config;config.session_id=42+scenario;config.epoch=8;config.local_peer=i+1;config.remote_peer=2-i;config.logical_channels=3;
            config.channel_modes[0]=superpos::DeliveryMode::ReliableOrdered;config.channel_modes[1]=config.channel_modes[2]=superpos::DeliveryMode::ReliableUnordered;
            config.channel_purposes[0]=superpos::ChannelPurpose::Control;
            VERIFY(SuperposNativeReceiverAccess::bind_fixture_transport(*sessions[i].ptr(),carriers[i],config)==OK);}
        factory.instantiate();factory->replica.instantiate();factory->delayed=delay;return true;
    }
    bool send(superpos::ReplicaWireMessage message,unsigned channel,unsigned source=0){
        message.context={7,8,9};std::array<std::byte,8192> bytes{};auto encoded=superpos::encode_replica_message(message,bytes);if(!encoded){std::printf("NATIVE_LIFECYCLE_WIRE scenario=%u kind=%u error=%u\n",scenario,unsigned(message.kind),unsigned(encoded.error()));std::fflush(stdout);}VERIFY(encoded);
        PackedByteArray packet;VERIFY(packet.resize(*encoded)==OK);std::memcpy(packet.ptrw(),bytes.data(),*encoded);
        auto result=sessions[source]->enqueue_packet(packet,channel);VERIFY(int(result["error"])==OK);return true;
    }
    bool begin(){
        superpos::ReceiverConfig config;config.authority_epoch=7;config.connection_epoch=8;config.replica_epoch=9;config.peer=2;config.maximum_active=4;config.maximum_transitions=4;config.state_stride=8;
        Registration registration{19,101,sizeof(Replica),factory};
        if(scenario==3){
            other_factory.instantiate();other_factory->replica.instantiate();other_factory->delayed=true;
            superpos::ReplicaKey key{1,1,7,8,9,1};superpos::ReplicaWireMessage bind;bind.kind=superpos::ReplicaWireKind::Bind;bind.binding={key,superpos::ObjectHandle::from_parts(71,1),19,1,1,1};VERIFY(send(bind,0,1));
            std::array<std::byte,8> state{};state[0]=std::byte{43};superpos::ReplicaWireMessage offer;offer.kind=superpos::ReplicaWireKind::BaselineOffer;offer.sequence=1;offer.baseline={key,0,1,1,state};offer.tick=1;VERIFY(send(offer,2,1));
        }
        VERIFY(SuperposNativeReceiverAccess::attach(*sessions[1].ptr(),std::span(&registration,1),config,{}, {4,4,1,4096})==OK);
        VERIFY(SuperposNativeReceiverAccess::attach(*sessions[1].ptr(),std::span(&registration,1),config,{}, {4,4,1,4096})==ERR_ALREADY_IN_USE);
        VERIFY(int(sessions[1]->read_packet(0)["error"])==ERR_BUSY);
        superpos::ReplicaKey key{1,1,7,8,9,1};superpos::ReplicaWireMessage bind;bind.kind=superpos::ReplicaWireKind::Bind;bind.binding={key,superpos::ObjectHandle::from_parts(70,1),19,2,1,1};VERIFY(send(bind,0));
        std::array<std::byte,8> state{};state[0]=std::byte{42};superpos::ReplicaWireMessage offer;offer.kind=superpos::ReplicaWireKind::BaselineOffer;offer.sequence=1;offer.baseline={key,0,1,1,state};offer.tick=1;VERIFY(send(offer,2));
        if(scenario==3){config.peer=1;Registration other{19,102,sizeof(Replica),other_factory};VERIFY(SuperposNativeReceiverAccess::attach(*sessions[0].ptr(),std::span(&other,1),config,{}, {4,4,1,4096})==OK);}
        return true;
    }
    bool step(){
        if(failed)return false;
        if(std::chrono::steady_clock::now()>=deadline){
            std::printf("NATIVE_LIFECYCLE_TIMEOUT scenario=%u stage=%u started=%u commits=%u cancelled=%u\n",scenario,stage,factory.is_valid()?factory->started:0,factory.is_valid()?factory->commits:0,factory.is_valid()?factory->cancelled:0);std::fflush(stdout);
        }
        VERIFY(std::chrono::steady_clock::now()<deadline);
        if(stage==0){baseline=module_backing().total();VERIFY(create(scenario!=0));stage=1;}
        for(auto& session:sessions)if(session.is_valid()){const auto result=session->advance_tick();
            if(result!=OK && result!=ERR_BUSY){std::printf("NATIVE_LIFECYCLE_PUMP scenario=%u stage=%u error=%d last=%d state=%s\n",scenario,stage,int(result),int(session->get_last_error()),session->get_state().utf8().get_data());std::fflush(stdout);}
            VERIFY(result==OK || result==ERR_BUSY);}
        if(stage==1){if(sessions[0]->get_state()!=String("NetworkReady") || sessions[1]->get_state()!=String("NetworkReady"))return false;VERIFY(begin());stage=2;}
        if(stage==2){
            if((scenario==0 && !factory->commits) || (scenario!=0 && !factory->started) || (scenario==3 && !other_factory->started))return false;
            departed=sessions[1]->get_instance_id();
            if(scenario==0){VERIFY(factory->replica->current[0]==std::byte{42});VERIFY(sessions[1]->close_checked()==ERR_BUSY);}
            else if(scenario==1){sessions[1].unref();VERIFY(ObjectDB::get_instance(departed)==nullptr);}
            else if(scenario==2){VERIFY(SuperposNativeReceiverAccess::shutdown()==OK);VERIFY(factory->quiesced==1);}
            else if(scenario==3){factory->close_other=sessions[0]->get_instance_id();VERIFY(sessions[0]->close_checked()==ERR_BUSY);VERIFY(sessions[1]->close_checked()==ERR_BUSY);}
            else if(scenario==5){VERIFY(sessions[0]->close_checked()==OK);sessions[0].unref();drop_schema->recipient=&sessions[1];drop_schema->drops=&cleanup_drops;drop_schema->remained_live=&cleanup_remained_live;SuperposNativeReceiverAccess::retire_fixture_owner(*sessions[1].ptr());}
            else {Ref<SuperposSession> transferred=sessions[1];sessions[1].unref();std::thread worker([&]{transferred.unref();});worker.join();
                SuperposManagedReload::drain_native_references();VERIFY(ObjectDB::get_instance(departed)==nullptr);}
            stage=3;
        }
        if(stage==3){
            SuperposNativeReceiverAccess::drain();
            if(scenario==0)VERIFY(factory->destroyed==1);
            else VERIFY(factory->cancelled==1);
            if(scenario==3)VERIFY(factory->close_result==ERR_BUSY && other_factory->cancelled==1);
            if(scenario==5)VERIFY(cleanup_drops==1 && cleanup_remained_live && sessions[1].is_null() && ObjectDB::get_instance(departed)==nullptr);
            for(auto& session:sessions)if(session.is_valid()){VERIFY(session->close_checked()==OK || session->get_state()==String("Closed"));session.unref();}
            SuperposNativeReceiverAccess::drain();factory.unref();other_factory.unref();VERIFY(module_backing().total()==baseline);
            if(++scenario==6)return true;
            for(auto& carrier:carriers){carrier.head=carrier.count=0;}stage=0;
        }
        return false;
    }
};
Run* run{};std::size_t parent_baseline{};
void idle(){if(!run){parent_baseline=module_backing().total();void* memory=module_backing().allocate(sizeof(Run),alignof(Run),superpos::MemoryDomain::Backend);if(!memory){SceneTree::get_singleton()->quit(1);return;}run=std::construct_at(static_cast<Run*>(memory));}
 if(run->step()){std::destroy_at(run);module_backing().deallocate(run);run=nullptr;
  if(module_backing().total()!=parent_baseline){std::puts("NATIVE_LIFECYCLE_FAIL parent allocation imbalance");SceneTree::get_singleton()->quit(1);return;}
  std::printf("EGP_NATIVE_LIFECYCLE_OK checks=%u cases=6 cleanup_last_ref=1 last_ref=1 worker_last_ref=1 shutdown=1 reentrant_close=1 parent_restored=1 mock_carrier=1\n",checks);std::fflush(stdout);SceneTree::get_singleton()->quit(0);}}

}
void superpos_register_lifecycle_fixture(){for(const String& arg:OS::get_singleton()->get_cmdline_user_args())if(arg=="--superpos-lifecycle-fixture"){
    GDREGISTER_CLASS(superpos_egp::lifecycle_engine::fixture::DropSchema);GDREGISTER_CLASS(superpos_egp::lifecycle_engine::fixture::Replica);GDREGISTER_CLASS(superpos_egp::lifecycle_engine::fixture::Factory);
    SceneTree::add_idle_callback(superpos_egp::lifecycle_engine::fixture::idle);return;}}
#endif
