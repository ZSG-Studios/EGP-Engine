/**
 * Copyright (c) 2019 Paul-Louis Ageneau
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "peerconnection.hpp"
#include "certificate.hpp"
#include "dtlstransport.hpp"
#include "icetransport.hpp"
#include "internals.hpp"
#include "logcounter.hpp"
#include "peerconnection.hpp"
#include "processor.hpp"
#include "rtp.hpp"
#include "sctptransport.hpp"
#include "utils.hpp"
#if RTC_SUPERPOS_PROFILE
#include "profile_boundary.hpp"
#endif

#if RTC_ENABLE_MEDIA
#include "dtlssrtptransport.hpp"
#endif

#include <algorithm>
#include <array>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>

using namespace std::placeholders;

namespace rtc::impl {
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
void superposPeerBeforeChannelAllocation(PeerConnection*);
void superposPeerBeforePublicWrapper(PeerConnection*);
void superposPeerUnwindPhase(PeerConnection*,unsigned);
void superposPeerLifecyclePhase(PeerConnection*,unsigned) noexcept;
void superposSignalingBeforePublication(PeerConnection*,unsigned) noexcept;
#endif

static LogCounter COUNTER_MEDIA_TRUNCATED(plog::warning,
                                          "Number of truncated RTP packets over past second");
static LogCounter COUNTER_SRTP_DECRYPT_ERROR(plog::warning,
                                             "Number of SRTP decryption errors over past second");
static LogCounter COUNTER_SRTP_ENCRYPT_ERROR(plog::warning,
                                             "Number of SRTP encryption errors over past second");
static LogCounter
    COUNTER_UNKNOWN_PACKET_TYPE(plog::warning,
                                "Number of unknown RTCP packet types over past second");

const string PemBeginCertificateTag = "-----BEGIN CERTIFICATE-----";

namespace {
bool decodeSha256Fingerprint(std::string_view text, std::array<std::byte,32>& output) noexcept {
    if(text.size()!=95)return false;
    const auto digit=[](char c) noexcept -> int {
        if(c>='0'&&c<='9')return c-'0';
        if(c>='A'&&c<='F')return c-'A'+10;
        if(c>='a'&&c<='f')return c-'a'+10;
        return -1;
    };
    std::array<std::byte,32> decoded{};
    for(std::size_t i=0;i<decoded.size();++i){
        const int high=digit(text[i*3]), low=digit(text[i*3+1]);
        if(high<0||low<0||(i!=31&&text[i*3+2]!=':'))return false;
        decoded[i]=std::byte((high<<4)|low);
    }
    output=decoded;return true;
}
} // namespace

PeerConnection::PeerConnection(Configuration config_) : config(std::move(config_)), mProcessor(0,config.superposControlAssociation,config.superposRetirement) {
	PLOG_VERBOSE << "Creating PeerConnection";

#if RTC_SUPERPOS_TASK_PROFILE
	if(config.certificatePemFile || config.keyPemFile || config.keyPemPass)
		throw bounded_certificate::Failure(bounded_certificate::cc::Error::Unsupported);
	mCertificate=make_certificate(config.certificateType);
#else
	if (config.certificatePemFile && config.keyPemFile) {
		std::promise<certificate_ptr> cert;
		cert.set_value(std::make_shared<Certificate>(
		    config.certificatePemFile->find(PemBeginCertificateTag) != string::npos
		        ? Certificate::FromString(*config.certificatePemFile, *config.keyPemFile)
		        : Certificate::FromFile(*config.certificatePemFile, *config.keyPemFile,
		                                config.keyPemPass.value_or(""))));
		mCertificate = cert.get_future();
	} else if (!config.certificatePemFile && !config.keyPemFile) {
		mCertificate = make_certificate(config.certificateType);
	} else {
		throw std::invalid_argument(
		    "Either none or both certificate and key PEM files must be specified");
	}

#endif
	if (config.portRangeEnd && config.portRangeBegin > config.portRangeEnd)
		throw std::invalid_argument("Invalid port range");

	if (config.mtu) {
		if (*config.mtu < 576) // Min MTU for IPv4
			throw std::invalid_argument("Invalid MTU value");

		if (*config.mtu > 1500) { // Standard Ethernet
			PLOG_WARNING << "MTU set to " << *config.mtu;
		} else {
			PLOG_VERBOSE << "MTU set to " << *config.mtu;
		}
	}
}

PeerConnection::~PeerConnection() {
	PLOG_VERBOSE << "Destroying PeerConnection";
	mProcessor.join();
}

void PeerConnection::close() {
    if(config.superposRetirement)config.superposRetirement.seal();
#if RTC_SUPERPOS_PROFILE
    mSuperposChannelAdmission.exchange(::superpos::rtc_profile::ChannelState::Sealed,std::memory_order_acq_rel);
#endif
    mTaskCloseRequested.store(true);closeTransports();
}
void PeerConnection::remoteClose() {close();}

optional<Description> PeerConnection::localDescription() const {
	std::lock_guard lock(mLocalDescriptionMutex);
	return mLocalDescription;
}

optional<Description> PeerConnection::remoteDescription() const {
	std::lock_guard lock(mRemoteDescriptionMutex);
	return mRemoteDescription;
}

size_t PeerConnection::remoteMaxMessageSize() const {
	const size_t localMax = config.maxMessageSize.value_or(DEFAULT_LOCAL_MAX_MESSAGE_SIZE);

	size_t remoteMax = DEFAULT_REMOTE_MAX_MESSAGE_SIZE;
	std::lock_guard lock(mRemoteDescriptionMutex);
	if (mRemoteDescription)
		if (auto *application = mRemoteDescription->application())
			if (auto max = application->maxMessageSize()) {
				// RFC 8841: If the SDP "max-message-size" attribute contains a maximum message
				// size value of zero, it indicates that the SCTP endpoint will handle messages
				// of any size, subject to memory capacity, etc.
				remoteMax = *max > 0 ? *max : std::numeric_limits<size_t>::max();
			}

	return std::min(remoteMax, localMax);
}

// Helper for PeerConnection::initXTransport methods: start and emplace the transport
template <typename T>
shared_ptr<T> emplaceTransport(PeerConnection *pc, shared_ptr<T> *member, shared_ptr<T> transport) {
	std::atomic_store(member, transport);
	try {
		transport->start();
	} catch (...) {
		std::atomic_store(member, decltype(transport)(nullptr));
		throw;
	}

	if (pc->closing.load() || pc->state.load() == PeerConnection::State::Closed) {
		std::atomic_store(member, decltype(transport)(nullptr));
		transport->stop();
		return nullptr;
	}

	return transport;
}

shared_ptr<IceTransport> PeerConnection::initIceTransport() {
	try {
		if (auto transport = std::atomic_load(&mIceTransport))
			return transport;

		PLOG_VERBOSE << "Starting ICE transport";

		auto transport = retirement::make_owned<IceTransport>(config.superposRetirement,
		    config, weak_bind(&PeerConnection::processLocalCandidate, this, _1),
		    [this, weak_this = weak_from_this()](IceTransport::State transportState) {
			    if (auto locked = weak_this.lock())
				    std::invoke([=]() {
					    switch (transportState) {
					    case IceTransport::State::Connecting:
						    changeIceState(IceState::Checking);
						    changeState(State::Connecting);
						    break;
					    case IceTransport::State::Connected:
						    changeIceState(IceState::Connected);
						    if (remoteDescription())
							    requestTransportInit(InitDtls);
						    break;
					    case IceTransport::State::Completed:
						    changeIceState(IceState::Completed);
						    break;
					    case IceTransport::State::Failed:
						    changeIceState(IceState::Failed);
						    changeState(State::Failed);
						    remoteClose();
						    break;
					    case IceTransport::State::Disconnected:
						    changeIceState(IceState::Disconnected);
						    changeState(State::Disconnected);
						    remoteClose();
						    break;
					    default:
						    // Ignore
						    break;
					    }
				    });
		    },
		    [this, weak_this = weak_from_this()](IceTransport::GatheringState gatheringState) {
			    if (auto locked = weak_this.lock())
				    std::invoke([=]() {
					    switch (gatheringState) {
					    case IceTransport::GatheringState::InProgress:
						    changeGatheringState(GatheringState::InProgress);
						    break;
					    case IceTransport::GatheringState::Complete:
						    endLocalCandidates();
						    changeGatheringState(GatheringState::Complete);
						    break;
					    default:
						    // Ignore
						    break;
					    }
				    });
		    });

		return emplaceTransport(this, &mIceTransport, std::move(transport));

	} catch (const std::exception &e) {
		PLOG_ERROR << e.what();
		changeState(State::Failed);
		throw std::runtime_error("ICE transport initialization failed");
	}
}

shared_ptr<DtlsTransport> PeerConnection::initDtlsTransport() {
	try {
		std::lock_guard lock(mDtlsTransportInitMutex);
		if(mTaskCloseRequested.load(std::memory_order_acquire)||closing.load())return nullptr;
		if (auto transport = std::atomic_load(&mDtlsTransport))
			return transport;

		PLOG_VERBOSE << "Starting DTLS transport";

		CertificateFingerprint::Algorithm fingerprintAlgorithm;
		{
			std::lock_guard lock(mRemoteDescriptionMutex);
			if (mRemoteDescription && mRemoteDescription->fingerprint()) {
				mRemoteFingerprintAlgorithm = mRemoteDescription->fingerprint()->algorithm;
			}
			fingerprintAlgorithm = mRemoteFingerprintAlgorithm;
		}

		auto lower = std::atomic_load(&mIceTransport);
		if (!lower)
			throw std::logic_error("No underlying ICE transport for DTLS transport");

		auto certificate = mCertificate.get();
        const auto localFingerprint = certificate->fingerprint();
        {
            std::lock_guard identityLock(mRemoteDescriptionMutex);
            mSuperposLocalIdentityValid =
                localFingerprint.algorithm == CertificateFingerprint::Algorithm::Sha256 &&
                decodeSha256Fingerprint(localFingerprint.value, mSuperposLocalIdentity);
        }
		auto verifierCallback = weak_bind(&PeerConnection::checkFingerprint, this, _1);
		auto dtlsStateChangeCallback = [this, weak_this = weak_from_this()](
		                                   DtlsTransport::State transportState) {
			if (auto locked = weak_this.lock())
				std::invoke([=]() {
					switch (transportState) {
					case DtlsTransport::State::Connected:
						if (auto remote = remoteDescription(); remote && remote->hasApplication())
							requestTransportInit(InitSctp);
						else
							changeState(State::Connected);

						requestTask(OpenMedia);
						break;
					case DtlsTransport::State::Failed:
						changeState(State::Failed);
						remoteClose();
						break;
					case DtlsTransport::State::Disconnected:
						changeState(State::Disconnected);
						remoteClose();
						break;
					default:
						// Ignore
						break;
					}
				});
		};

		shared_ptr<DtlsTransport> transport;
		auto local = localDescription();
		if (config.forceMediaTransport || (local && local->hasAudioOrVideo())) {
#if RTC_ENABLE_MEDIA
			PLOG_INFO << "This connection requires media support";

			// DTLS-SRTP
			transport = std::make_shared<DtlsSrtpTransport>(
			    lower, certificate, config.mtu, fingerprintAlgorithm, verifierCallback,
			    weak_bind(&PeerConnection::forwardMedia, this, _1), dtlsStateChangeCallback);
#else
			PLOG_WARNING << "Ignoring media support (not compiled with media support)";
#endif
		}

		if (!transport) {
			// DTLS only
			transport = retirement::make_owned<DtlsTransport>(config.superposRetirement,lower, certificate, config.mtu,
			                                            fingerprintAlgorithm, verifierCallback,
			                                            dtlsStateChangeCallback,config.superposControlAssociation,config.superposRetirement);
		}

		return emplaceTransport(this, &mDtlsTransport, std::move(transport));

    } catch (const std::exception &e) {
        if(auto* rejected=dynamic_cast<const ProcessorFailure*>(&e);
           rejected && rejected->code()==pa::Admission::Contended){
            requestTransportInit(InitDtls);return nullptr;
        }
#ifdef RTC_SUPERPOS_PROCESSOR_DIAGNOSTICS
        if(auto* rejected=dynamic_cast<const ProcessorFailure*>(&e)){
            std::printf("RTC_PROCESSOR_INIT_REJECTION 2 %u\n",unsigned(rejected->code()));std::fflush(stdout);
        }
#endif
        PLOG_ERROR << e.what();
        changeState(State::Failed);
        throw std::runtime_error("DTLS transport initialization failed");
	}
}

shared_ptr<SctpTransport> PeerConnection::initSctpTransport() {
	try {
		std::lock_guard lock(mSctpTransportInitMutex);
		if(mTaskCloseRequested.load(std::memory_order_acquire)||closing.load())return nullptr;
		if (auto transport = std::atomic_load(&mSctpTransport))
			return transport;

		PLOG_VERBOSE << "Starting SCTP transport";

		auto lower = std::atomic_load(&mDtlsTransport);
		if (!lower)
			throw std::logic_error("No underlying DTLS transport for SCTP transport");

		auto local = localDescription();
		if (!local || !local->application())
			throw std::logic_error("Starting SCTP transport without local application description");

		auto remote = remoteDescription();
		if (!remote || !remote->application())
			throw std::logic_error(
			    "Starting SCTP transport without remote application description");

		SctpTransport::Ports ports = {};
		ports.local = local->application()->sctpPort().value_or(DEFAULT_SCTP_PORT);
		ports.remote = remote->application()->sctpPort().value_or(DEFAULT_SCTP_PORT);

		auto transport = retirement::make_owned<SctpTransport>(config.superposRetirement,
		    lower, config, std::move(ports), weak_bind(&PeerConnection::forwardMessage, this, _1),
		    weak_bind(&PeerConnection::forwardBufferedAmount, this, _1, _2),
		    [this, weak_this = weak_from_this()](SctpTransport::State transportState) {
			    if (auto locked = weak_this.lock())
				    std::invoke([=]() {
					    switch (transportState) {
					    case SctpTransport::State::Connected:
						    changeState(State::Connected);
						    assignDataChannels();
						    requestTask(OpenData);
						    break;
					    case SctpTransport::State::Failed:
						    changeState(State::Failed);
						    remoteClose();
						    break;
					    case SctpTransport::State::Disconnected:
						    changeState(State::Disconnected);
						    remoteClose();
						    break;
					    default:
						    // Ignore
						    break;
					    }
				    });
		    });

		return emplaceTransport(this, &mSctpTransport, std::move(transport));

	} catch (const std::exception &e) {
        if(auto* rejected=dynamic_cast<const ProcessorFailure*>(&e);
           rejected && rejected->code()==pa::Admission::Contended){
            requestTransportInit(InitSctp);return nullptr;
        }
		PLOG_ERROR << e.what();
		changeState(State::Failed);
		throw std::runtime_error("SCTP transport initialization failed");
	}
}

shared_ptr<IceTransport> PeerConnection::getIceTransport() const {
	return std::atomic_load(&mIceTransport);
}

shared_ptr<DtlsTransport> PeerConnection::getDtlsTransport() const {
	return std::atomic_load(&mDtlsTransport);
}

shared_ptr<SctpTransport> PeerConnection::getSctpTransport() const {
	return std::atomic_load(&mSctpTransport);
}

PeerConnection::ScalarReserved PeerConnection::reserveStateEvent() noexcept {
    if(mStateEventsClosed.load(std::memory_order_acquire))return {pa::Admission::Closed,0};
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
    if(mTestStateBeforePause.load(std::memory_order_acquire)){
        mTestStateBeforeEntered.store(true,std::memory_order_release);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!mTestStateBeforeRelease.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    }
#endif
    auto active=mStateAdmissions.load(std::memory_order_seq_cst);bool admitted=false;
    for(unsigned n=0;n!=16;++n){
        if(active>=16)return {mProcessor.note(pa::Admission::Full),0};
        if(mStateAdmissions.compare_exchange_weak(active,active+1,std::memory_order_seq_cst)){admitted=true;break;}
    }
    if(!admitted)return {mProcessor.note(pa::Admission::Contended),0};
    struct Entry{PeerConnection* self;~Entry(){self->mStateAdmissions.fetch_sub(1,std::memory_order_seq_cst);}} entry{this};
    // SC entry/closed ordering prevents lifecycle's empty check from passing
    // a producer that observed open before its head reservation.
    if(mStateEventsClosed.load(std::memory_order_seq_cst))return {pa::Admission::Closed,0};
    auto head=mStateHead.load(std::memory_order_relaxed);
    for(unsigned attempt=0;attempt!=16;++attempt){
        const auto tail=mStateTail.load(std::memory_order_acquire);
        if(head<tail){head=mStateHead.load(std::memory_order_acquire);continue;}
        if(head==UINT64_MAX)return {mProcessor.note(pa::Admission::Exhausted),0};
        if(head-tail>=StateEventCapacity)return {mProcessor.note(pa::Admission::Full),0};
        if(mStateHead.compare_exchange_weak(head,head+1,std::memory_order_acq_rel)){
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
            if(mTestStatePause.load(std::memory_order_acquire)){
                mTestStateEntered.store(true,std::memory_order_release);
                const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(!mTestStateRelease.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            }
#endif
            return {pa::Admission::Accepted,head};
        }
    }
    return {mProcessor.note(pa::Admission::Contended),0};
}
pa::Admission PeerConnection::publishStateEvent(ScalarReserved reserved,unsigned kind,unsigned value) noexcept {
    if(reserved.status!=pa::Admission::Accepted)return reserved.status;
    const bool closed=mStateEventsClosed.load(std::memory_order_acquire);
    auto& record=mStateEvents[reserved.position%StateEventCapacity];
    record.kind=closed?4:kind;record.value=value;record.ready.store(true,std::memory_order_release);
    if(!closed)requestTask(StateEvents);
    return closed?pa::Admission::Closed:pa::Admission::Accepted;
}
pa::Admission PeerConnection::queueStateEvent(unsigned kind,unsigned value) noexcept {
    return publishStateEvent(reserveStateEvent(),kind,value);
}

void PeerConnection::drainStateEvents() {
    for(unsigned n=0;n!=StateEventCapacity;++n){
        auto tail=mStateTail.load(std::memory_order_relaxed);
        if(tail==mStateHead.load(std::memory_order_acquire))break;
        auto& record=mStateEvents[tail%StateEventCapacity];
        if(!record.ready.load(std::memory_order_acquire))break;
        const auto kind=record.kind,value=record.value;
        record.ready.store(false,std::memory_order_release);mStateTail.store(tail+1,std::memory_order_release);
        try{switch(kind){case 0:trigger(&stateChangeCallback,State(value));break;
            case 1:trigger(&iceStateChangeCallback,IceState(value));break;
            case 2:trigger(&gatheringStateChangeCallback,GatheringState(value));break;
            case 3:trigger(&signalingStateChangeCallback,SignalingState(value));break;
            case 4:break;default:std::terminate();}}
        catch(...){mProcessor.note(pa::Admission::Unsupported);}
    }
    const auto tail=mStateTail.load(std::memory_order_relaxed);
    if(tail!=mStateHead.load(std::memory_order_acquire)&&mStateEvents[tail%StateEventCapacity].ready.load(std::memory_order_acquire))mTaskWork.fetch_or(StateEvents,std::memory_order_release);
}
#if RTC_SUPERPOS_PROFILE
PeerConnection::SignalingReserved PeerConnection::reserveSignaling(unsigned kind) noexcept {
    if(kind>1)return {mProcessor.note(pa::Admission::Unsupported),0,kind};
    if(mSignalingClosed.load(std::memory_order_acquire)||mTaskCloseRequested.load(std::memory_order_acquire))return {pa::Admission::Closed,0,kind};
    if(mSignalingGate.test_and_set(std::memory_order_acquire))return {mProcessor.note(pa::Admission::Contended),0,kind};
    struct Gate{PeerConnection* self;~Gate(){self->mSignalingGate.clear(std::memory_order_release);}} gate{this};
    // Seal may race the first fast check. The lifecycle cannot pass this gate
    // until the reserved head is visible, so it cannot finalize ahead of it.
    if(mSignalingClosed.load(std::memory_order_acquire)||mTaskCloseRequested.load(std::memory_order_acquire))return {pa::Admission::Closed,0,kind};
    auto& count=kind==0?mSignalingDescriptions:mSignalingCandidates;
    const unsigned limit=kind==0?1:CandidateCapacity;
    auto used=count.load(std::memory_order_relaxed);bool counted=false;
    for(unsigned n=0;n!=16;++n){
        if(used>=limit)return {mProcessor.note(pa::Admission::Full),0,kind};
        if(count.compare_exchange_weak(used,used+1,std::memory_order_acq_rel)){counted=true;break;}
    }
    if(!counted)return {mProcessor.note(pa::Admission::Contended),0,kind};
    auto head=mSignalingHead.load(std::memory_order_relaxed);
    for(unsigned n=0;n!=16;++n){
        const auto tail=mSignalingTail.load(std::memory_order_acquire);
        if(head<tail){head=mSignalingHead.load(std::memory_order_acquire);continue;}
        if(head==UINT64_MAX){count.fetch_sub(1,std::memory_order_release);return {mProcessor.note(pa::Admission::Exhausted),0,kind};}
        if(head-tail>=SignalingCapacity){count.fetch_sub(1,std::memory_order_release);return {mProcessor.note(pa::Admission::Full),0,kind};}
        if(mSignalingHead.compare_exchange_weak(head,head+1,std::memory_order_acq_rel))return {pa::Admission::Accepted,head,kind};
    }
    count.fetch_sub(1,std::memory_order_release);return {mProcessor.note(pa::Admission::Contended),0,kind};
}
void PeerConnection::publishSignaling(SignalingReserved reserved,optional<Description> description,optional<Candidate> candidate) noexcept {
    if(reserved.status!=pa::Admission::Accepted)return;
    auto& record=mSignaling[reserved.position%SignalingCapacity];
    record.description=std::move(description);record.candidate=std::move(candidate);
    record.kind=reserved.kind;
    record.ready.store(true,std::memory_order_release);
    if(!mSignalingClosed.load(std::memory_order_acquire))requestSignaling();
}
void PeerConnection::cancelSignaling(SignalingReserved reserved) noexcept {
    if(reserved.status!=pa::Admission::Accepted)return;
    auto& record=mSignaling[reserved.position%SignalingCapacity];record.kind=reserved.kind+2;
    record.ready.store(true,std::memory_order_release);
    if(!mSignalingClosed.load(std::memory_order_acquire))requestSignaling();
}
pa::Admission PeerConnection::requestSignaling() noexcept {
    if(mSignalingClosed.load(std::memory_order_acquire))return pa::Admission::Closed;
    if(mSignalingQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
    auto reserved=mProcessor.try_reserve_retained_control();
    if(reserved.status!=pa::Admission::Accepted){mSignalingQueued.store(false,std::memory_order_release);return reserved.status;}
    auto self=weak_from_this().lock();
    if(!self){mSignalingQueued.store(false,std::memory_order_release);return pa::Admission::Closed;}
    const auto status=mProcessor.commit(reserved,&PeerConnection::doSignaling,std::move(self));
    if(status!=pa::Admission::Accepted)mSignalingQueued.store(false,std::memory_order_release);
    return status;
}
void PeerConnection::finishSignaling() noexcept {
    mSignalingQueued.store(false,std::memory_order_release);
    const auto tail=mSignalingTail.load(std::memory_order_acquire);
    if(tail!=mSignalingHead.load(std::memory_order_acquire)&&mSignaling[tail%SignalingCapacity].ready.load(std::memory_order_acquire))requestSignaling();
}
void PeerConnection::drainSignaling(bool discard) {
    static_assert(std::is_nothrow_move_constructible_v<Description> && std::is_nothrow_move_constructible_v<Candidate> && std::is_nothrow_move_assignable_v<Description> && std::is_nothrow_move_assignable_v<Candidate>);
    for(unsigned n=0;n!=SignalingCapacity;++n){
        const auto tail=mSignalingTail.load(std::memory_order_relaxed);
        if(tail==mSignalingHead.load(std::memory_order_acquire))break;
        auto& record=mSignaling[tail%SignalingCapacity];
        if(!record.ready.load(std::memory_order_acquire))break;
        const auto kind=record.kind;
        // Keep this row charged through callback and payload destruction.
        // A callback cannot create a tenth retained metadata record.
        {
            auto description=std::move(record.description);auto candidate=std::move(record.candidate);
            if(!discard&&!mSignalingClosed.load(std::memory_order_acquire)&&kind<2){
                try{if(kind==0)localDescriptionCallback(std::move(*description));else localCandidateCallback(std::move(*candidate));}
                catch(...){mProcessor.note(pa::Admission::Unsupported);mTaskCloseRequested.store(true,std::memory_order_release);mSignalingClosed.store(true,std::memory_order_release);}
            }
        }
        record.description.reset();record.candidate.reset();record.ready.store(false,std::memory_order_release);
        (kind%2==0?mSignalingDescriptions:mSignalingCandidates).fetch_sub(1,std::memory_order_release);
        mSignalingTail.store(tail+1,std::memory_order_release);
    }
}
void PeerConnection::doSignaling() {
    struct Completion{PeerConnection* self;~Completion(){self->finishSignaling();}} completion{this};
    drainSignaling();
}
#endif
pa::Admission PeerConnection::requestTransportInit(unsigned bits) noexcept {
    if(mTaskCloseRequested.load(std::memory_order_acquire))return pa::Admission::Closed;
    mTransportInitPending.fetch_or(bits,std::memory_order_release);
    if(mTransportInitQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
    auto reserved=mProcessor.try_reserve_retained_control();
    if(reserved.status!=pa::Admission::Accepted){
        mTransportInitQueued.store(false,std::memory_order_release);return reserved.status;
    }
    if(auto strong=weak_from_this().lock()){
        auto status=mProcessor.commit(reserved,&PeerConnection::doTransportInit,std::move(strong));
        if(status!=pa::Admission::Accepted)mTransportInitQueued.store(false,std::memory_order_release);
        return status;
    }
    mTransportInitQueued.store(false,std::memory_order_release);return pa::Admission::Closed;
}
void PeerConnection::doTransportInit() {
    struct Completion {
        PeerConnection* self;
        ~Completion(){self->mTransportInitQueued.store(false,std::memory_order_release);}
    } completion{this};
    const auto pending=mTransportInitPending.exchange(0,std::memory_order_acq_rel);
    if(mTaskCloseRequested.load(std::memory_order_acquire)||closing.load())return;
    if(pending&InitDtls)initDtlsTransport();
    if(pending&InitSctp)initSctpTransport();
    // A rejected constructor retains its bit. Only the bounded host poll
    // schedules another attempt, avoiding a callback or worker retry loop.
}
pa::Admission PeerConnection::requestTask(unsigned bits) noexcept {
    mTaskWork.fetch_or(bits,std::memory_order_release);
    if(mTaskWorkQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
    auto reserved=mProcessor.try_reserve(mProcessor.lane(),{},true);
    if(reserved.status!=pa::Admission::Accepted){mTaskWorkQueued.store(false,std::memory_order_release);return reserved.status;}
    if(auto strong=weak_from_this().lock())return mProcessor.commit(reserved,&PeerConnection::doTaskWork,std::move(strong));
    mTaskWorkQueued.store(false,std::memory_order_release);return pa::Admission::Closed;
}
void PeerConnection::finishTaskWork() noexcept {
    mTaskWorkQueued.store(false,std::memory_order_release);
    if(mTaskWork.load(std::memory_order_acquire))requestTask(0);
}
void PeerConnection::doTaskWork() {
    struct Completion {PeerConnection* self;~Completion(){self->finishTaskWork();}} completion{this};
    const auto bits=mTaskWork.exchange(0,std::memory_order_acq_rel);
    if(bits&StateEvents)drainStateEvents();
    if(mTaskCloseRequested.load(std::memory_order_acquire)||state.load()==State::Closed)return;
    if(bits&CloseData)remoteCloseDataChannels();
    if(bits&OpenData)openDataChannels();
    if(bits&OpenMedia)openTracks();
    if(bits&PendingData)triggerPendingDataChannels();
    if(bits&PendingMedia)triggerPendingTracks();
}
void PeerConnection::closeTransports() {
    if(config.superposRetirement)config.superposRetirement.seal();
#if RTC_SUPERPOS_PROFILE
    // Sticky provider/admission failure enters here directly from host polling.
    mSuperposChannelAdmission.exchange(::superpos::rtc_profile::ChannelState::Sealed,std::memory_order_acq_rel);
    mSignalingClosed.store(true,std::memory_order_release);
#endif
#if RTC_SUPERPOS_PROFILE
    if(mTaskCloseComplete.load(std::memory_order_acquire))return;
#else
    if(state.load()==State::Closed)return;
#endif
    mTaskCloseRequested.store(true,std::memory_order_release);
    if(mTaskCloseQueued.exchange(true,std::memory_order_acq_rel))return;
    auto reserved=mProcessor.try_reserve(pa::TaskClass::Lifecycle,{},true);
    if(reserved.status!=pa::Admission::Accepted){mTaskCloseQueued.store(false,std::memory_order_release);return;}
    // Commit before any authoritative state/callback/provider mutation. The
    // empty fixed provider array is filled in accepted work and retained until
    // capture reclamation; no worker-local last release is required.
    auto teardown=[self=shared_from_this(),transports=std::array<shared_ptr<Transport>,3>{},token=Init::Instance().token()]() mutable {
#if RTC_SUPERPOS_PROFILE
        if(self->mTaskClosePrepared.load(std::memory_order_acquire)){
            auto dtls=std::atomic_load(&self->mDtlsTransport);auto sctp=std::atomic_load(&self->mSctpTransport);
            if((dtls&&!dtls->superposLocalStopComplete())||(sctp&&!sctp->superposLocalStopComplete())){
                self->mTaskCloseQueued.store(false,std::memory_order_release);return;
            }
            // Final releases remain in the accepted lifecycle capture. Until
            // this phase the fixed active fields own all retry continuations.
            transports[0]=std::atomic_exchange(&self->mSctpTransport,decltype(self->mSctpTransport)(nullptr));
            transports[1]=std::atomic_exchange(&self->mDtlsTransport,decltype(self->mDtlsTransport)(nullptr));
            transports[2]=std::atomic_exchange(&self->mIceTransport,decltype(self->mIceTransport)(nullptr));
            self->mTaskCloseComplete.store(true,std::memory_order_release);
            self->mTaskCloseQueued.store(false,std::memory_order_release);return;
        }
#endif
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
        superposPeerLifecyclePhase(self.get(),0);
#endif
        self->closing.store(true,std::memory_order_release);
        self->mStateEventsClosed.store(true,std::memory_order_seq_cst);
        if(self->mStateAdmissions.load(std::memory_order_seq_cst)!=0){
            self->mTaskCloseQueued.store(false,std::memory_order_release);return;
        }
        self->drainStateEvents();
#if RTC_SUPERPOS_PROFILE
        if(self->mSignalingGate.test_and_set(std::memory_order_acquire)){
            self->mTaskCloseQueued.store(false,std::memory_order_release);return;
        }
        self->mSignalingGate.clear(std::memory_order_release);
        self->drainSignaling(true);
        if(self->mSignalingTail.load(std::memory_order_acquire)!=self->mSignalingHead.load(std::memory_order_acquire)){
            self->mTaskCloseQueued.store(false,std::memory_order_release);return;
        }
#endif
        // Never spin on a reserved, unpublished head. Its publisher will
        // finish/cancel, and the fixed host poll retries lifecycle admission.
        if(self->mStateTail.load(std::memory_order_acquire)!=self->mStateHead.load(std::memory_order_acquire)){
            self->mTaskCloseQueued.store(false,std::memory_order_release);return;
        }
        try{self->changeIceState(IceState::Closed);}catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
        try{self->changeState(State::Closed);}catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
        try{self->setMediaHandler(nullptr);self->resetCallbacks();}
        catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
#if RTC_SUPERPOS_PROFILE
        transports[0]=std::atomic_load(&self->mSctpTransport);
        transports[1]=std::atomic_load(&self->mDtlsTransport);
        transports[2]=std::atomic_load(&self->mIceTransport);
#else
        transports[0]=std::atomic_exchange(&self->mSctpTransport,decltype(self->mSctpTransport)(nullptr));
        transports[1]=std::atomic_exchange(&self->mDtlsTransport,decltype(self->mDtlsTransport)(nullptr));
        transports[2]=std::atomic_exchange(&self->mIceTransport,decltype(self->mIceTransport)(nullptr));
#endif
        if(auto sctp=std::static_pointer_cast<SctpTransport>(transports[0])){sctp->onRecv(nullptr);sctp->onBufferedAmount(nullptr);}
        for(const auto& t:transports)if(t)t->onStateChange(nullptr);
#if RTC_SUPERPOS_PROFILE
        try{self->closeDataChannels();}catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
        try{self->closeTracks();}catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
        // Stop every retained provider even if an earlier callback/stop throws.
        for(const auto& t:transports)if(t){
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
            superposPeerLifecyclePhase(self.get(),1);
#endif
            try{t->stop();}catch(...){self->mProcessor.note(pa::Admission::Unsupported);}}
        self->mTaskClosePrepared.store(true,std::memory_order_release);
        auto dtls=std::static_pointer_cast<DtlsTransport>(transports[1]);auto sctp=std::static_pointer_cast<SctpTransport>(transports[0]);
        if((!dtls||dtls->superposLocalStopComplete())&&(!sctp||sctp->superposLocalStopComplete())){
            transports[0]=std::atomic_exchange(&self->mSctpTransport,decltype(self->mSctpTransport)(nullptr));
            transports[1]=std::atomic_exchange(&self->mDtlsTransport,decltype(self->mDtlsTransport)(nullptr));
            transports[2]=std::atomic_exchange(&self->mIceTransport,decltype(self->mIceTransport)(nullptr));
            self->mTaskCloseComplete.store(true,std::memory_order_release);
        }
        self->mTaskCloseQueued.store(false,std::memory_order_release);
#else
        try{self->closeDataChannels();self->closeTracks();for(const auto& t:transports)if(t){t->stop();break;}}
        catch(...){self->mProcessor.note(pa::Admission::Unsupported);}
#endif
    };
    if(mProcessor.commit(reserved,std::move(teardown))!=pa::Admission::Accepted)std::terminate();
}
pa::Admission PeerConnection::superposTaskPoll() noexcept {
    if(!mTaskCloseRequested.load(std::memory_order_acquire)&&
       mTransportInitPending.load(std::memory_order_acquire)&&
       !mTransportInitQueued.load(std::memory_order_acquire))requestTransportInit(0);
#if RTC_SUPERPOS_PROFILE
    const auto signalingTail=mSignalingTail.load(std::memory_order_acquire);
    if(signalingTail!=mSignalingHead.load(std::memory_order_acquire)&&mSignaling[signalingTail%SignalingCapacity].ready.load(std::memory_order_acquire)&&!mSignalingQueued.load(std::memory_order_acquire))requestSignaling();
#endif
    if(mTaskWork.load(std::memory_order_acquire)&&!mTaskWorkQueued.load(std::memory_order_acquire))requestTask(0);
    auto result=mProcessor.status();
    if(auto transport=std::atomic_load(&mDtlsTransport)){
        auto status=transport->superposTaskPoll();
        if(status!=pa::Admission::Accepted&&status!=pa::Admission::Contended)result=mProcessor.note(status);
    }
    if(auto transport=std::atomic_load(&mSctpTransport)){
        auto status=transport->pollTasks();if(status!=pa::Admission::Accepted&&status!=pa::Admission::Contended)result=mProcessor.note(status);
    }
    if((mTaskCloseRequested.load(std::memory_order_acquire)||result!=pa::Admission::Accepted)&&!mTaskCloseComplete.load(std::memory_order_acquire))closeTransports();
    return result;
}

void PeerConnection::endLocalCandidates() {
	std::lock_guard lock(mLocalDescriptionMutex);
	if (mLocalDescription)
		mLocalDescription->endCandidates();
}

void PeerConnection::rollbackLocalDescription() {
	PLOG_DEBUG << "Rolling back pending local description";

	std::unique_lock lock(mLocalDescriptionMutex);
	if (mCurrentLocalDescription) {
		std::vector<Candidate> existingCandidates;
		if (mLocalDescription)
			existingCandidates = mLocalDescription->extractCandidates();

		mLocalDescription.emplace(std::move(*mCurrentLocalDescription));
		mLocalDescription->addCandidates(std::move(existingCandidates));
		mCurrentLocalDescription.reset();
	}
}

bool PeerConnection::checkFingerprint(const std::string &fingerprint) {
    std::lock_guard lock(mRemoteDescriptionMutex);
    mSuperposRemoteIdentityValid = false;
    mSuperposRemoteIdentity = {};
    mRemoteFingerprint = fingerprint; // Diagnostic observation, not verification evidence.
    if (!mRemoteDescription || !mRemoteDescription->fingerprint() ||
        mRemoteFingerprintAlgorithm != mRemoteDescription->fingerprint()->algorithm)
        return false;
    if (config.disableFingerprintVerification) return true;
    if (mRemoteDescription->fingerprint()->value != fingerprint) return false;
    mSuperposRemoteIdentityValid =
        mRemoteFingerprintAlgorithm == CertificateFingerprint::Algorithm::Sha256 &&
        decodeSha256Fingerprint(fingerprint, mSuperposRemoteIdentity);
    return true;
}

rtc::PeerConnection::SuperposIdentity PeerConnection::superposVerifiedIdentity() const noexcept {
    using Identity = rtc::PeerConnection::SuperposIdentity;
    Identity result;
    if(config.disableFingerprintVerification){result.status=Identity::Status::VerificationDisabled;return result;}
    try {
        std::unique_lock lock(mRemoteDescriptionMutex,std::try_to_lock);
        if(!lock.owns_lock()){result.status=Identity::Status::Busy;return result;}
        if(state.load(std::memory_order_acquire)!=State::Connected ||
           mTaskCloseRequested.load(std::memory_order_acquire))return result;
        if(mRemoteFingerprintAlgorithm!=CertificateFingerprint::Algorithm::Sha256){
            result.status=Identity::Status::Unsupported;return result;
        }
        if(!mSuperposLocalIdentityValid||!mSuperposRemoteIdentityValid)return result;
        result.local=mSuperposLocalIdentity;result.remote=mSuperposRemoteIdentity;
        result.status=Identity::Status::Ready;
    } catch (...) { result=Identity{}; }
    return result;
}

void PeerConnection::forwardMessage(message_ptr message) {
#if RTC_SUPERPOS_PROFILE
    // Reject DCEP before OPEN conflict handling can close negotiated channel0.
    if(message && (message->stream!=0 || (message->type!=Message::Binary && message->type!=Message::Reset)))return;
#endif
	if (!message) {
		remoteCloseDataChannels();
		return;
	}

	auto iceTransport = std::atomic_load(&mIceTransport);
	auto sctpTransport = std::atomic_load(&mSctpTransport);
	if (!iceTransport || !sctpTransport)
		return;

	const uint16_t stream = uint16_t(message->stream);
	auto [channel, found] = findDataChannel(stream);

	if (DataChannel::IsOpenMessage(message)) {
		if (found) {
			// The stream is already used, the receiver must close the DataChannel
			PLOG_WARNING << "Got open message on already used stream " << stream;
			if (channel && !channel->isClosed())
				channel->close();
			else
				sctpTransport->closeStream(message->stream);

			return;
		}

		const uint16_t remoteParity = (iceTransport->role() == Description::Role::Active) ? 1 : 0;
		if (stream % 2 != remoteParity) {
			// The odd/even rule is violated, the receiver must close the DataChannel
			PLOG_WARNING << "Got open message violating the odd/even rule on stream " << stream;
			sctpTransport->closeStream(message->stream);
			return;
		}

		channel = std::make_shared<IncomingDataChannel>(weak_from_this(), sctpTransport);
		channel->assignStream(stream);
		channel->openCallback =
		    weak_bind(&PeerConnection::triggerDataChannel, this, weak_ptr<DataChannel>{channel});

		std::unique_lock lock(mDataChannelsMutex); // we are going to emplace
		mDataChannels.emplace(stream, channel);
	} else if (!found) {
		if (message->type == Message::Reset)
			return; // ignore

		// Invalid, close the DataChannel
		PLOG_WARNING << "Got unexpected message on stream " << stream;
		sctpTransport->closeStream(message->stream);
		return;
	}

	if (message->type == Message::Reset) {
		// Incoming stream is reset, unregister it
		removeDataChannel(stream);
	}

	if (channel) {
		// Forward the message
		channel->incoming(message);
	} else {
		// DataChannel was destroyed, ignore
		PLOG_DEBUG << "Ignored message on stream " << stream << ", DataChannel is destroyed";
	}
}

void PeerConnection::forwardMedia([[maybe_unused]] message_ptr message) {
#if RTC_ENABLE_MEDIA
	if (!message)
		return;

	// TODO: outgoing
	if (auto handler = getMediaHandler()) {
		message_vector messages{std::move(message)};

		try {
			handler->incomingChain(messages, [this](message_ptr message) {
				auto transport = std::atomic_load(&mDtlsTransport);
				if (auto srtpTransport = std::dynamic_pointer_cast<DtlsSrtpTransport>(transport))
					srtpTransport->send(std::move(message));
			});
		} catch(const std::exception &e) {
			PLOG_WARNING << "Exception in global incoming media handler: " << e.what();
			return;
		}

		for (auto &m : messages)
			dispatchMedia(std::move(m));

	} else {
		dispatchMedia(std::move(message));
	}
#endif
}

void PeerConnection::dispatchMedia([[maybe_unused]] message_ptr message) {
#if RTC_ENABLE_MEDIA
	std::shared_lock lock(mTracksMutex); // read-only
	if (mTrackLines.size() == 1) {
		if (auto track = mTrackLines.front().lock())
			track->incoming(message);
		return;
	}
	// Browsers like to compound their packets with a random SSRC.
	// we have to do this monstrosity to distribute the report blocks
	if (message->type == Message::Control) {
		std::set<uint32_t> ssrcs;
		size_t offset = 0;
		while (offset + sizeof(RtcpHeader) <= message->size()) {
			auto header = reinterpret_cast<RtcpHeader *>(message->data() + offset);
			size_t length = header->lengthInBytes();
			if (offset + length > message->size()) {
				COUNTER_MEDIA_TRUNCATED++;
				break;
			}
			switch(header->payloadType()) {
			case 200: // SR
				if (length >= sizeof(RtcpSr)) {
					auto rtcpsr = reinterpret_cast<RtcpSr *>(header);
					ssrcs.insert(rtcpsr->senderSSRC());
					for (int i = 0; i < rtcpsr->header.reportCount(); ++i)
						if (const auto *reportBlock = rtcpsr->getReportBlock(i))
							ssrcs.insert(reportBlock->getSSRC());
				}
				break;

			case 201: // RR
				if (length >= sizeof(RtcpRr)) {
					auto rtcprr = reinterpret_cast<RtcpRr *>(header);
					ssrcs.insert(rtcprr->senderSSRC());
					for (int i = 0; i < rtcprr->header.reportCount(); ++i)
						if (const auto *reportBlock = rtcprr->getReportBlock(i))
							ssrcs.insert(reportBlock->getSSRC());
				}
				break;

			case 202: // SDES
				if (length >= sizeof(RtcpSdes)) {
					auto sdes = reinterpret_cast<RtcpSdes *>(header);
					if (!sdes->isValid()) {
						PLOG_WARNING << "RTCP SDES packet is invalid";
						continue;
					}
					for (unsigned int i = 0; i < sdes->chunksCount(); i++) {
						auto chunk = sdes->getChunk(i);
						ssrcs.insert(chunk->ssrc());
					}
				}
				break;


			case 205: // FB
			case 206:
				if (length >= sizeof(RtcpFbHeader)) {
					auto rtcpfb = reinterpret_cast<RtcpFbHeader *>(header);
					ssrcs.insert(rtcpfb->packetSenderSSRC());
					ssrcs.insert(rtcpfb->mediaSourceSSRC());
					if (header->payloadType() == 206 && header->reportCount() == 15 &&
						length >= sizeof(RtcpRemb)) {
						auto remb = reinterpret_cast<RtcpRemb *>(header);
						if (remb->hasValidId())
							for (int i = 0; i < remb->getSSRCCount(); ++i)
								ssrcs.insert(remb->getSSRC(i));
					}
				}
				break;

			default:
				// PT=203 == Goodbye
				// PT=204 == Application Specific
				// PT=207 == Extended Report
				if (header->payloadType() != 203 && header->payloadType() != 204 &&
				    header->payloadType() != 207) {
					COUNTER_UNKNOWN_PACKET_TYPE++;
				}
				break;
			}
			offset += header->lengthInBytes();
		}

		if (!ssrcs.empty()) {
			for (uint32_t ssrc : ssrcs) {
				if (auto it = mTracksBySsrc.find(ssrc); it != mTracksBySsrc.end()) {
					if (auto track = it->second.lock())
						track->incoming(message);
				}
			}
			return;
		}
	}

	uint32_t ssrc = uint32_t(message->stream);

	if (auto it = mTracksBySsrc.find(ssrc); it != mTracksBySsrc.end()) {
		if (auto track = it->second.lock())
			track->incoming(message);
	} else {
		/*
		 * TODO: So the problem is that when stop sending streams, we stop getting report blocks for
		 * those streams Therefore when we get compound RTCP packets, they are empty, and we can't
		 * forward them. Therefore, it is expected that we don't know where to forward packets. Is
		 * this ideal? No! Do I know how to fix it? No!
		 */
		// PLOG_WARNING << "Track not found for SSRC " << ssrc << ", dropping";
		return;
	}
