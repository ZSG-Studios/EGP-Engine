// SPDX-License-Identifier: MIT
#include "superpos/service/rtc/adapter.hpp"
#include "executor_identity.hpp"
#include "processor_host.hpp"
#include "bounded_certificate.hpp"
#include "superpos_send.hpp"
#include <rtc/rtc.hpp>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>

namespace superpos::service::pairing {
namespace {
std::atomic<std::uint64_t> native_id_source{1};
namespace pa=::superpos::processor_admission;
namespace bc=rtc::impl::bounded_certificate;
struct Enter {bool& busy;explicit Enter(bool& b):busy(b){busy=true;}~Enter(){busy=false;}};
Error map(pa::Admission e)noexcept {
    switch(e){case pa::Admission::Accepted:return Error::None;
    case pa::Admission::Contended:return Error::Busy;
    case pa::Admission::Full:case pa::Admission::ByteLimit:return Error::CapacityExceeded;
    case pa::Admission::Exhausted:return Error::CounterExhausted;
    default:return Error::ChannelFailed;}
}
Error map(::superpos::certificate_completion::Error e)noexcept {
    using C=::superpos::certificate_completion::Error;
    switch(e){case C::NotReady:return Error::NotReady;case C::Contended:return Error::Busy;
    case C::Full:case C::ByteLimit:return Error::CapacityExceeded;case C::Exhausted:return Error::CounterExhausted;
    case C::OutOfMemory:return Error::OutOfMemory;case C::WrongThread:return Error::PermissionDenied;
    case C::Unsupported:return Error::Unsupported;default:return Error::ChannelFailed;}
}
struct StoredIce {
    struct Server {
        IceServerKind kind{};std::uint16_t port{};
        std::array<char,254> host{};
        std::array<char,257> username{},password{};
    };
    std::array<Server,3> servers{};
    std::size_t count{};
    bool relay_candidates_only{};
    std::uint16_t port_begin{1024},port_end{65535};
    ~StoredIce(){
        // No credential bytes survive in our fixed storage after destruction.
        auto* p=reinterpret_cast<volatile unsigned char*>(servers.data());
        for(std::size_t i=0;i<sizeof(servers);++i)p[i]=0;
    }
    Status capture(NativeIceConfig input)noexcept {
        if(input.servers.size()>servers.size())return fail(Error::CapacityExceeded);
        if(!input.servers.empty()&&!input.servers.data())return fail(Error::InvalidArgument);
        if(input.port_begin<1024||input.port_end<input.port_begin)return fail(Error::InvalidArgument);
        auto text=[](std::string_view value,std::size_t max,bool host){
            if(value.empty()||value.size()>max||!value.data())return false;
            for(unsigned char c:value)if(c==0||c<32||c==127||(host&&(c<=32||c>=127||c=='/'||c=='\\'||c=='@'||c=='?'||c=='#')))return false;
            return true;
        };
        unsigned stun=0,turn=0;
        for(const auto& value:input.servers){
            if(value.kind==IceServerKind::TurnTcp||value.kind==IceServerKind::TurnTls)return fail(Error::Unsupported);
            if(value.kind!=IceServerKind::Stun&&value.kind!=IceServerKind::TurnUdp)return fail(Error::InvalidArgument);
            if(!value.port||!text(value.host,253,true))return fail(Error::InvalidArgument);
            if(value.kind==IceServerKind::Stun){
                if(++stun>1)return fail(Error::CapacityExceeded);
                if(!value.username.empty()||!value.password.empty())return fail(Error::InvalidArgument);
            }else{
                if(++turn>2)return fail(Error::CapacityExceeded);
                if(!text(value.username,256,false)||!text(value.password,256,false))return fail(Error::InvalidArgument);
            }
            auto& target=servers[count++];target.kind=value.kind;target.port=value.port;
            std::copy(value.host.begin(),value.host.end(),target.host.begin());
            std::copy(value.username.begin(),value.username.end(),target.username.begin());
            std::copy(value.password.begin(),value.password.end(),target.password.begin());
        }
        if(input.relay_candidates_only&&!turn)return fail(Error::InvalidArgument);
        relay_candidates_only=input.relay_candidates_only;port_begin=input.port_begin;port_end=input.port_end;
        return {};
    }
    void apply(rtc::Configuration& config)const {
        config.iceTransportPolicy=relay_candidates_only?rtc::TransportPolicy::Relay:rtc::TransportPolicy::All;
        config.portRangeBegin=port_begin;config.portRangeEnd=port_end;
        config.iceServers.reserve(count);
        for(std::size_t i=0;i<count;++i){const auto& s=servers[i];
            if(s.kind==IceServerKind::Stun)config.iceServers.emplace_back(std::string(s.host.data()),s.port);
            else config.iceServers.emplace_back(std::string(s.host.data()),s.port,std::string(s.username.data()),std::string(s.password.data()),rtc::IceServer::RelayType::TurnUdp);
        }
    }
};
bool lane(CarrierLane r)noexcept{return r==CarrierLane::Control||r==CarrierLane::State;}
bool overlaps(const void* bytes,std::size_t size,const void* object,std::size_t extent)noexcept {
    if(!size)return false;
    const auto start=reinterpret_cast<std::uintptr_t>(bytes),base=reinterpret_cast<std::uintptr_t>(object);
    if(!bytes||size>UINTPTR_MAX-start)return true;
    return start<base?base-start<size:start-base<extent;
}
}
struct NativeAdapter::Storage {
    struct Row {
        std::uint64_t identity{};
        Offer offer{};
        CarrierLane role{};
        NativePhase phase{NativePhase::Created};
        Negotiation negotiation{};
        Error failure{};
        bool started{},retained{},channel_attempted{},local_issued{},remote_submitted{},remote_applied{};
        std::shared_ptr<rtc::PeerConnection> peer;
        std::shared_ptr<rtc::DataChannel> channel;
        DescriptionText local{},remote{};
        std::array<std::byte,960> received{};
        std::size_t received_size{};
        // Reserved before construction, retained even when a constructor's
        // unpublished Processor strand survives its exception unwind.
        std::optional<rtc::PeerConnection::SuperposRetirement> retirement;
    };
    StoredIce ice;
    std::optional<rtc::impl::ProcessorHost> host;
    std::optional<bc::Runtime> certificates;
    std::array<Row,native_association_capacity> rows{};
    std::size_t live{},peak{},cursor{};
    bool prewarmed{},host_closed{},certificates_closed{};
};
Status NativeAdapter::own()const noexcept {
    if(std::this_thread::get_id()!=owner_)return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);
    if(!storage_)return fail(Error::NotReady);
    return {};
}
Result<bool> NativeAdapter::allocated()const noexcept {
    if(std::this_thread::get_id()!=owner_)return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);return storage_!=nullptr;
}
NativeAdapter::~NativeAdapter(){
    if(std::this_thread::get_id()!=owner_||busy_||storage_)std::abort();
}
Status NativeAdapter::initialize(NativeIceConfig config)noexcept {
    static_assert(sizeof(Storage)<=8*1024*1024);
    if(std::this_thread::get_id()!=owner_)return fail(Error::PermissionDenied);
    if(busy_||storage_||closed_)return fail(Error::Busy);Enter enter(busy_);
    StoredIce captured;if(auto valid=captured.capture(config);!valid)return valid;
    void* memory=allocator_.allocate(sizeof(Storage),alignof(Storage),MemoryDomain::Backend);
    if(!memory)return fail(Error::OutOfMemory);
    storage_=std::construct_at(static_cast<Storage*>(memory));
    storage_->ice=captured;
    try {
        // The only host pointer is private. Record reservation before create
        // prevents this owner from retaining more than its 528 host slots.
        storage_->host.emplace(allocator_);
        storage_->certificates.emplace(allocator_);
        auto warmed=storage_->certificates->prewarm();
        if(!warmed){stopping_=true;return fail(Error::NotReady);}
        storage_->prewarmed=true;return {};
    }catch(const bc::Failure& error){stopping_=true;return fail(map(error.code()));}
     catch(const std::bad_alloc&){stopping_=true;return fail(Error::OutOfMemory);}
     catch(...){stopping_=true;return fail(Error::ChannelFailed);}
}
Result<Fingerprint> NativeAdapter::local_fingerprint()noexcept {
    if(auto ready=own();!ready)return fail(ready.error());if(stopping_)return fail(Error::NotReady);Enter enter(busy_);
    try {
        auto ticket=bc::Runtime::acquire_active(rtc::CertificateType::Default);if(!ticket)return fail(map(ticket.error()));
        const auto fp=ticket->get()->fingerprint();
        if(fp.algorithm!=rtc::CertificateFingerprint::Algorithm::Sha256||fp.value.size()!=95)return fail(Error::Unsupported);
        Fingerprint output{};
        auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
        for(std::size_t i=0;i<32;++i){const auto high=hex(fp.value[3*i]),low=hex(fp.value[3*i+1]);
            if(high<0||low<0||(i<31&&fp.value[3*i+2]!=':'))return fail(Error::NonCanonical);
            output[i]=std::byte((high<<4)|low);
        }
        if(!known_fingerprint(output))return fail(Error::AuthenticationFailed);return output;
    }catch(const bc::Failure& e){return fail(map(e.code()));}
     catch(const std::bad_alloc&){return fail(Error::OutOfMemory);}
     catch(...){return fail(Error::ChannelFailed);}
}
Result<std::size_t> NativeAdapter::find(std::uint64_t id)const noexcept {
    if(!id)return fail(Error::InvalidArgument);
    for(std::size_t i=0;i<storage_->rows.size();++i)if(storage_->rows[i].identity==id)return i;
    return fail(Error::StaleGeneration);
}
Result<std::uint64_t> NativeAdapter::create(const Offer& offer,CarrierLane role)noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    if(stopping_||!storage_->prewarmed)return fail(Error::NotReady);
    if(!lane(role)||!offer.pair.registry||!offer.pair.generation)return fail(Error::InvalidArgument);
    Enter enter(busy_);
    auto now=continuity_.observe(clock_);if(!now)return fail(now.error());
    if(now->proof_generation!=offer.continuity_generation)return fail(Error::StaleGeneration);
    if(now->now_us>=offer.expires_us)return fail(Error::Timeout);
    for(const auto& row:storage_->rows)if(row.identity&&row.offer.pair==offer.pair&&row.role==role)return fail(Error::Busy);
    std::size_t free=storage_->rows.size();
    for(std::size_t i=0;i<storage_->rows.size();++i)if(!storage_->rows[i].identity){free=i;break;}
    if(free==storage_->rows.size())return fail(Error::CapacityExceeded);
    auto group=storage_->host->reserveRetirement();
    using RS=rtc::impl::retirement::Status;
    if(group.status!=RS::Ready)return fail(group.status==RS::Exhausted?Error::CounterExhausted:
        group.status==RS::Full?Error::CapacityExceeded:Error::NotReady);
    auto id=detail::reserve_executor_identity(native_id_source);if(!id)return fail(id.error());
    auto& row=storage_->rows[free];row.identity=*id;row.offer=offer;row.role=role;
    row.retirement.emplace(std::move(group.token));
    try {
        rtc::Configuration config;
        config.superposRetirement=*row.retirement;
        config.superposControlAssociation=role==CarrierLane::Control;
        config.disableAutoNegotiation=true;config.maxMessageSize=960;config.mtu=1200;
        config.disableFingerprintVerification=false;
        storage_->ice.apply(config);
        row.peer=rtc::PeerConnection::superposCreate(std::move(config));
    }catch(const std::bad_alloc&){row.failure=Error::OutOfMemory;}
     catch(const rtc::impl::ProcessorFailure& e){row.failure=map(e.code());}
     catch(const bc::Failure& e){row.failure=map(e.code());}
     catch(...){row.failure=Error::ChannelFailed;}
    if(!row.peer){
        row.retirement->seal();
        if(row.retirement->quiescent()){const auto reason=row.failure;row=Storage::Row{};return fail(reason);}
        row.phase=NativePhase::Reclaiming; // async unpublished strand is still custody
    }
    // No error return while the group retains unpublished or published work.
    ++storage_->live;storage_->peak=std::max(storage_->peak,storage_->live);
    if(!first_identity_)first_identity_=*id;last_identity_=*id;
    if(row.peer){const auto retained=storage_->host->retain(row.peer);
        row.retained=retained==pa::Admission::Accepted;
        if(!row.retained)(void)begin_close(free,map(retained));}
    return *id;
}
Status NativeAdapter::start(std::uint64_t id,Negotiation mode)noexcept {
    if(auto ready=own();!ready)return ready;
    if(stopping_)return fail(Error::NotReady);
    if(mode!=Negotiation::Offerer&&mode!=Negotiation::Answerer)return fail(Error::InvalidArgument);
    auto index=find(id);if(!index)return fail(index.error());auto& row=storage_->rows[*index];
    if(row.phase!=NativePhase::Created||row.started)return fail(Error::Busy);
    row.negotiation=mode;row.started=true;return {};
}
Status NativeAdapter::submit_remote(std::uint64_t id,Negotiation sender,std::span<const char> bytes)noexcept {
    if(auto ready=own();!ready)return ready;
    if(stopping_)return fail(Error::NotReady);
    if(sender!=Negotiation::Offerer&&sender!=Negotiation::Answerer)return fail(Error::InvalidArgument);
    if(bytes.empty()||bytes.size()>signaling_bytes||std::find(bytes.begin(),bytes.end(),'\0')!=bytes.end())return fail(Error::InvalidArgument);
    auto index=find(id);if(!index)return fail(index.error());auto& row=storage_->rows[*index];
    if(!row.started||row.remote_submitted||row.phase==NativePhase::Closing||row.phase==NativePhase::Reclaiming)return fail(Error::Busy);
    if(sender==row.negotiation)return fail(Error::ProtocolViolation);
    const auto start=reinterpret_cast<std::uintptr_t>(storage_),end=start+sizeof(Storage);
    const auto input=reinterpret_cast<std::uintptr_t>(bytes.data());
    if(input>UINTPTR_MAX-bytes.size())return fail(Error::InvalidArgument);
    if(input<end&&input+bytes.size()>start)return fail(Error::InvalidArgument);
    std::copy(bytes.begin(),bytes.end(),row.remote.bytes.begin());row.remote.size=bytes.size();row.remote.sender=sender;
    row.remote_submitted=true;return {};
}
Result<DescriptionText> NativeAdapter::local_description(std::uint64_t id)const noexcept {
    if(auto ready=own();!ready)return fail(ready.error());auto index=find(id);if(!index)return fail(index.error());
    const auto& row=storage_->rows[*index];
    if(row.phase==NativePhase::Closing||row.phase==NativePhase::Reclaiming||!row.local.size)return fail(Error::NotReady);return row.local;
}
Result<NativeInfo> NativeAdapter::info(std::uint64_t id)const noexcept {
    if(auto ready=own();!ready)return fail(ready.error());auto index=find(id);if(!index)return fail(index.error());
    const auto& r=storage_->rows[*index];return NativeInfo{id,r.offer.pair,r.role,r.phase,r.failure,r.local.size!=0,r.remote_submitted};
}
Result<NativeCounts> NativeAdapter::counts()const noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    return NativeCounts{storage_->live,storage_->peak,sizeof(Storage),rtc::impl::ProcessorHost::metadata_bytes(),storage_->prewarmed,stopping_};
}
Status NativeAdapter::begin_close(std::size_t index,Error reason)noexcept {
    auto& row=storage_->rows[index];if(reason!=Error::None&&row.failure==Error::None)row.failure=reason;
    if(row.phase==NativePhase::Reclaiming)return {};
    row.phase=NativePhase::Closing;
    if(!row.peer){row.retirement->seal();row.phase=NativePhase::Reclaiming;return {};}
    try{row.peer->close();return {};}catch(...){return fail(Error::ChannelFailed);}
}
bool NativeAdapter::advance_close(std::size_t index)noexcept {
    auto& row=storage_->rows[index];
    if(row.phase==NativePhase::Closing){
        (void)begin_close(index,Error::None);
        (void)row.peer->superposTaskPoll();
        if(!row.peer->superposCloseComplete())return false;
        // Group was reserved before any owner construction. Complete close
        // seals it; custom deleters and strand reclamation retire its activity.
        row.retirement->seal();
        if(row.retained){
            if(storage_->host->release(row.peer)!=rtc::impl::ProcessorHost::Release::Released)return false;
            row.retained=false;
        }
        row.channel.reset();row.peer.reset();row.phase=NativePhase::Reclaiming;
    }
    if(row.phase!=NativePhase::Reclaiming||!row.retirement->quiescent())return false;
    row=Storage::Row{};--storage_->live;return true;
}
Status NativeAdapter::close_and_quiesce(std::uint64_t id)noexcept {
    if(auto ready=own();!ready)return ready;Enter enter(busy_);
    auto index=find(id);
    // IDs never recycle; this closed range consists only of this exclusive
    // adapter's successful creates. No per-ID tombstone allocation is needed.
    if(!index){if(id&&first_identity_&&id>=first_identity_&&id<=last_identity_)return {};return fail(index.error());}
    (void)begin_close(*index,Error::None);
    return advance_close(*index)?Status{}:Status(fail(Error::Busy));
}
Result<ObservedAssociation> NativeAdapter::observed(std::uint64_t id)const noexcept {
    if(auto ready=own();!ready)return fail(ready.error());auto index=find(id);if(!index)return fail(index.error());
    const auto& row=storage_->rows[*index];
    if(row.phase!=NativePhase::Ready&&row.phase!=NativePhase::Bound)return fail(Error::NotReady);
    if(!row.channel||!row.channel->isOpen()||row.peer->state()!=rtc::PeerConnection::State::Connected)return fail(Error::NotReady);
    const auto observed=row.peer->superposVerifiedIdentity();
    using S=rtc::PeerConnection::SuperposIdentity::Status;
    if(observed.status!=S::Ready)return fail(observed.status==S::Busy?Error::Busy:Error::AuthenticationFailed);
    return ObservedAssociation{row.role,id,observed.local,observed.remote};
}
Result<bool> NativeAdapter::try_send(std::uint64_t id,std::span<const std::byte> bytes)noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    if(bytes.empty())return fail(Error::InvalidArgument);
    if(bytes.size()>960)return fail(Error::CapacityExceeded);
    if(overlaps(bytes.data(),bytes.size(),this,sizeof(*this))||overlaps(bytes.data(),bytes.size(),storage_,sizeof(Storage)))return fail(Error::InvalidArgument);
    auto found=find(id);if(!found)return fail(found.error());auto& row=storage_->rows[*found];
    if(stopping_||row.phase!=NativePhase::Bound)return fail(Error::NotReady);
    Enter enter(busy_);
    if(!row.channel||!row.channel->isOpen()||row.peer->state()!=rtc::PeerConnection::State::Connected){(void)begin_close(*found,Error::ChannelFailed);return fail(Error::ChannelFailed);}
    try {
        (void)row.channel->send(bytes.data(),bytes.size());
        return true; // false from libdatachannel also transfers whole-frame ownership
    }catch(const rtc::impl::SuperposSendRejected& e){
        using Reason=rtc::impl::SuperposSendRejected::Reason;
        if(e.reason==Reason::Full||e.reason==Reason::Contended)return false;
        const auto error=e.reason==Reason::Counter?Error::CounterExhausted:Error::ChannelFailed;
        (void)begin_close(*found,error);return fail(error);
    }catch(...){(void)begin_close(*found,Error::UnknownOutcome);return fail(Error::UnknownOutcome);}
}
Result<std::size_t> NativeAdapter::receive_frame(std::uint64_t id,std::span<std::byte> bytes)noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    if(overlaps(bytes.data(),bytes.size(),this,sizeof(*this))||overlaps(bytes.data(),bytes.size(),storage_,sizeof(Storage)))return fail(Error::InvalidArgument);
    auto found=find(id);if(!found)return fail(found.error());auto& row=storage_->rows[*found];
    if(stopping_||row.phase!=NativePhase::Bound)return fail(Error::NotReady);Enter enter(busy_);
    if(!row.channel||!row.channel->isOpen()||row.peer->state()!=rtc::PeerConnection::State::Connected){(void)begin_close(*found,Error::ChannelFailed);return fail(Error::ChannelFailed);}
    try {
        if(!row.received_size){
            auto message=row.channel->receive();if(!message)return fail(Error::NotReady);
            const auto* binary=std::get_if<rtc::binary>(&*message);
            if(!binary||binary->empty()||binary->size()>row.received.size()){(void)begin_close(*found,Error::ProtocolViolation);return fail(Error::ProtocolViolation);}
            std::copy(binary->begin(),binary->end(),row.received.begin());row.received_size=binary->size();
        }
        if(bytes.size()<row.received_size)return fail(Error::CapacityExceeded);
        std::copy_n(row.received.begin(),row.received_size,bytes.begin());const auto size=row.received_size;row.received_size=0;return size;
    }catch(...){(void)begin_close(*found,Error::ChannelFailed);return fail(Error::ChannelFailed);}
}
Result<NativePathInfo> NativeAdapter::selected_path(std::uint64_t id)noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    Enter enter(busy_);
    auto index=find(id);if(!index)return fail(index.error());const auto& row=storage_->rows[*index];
    if(stopping_||(row.phase!=NativePhase::Ready&&row.phase!=NativePhase::Bound)||!row.peer)return fail(Error::NotReady);
    try {
        rtc::Candidate local,remote;
        if(!row.peer->getSelectedCandidatePair(&local,&remote))return fail(Error::NotReady);
        return NativePathInfo{local.type()==rtc::Candidate::Type::Relayed,remote.type()==rtc::Candidate::Type::Relayed};
    }catch(...){return fail(Error::ChannelFailed);}
}
Status NativeAdapter::activate(const Registry& registry,OwnershipToken token)noexcept {
    if(auto ready=own();!ready)return ready;if(stopping_)return fail(Error::NotReady);Enter enter(busy_);
    auto bound=registry.binding(token);if(!bound)return fail(bound.error());
    auto a=find(bound->control_association),b=find(bound->state_association);
    if(!a||!b)return fail(Error::StaleGeneration);auto& first=storage_->rows[*a];auto& second=storage_->rows[*b];
    if(first.offer.pair!=bound->offer.pair||second.offer.pair!=bound->offer.pair||first.role!=CarrierLane::Control||second.role!=CarrierLane::State)return fail(Error::AuthenticationFailed);
    if(first.phase!=NativePhase::Ready||second.phase!=NativePhase::Ready)return fail(Error::NotReady);
    auto now=continuity_.observe(clock_);if(!now)return fail(now.error());
    if(now->proof_generation!=bound->offer.continuity_generation||now->now_us>=bound->offer.expires_us)return fail(Error::Timeout);
    // sample() is an external callback. A Registry release during that call
    // keeps native custody (this adapter is busy), but revokes this binding.
    auto current=registry.binding(token);if(!current)return fail(current.error());
    if(current->offer.pair!=bound->offer.pair||current->control_association!=bound->control_association||
       current->state_association!=bound->state_association)return fail(Error::StaleGeneration);
    for(const auto* row:{&first,&second}){
        if(!row->channel||!row->channel->isOpen()||row->peer->state()!=rtc::PeerConnection::State::Connected)return fail(Error::NotReady);
        if(storage_->ice.relay_candidates_only){
            try {
                rtc::Candidate local,remote;
                if(!row->peer->getSelectedCandidatePair(&local,&remote))return fail(Error::NotReady);
                if(local.type()!=rtc::Candidate::Type::Relayed&&remote.type()!=rtc::Candidate::Type::Relayed)return fail(Error::PermissionDenied);
            }catch(...){return fail(Error::ChannelFailed);}
        }
        const auto identity=row->peer->superposVerifiedIdentity();
        using S=rtc::PeerConnection::SuperposIdentity::Status;
        if(identity.status!=S::Ready)return fail(identity.status==S::Busy?Error::Busy:Error::AuthenticationFailed);
        const auto& certificates=bound->offer.certificates;
        if(identity.local!=(row->role==CarrierLane::Control?certificates.local_control:certificates.local_state)||
           identity.remote!=(row->role==CarrierLane::Control?certificates.remote_control:certificates.remote_state))return fail(Error::AuthenticationFailed);
    }
    // Candidate inspection may synchronously call the external logger. Recheck
    // ownership after every such callback, immediately before publication.
    current=registry.binding(token);if(!current)return fail(current.error());
    if(current->offer.pair!=bound->offer.pair||current->control_association!=bound->control_association||
       current->state_association!=bound->state_association)return fail(Error::StaleGeneration);
    first.phase=second.phase=NativePhase::Bound;return {};
}
Result<NativePoll> NativeAdapter::poll(std::size_t quantum)noexcept {
    if(auto ready=own();!ready)return fail(ready.error());
    if(!quantum||quantum>8)return fail(Error::InvalidArgument);Enter enter(busy_);NativePoll out;
    if(storage_->host&&!storage_->host_closed)(void)storage_->host->poll(quantum);
    auto now=continuity_.observe(clock_);
    // Live includes Closing/Reclaiming rows; even an empty host must first pump
    // queued host/certificate work and observe continuity above.
    if(!storage_->live)return out;
    // Inspect at most one fixed metadata ring. Vacancies consume inspection
    // budget, not the bounded quota of live backend work. Never revisit a row.
    for(std::size_t inspected=0,work=0;inspected<storage_->rows.size()&&work<quantum;++inspected){
        const auto index=storage_->cursor;storage_->cursor=(index+1)%storage_->rows.size();
        auto& row=storage_->rows[index];if(!row.identity)continue;++work;++out.scanned;
        if(stopping_||(!now)||(row.phase!=NativePhase::Bound&&now->now_us>=row.offer.expires_us)||
           (row.phase!=NativePhase::Bound&&now->proof_generation!=row.offer.continuity_generation))
            (void)begin_close(index,!now?now.error():Error::Timeout);
        if(row.phase==NativePhase::Closing||row.phase==NativePhase::Reclaiming){out.closed+=advance_close(index);continue;}
        if(!row.started)continue;
        try {
            auto status=row.peer->superposTaskPoll();if(status!=pa::Admission::Accepted&&status!=pa::Admission::Contended){(void)begin_close(index,map(status));continue;}
            if(row.peer->state()==rtc::PeerConnection::State::Failed||row.peer->state()==rtc::PeerConnection::State::Closed){(void)begin_close(index,Error::ChannelFailed);continue;}
            if(row.channel&&row.channel->isClosed()){(void)begin_close(index,Error::ChannelFailed);continue;}
            if(!row.channel_attempted){
                row.channel_attempted=true;rtc::DataChannelInit init;init.id=0;init.negotiated=true;init.protocol="superpos.profile";
                init.reliability.unordered=true;init.reliability.maxRetransmits=0;
                row.channel=row.peer->createDataChannel(row.role==CarrierLane::Control?"control":"state",std::move(init));
                if(!row.channel){(void)begin_close(index,Error::ChannelFailed);continue;}
                row.phase=NativePhase::Negotiating;++out.progressed;continue;
            }
            if(row.remote_submitted&&!row.remote_applied){
                row.peer->setRemoteDescription(rtc::Description(std::string(row.remote.bytes.data(),row.remote.size),
                    row.remote.sender==Negotiation::Offerer?rtc::Description::Type::Offer:rtc::Description::Type::Answer));
                row.remote_applied=true;++out.progressed;continue;
            }
            if(!row.local_issued&&(row.negotiation==Negotiation::Offerer||row.remote_applied)){
                row.peer->setLocalDescription(row.negotiation==Negotiation::Offerer?rtc::Description::Type::Offer:rtc::Description::Type::Answer);
                row.local_issued=true;++out.progressed;continue;
            }
            if(row.local_issued&&!row.local.size&&row.peer->gatheringState()==rtc::PeerConnection::GatheringState::Complete){
                auto description=row.peer->localDescription();if(!description)continue;
                auto text=static_cast<std::string>(*description);
                if(text.empty()||text.size()>signaling_bytes){(void)begin_close(index,Error::CapacityExceeded);continue;}
                std::copy(text.begin(),text.end(),row.local.bytes.begin());row.local.size=text.size();row.local.sender=row.negotiation;++out.progressed;
            }
            if(row.phase!=NativePhase::Bound&&row.peer->state()==rtc::PeerConnection::State::Connected&&row.channel->isOpen())row.phase=NativePhase::Ready;
        }catch(const std::bad_alloc&){(void)begin_close(index,Error::OutOfMemory);}
         catch(...){(void)begin_close(index,Error::ChannelFailed);}
    }
    return out;
}
Status NativeAdapter::shutdown(std::chrono::steady_clock::time_point deadline)noexcept {
    if(std::this_thread::get_id()!=owner_)return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);if(!storage_)return {};Enter enter(busy_);stopping_=true;
    for(std::size_t i=0;i<storage_->rows.size();++i)if(storage_->rows[i].identity)(void)begin_close(i,Error::None);
    // Host loops call poll between retries. Never spin/wait for association IO here.
    if(storage_->live)return fail(Error::Busy);
    if(storage_->host&&!storage_->host_closed){
        if(storage_->host->shutdown(deadline)!=pa::Wait::Complete)return fail(Error::Busy);storage_->host_closed=true;
    }
    if(storage_->certificates&&!storage_->certificates_closed){
        if(!storage_->certificates->shutdown(deadline))return fail(Error::Busy);storage_->certificates_closed=true;
    }
    auto* old=storage_;storage_=nullptr;std::destroy_at(old);allocator_.deallocate(old);closed_=true;return {};
}
}