#endif
}

void PeerConnection::forwardBufferedAmount(uint16_t stream, size_t amount) {
	[[maybe_unused]] auto [channel, found] = findDataChannel(stream);
	if (channel)
		channel->triggerBufferedAmount(amount);
}

shared_ptr<DataChannel> PeerConnection::emplaceDataChannel(string label, DataChannelInit init, shared_ptr<rtc::DataChannel>* publicResult) {
#if RTC_SUPERPOS_PROFILE
    namespace boundary=::superpos::rtc_profile;
    using S=boundary::ChannelState;
    if(!publicResult || !boundary::channel_allowed(label,init))throw boundary::Failure(boundary::Error::Unsupported);
    if(mTaskCloseRequested.load(std::memory_order_acquire))throw boundary::Failure(boundary::Error::Closed);
    auto expected=S::Empty;
    if(!mSuperposChannelAdmission.compare_exchange_strong(expected,S::Constructing,std::memory_order_acq_rel))
        throw boundary::Failure(expected==S::Sealed?boundary::Error::Closed:boundary::Error::Full);
    try {
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
        superposPeerBeforeChannelAllocation(this);
#endif
        if(mSuperposChannelAdmission.load(std::memory_order_acquire)!=S::Constructing)throw boundary::Failure(boundary::Error::Closed);
        auto channel=retirement::make_owned<DataChannel>(config.superposRetirement,weak_from_this(),std::move(label),std::move(init.protocol),std::move(init.reliability));
        channel->assignStream(0);
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
        superposPeerBeforePublicWrapper(this);
#endif
        // The public wrapper's allocation is fallible before registration.
        auto wrapper=retirement::make_owned<rtc::DataChannel>(config.superposRetirement,channel);
        std::unique_lock lock(mDataChannelsMutex,std::try_to_lock);
        if(!lock.owns_lock())throw boundary::Failure(boundary::Error::Contended);
        if(mSuperposChannelAdmission.load(std::memory_order_acquire)!=S::Constructing)throw boundary::Failure(boundary::Error::Closed);
        if(!mDataChannels.emplace(0,channel).second)throw boundary::Failure(boundary::Error::Full);
        expected=S::Constructing;
        if(!mSuperposChannelAdmission.compare_exchange_strong(expected,S::Published,std::memory_order_acq_rel)) {
            mDataChannels.erase(0);throw boundary::Failure(boundary::Error::Closed);
        }
        lock.unlock();
        if(auto transport=std::atomic_load(&mSctpTransport);transport && transport->state()==SctpTransport::State::Connected)
            channel->open(transport);
        *publicResult=std::move(wrapper);
        return channel;
    }catch(...){
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
        superposPeerUnwindPhase(this,40);
#endif
        // Failed construction releases only this still-unpublished reservation.
        // A close seal or committed tombstone can never be overwritten.
        expected=S::Constructing;
        if(!mSuperposChannelAdmission.compare_exchange_strong(expected,S::Empty,std::memory_order_acq_rel)
            && expected==S::Published)close();
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
        superposPeerUnwindPhase(this,41);
#endif
        throw;
    }

#else
	std::unique_lock lock(mDataChannelsMutex); // we are going to emplace

	// If the DataChannel is user-negotiated, do not negotiate it in-band
	auto channel =
	    init.negotiated
	        ? std::make_shared<DataChannel>(weak_from_this(), std::move(label),
	                                        std::move(init.protocol), std::move(init.reliability))
	        : std::make_shared<OutgoingDataChannel>(weak_from_this(), std::move(label),
	                                                std::move(init.protocol),
	                                                std::move(init.reliability));

	// If the user supplied a stream id, use it, otherwise assign it later
	if (init.id) {
		uint16_t stream = *init.id;
		if (stream > maxDataChannelStream())
			throw std::invalid_argument("DataChannel stream id is too high");

		channel->assignStream(stream);
		mDataChannels.emplace(std::make_pair(stream, channel));

	} else {
		mUnassignedDataChannels.push_back(channel);
	}

	lock.unlock(); // we are going to call assignDataChannels()

	// If SCTP is connected, assign and open now
	auto sctpTransport = std::atomic_load(&mSctpTransport);
	if (sctpTransport && sctpTransport->state() == SctpTransport::State::Connected) {
		assignDataChannels();
		channel->open(sctpTransport);
	}

	return channel;
#endif
}

std::pair<shared_ptr<DataChannel>, bool> PeerConnection::findDataChannel(uint16_t stream) {
	std::shared_lock lock(mDataChannelsMutex); // read-only
	if (auto it = mDataChannels.find(stream); it != mDataChannels.end())
		return std::make_pair(it->second.lock(), true);
	else
		return std::make_pair(nullptr, false);
}

bool PeerConnection::removeDataChannel(uint16_t stream) {
#if RTC_SUPERPOS_PROFILE
    // Keep a weak tombstone. A reset ends this channel, never authorizes another.
    return false;
#else
	std::unique_lock lock(mDataChannelsMutex); // we are going to erase
	return mDataChannels.erase(stream) != 0;
#endif
}

uint16_t PeerConnection::maxDataChannelStream() const {
	auto sctpTransport = std::atomic_load(&mSctpTransport);
	return sctpTransport ? sctpTransport->maxStream() : (MAX_SCTP_STREAMS_COUNT - 1);
}

void PeerConnection::assignDataChannels() {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	std::unique_lock lock(mDataChannelsMutex); // we are going to emplace

	auto iceTransport = std::atomic_load(&mIceTransport);
	if (!iceTransport)
		throw std::logic_error("Attempted to assign DataChannels without ICE transport");

	const uint16_t maxStream = maxDataChannelStream();
	for (auto it = mUnassignedDataChannels.begin(); it != mUnassignedDataChannels.end(); ++it) {
		auto channel = it->lock();
		if (!channel)
			continue;

		// RFC 8832: The peer that initiates opening a data channel selects a stream identifier
		// for which the corresponding incoming and outgoing streams are unused.  If the side is
		// acting as the DTLS client, it MUST choose an even stream identifier; if the side is
		// acting as the DTLS server, it MUST choose an odd one. See
		// https://www.rfc-editor.org/rfc/rfc8832.html#section-6
		uint16_t stream = (iceTransport->role() == Description::Role::Active) ? 0 : 1;
		while (true) {
			if (stream > maxStream)
				throw std::runtime_error("Too many DataChannels");

			if (mDataChannels.find(stream) == mDataChannels.end())
				break;

			stream += 2;
		}

		PLOG_DEBUG << "Assigning stream " << stream << " to DataChannel";

		channel->assignStream(stream);
		mDataChannels.emplace(std::make_pair(stream, channel));
	}

	mUnassignedDataChannels.clear();
#endif
}

void PeerConnection::iterateDataChannels(
    std::function<void(shared_ptr<DataChannel> channel)> func) {
#if RTC_SUPERPOS_PROFILE
    shared_ptr<DataChannel> channel;
    {std::shared_lock lock(mDataChannelsMutex);if(auto it=mDataChannels.find(0);it!=mDataChannels.end())channel=it->second.lock();}
    if(channel && !channel->isClosed())func(std::move(channel));
    return;
#else
	std::vector<shared_ptr<DataChannel>> locked;
	{
		std::shared_lock lock(mDataChannelsMutex); // read-only
		locked.reserve(mDataChannels.size());
		for (auto it = mDataChannels.begin(); it != mDataChannels.end(); ++it) {
			auto channel = it->second.lock();
			if (channel && !channel->isClosed())
				locked.push_back(std::move(channel));
		}
	}

	for (auto &channel : locked) {
		try {
			func(std::move(channel));
		} catch (const std::exception &e) {
			PLOG_WARNING << e.what();
		}
	}
#endif
}

void PeerConnection::openDataChannels() {
	if (auto transport = std::atomic_load(&mSctpTransport))
		iterateDataChannels([&](shared_ptr<DataChannel> channel) {
			if (!channel->isOpen())
				channel->open(transport);
		});
}

void PeerConnection::closeDataChannels() {
	iterateDataChannels([&](shared_ptr<DataChannel> channel) { channel->close(); });
}

void PeerConnection::remoteCloseDataChannels() {
	iterateDataChannels([&](shared_ptr<DataChannel> channel) { channel->remoteClose(); });
}

shared_ptr<Track> PeerConnection::emplaceTrack(Description::Media description) {
#if RTC_SUPERPOS_PROFILE
    throw ::superpos::rtc_profile::Failure(::superpos::rtc_profile::Error::Unsupported);
#else
	std::unique_lock lock(mTracksMutex); // we are going to emplace

#if !RTC_ENABLE_MEDIA
	// No media support, mark as removed
	PLOG_WARNING << "Tracks are disabled (not compiled with media support)";
	description.markRemoved();
#endif

	shared_ptr<Track> track;
	if (auto it = mTracks.find(description.mid()); it != mTracks.end())
		if (auto t = it->second.lock(); t && !t->isClosed())
			track = std::move(t);

	if (track) {
		track->setDescription(std::move(description));
	} else {
		track = std::make_shared<Track>(weak_from_this(), std::move(description));
		mTracks.emplace(std::make_pair(track->mid(), track));
		mTrackLines.emplace_back(track);
	}

	auto handler = getMediaHandler();
	if (handler)
		handler->media(track->description());

	if (track->description().isRemoved())
		track->close();

	return track;
#endif
}

void PeerConnection::iterateTracks(std::function<void(shared_ptr<Track> track)> func) {
	std::vector<shared_ptr<Track>> locked;
	{
		std::shared_lock lock(mTracksMutex); // read-only
		locked.reserve(mTrackLines.size());
		for (auto it = mTrackLines.begin(); it != mTrackLines.end(); ++it) {
			auto track = it->lock();
			if (track && !track->isClosed())
				locked.push_back(std::move(track));
		}
	}

	for (auto &track : locked) {
		try {
			func(std::move(track));
		} catch (const std::exception &e) {
			PLOG_WARNING << e.what();
		}
	}
}

void PeerConnection::iterateRemoteTracks(std::function<void(shared_ptr<Track> track)> func) {
	auto remote = remoteDescription();
	if(!remote)
		return;

	std::vector<shared_ptr<Track>> locked;
	{
		std::shared_lock lock(mTracksMutex); // read-only
		locked.reserve(remote->mediaCount());
		for(int i = 0; i < remote->mediaCount(); ++i) {
			auto media = remote->media(i);
			if (std::holds_alternative<Description::Media *>(media)) {
				auto remoteMedia = std::get<Description::Media *>(media);
				if (!remoteMedia->isRemoved())
					if (auto it = mTracks.find(remoteMedia->mid()); it != mTracks.end())
						if (auto track = it->second.lock())
							locked.push_back(std::move(track));
			}
		}
	}

	for (auto &track : locked) {
		try {
			func(std::move(track));
		} catch (const std::exception &e) {
			PLOG_WARNING << e.what();
		}
	}
}


void PeerConnection::openTracks() {
#if RTC_ENABLE_MEDIA
	auto transport = std::atomic_load(&mDtlsTransport);
	if (!transport)
		return;

	auto srtpTransport = std::dynamic_pointer_cast<DtlsSrtpTransport>(transport);
	iterateRemoteTracks([&](shared_ptr<Track> track) {
		if(!track->isOpen()) {
			if (srtpTransport) {
				track->open(srtpTransport);
			} else {
				// A track was added during a latter renegotiation, whereas SRTP transport was
				// not initialized. This is an optimization to use the library with data
				// channels only. Set forceMediaTransport to true to initialize the transport
				// before dynamically adding tracks.
				auto errorMsg = "The connection has no media transport";
				PLOG_ERROR << errorMsg;
				track->triggerError(errorMsg);
			}
		}
	});
#endif
}

void PeerConnection::closeTracks() {
	iterateTracks([&](shared_ptr<Track> track) { track->close(); });
}

void PeerConnection::validateRemoteDescription(const Description &description) {
	if (!description.iceUfrag())
		throw std::invalid_argument("Remote description has no ICE user fragment");

	if (!description.icePwd())
		throw std::invalid_argument("Remote description has no ICE password");

	if (!description.fingerprint())
		throw std::invalid_argument("Remote description has no valid fingerprint");

	if (description.mediaCount() == 0)
		throw std::invalid_argument("Remote description has no media line");

	int activeMediaCount = 0;
	for (int i = 0; i < description.mediaCount(); ++i)
		std::visit(rtc::overloaded{[&](const Description::Application *application) {
			                           if (!application->isRemoved())
				                           ++activeMediaCount;
		                           },
		                           [&](const Description::Media *media) {
			                           if (!media->isRemoved() ||
			                               media->direction() != Description::Direction::Inactive)
				                           ++activeMediaCount;
		                           }},
		           description.media(i));

	if (activeMediaCount == 0)
		throw std::invalid_argument("Remote description has no active media");

	PLOG_VERBOSE << "Remote description looks valid";
}

void PeerConnection::populateLocalDescription(Description &description) const {
	const uint16_t localSctpPort = DEFAULT_SCTP_PORT;
	const size_t localMaxMessageSize =
	    config.maxMessageSize.value_or(DEFAULT_LOCAL_MAX_MESSAGE_SIZE);

	// Clean up the application entry the ICE transport might have added already (libnice)
	description.clearMedia();

	if (auto remote = remoteDescription()) {
		// Reciprocate remote description
		for (int i = 0; i < remote->mediaCount(); ++i) {
			std::visit( // reciprocate each media
			    rtc::overloaded{
			        [&](Description::Application *remoteApp) {
				        std::shared_lock lock(mDataChannelsMutex);
				        if (!mDataChannels.empty() || !mUnassignedDataChannels.empty()) {
					        // Prefer local description
					        Description::Application app(remoteApp->mid());
					        app.setSctpPort(localSctpPort);
					        app.setMaxMessageSize(localMaxMessageSize);

					        PLOG_DEBUG << "Adding application to local description, mid=\""
					                   << app.mid() << "\"";

					        description.addMedia(std::move(app));

				        } else {
							auto reciprocated = remoteApp->reciprocate();
							reciprocated.hintSctpPort(localSctpPort);
							reciprocated.setMaxMessageSize(localMaxMessageSize);

							PLOG_DEBUG << "Reciprocating application in local description, mid=\""
								       << reciprocated.mid() << "\"";

							description.addMedia(std::move(reciprocated));
						}
			        },
			        [&](Description::Media *remoteMedia) {
				        std::shared_lock lock(mTracksMutex);
				        auto it = mTracks.find(remoteMedia->mid());
					    auto track = it != mTracks.end() ? it->second.lock() : nullptr;
						if(track) {
							// Prefer local description
							auto media = track->description();

						    PLOG_DEBUG << "Adding media to local description, mid=\""
						                << media.mid() << "\", removed=" << std::boolalpha
						                << media.isRemoved();

						    description.addMedia(std::move(media));

					    } else {
							auto reciprocated = remoteMedia->reciprocate();
							reciprocated.markRemoved();

						    PLOG_DEBUG << "Adding media to local description, mid=\""
						                << reciprocated.mid()
						                << "\", removed=true (track is destroyed)";

						    description.addMedia(std::move(reciprocated));
					    }
			        },
			    },
			    remote->media(i));
		}
	}

	if (description.type() == Description::Type::Offer) {
		// This is an offer, add locally created data channels and tracks
		// Add media for local tracks
		std::shared_lock lock(mTracksMutex);
		for (auto it = mTrackLines.begin(); it != mTrackLines.end(); ++it) {
			if (auto track = it->lock()) {
				if (description.hasMid(track->mid()))
					continue;

				auto media = track->description();

				PLOG_DEBUG << "Adding media to local description, mid=\"" << media.mid()
				           << "\", removed=" << std::boolalpha << media.isRemoved();

				description.addMedia(std::move(media));
			}
		}

		// Add application for data channels
		if (!description.hasApplication()) {
			std::shared_lock lock(mDataChannelsMutex);
			if (!mDataChannels.empty() || !mUnassignedDataChannels.empty()) {
				// Prevents mid collision with remote or local tracks
				unsigned int m = 0;
				while (description.hasMid(std::to_string(m)))
					++m;

				Description::Application app(std::to_string(m));
				app.setSctpPort(localSctpPort);
				app.setMaxMessageSize(localMaxMessageSize);

				PLOG_DEBUG << "Adding application to local description, mid=\"" << app.mid()
				           << "\"";

				description.addMedia(std::move(app));
			}
		}
	}

	// Set local fingerprint (wait for certificate if necessary)
	description.setFingerprint(mCertificate.get()->fingerprint());
}

void PeerConnection::processLocalDescription(Description description) {
	PLOG_VERBOSE << "Issuing local description: " << description;

	if (description.mediaCount() == 0)
		throw std::logic_error("Local description has no media line");

#if RTC_SUPERPOS_PROFILE
    auto reserved=reserveSignaling(0);
    if(reserved.status!=pa::Admission::Accepted)throw ProcessorFailure(reserved.status);
    struct Pending{PeerConnection* self;SignalingReserved reserved;bool published{};~Pending(){if(!published){self->mProcessor.note(pa::Admission::Unsupported);self->mTaskCloseRequested.store(true,std::memory_order_release);self->mSignalingClosed.store(true,std::memory_order_release);self->cancelSignaling(reserved);}}} pending{this,reserved};
#endif
	// Update the SSRC cache
	updateTrackSsrcCache(description);

	{
		// Set as local description
		std::lock_guard lock(mLocalDescriptionMutex);
#if RTC_SUPERPOS_PROFILE
        // This experimental carrier supports one local negotiation. A new
        // negotiation uses a new PeerConnection, preventing callback ABA.
        if(mProfileLocalDescriptionIssued)throw ProcessorFailure(mProcessor.note(pa::Admission::Unsupported));
        const auto candidateCount=description.candidates().size();
        if(candidateCount>CandidateCapacity)throw ProcessorFailure(mProcessor.note(pa::Admission::Full));
        mProfileLocalDescriptionIssued=true;
        mProfileLocalCandidates=unsigned(candidateCount);
#endif

		std::vector<Candidate> existingCandidates;
		if (mLocalDescription) {
			existingCandidates = mLocalDescription->extractCandidates();
			mCurrentLocalDescription.emplace(std::move(*mLocalDescription));
		}

		mLocalDescription.emplace(description);
		mLocalDescription->addCandidates(std::move(existingCandidates));
	}

#if RTC_SUPERPOS_PROFILE
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
    superposSignalingBeforePublication(this,0);
#endif
    publishSignaling(reserved,std::move(description),{});pending.published=true;
#else
	mProcessor.enqueue(&PeerConnection::trigger<Description>, shared_from_this(),
	                   &localDescriptionCallback, std::move(description));
#endif
}

void PeerConnection::processLocalCandidate(Candidate candidate) {
	std::lock_guard lock(mLocalDescriptionMutex);
	if (!mLocalDescription)
		throw std::logic_error("Got a local candidate without local description");

	if (config.iceTransportPolicy == TransportPolicy::Relay &&
	    candidate.type() != Candidate::Type::Relayed) {
		PLOG_VERBOSE << "Not issuing local candidate because of transport policy: " << candidate;
		return;
	}

	PLOG_VERBOSE << "Issuing local candidate: " << candidate;

#if RTC_SUPERPOS_PROFILE
    auto reserved=reserveSignaling(1);
    if(reserved.status!=pa::Admission::Accepted)throw ProcessorFailure(reserved.status);
    struct Pending{PeerConnection* self;SignalingReserved reserved;bool published{};~Pending(){if(!published){self->mProcessor.note(pa::Admission::Unsupported);self->mTaskCloseRequested.store(true,std::memory_order_release);self->mSignalingClosed.store(true,std::memory_order_release);self->cancelSignaling(reserved);}}} pending{this,reserved};
    if(mProfileLocalCandidates>=CandidateCapacity)throw ProcessorFailure(mProcessor.note(pa::Admission::Full));
#endif
	candidate.resolve(Candidate::ResolveMode::Simple);
	mLocalDescription->addCandidate(candidate);
#if RTC_SUPERPOS_PROFILE
    ++mProfileLocalCandidates;
    publishSignaling(reserved,{},std::move(candidate));pending.published=true;
#else
	mProcessor.enqueue(&PeerConnection::trigger<Candidate>, shared_from_this(),
	                   &localCandidateCallback, std::move(candidate));
#endif
}

void PeerConnection::processRemoteDescription(Description description) {
#if RTC_SUPERPOS_PROFILE
    ::superpos::rtc_profile::require_description(description);
#endif
	// Create tracks from remote description
	for (int i = 0; i < description.mediaCount(); ++i) {
		auto media = description.media(i);
		if (std::holds_alternative<Description::Media *>(media)) {
			auto remoteMedia = std::get<Description::Media *>(media);
			std::unique_lock lock(mTracksMutex); // we may emplace a track
			if (auto it = mTracks.find(remoteMedia->mid()); it != mTracks.end())
				continue;

			PLOG_DEBUG << "New remote track, mid=\"" << remoteMedia->mid() << "\"";

			auto reciprocated = remoteMedia->reciprocate();
#if !RTC_ENABLE_MEDIA
			if (!reciprocated.isRemoved()) {
				// No media support, mark as removed
				PLOG_WARNING << "Rejecting track (not compiled with media support)";
				reciprocated.markRemoved();
			}
#endif

			// Create incoming track
			auto track = std::make_shared<Track>(weak_from_this(), std::move(reciprocated));
			mTracks.emplace(std::make_pair(track->mid(), track));
			mTrackLines.emplace_back(track);
			triggerTrack(track); // The user may modify the track description

			auto handler = getMediaHandler();
			if (handler)
				handler->media(track->description());

			if (track->description().isRemoved())
				track->close();
		}
	}

	// Update the SSRC cache for existing tracks
	updateTrackSsrcCache(description);

	{
		// Set as remote description
		std::lock_guard lock(mRemoteDescriptionMutex);

		std::vector<Candidate> existingCandidates;
		if (mRemoteDescription)
			existingCandidates = mRemoteDescription->extractCandidates();

		mRemoteDescription.emplace(description);
		mRemoteDescription->addCandidates(std::move(existingCandidates));
	}

	auto dtlsTransport = std::atomic_load(&mDtlsTransport);
	if (!dtlsTransport) {
		// ICE might have connected before the remote fingerprint was committed.
		auto iceTransport = std::atomic_load(&mIceTransport);
		if (iceTransport && (iceTransport->state() == Transport::State::Connected ||
		                     iceTransport->state() == Transport::State::Completed))
			requestTransportInit(InitDtls);
	}

	if (description.hasApplication()) {
		auto sctpTransport = std::atomic_load(&mSctpTransport);
		if (!sctpTransport && dtlsTransport &&
		    dtlsTransport->state() == Transport::State::Connected)
			requestTransportInit(InitSctp);
	} else {
		requestTask(CloseData);
	}

	// Reciprocated tracks might need to be open
	if (dtlsTransport && dtlsTransport->state() == Transport::State::Connected)
		requestTask(OpenMedia);
}

void PeerConnection::processRemoteCandidate(Candidate candidate) {
	auto iceTransport = std::atomic_load(&mIceTransport);
	{
		// Set as remote candidate
		std::lock_guard lock(mRemoteDescriptionMutex);
		if (!mRemoteDescription)
			throw std::logic_error("Got a remote candidate without remote description");

		if (!iceTransport)
			throw std::logic_error("Got a remote candidate without ICE transport");

		candidate.hintMid(mRemoteDescription->bundleMid());

		if (mRemoteDescription->hasCandidate(candidate))
			return; // already in description, ignore

		candidate.resolve(Candidate::ResolveMode::Simple);
		mRemoteDescription->addCandidate(candidate);
	}

	if (candidate.isResolved()) {
		iceTransport->addRemoteCandidate(std::move(candidate));
	} else {
		// We might need a lookup, do it asynchronously
		// We don't use the thread pool because we have no control on the timeout
		if ((iceTransport = std::atomic_load(&mIceTransport))) {
			weak_ptr<IceTransport> weakIceTransport{iceTransport};
			std::thread t([weakIceTransport, candidate = std::move(candidate)]() mutable {
				utils::this_thread::set_name("RTC resolver");
				if (candidate.resolve(Candidate::ResolveMode::Lookup))
					if (auto iceTransport = weakIceTransport.lock())
						iceTransport->addRemoteCandidate(std::move(candidate));
			});
			t.detach();
		}
	}
}

string PeerConnection::localBundleMid() const {
	std::lock_guard lock(mLocalDescriptionMutex);
	return mLocalDescription ? mLocalDescription->bundleMid() : "0";
}

bool PeerConnection::negotiationNeeded() const {
	auto description = localDescription();

	{
		std::shared_lock lock(mDataChannelsMutex);
		if (!mDataChannels.empty() || !mUnassignedDataChannels.empty())
			if(!description || !description->hasApplication()) {
				PLOG_DEBUG << "Negotiation needed for data channels";
				return true;
			}
	}

	{
		std::shared_lock lock(mTracksMutex);
		for(const auto &[mid, weakTrack] : mTracks)
			if (auto track = weakTrack.lock())
				if (!description || !description->hasMid(track->mid())) {
					PLOG_DEBUG << "Negotiation needed to add track, mid=" << track->mid();
					return true;
				}

		if(description) {
			for(int i = 0; i < description->mediaCount(); ++i) {
				if (std::holds_alternative<Description::Media *>(description->media(i))) {
					auto media = std::get<Description::Media *>(description->media(i));
					if (!media->isRemoved())
						if (auto it = mTracks.find(media->mid()); it != mTracks.end())
							if (auto track = it->second.lock(); !track || track->isClosed()) {
								PLOG_DEBUG << "Negotiation needed to remove track, mid=" << media->mid();
								return true;
							}
				}
			}
		}
	}

	return false;
}

void PeerConnection::setMediaHandler(shared_ptr<MediaHandler> handler) {
	std::unique_lock lock(mMediaHandlerMutex);
	mMediaHandler = handler;
}

shared_ptr<MediaHandler> PeerConnection::getMediaHandler() const {
	std::shared_lock lock(mMediaHandlerMutex);
	return mMediaHandler;
}

void PeerConnection::triggerDataChannel(weak_ptr<DataChannel> weakDataChannel) {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	auto dataChannel = weakDataChannel.lock();
	if (dataChannel) {
		dataChannel->resetOpenCallback(); // might be set internally
		mPendingDataChannels.push(std::move(dataChannel));
	}
	triggerPendingDataChannels();
#endif
}

void PeerConnection::triggerTrack(weak_ptr<Track> weakTrack) {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	auto track = weakTrack.lock();
	if (track) {
		track->resetOpenCallback(); // might be set internally
		mPendingTracks.push(std::move(track));
	}
	triggerPendingTracks();
#endif
}

void PeerConnection::triggerPendingDataChannels() {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	while (dataChannelCallback) {
		auto next = mPendingDataChannels.pop();
		if (!next)
			break;

		auto impl = std::move(*next);

		try {
			dataChannelCallback(std::make_shared<rtc::DataChannel>(impl));
		} catch (const std::exception &e) {
			PLOG_WARNING << "Uncaught exception in callback: " << e.what();
		}

		impl->triggerOpen();
	}
#endif
}

void PeerConnection::triggerPendingTracks() {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	while (trackCallback) {
		auto next = mPendingTracks.pop();
		if (!next)
			break;

		auto impl = std::move(*next);

		try {
			trackCallback(std::make_shared<rtc::Track>(impl));
		} catch (const std::exception &e) {
			PLOG_WARNING << "Uncaught exception in callback: " << e.what();
		}

		// Do not trigger open immediately for tracks as it'll be done later
	}
#endif
}

void PeerConnection::flushPendingDataChannels() {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	requestTask(PendingData);
#endif
}

void PeerConnection::flushPendingTracks() {
#if RTC_SUPERPOS_PROFILE
    return;
#else
	requestTask(PendingMedia);
#endif
}

bool PeerConnection::changeState(State newState) {
    const bool observed=stateChangeCallback.hasObserver();
    ScalarReserved reserved{pa::Admission::Closed,0};
    if(newState!=State::Closed){
        if(mStateEventsClosed.load(std::memory_order_acquire))return false;
        reserved=reserveStateEvent();if(reserved.status!=pa::Admission::Accepted)return false;
    }

	State current;
	do {
		current = state.load();
		if (current == State::Closed){publishStateEvent(reserved,4,0);return false;}
		if (current == newState){publishStateEvent(reserved,4,0);return false;}

	} while (!state.compare_exchange_weak(current, newState));

    publishStateEvent(reserved,observed?0:4,unsigned(newState));
	std::ostringstream s;
	s << newState;
	PLOG_INFO << "Changed state to " << s.str();

	if (newState == State::Closed) {
		auto callback = std::move(stateChangeCallback); // steal the callback
		callback(State::Closed);                        // call it synchronously
	} else {
	}
	return true;
}

bool PeerConnection::changeIceState(IceState newState) {
    const bool observed=iceStateChangeCallback.hasObserver();
    ScalarReserved reserved{pa::Admission::Closed,0};
    if(newState!=IceState::Closed){
        if(mStateEventsClosed.load(std::memory_order_acquire))return false;
        reserved=reserveStateEvent();if(reserved.status!=pa::Admission::Accepted)return false;
    }

	if (iceState.exchange(newState) == newState){publishStateEvent(reserved,4,0);return false;}

    publishStateEvent(reserved,observed?1:4,unsigned(newState));
	std::ostringstream s;
	s << newState;
	PLOG_INFO << "Changed ICE state to " << s.str();

	if (newState == IceState::Closed) {
		auto callback = std::move(iceStateChangeCallback); // steal the callback
		callback(IceState::Closed);                        // call it synchronously
	} else {
	}
	return true;
}

bool PeerConnection::changeGatheringState(GatheringState newState) {
    const bool observed=gatheringStateChangeCallback.hasObserver();
    ScalarReserved reserved{pa::Admission::Closed,0};
    if(true){
        if(mStateEventsClosed.load(std::memory_order_acquire))return false;
        reserved=reserveStateEvent();if(reserved.status!=pa::Admission::Accepted)return false;
    }

	if (gatheringState.exchange(newState) == newState){publishStateEvent(reserved,4,0);return false;}

    publishStateEvent(reserved,observed?2:4,unsigned(newState));
	std::ostringstream s;
	s << newState;
	PLOG_INFO << "Changed gathering state to " << s.str();

	return true;
}

bool PeerConnection::changeSignalingState(SignalingState newState) {
    const bool observed=signalingStateChangeCallback.hasObserver();
    ScalarReserved reserved{pa::Admission::Closed,0};
    if(true){
        if(mStateEventsClosed.load(std::memory_order_acquire))return false;
        reserved=reserveStateEvent();if(reserved.status!=pa::Admission::Accepted)return false;
    }

	if (signalingState.exchange(newState) == newState){publishStateEvent(reserved,4,0);return false;}

    publishStateEvent(reserved,observed?3:4,unsigned(newState));
	std::ostringstream s;
	s << newState;
	PLOG_INFO << "Changed signaling state to " << s.str();

	return true;
}

void PeerConnection::resetCallbacks() {
	// Unregister all callbacks
	dataChannelCallback = nullptr;
	localDescriptionCallback = nullptr;
	localCandidateCallback = nullptr;
	stateChangeCallback = nullptr;
	iceStateChangeCallback = nullptr;
	gatheringStateChangeCallback = nullptr;
	signalingStateChangeCallback = nullptr;
	trackCallback = nullptr;
}

CertificateFingerprint PeerConnection::remoteFingerprint() {
	std::lock_guard lock(mRemoteDescriptionMutex);
	if (mRemoteFingerprint)
		return {CertificateFingerprint{mRemoteFingerprintAlgorithm, *mRemoteFingerprint}};
	else
		return {};
}

void PeerConnection::updateTrackSsrcCache(const Description &description) {
	std::unique_lock lock(mTracksMutex); // for safely writing to mTracksBySsrc

	// Setup SSRC -> Track mapping
	for (int i = 0; i < description.mediaCount(); ++i)
		std::visit( // ssrc -> track mapping
		    rtc::overloaded{
		        [&](Description::Application const *) { return; },
		        [&](Description::Media const *media) {
			        const auto ssrcs = media->getSSRCs();

			        // Note: We don't want to lock (or do any other lookups), if we
			        // already know there's no SSRCs to loop over.
			        if (ssrcs.size() <= 0) {
				        return;
			        }

			        std::shared_ptr<Track> track{nullptr};
			        if (auto it = mTracks.find(media->mid()); it != mTracks.end())
				        if (auto track_for_mid = it->second.lock())
					        track = track_for_mid;

			        if (!track) {
				        // Unable to find track for MID
				        return;
			        }

			        for (auto ssrc : ssrcs) {
				        mTracksBySsrc.insert_or_assign(ssrc, track);
			        }
		        },
		    },
		    description.media(i));
}

} // namespace rtc::impl
