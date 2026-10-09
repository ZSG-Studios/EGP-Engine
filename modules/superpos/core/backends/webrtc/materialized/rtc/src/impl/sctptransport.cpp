/**
 * Copyright (c) 2019 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "sctptransport.hpp"
#if RTC_SUPERPOS_PROFILE
#include "superpos_notification.hpp"
#include "superpos_message.hpp"
#include "callback_identity.hpp"
#endif
#include "dtlstransport.hpp"
#include "internals.hpp"
#include "logcounter.hpp"
#include "utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <iostream>
#include <limits>
#include <shared_mutex>
#include <thread>
#include <unordered_set>
#include <vector>

// RFC 8831: SCTP MUST support performing Path MTU discovery without relying on ICMP or ICMPv6 as
// specified in [RFC4821] by using probing messages specified in [RFC4820].
// See https://www.rfc-editor.org/rfc/rfc8831.html#section-5
//
// However, usrsctp does not implement Path MTU discovery, so we need to disable it for now.
// See https://github.com/sctplab/usrsctp/issues/205
#define USE_PMTUD 0

// TODO: When Path MTU discovery is supported, it needs to be enabled with libjuice as ICE backend
// on all platforms except Mac OS where the Don't Fragment (DF) flag can't be set:
/*
#if !USE_NICE
#ifndef __APPLE__
// libjuice enables Linux path MTU discovery or sets the DF flag
#define USE_PMTUD 1
#else
// Setting the DF flag is not available on Mac OS
#define USE_PMTUD 0
#endif
#else // USE_NICE == 1
#define USE_PMTUD 0
#endif
*/

using namespace std::chrono_literals;
using namespace std::chrono;

namespace rtc::impl {

using utils::to_uint16;
using utils::to_uint32;

static LogCounter COUNTER_UNKNOWN_PPID(plog::warning,
                                       "Number of SCTP packets received with an unknown PPID");

class SctpTransport::InstancesSet {
public:
    using shared_lock=std::shared_lock<std::shared_mutex>;
#if RTC_SUPERPOS_PROFILE
    struct Admitted {shared_lock lock;SctpTransport* transport;};
    void insert(std::uint64_t identity,SctpTransport* instance) {
        std::unique_lock lock(mMutex);
        for(auto& entry:mEntries)if(!entry.transport){entry={identity,instance};return;}
        throw std::length_error("SCTP native identity registry is full");
    }
    void erase(std::uint64_t identity,SctpTransport* instance) {
        std::unique_lock lock(mMutex);
        for(auto& entry:mEntries)if(entry.identity==identity&&entry.transport==instance){entry={};return;}
    }
    bool tryErase(std::uint64_t identity,SctpTransport* instance) {
        std::unique_lock lock(mMutex,std::try_to_lock);
        if(!lock.owns_lock())return false;
        for(auto& entry:mEntries)if(entry.identity==identity&&entry.transport==instance){entry={};break;}
        return true;
    }
    optional<Admitted> lookup(void* opaque)noexcept {
        // Never dereference the opaque value, even for a rejected old callback.
        const auto identity=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(opaque));
        shared_lock lock(mMutex);
        for(const auto& entry:mEntries)if(entry.identity==identity&&entry.transport)
            return Admitted{std::move(lock),entry.transport};
        return nullopt;
    }
private:
    struct Entry {std::uint64_t identity{};SctpTransport* transport{};};
    std::array<Entry,profile_limits::associations> mEntries{}; // one identity per admitted SCTP association
#else
    void insert(SctpTransport* instance){std::unique_lock lock(mMutex);mSet.insert(instance);}
    void erase(SctpTransport* instance){std::unique_lock lock(mMutex);mSet.erase(instance);}
    optional<shared_lock> lock(SctpTransport* instance)noexcept {
        shared_lock lock(mMutex);
        return mSet.find(instance)!=mSet.end()?std::make_optional(std::move(lock)):nullopt;
    }
private:
    std::unordered_set<SctpTransport*> mSet;
#endif
    std::shared_mutex mMutex;
};

SctpTransport::InstancesSet* SctpTransport::Instances = nullptr;
#if RTC_SUPERPOS_PROFILE
namespace {
// Process-wide identity domain survives SCTP Init/Cleanup. Only numeric opaque
// values cross usrsctp; no provider callback can alias a recycled object pointer.
std::atomic<std::uint64_t> callbackIdentityCounter{};
std::uint64_t freshCallbackIdentity(){
    auto identity=::superpos::rtc_profile::mint_callback_identity(callbackIdentityCounter,
        static_cast<std::uint64_t>(std::numeric_limits<std::uintptr_t>::max()));
    if(!identity)throw std::overflow_error("SCTP native callback identity exhausted");
    return *identity;
}
// Stack-owned linked frames support synchronous nested provider callbacks.
// No allocation, global registry lookup or socket dereference is needed here.
struct NativeSctpCallbackScope {
    const SctpTransport* transport;
    NativeSctpCallbackScope* previous;
    static thread_local NativeSctpCallbackScope* current;
    explicit NativeSctpCallbackScope(const SctpTransport* value)noexcept:transport(value),previous(current){current=this;}
    ~NativeSctpCallbackScope(){current=previous;}
    static bool contains(const SctpTransport* value)noexcept {
        for(auto* frame=current;frame;frame=frame->previous)if(frame->transport==value)return true;
        return false;
    }
};
thread_local NativeSctpCallbackScope* NativeSctpCallbackScope::current{};
}
#endif

void SctpTransport::Init() {
	usrsctp_init(0, SctpTransport::WriteCallback, SctpTransport::DebugCallback);
	usrsctp_sysctl_set_sctp_pr_enable(1);  // Enable Partial Reliability Extension (RFC 3758)
	usrsctp_sysctl_set_sctp_ecn_enable(0); // Disable Explicit Congestion Notification
#ifndef SCTP_ACCEPT_ZERO_CHECKSUM
	usrsctp_enable_crc32c_offload(); // We'll compute CRC32 only for outgoing packets
#endif
#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif

	Instances = new InstancesSet;
}

void SctpTransport::SetSettings(const SctpSettings &s) {
	// The send and receive window size of usrsctp is 256KiB, which is too small for realistic RTTs,
	// therefore we increase it to 1MiB by default for better performance.
	// See https://bugzilla.mozilla.org/show_bug.cgi?id=1051685
	usrsctp_sysctl_set_sctp_recvspace(to_uint32(s.recvBufferSize.value_or(1024 * 1024)));
	usrsctp_sysctl_set_sctp_sendspace(to_uint32(s.sendBufferSize.value_or(1024 * 1024)));

	// Increase maximum chunks number on queue to 10K by default
	usrsctp_sysctl_set_sctp_max_chunks_on_queue(to_uint32(s.maxChunksOnQueue.value_or(10 * 1024)));

	// Increase initial congestion window size to 10 MTUs (RFC 6928) by default
	usrsctp_sysctl_set_sctp_initial_cwnd(to_uint32(s.initialCongestionWindow.value_or(10)));

	// Set max burst to 10 MTUs by default (max burst is initially 0, meaning disabled)
	usrsctp_sysctl_set_sctp_max_burst_default(to_uint32(s.maxBurst.value_or(10)));

	// Use standard SCTP congestion control (RFC 4960) by default
	// See https://github.com/paullouisageneau/libdatachannel/issues/354
	usrsctp_sysctl_set_sctp_default_cc_module(to_uint32(s.congestionControlModule.value_or(0)));

	// Reduce SACK delay to 20ms by default (the recommended default value from RFC 4960 is 200ms)
	usrsctp_sysctl_set_sctp_delayed_sack_time_default(
	    to_uint32(s.delayedSackTime.value_or(20ms).count()));

	// RTO settings
	// RFC 2988 recommends a 1s min RTO, which is very high, but TCP on Linux has a 200ms min RTO
	usrsctp_sysctl_set_sctp_rto_min_default(
	    to_uint32(s.minRetransmitTimeout.value_or(200ms).count()));
	// Set only 10s as max RTO instead of 60s for shorter connection timeout
	usrsctp_sysctl_set_sctp_rto_max_default(
	    to_uint32(s.maxRetransmitTimeout.value_or(10000ms).count()));
	usrsctp_sysctl_set_sctp_init_rto_max_default(
	    to_uint32(s.maxRetransmitTimeout.value_or(10000ms).count()));
	// Still set 1s as initial RTO
	usrsctp_sysctl_set_sctp_rto_initial_default(
	    to_uint32(s.initialRetransmitTimeout.value_or(1000ms).count()));

	// RTX settings
	// 5 retransmissions instead of 8 to shorten the backoff for shorter connection timeout
	auto maxRtx = to_uint32(s.maxRetransmitAttempts.value_or(5));
	usrsctp_sysctl_set_sctp_init_rtx_max_default(maxRtx);
	usrsctp_sysctl_set_sctp_assoc_rtx_max_default(maxRtx);
	usrsctp_sysctl_set_sctp_path_rtx_max_default(maxRtx); // single path

	// Heartbeat interval
	usrsctp_sysctl_set_sctp_heartbeat_interval_default(
	    to_uint32(s.heartbeatInterval.value_or(10000ms).count()));
}

void SctpTransport::Cleanup() {
	while (usrsctp_finish())
		std::this_thread::sleep_for(100ms);

	delete Instances;
	Instances = nullptr;
}

SctpTransport::SctpTransport(shared_ptr<Transport> lower, const Configuration &config, Ports ports,
                             message_callback recvCallback, amount_callback bufferedAmountCallback,
                             state_callback stateChangeCallback)
    : TransportProcessorAdmission(config.superposControlAssociation,config.superposRetirement),
      Transport(lower, std::move(stateChangeCallback)),
      mMaxMessageSize(config.maxMessageSize.value_or(DEFAULT_LOCAL_MAX_MESSAGE_SIZE)),
      mPorts(std::move(ports)),
#if RTC_SUPERPOS_PROFILE
      mSuperposCallbackIdentity(freshCallbackIdentity()),
#endif
#if RTC_SUPERPOS_PROFILE
      mSendQueue(superposSendSlots,superposPayloadCharge,superposSendBytes),
#else
      mSendQueue(0, message_size_func),
#endif
      mBufferedAmountCallback(std::move(bufferedAmountCallback)) {
#if RTC_SUPERPOS_PROFILE
    bool registeredAddress=false,publishedIdentity=false;
    try {
#endif
	onRecv(std::move(recvCallback));

	PLOG_DEBUG << "Initializing SCTP transport";

	mSock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, nullptr, nullptr, 0, nullptr);
	if (!mSock)
		throw std::runtime_error("Could not create SCTP socket, errno=" + std::to_string(errno));

#if RTC_SUPERPOS_PROFILE
    usrsctp_set_upcall(mSock,&SctpTransport::UpcallCallback,callbackAddress());
#else
    usrsctp_set_upcall(mSock,&SctpTransport::UpcallCallback,this);
#endif

	if (usrsctp_set_non_blocking(mSock, 1))
		throw std::runtime_error("Unable to set non-blocking mode, errno=" + std::to_string(errno));

	// SCTP must stop sending after the lower layer is shut down, so disable linger
	struct linger sol = {};
	sol.l_onoff = 1;
	sol.l_linger = 0;
	if (usrsctp_setsockopt(mSock, SOL_SOCKET, SO_LINGER, &sol, sizeof(sol)))
		throw std::runtime_error("Could not set socket option SO_LINGER, errno=" +
		                         std::to_string(errno));

	struct sctp_assoc_value av = {};
	av.assoc_id = SCTP_ALL_ASSOC;
	av.assoc_value = 1;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av)))
		throw std::runtime_error("Could not set socket option SCTP_ENABLE_STREAM_RESET, errno=" +
		                         std::to_string(errno));
	int on = 1;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, sizeof(on)))
		throw std::runtime_error("Could set socket option SCTP_RECVRCVINFO, errno=" +
		                         std::to_string(errno));

	struct sctp_event se = {};
	se.se_assoc_id = SCTP_ALL_ASSOC;
	se.se_on = 1;
	se.se_type = SCTP_ASSOC_CHANGE;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_EVENT, &se, sizeof(se)))
		throw std::runtime_error("Could not subscribe to event SCTP_ASSOC_CHANGE, errno=" +
		                         std::to_string(errno));
	se.se_type = SCTP_SENDER_DRY_EVENT;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_EVENT, &se, sizeof(se)))
		throw std::runtime_error("Could not subscribe to event SCTP_SENDER_DRY_EVENT, errno=" +
		                         std::to_string(errno));
	se.se_type = SCTP_STREAM_RESET_EVENT;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_EVENT, &se, sizeof(se)))
		throw std::runtime_error("Could not subscribe to event SCTP_STREAM_RESET_EVENT, errno=" +
		                         std::to_string(errno));

	// RFC 8831 6.6. Transferring User Data on a Data Channel
	// The sender SHOULD disable the Nagle algorithm (see [RFC1122) to minimize the latency
	// See https://www.rfc-editor.org/rfc/rfc8831.html#section-6.6
	int nodelay = 1;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay)))
		throw std::runtime_error("Could not set socket option SCTP_NODELAY, errno=" +
		                         std::to_string(errno));

	struct sctp_paddrparams spp = {};
	// Enable SCTP heartbeats
	spp.spp_flags = SPP_HB_ENABLE;

	// RFC 8261 5. DTLS considerations:
	// If path MTU discovery is performed by the SCTP layer and IPv4 is used as the network-layer
	// protocol, the DTLS implementation SHOULD allow the DTLS user to enforce that the
	// corresponding IPv4 packet is sent with the Don't Fragment (DF) bit set. If controlling the DF
	// bit is not possible (for example, due to implementation restrictions), a safe value for the
	// path MTU has to be used by the SCTP stack. It is RECOMMENDED that the safe value not exceed
	// 1200 bytes.
	// See https://www.rfc-editor.org/rfc/rfc8261.html#section-5
#if USE_PMTUD
	if (!config.mtu.has_value()) {
#else
	if (false) {
#endif
		// Enable SCTP path MTU discovery
		spp.spp_flags |= SPP_PMTUD_ENABLE;
		PLOG_VERBOSE << "Path MTU discovery enabled";

	} else {
		// Fall back to a safe MTU value.
		spp.spp_flags |= SPP_PMTUD_DISABLE;
		// The MTU value provided specifies the space available for chunks in the
		// packet, so we also subtract the SCTP header size.
		size_t pmtu = config.mtu.value_or(DEFAULT_MTU) - 12 - 48 - 8 - 40; // SCTP/DTLS/UDP/IPv6
		spp.spp_pathmtu = to_uint32(pmtu);
		PLOG_VERBOSE << "Path MTU discovery disabled, SCTP MTU set to " << pmtu;
	}

	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &spp, sizeof(spp)))
		throw std::runtime_error("Could not set socket option SCTP_PEER_ADDR_PARAMS, errno=" +
		                         std::to_string(errno));

	// RFC 8831 6.2. SCTP Association Management
	// The number of streams negotiated during SCTP association setup SHOULD be 65535, which is the
	// maximum number of streams that can be negotiated during the association setup.
	// See https://www.rfc-editor.org/rfc/rfc8831.html#section-6.2
	// However, usrsctp allocates tables to hold the stream states. For 65535 streams, it results in
	// the waste of a few MBs for each association. Therefore, we use a lower limit to save memory.
	// See https://github.com/sctplab/usrsctp/issues/121
	struct sctp_initmsg sinit = {};
	sinit.sinit_num_ostreams = MAX_SCTP_STREAMS_COUNT;
	sinit.sinit_max_instreams = MAX_SCTP_STREAMS_COUNT;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_INITMSG, &sinit, sizeof(sinit)))
		throw std::runtime_error("Could not set socket option SCTP_INITMSG, errno=" +
		                         std::to_string(errno));

	// Prevent fragmented interleave of messages (i.e. level 0), see RFC 6458 section 8.1.20.
	// Unless the user has set the fragmentation interleave level to 0, notifications
	// may also be interleaved with partially delivered messages.
	int level = 0;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_FRAGMENT_INTERLEAVE, &level, sizeof(level)))
		throw std::runtime_error("Could not disable SCTP fragmented interleave, errno=" +
		                         std::to_string(errno));

#ifdef SCTP_ACCEPT_ZERO_CHECKSUM // not available in usrsctp v0.9.5.0
	// When using SCTP over DTLS, the data integrity is ensured by DTLS. Therefore, there's no
	// need to check CRC32c additionally when receiving. See
	// https://datatracker.ietf.org/doc/html/draft-ietf-tsvwg-sctp-zero-checksum
	int edmid = SCTP_EDMID_LOWER_LAYER_DTLS;
	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_ACCEPT_ZERO_CHECKSUM, &edmid, sizeof(edmid)))
		throw std::runtime_error("Could set socket option SCTP_ACCEPT_ZERO_CHECKSUM, errno=" +
		                         std::to_string(errno));
#endif

	int rcvBuf = 0;
	socklen_t rcvBufLen = sizeof(rcvBuf);
	if (usrsctp_getsockopt(mSock, SOL_SOCKET, SO_RCVBUF, &rcvBuf, &rcvBufLen))
		throw std::runtime_error("Could not get SCTP recv buffer size, errno=" +
		                         std::to_string(errno));
	int sndBuf = 0;
	socklen_t sndBufLen = sizeof(sndBuf);
	if (usrsctp_getsockopt(mSock, SOL_SOCKET, SO_SNDBUF, &sndBuf, &sndBufLen))
		throw std::runtime_error("Could not get SCTP send buffer size, errno=" +
		                         std::to_string(errno));

	// Ensure the buffer is also large enough to accomodate the largest messages
	const int minBuf = int(std::min(mMaxMessageSize, size_t(std::numeric_limits<int>::max())));
	rcvBuf = std::max(rcvBuf, minBuf);
	sndBuf = std::max(sndBuf, minBuf);

	if (usrsctp_setsockopt(mSock, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(rcvBuf)))
		throw std::runtime_error("Could not set SCTP recv buffer size, errno=" +
		                         std::to_string(errno));

	if (usrsctp_setsockopt(mSock, SOL_SOCKET, SO_SNDBUF, &sndBuf, sizeof(sndBuf)))
		throw std::runtime_error("Could not set SCTP send buffer size, errno=" +
		                         std::to_string(errno));

#if RTC_SUPERPOS_PROFILE
    usrsctp_register_address(callbackAddress());registeredAddress=true;
    Instances->insert(mSuperposCallbackIdentity,this);publishedIdentity=true;
    }catch(...){
        if(publishedIdentity)Instances->erase(mSuperposCallbackIdentity,this);
        if(mSock)usrsctp_close(mSock);
        if(registeredAddress)usrsctp_deregister_address(callbackAddress());
        throw;
    }
#else
    usrsctp_register_address(this);
    Instances->insert(this);
#endif
}

SctpTransport::~SctpTransport() {
    PLOG_DEBUG << "Destroying SCTP transport";
#if RTC_SUPERPOS_PROFILE
    // Last release in a raw provider callback cannot join/drain the Instances
    // shared lock it holds. Retained PC owners + accepted lifecycle captures
    // prevent this in the supported host contract; fail before freeing on misuse.
    // Another association's callback holds the same registry read lock, too.
    if(NativeSctpCallbackScope::current)std::terminate();
    if(auto* pool=active_processor_pool();pool&&pool->on_scheduler_worker_thread())std::terminate();
    const bool stopRequested=mSuperposStopRequested.exchange(true,std::memory_order_acq_rel);
    // Explicit close retains this transport until the host observes quiescence.
    // Only a caller-owned, never-stopped standalone object may need the final
    // synchronous registry drain. Never put that wait on a scheduler worker.
    if(!mSuperposProducersQuiescent.load(std::memory_order_acquire)){
        if(stopRequested)std::terminate();
        sealProducers();
        if(!mSuperposProducersQuiescent.load(std::memory_order_acquire)){
            Instances->erase(mSuperposCallbackIdentity,this);
            mSuperposProducersQuiescent.store(true,std::memory_order_release);
        }
    }
    mProcessor.join();
    usrsctp_close(mSock);
    usrsctp_deregister_address(callbackAddress());
#else
    mProcessor.join();
    mWrittenOnce = true;
    mWrittenCondition.notify_all();
    unregisterIncoming();
    usrsctp_close(mSock);
    usrsctp_deregister_address(this);
    Instances->erase(this);
#endif
}

void SctpTransport::onBufferedAmount(amount_callback callback) {
	mBufferedAmountCallback = std::move(callback);
}

void SctpTransport::start() {
	registerIncoming();
	connect();
}

void SctpTransport::stop() { close(); }

struct sockaddr_conn SctpTransport::getSockAddrConn(uint16_t port) {
	struct sockaddr_conn sconn = {};
	sconn.sconn_family = AF_CONN;
	sconn.sconn_port = htons(port);
#if RTC_SUPERPOS_PROFILE
    sconn.sconn_addr=callbackAddress();
#else
    sconn.sconn_addr=this;
#endif
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(sconn);
#endif
	return sconn;
}

void SctpTransport::connect() {
	PLOG_DEBUG << "SCTP connecting (local port=" << mPorts.local
	           << ", remote port=" << mPorts.remote << ")";
	changeState(State::Connecting);

	auto local = getSockAddrConn(mPorts.local);
	if (usrsctp_bind(mSock, reinterpret_cast<struct sockaddr *>(&local), sizeof(local)))
		throw std::runtime_error("Could not bind usrsctp socket, errno=" + std::to_string(errno));

	// According to RFC 8841, both endpoints must initiate the SCTP association, in a
	// simultaneous-open manner, irrelevent to the SDP setup role.
	// See https://www.rfc-editor.org/rfc/rfc8841.html#section-9.3
	auto remote = getSockAddrConn(mPorts.remote);
	int ret = usrsctp_connect(mSock, reinterpret_cast<struct sockaddr *>(&remote), sizeof(remote));
	if (ret && errno != EINPROGRESS)
		throw std::runtime_error("Connection attempt failed, errno=" + std::to_string(errno));
}

bool SctpTransport::send(message_ptr message) {
#if RTC_SUPERPOS_PROFILE
    std::unique_lock lock(mSendMutex,std::try_to_lock);
    if(!lock.owns_lock() || mSuperposCallbackActive)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Contended);
    if(mSuperposStopRequested.load(std::memory_order_acquire))throw SuperposSendRejected(SuperposSendRejected::Reason::Closed);
    if(!message)return state()==State::Connected && trySendQueue();
    if(state()!=State::Connected || mSuperposClosing || mSuperposStopRequested.load(std::memory_order_acquire))
        throw SuperposSendRejected(SuperposSendRejected::Reason::Closed);
    superposValidateSend(message);
    const auto remaining=std::numeric_limits<size_t>::max()-mBytesSent.load();
    if(mSuperposBufferedAmount>remaining || message->size()>remaining-mSuperposBufferedAmount)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Counter);
    const bool queue_empty=trySendQueue();
    // Draining invokes a synchronous trusted amount callback. Lifecycle close
    // remains allowed there, but new ownership must observe its final state.
    if(state()!=State::Connected || mSuperposClosing || mSuperposStopRequested.load(std::memory_order_acquire))
        throw SuperposSendRejected(SuperposSendRejected::Reason::Closed);
    superposValidateSend(message);
    const auto after_remaining=std::numeric_limits<size_t>::max()-mBytesSent.load();
    if(mSuperposBufferedAmount>after_remaining || message->size()>after_remaining-mSuperposBufferedAmount)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Counter);
    if(queue_empty && trySendMessage(message))return true;
    if(mSendQueue.full())throw SuperposSendRejected(SuperposSendRejected::Reason::Full);
    // Queue ownership is a deep immutable snapshot. Neither a retained caller
    // Message nor a mutable shared Reliability can grow or poison admitted data.
    // Allocation failure precedes queue admission and bufferedAmount mutation.
    auto reliability=std::make_shared<Reliability>();
    reliability->unordered=true;reliability->maxRetransmits=0;
    auto owned=make_message(message->begin(),message->end(),Message::Binary,0,std::move(reliability));
    superposValidateSend(owned);
    if(!mSendQueue.tryPush(std::move(owned)))
        throw SuperposSendRejected(SuperposSendRejected::Reason::Full);
    // Fixed stream0 accounting cannot allocate or fail after queue ownership.
    updateBufferedAmount(0,ptrdiff_t(message->size()));
    return false;
#else
	std::lock_guard lock(mSendMutex);
	if (state() != State::Connected)
		return false;

	if (!message)
		return trySendQueue();

	PLOG_VERBOSE << "Send size=" << message->size();

	if (message->size() > mMaxMessageSize)
		throw std::invalid_argument("Message is too large");

	// Flush the queue, and if nothing is pending, try to send directly
	if (trySendQueue() && trySendMessage(message))
		return true;

	mSendQueue.push(message);
	updateBufferedAmount(to_uint16(message->stream), ptrdiff_t(message_size_func(message)));
	return false;
#endif
}

bool SctpTransport::flush() {
	try {
#if RTC_SUPERPOS_PROFILE
        std::unique_lock lock(mSendMutex,std::try_to_lock);
        if(!lock.owns_lock() || mSuperposCallbackActive || mSuperposStopRequested.load(std::memory_order_acquire))return false;
#else
		std::lock_guard lock(mSendMutex);
#endif
		if (state() != State::Connected)
			return false;

		trySendQueue();
		return true;

	} catch (const std::exception &e) {
		PLOG_WARNING << "SCTP flush: " << e.what();
		return false;
	}
}

void SctpTransport::closeStream(unsigned int stream) {
	std::lock_guard lock(mSendMutex);
#if RTC_SUPERPOS_PROFILE
    if(stream!=0)throw SuperposSendRejected(SuperposSendRejected::Reason::Channel);
    if(mSuperposClosing || mSuperposStopRequested.load(std::memory_order_acquire))return;
    mSuperposClosing=true;mSuperposResetPending=true;
    // One allocation-free/coalesced reset slot is independent of data credit.
    // Reserved task admission is a separate still-unqualified scheduler gate.
    enqueueFlush();
#else

	// RFC 8831 6.7. Closing a Data Channel
	// Closing of a data channel MUST be signaled by resetting the corresponding outgoing streams
	// See https://www.rfc-editor.org/rfc/rfc8831.html#section-6.7
	mSendQueue.push(make_message(0, Message::Reset, to_uint16(stream)));

	// This method must not call the buffered callback synchronously
	mProcessor.enqueue(&SctpTransport::flush, shared_from_this());
#endif
}

void SctpTransport::close() {
#if RTC_SUPERPOS_PROFILE
    // Callback-side close only publishes intent and reserves owned work. It
    // cannot wait for callback barriers (including the caller's own read lock).
    if(!mSuperposStopRequested.exchange(true,std::memory_order_acq_rel))
        mStopDirty.store(true,std::memory_order_release);
    if(mStopDirty.load(std::memory_order_acquire))enqueueStop();
#else
    mSendQueue.stop();
    if (state() == State::Connected) {
        enqueueFlush();
    } else if (state() == State::Connecting) {
        PLOG_DEBUG << "SCTP early shutdown";
        if (usrsctp_shutdown(mSock, SHUT_RDWR)) {
            if (errno == ENOTCONN) PLOG_VERBOSE << "SCTP already shut down";
            else PLOG_WARNING << "SCTP shutdown failed, errno=" << errno;
        }
        changeState(State::Failed);
        mWrittenCondition.notify_all();
    }
#endif
}
#if RTC_SUPERPOS_PROFILE
void SctpTransport::sealProducers() {
    if(mSuperposProducersQuiescent.load(std::memory_order_acquire))return;
    if(NativeSctpCallbackScope::contains(this))std::terminate();
    // Detach lower ingress before sealing native identities. Another
    // association's admitted callback may hold the shared registry lock; a
    // lifecycle attempt must return to its worker instead of waiting for it.
    unregisterIncoming();
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
    testProducerSealEntered.store(true,std::memory_order_release);
#endif
    if(!Instances->tryErase(mSuperposCallbackIdentity,this))return;
    // Acquiring the exclusive lock proves all admitted callbacks drained;
    // identity removal prevents subsequent callback admission.
    mSuperposProducersQuiescent.store(true,std::memory_order_release);
}
pa::Admission SctpTransport::enqueueStop() {
    if(!mStopDirty.load(std::memory_order_acquire))return pa::Admission::Accepted;
    if(mStopQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
    // An old host-poll observation cannot recreate terminal work after the
    // release of a completed stop token (which publishes StopDirty=false).
    if(!mStopDirty.load(std::memory_order_acquire)){mStopQueued.store(false,std::memory_order_release);return pa::Admission::Accepted;}
    auto reserved=mProcessor.try_reserve(pa::TaskClass::Lifecycle,{},true);
    if(reserved.status!=pa::Admission::Accepted){mStopQueued.store(false,std::memory_order_release);return reserved.status;}
    if(auto strong=weak_from_this().lock()){
        auto result=mProcessor.commit(reserved,&SctpTransport::doStop,std::move(strong));
        if(result!=pa::Admission::Accepted)mStopQueued.store(false,std::memory_order_release);
        return result;
    }
    mStopQueued.store(false,std::memory_order_release);return pa::Admission::Closed;
}
void SctpTransport::doStop() {
    // This strand-owned capture is outside every raw provider callback. Keep
    // StopQueued true until all terminal dirty/admission state is published.
    sealProducers();
    if(!mSuperposProducersQuiescent.load(std::memory_order_acquire)){
        // Keep StopDirty set and retain ownership in the peer. The fixed host
        // poll retries after this lifecycle capture and its cell have retired.
        mStopQueued.store(false,std::memory_order_release);
        return;
    }
    std::lock_guard lock(mSendMutex);
    mSuperposClosing=true;mSendQueue.stop();
    mRecvDirty.store(false,std::memory_order_release);
    mFlushDirty.store(false,std::memory_order_release);
    if(state()==State::Connected){mFlushDirty.store(true,std::memory_order_release);enqueueFlush(true);}
    else if(state()==State::Connecting){
        if(usrsctp_shutdown(mSock,SHUT_RDWR)&&errno!=ENOTCONN){
            PLOG_WARNING << "SCTP shutdown failed, errno=" << errno;
        }
        changeState(State::Failed);
        mWrittenCondition.notify_all();
    }
    mStopDirty.store(false,std::memory_order_release);
    mStopQueued.store(false,std::memory_order_release);
}
#endif

unsigned int SctpTransport::maxStream() const {
	unsigned int streamsCount = mNegotiatedStreamsCount.value_or(MAX_SCTP_STREAMS_COUNT);
	return streamsCount > 0 ? streamsCount - 1 : 0;
}

void SctpTransport::incoming(message_ptr message) {
#if RTC_SUPERPOS_PROFILE
    NativeSctpCallbackScope callbackScope(this);
    if(mSuperposStopRequested.load(std::memory_order_acquire))return;
    // Never wait for a local INIT on the backend receive callback. Native SCTP
    // retries discarded pre-INIT packets; qualify this path under handshake loss.
    // Null closure notifications must still detach immediately.
    if (state() == State::Failed || state() == State::Disconnected) return;
    if (message && (!mWrittenOnce || message->size() > 4096)) return;
#else
	// There could be a race condition here where we receive the remote INIT before the local one is
	// sent, which would result in the connection being aborted. Therefore, we need to wait for data
	// to be sent on our side (i.e. the local INIT) before proceeding.
	if (!mWrittenOnce) { // test the atomic boolean is not set first to prevent a lock contention
		std::unique_lock lock(mWriteMutex);
		mWrittenCondition.wait(lock, [&]() { return mWrittenOnce || state() == State::Failed; });
	}

#endif

	if (state() == State::Failed)
		return;

	if (!message) {
		PLOG_INFO << "SCTP disconnected";
		changeState(State::Disconnected);
		recv(nullptr);
		return;
	}

	PLOG_VERBOSE << "Incoming size=" << message->size();

#if RTC_SUPERPOS_PROFILE
    usrsctp_conninput(callbackAddress(),message->data(),message->size(),0);
#else
    usrsctp_conninput(this,message->data(),message->size(),0);
#endif
}

bool SctpTransport::outgoing(message_ptr message) {
	// Set recommended medium-priority DSCP value
	// See https://www.rfc-editor.org/rfc/rfc8837.html#section-5
	message->dscp = 10; // AF11: Assured Forwarding class 1, low drop probability
	return Transport::outgoing(std::move(message));
}

void SctpTransport::doRecv() {
	std::lock_guard lock(mRecvMutex);
	mRecvDirty.store(false,std::memory_order_release);
	bool continuation=false;size_t pass=0;auto passStart=std::chrono::steady_clock::now();
	try {
		while (state() != State::Disconnected && state() != State::Failed) {
#if RTC_SUPERPOS_PROFILE
            if(mSuperposStopRequested.load(std::memory_order_acquire))break;
#endif
			if(pass++==16 || std::chrono::steady_clock::now()-passStart>=std::chrono::milliseconds(2)){continuation=true;break;}
			const size_t bufferSize = 65536;
			byte buffer[bufferSize];
			socklen_t fromlen = 0;
			struct sctp_rcvinfo info = {};
			socklen_t infolen = sizeof(info);
			unsigned int infotype = 0;
			int flags = 0;
			ssize_t len = usrsctp_recvv(mSock, buffer, bufferSize, nullptr, &fromlen, &info,
			                            &infolen, &infotype, &flags);
			if (len < 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN || errno == ECONNRESET)
					break;
				else
					throw std::runtime_error("SCTP recv failed, errno=" + std::to_string(errno));
			} else if (len == 0) {
				break;
			}

			PLOG_VERBOSE << "SCTP recv, len=" << len;

#ifdef RTC_SUPERPOS_PROFILE
            if(flags&MSG_NOTIFICATION){
                const auto state=mSuperposNotification.append(std::span(reinterpret_cast<const std::byte*>(buffer),static_cast<size_t>(len)),bool(flags&MSG_EOR));
                if(state==RecordAdmission::Complete){
                    const auto bytes=mSuperposNotification.bytes();
                    alignas(union sctp_notification) std::array<std::byte,4096> aligned{};
                    const auto* notification=reinterpret_cast<const union sctp_notification*>(aligned.data());
                    if(bytes.size()<sizeof(notification->sn_header))throw std::runtime_error("Truncated SCTP notification");
                    std::memcpy(aligned.data(),bytes.data(),bytes.size());
                    processNotification(notification,bytes.size());
                }
            }else{
                if(infotype!=SCTP_RECVV_RCVINFO){mSuperposMessage.reject(bool(flags&MSG_EOR));throw std::runtime_error("Missing SCTP receive metadata");}
                const auto ppid=PayloadId(ntohl(info.rcv_ppid));
                if(ppid!=PPID_BINARY&&ppid!=PPID_BINARY_EMPTY){mSuperposMessage.reject(bool(flags&MSG_EOR));continue;}
                const auto state=mSuperposMessage.append(std::span(reinterpret_cast<const std::byte*>(buffer),static_cast<size_t>(len)),bool(flags&MSG_EOR),{info.rcv_sid,static_cast<uint32_t>(ppid)});
                if(state==RecordAdmission::Complete){const auto bytes=mSuperposMessage.bytes();processData(binary(bytes.begin(),bytes.end()),info.rcv_sid,ppid);}
            }
#else
			// SCTP_FRAGMENT_INTERLEAVE does not seem to work as expected for messages > 64KB,
			// therefore partial notifications and messages need to be handled separately.
			if (flags & MSG_NOTIFICATION) {
				// SCTP event notification
				mPartialNotification.insert(mPartialNotification.end(), buffer, buffer + len);

				if (flags & MSG_EOR) {
					// Notification is complete, process it
					binary notification;
					mPartialNotification.swap(notification);
					auto n = reinterpret_cast<union sctp_notification *>(notification.data());
					processNotification(n, notification.size());
				}

			} else {
				// SCTP message
				mPartialMessage.insert(mPartialMessage.end(), buffer, buffer + len);
				if (mPartialMessage.size() > mMaxMessageSize) {
					PLOG_WARNING << "SCTP message is too large, truncating it";
					mPartialMessage.resize(mMaxMessageSize);
				}

				if (flags & MSG_EOR) {
					// Message is complete, process it
					binary message;
					mPartialMessage.swap(message);
					if (infotype != SCTP_RECVV_RCVINFO)
						throw std::runtime_error("Missing SCTP recv info");

					processData(std::move(message), info.rcv_sid, PayloadId(ntohl(info.rcv_ppid)));
				}
			}
#endif
		}
	} catch (const std::exception &e) {
		PLOG_WARNING << e.what();
	}
#if RTC_SUPERPOS_PROFILE
    if(mSuperposStopRequested.load(std::memory_order_acquire)){
        mRecvDirty.store(false,std::memory_order_release);
        mRecvQueued.store(false,std::memory_order_release);return;
    }
#endif
    // Continuation is dirty before the owned queue flag is released.
    const bool again=continuation||mRecvDirty.exchange(false,std::memory_order_acq_rel);
    if(again)mRecvDirty.store(true,std::memory_order_release);
    mRecvQueued.store(false,std::memory_order_release);
    if(again)enqueueRecv();
}

void SctpTransport::doFlush() {
	std::lock_guard lock(mSendMutex);
	mFlushDirty.store(false,std::memory_order_release);
	mFlushContinuation=false;
	try {
		trySendQueue();
	} catch (const std::exception &e) {
		PLOG_WARNING << e.what();
	}
    const bool again=mFlushContinuation||mFlushDirty.exchange(false,std::memory_order_acq_rel);
    if(again)mFlushDirty.store(true,std::memory_order_release);
    mFlushQueued.store(false,std::memory_order_release);
    if(again)enqueueFlush(mSuperposStopRequested.load(std::memory_order_acquire));
}

pa::Admission SctpTransport::enqueueRecv() {
#if RTC_SUPERPOS_PROFILE
    if(mSuperposStopRequested.load(std::memory_order_acquire))return pa::Admission::Accepted;
#endif
    mRecvDirty.store(true,std::memory_order_release);
    if(mRecvQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
    auto reserved=mProcessor.try_reserve(mSuperposStopRequested.load(std::memory_order_acquire)?pa::TaskClass::Lifecycle:mProcessor.lane(),{},true);
    if(reserved.status!=pa::Admission::Accepted){mRecvQueued.store(false,std::memory_order_release);return reserved.status;}
    if(auto strong=weak_from_this().lock()){
        auto result=mProcessor.commit(reserved,&SctpTransport::doRecv,std::move(strong));
        if(result!=pa::Admission::Accepted)mRecvQueued.store(false,std::memory_order_release);
        return result;
    }
    mRecvQueued.store(false,std::memory_order_release);return pa::Admission::Closed;
}
pa::Admission SctpTransport::enqueueFlush(bool terminal) {
#if RTC_SUPERPOS_PROFILE
    if(mSuperposStopRequested.load(std::memory_order_acquire)&&!terminal)return pa::Admission::Accepted;
    if(terminal&&!mFlushDirty.load(std::memory_order_acquire))return pa::Admission::Accepted;
#else
    (void)terminal;
#endif
#if RTC_SUPERPOS_PROFILE
    if(!terminal)mFlushDirty.store(true,std::memory_order_release);
#else
    mFlushDirty.store(true,std::memory_order_release);
#endif
    if(mFlushQueued.exchange(true,std::memory_order_acq_rel))return pa::Admission::Accepted;
#if RTC_SUPERPOS_PROFILE
    if(terminal&&!mFlushDirty.load(std::memory_order_acquire)){mFlushQueued.store(false,std::memory_order_release);return pa::Admission::Accepted;}
#endif
    auto reserved=mProcessor.try_reserve(mSuperposStopRequested.load(std::memory_order_acquire)?pa::TaskClass::Lifecycle:pa::TaskClass::Control,{},true);
    if(reserved.status!=pa::Admission::Accepted){mFlushQueued.store(false,std::memory_order_release);return reserved.status;}
    if(auto strong=weak_from_this().lock()){
        auto result=mProcessor.commit(reserved,&SctpTransport::doFlush,std::move(strong));
        if(result!=pa::Admission::Accepted)mFlushQueued.store(false,std::memory_order_release);
        return result;
    }
    mFlushQueued.store(false,std::memory_order_release);return pa::Admission::Closed;
}
pa::Admission SctpTransport::pollTasks() noexcept {
#if RTC_SUPERPOS_PROFILE
    if(mStopDirty.load(std::memory_order_acquire)&&!mStopQueued.load(std::memory_order_acquire))enqueueStop();
#endif
    if(mRecvDirty.load(std::memory_order_acquire)&&!mRecvQueued.load(std::memory_order_acquire))enqueueRecv();
    if(mFlushDirty.load(std::memory_order_acquire)&&!mFlushQueued.load(std::memory_order_acquire))enqueueFlush(mSuperposStopRequested.load(std::memory_order_acquire));
    return mProcessor.status();
}

bool SctpTransport::trySendQueue() {
	// Requires mSendMutex to be locked
#if RTC_SUPERPOS_PROFILE
    std::size_t quantum{};
#endif
	while (auto next = mSendQueue.peek()) {
#if RTC_SUPERPOS_PROFILE
        if(quantum++==superposSendQuantum){mFlushContinuation=true;return false;}
#endif
		message_ptr message = std::move(*next);
		if (!trySendMessage(message))
			return false;

		mSendQueue.pop();
		updateBufferedAmount(to_uint16(message->stream), -ptrdiff_t(message_size_func(message)));
	}
#if RTC_SUPERPOS_PROFILE
    if(mSuperposResetPending){
        if(!sendReset(0))return false;
        mSuperposResetPending=false;
    }
#endif

	if (!mSendQueue.running() && !std::exchange(mSendShutdown, true)) {
		PLOG_DEBUG << "SCTP shutdown";
		if (usrsctp_shutdown(mSock, SHUT_WR)) {
			if (errno == ENOTCONN) {
				PLOG_VERBOSE << "SCTP already shut down";
			} else {
				PLOG_WARNING << "SCTP shutdown failed, errno=" << errno;
				changeState(State::Disconnected);
				recv(nullptr);
			}
		}
	}

	return true;
}

bool SctpTransport::trySendMessage(message_ptr message) {
	// Requires mSendMutex to be locked
	if (state() != State::Connected)
		return false;
#if RTC_SUPERPOS_PROFILE
    // Validate again at the retained-message boundary. Trusted callers must not
    // mutate queued Message payloads or their shared reliability configuration.
    superposValidateSend(message);
    if(message->size()>std::numeric_limits<size_t>::max()-mBytesSent.load())
        throw SuperposSendRejected(SuperposSendRejected::Reason::Counter);
#endif

	uint32_t ppid;
	switch (message->type) {
	case Message::String:
		ppid = !message->empty() ? PPID_STRING : PPID_STRING_EMPTY;
		break;
	case Message::Binary:
		ppid = !message->empty() ? PPID_BINARY : PPID_BINARY_EMPTY;
		break;
	case Message::Control:
		ppid = PPID_CONTROL;
		break;
	case Message::Reset:
		sendReset(uint16_t(message->stream));
		return true;
	default:
		// Ignore
		return true;
	}

	PLOG_VERBOSE << "SCTP try send size=" << message->size();

	// TODO: Implement SCTP ndata specification draft when supported everywhere
	// See https://datatracker.ietf.org/doc/html/draft-ietf-tsvwg-sctp-ndata-08

	const Reliability reliability = message->reliability ? *message->reliability : Reliability();

	struct sctp_sendv_spa spa = {};

	// set sndinfo
	spa.sendv_flags |= SCTP_SEND_SNDINFO_VALID;
	spa.sendv_sndinfo.snd_sid = uint16_t(message->stream);
	spa.sendv_sndinfo.snd_ppid = htonl(ppid);
	spa.sendv_sndinfo.snd_flags |= SCTP_EOR; // implicit here

	// set prinfo
	spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
	if (reliability.unordered)
		spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;

	if (reliability.maxPacketLifeTime) {
		spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
		spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
		spa.sendv_prinfo.pr_value = to_uint32(reliability.maxPacketLifeTime->count());
	} else if (reliability.maxRetransmits) {
		spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
		spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
		spa.sendv_prinfo.pr_value = to_uint32(*reliability.maxRetransmits);
	}
	// else {
	// 	spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;
	// }
	// Deprecated
	else switch (reliability.typeDeprecated) {
	case Reliability::Type::Rexmit:
		spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
		spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
		spa.sendv_prinfo.pr_value = to_uint32(std::get<int>(reliability.rexmit));
		break;
	case Reliability::Type::Timed:
		spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
		spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
		spa.sendv_prinfo.pr_value = to_uint32(std::get<milliseconds>(reliability.rexmit).count());
		break;
	default:
		spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;
		break;
	}

	ssize_t ret;
	if (!message->empty()) {
		ret = usrsctp_sendv(mSock, message->data(), message->size(), nullptr, 0, &spa, sizeof(spa),
		                    SCTP_SENDV_SPA, 0);
	} else {
		const char zero = 0;
		ret = usrsctp_sendv(mSock, &zero, 1, nullptr, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
	}

#ifdef RTC_SUPERPOS_SEND_RESULT_TESTING
    // Test builds alter only the observed native result after the real call.
    extern std::ptrdiff_t superposTestSendResult(std::ptrdiff_t,std::size_t);
    ret=superposTestSendResult(ret,message->size());
#endif
	if (ret < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			PLOG_VERBOSE << "SCTP sending not possible";
			return false;
		}

		PLOG_ERROR << "SCTP sending failed, errno=" << errno;
		throw std::runtime_error("Sending failed, errno=" + std::to_string(errno));
	}

#if RTC_SUPERPOS_PROFILE
    if(static_cast<std::size_t>(ret)!=message->size()) {
        // Seal admission before any callback or task allocation. The result
        // can describe a partially accepted record: never retry its payload.
        mSuperposClosing=true;
        mSuperposStopRequested.store(true,std::memory_order_release);
        mStopDirty.store(true,std::memory_order_release);
        changeState(State::Failed);
        enqueueStop();
        throw SuperposSendUncertain();
    }
#endif
	PLOG_VERBOSE << "SCTP sent size=" << message->size();
	if (message->type == Message::Binary || message->type == Message::String)
		mBytesSent += message->size();
	return true;
}

void SctpTransport::updateBufferedAmount(uint16_t streamId, ptrdiff_t delta) {
	// Requires mSendMutex to be locked

	if (delta == 0)
		return;
#if RTC_SUPERPOS_PROFILE
    // Exactly one negotiated stream; the queue's fixed record/frame bounds
    // guarantee these additions/subtractions. No std::map node is allocated.
    if(delta>0)mSuperposBufferedAmount+=static_cast<size_t>(delta);
    else mSuperposBufferedAmount-=static_cast<size_t>(-delta);
    triggerBufferedAmount(0,mSuperposBufferedAmount);
#else

	auto it = mBufferedAmount.insert(std::make_pair(streamId, 0)).first;
	size_t amount = size_t(std::max(ptrdiff_t(it->second) + delta, ptrdiff_t(0)));
	if (amount == 0)
		mBufferedAmount.erase(it);
	else
		it->second = amount;

	// Synchronously call the buffered amount callback
	triggerBufferedAmount(streamId, amount);
#endif
}
#if RTC_SUPERPOS_PROFILE
SuperposSendSnapshot SctpTransport::superposSendSnapshot() {
    std::lock_guard lock(mSendMutex);
    return {mSendQueue.size(),mSendQueue.amount(),mSuperposBufferedAmount,
        mSendQueue.reservedBytes(),mSuperposClosing,mSuperposResetPending,mSuperposCallbackFailed};
}
#endif

void SctpTransport::triggerBufferedAmount(uint16_t streamId, size_t amount) {
#if RTC_SUPERPOS_PROFILE
    // Callback recursion cannot admit new data or recursively expand a flush
    // quantum. Lifecycle close/snapshot remain permitted under the recursive
    // send mutex. The callback flag is restored even for unknown exceptions.
    struct CallbackGuard {
        bool& active;
        explicit CallbackGuard(bool& value):active(value){active=true;}
        ~CallbackGuard(){active=false;}
    } callback_guard(mSuperposCallbackActive);
    try {mBufferedAmountCallback(streamId,amount);}
    catch (...) {
        // No logging allocation after accepted ownership. The private snapshot
        // exposes callback failure without throwing or losing retained data.
        mSuperposCallbackFailed=true;
    }
#else
	try {
		mBufferedAmountCallback(streamId, amount);
	} catch (const std::exception &e) {
		PLOG_WARNING << "SCTP buffered amount callback: " << e.what();
	}
#endif
}

bool SctpTransport::sendReset(uint16_t streamId) {
	// Requires mSendMutex to be locked
	if (state() != State::Connected)
		return false;

	PLOG_DEBUG << "SCTP resetting stream " << streamId;

	using srs_t = struct sctp_reset_streams;
	const size_t len = sizeof(srs_t) + sizeof(uint16_t);
	alignas(alignof(srs_t)) byte buffer[len] = {};
	srs_t &srs = *reinterpret_cast<srs_t *>(buffer);
	srs.srs_flags = SCTP_STREAM_RESET_OUTGOING;
	srs.srs_number_streams = 1;
	srs.srs_stream_list[0] = streamId;

	if (usrsctp_setsockopt(mSock, IPPROTO_SCTP, SCTP_RESET_STREAMS, &srs, len)) {
#if RTC_SUPERPOS_PROFILE
        // EINVAL also means association-not-open or invalid stream/body in the
        // pinned usrsctp provider; it cannot attest a previously sent reset.
        return false;
#else
		if (errno == EINVAL) {
			PLOG_DEBUG << "SCTP stream " << streamId << " already reset";
		} else {
			PLOG_WARNING << "SCTP reset stream " << streamId << " failed, errno=" << errno;
			return false;
		}
#endif
	}
	return true;
}

void SctpTransport::handleUpcall() noexcept {
#if RTC_SUPERPOS_PROFILE
    if(mSuperposStopRequested.load(std::memory_order_acquire))return;
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
    testUpcallVisits.fetch_add(1,std::memory_order_relaxed);
#endif
#endif
	try {
		PLOG_VERBOSE << "Handle upcall";

		int events = usrsctp_get_events(mSock);

		if (events & SCTP_EVENT_READ)
			enqueueRecv();

		if (events & SCTP_EVENT_WRITE)
			enqueueFlush();

	} catch (const std::exception &e) {
		PLOG_ERROR << "SCTP upcall: " << e.what();
	}
}

int SctpTransport::handleWrite(byte *data, size_t len, uint8_t /*tos*/,
                               uint8_t /*set_df*/) noexcept {
    try {
#if RTC_SUPERPOS_PROFILE
        if(mSuperposStopRequested.load(std::memory_order_acquire))return -1;
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
        testWriteVisits.fetch_add(1,std::memory_order_relaxed);
#endif
#endif
        std::unique_lock lock(mWriteMutex);
		PLOG_VERBOSE << "Handle write, len=" << len;

		if (!outgoing(make_message(data, data + len)))
			return -1;

		mWritten = true;
		mWrittenOnce = true;
		mWrittenCondition.notify_all();

	} catch (const std::exception &e) {
		PLOG_ERROR << "SCTP write: " << e.what();
		return -1;
	}
	return 0; // success
}

void SctpTransport::processData(binary &&data, uint16_t sid, PayloadId ppid) {
	PLOG_VERBOSE << "Process data, size=" << data.size();

	// RFC 8831: The usage of the PPIDs "WebRTC String Partial" and "WebRTC Binary Partial" is
	// deprecated. They were used for a PPID-based fragmentation and reassembly of user messages
	// belonging to reliable and ordered data channels.
	// See https://www.rfc-editor.org/rfc/rfc8831.html#section-6.6
	// We handle those PPIDs at reception for compatibility reasons but shall never send them.
	switch (ppid) {
	case PPID_CONTROL:
		recv(make_message(std::move(data), Message::Control, sid));
		break;

	case PPID_STRING_PARTIAL: // deprecated
		mPartialStringData.insert(mPartialStringData.end(), data.begin(), data.end());
		mPartialStringData.resize(mMaxMessageSize);
		break;

	case PPID_STRING:
		if (mPartialStringData.empty()) {
			mBytesReceived += data.size();
			recv(make_message(std::move(data), Message::String, sid));
		} else {
			mPartialStringData.insert(mPartialStringData.end(), data.begin(), data.end());
			mPartialStringData.resize(mMaxMessageSize);
			mBytesReceived += mPartialStringData.size();
			auto message = make_message(std::move(mPartialStringData), Message::String, sid);
			mPartialStringData.clear();
			recv(std::move(message));
		}
		break;

	case PPID_STRING_EMPTY:
		recv(make_message(std::move(mPartialStringData), Message::String, sid));
		mPartialStringData.clear();
		break;

	case PPID_BINARY_PARTIAL: // deprecated
		mPartialBinaryData.insert(mPartialBinaryData.end(), data.begin(), data.end());
		mPartialBinaryData.resize(mMaxMessageSize);
		break;

	case PPID_BINARY:
		if (mPartialBinaryData.empty()) {
			mBytesReceived += data.size();
			recv(make_message(std::move(data), Message::Binary, sid));
		} else {
			mPartialBinaryData.insert(mPartialBinaryData.end(), data.begin(), data.end());
			mPartialBinaryData.resize(mMaxMessageSize);
			mBytesReceived += mPartialBinaryData.size();
			auto message = make_message(std::move(mPartialBinaryData), Message::Binary, sid);
			mPartialBinaryData.clear();
			recv(std::move(message));
		}
		break;

	case PPID_BINARY_EMPTY:
		recv(make_message(std::move(mPartialBinaryData), Message::Binary, sid));
		mPartialBinaryData.clear();
		break;

	default:
		// Unknown
		COUNTER_UNKNOWN_PPID++;
		PLOG_VERBOSE << "Unknown PPID: " << uint32_t(ppid);
		return;
	}
}

void SctpTransport::processNotification(const union sctp_notification *notify, size_t len) {
#if RTC_SUPERPOS_PROFILE
	if (!superposNotificationValid({reinterpret_cast<const std::byte *>(notify),len})) {
		PLOG_WARNING << "Rejected malformed bounded SCTP notification";
		return;
	}
#endif
	if (len != size_t(notify->sn_header.sn_length)) {
		PLOG_WARNING << "Unexpected notification length, expected=" << notify->sn_header.sn_length
		             << ", actual=" << len;
		return;
	}

	auto type = notify->sn_header.sn_type;
	PLOG_VERBOSE << "Processing notification, type=" << type;

	switch (type) {
	case SCTP_ASSOC_CHANGE: {
		PLOG_VERBOSE << "SCTP association change event";
		const struct sctp_assoc_change &sac = notify->sn_assoc_change;
		if (sac.sac_state == SCTP_COMM_UP) {
			PLOG_DEBUG << "SCTP negotiated streams: incoming=" << sac.sac_inbound_streams
			           << ", outgoing=" << sac.sac_outbound_streams;
			mNegotiatedStreamsCount.emplace(
			    std::min(sac.sac_inbound_streams, sac.sac_outbound_streams));

			PLOG_INFO << "SCTP connected";
			changeState(State::Connected);
		} else {
			if (state() == State::Connected) {
				PLOG_INFO << "SCTP disconnected";
				changeState(State::Disconnected);
				recv(nullptr);
			} else {
				PLOG_ERROR << "SCTP connection failed";
				changeState(State::Failed);
			}
			mWrittenCondition.notify_all();
		}
		break;
	}

	case SCTP_SENDER_DRY_EVENT: {
		PLOG_VERBOSE << "SCTP sender dry event";
		// It should not be necessary since the send callback should have been called already,
		// but to be sure, let's try to send now.
		flush();
		break;
	}

	case SCTP_STREAM_RESET_EVENT: {
		const struct sctp_stream_reset_event &reset_event = notify->sn_strreset_event;
		const int count = (reset_event.strreset_length - sizeof(reset_event)) / sizeof(uint16_t);
		const uint16_t flags = reset_event.strreset_flags;

		IF_PLOG(plog::verbose) {
			std::ostringstream desc;
			desc << "flags=";
			if (flags & SCTP_STREAM_RESET_OUTGOING_SSN && flags & SCTP_STREAM_RESET_INCOMING_SSN)
				desc << "outgoing|incoming";
			else if (flags & SCTP_STREAM_RESET_OUTGOING_SSN)
				desc << "outgoing";
			else if (flags & SCTP_STREAM_RESET_INCOMING_SSN)
				desc << "incoming";
			else
				desc << "0";

			desc << ", streams=[";
			for (int i = 0; i < count; ++i) {
				uint16_t streamId = reset_event.strreset_stream_list[i];
				desc << (i != 0 ? "," : "") << streamId;
			}
			desc << "]";

			PLOG_VERBOSE << "SCTP reset event, " << desc.str();
		}

		// RFC 8831 6.7. Closing a Data Channel
		// If one side decides to close the data channel, it resets the corresponding outgoing
		// stream. When the peer sees that an incoming stream was reset, it also resets its
		// corresponding outgoing stream.
		// See https://www.rfc-editor.org/rfc/rfc8831.html#section-6.7
		if (flags & SCTP_STREAM_RESET_INCOMING_SSN) {
			for (int i = 0; i < count; ++i) {
				uint16_t streamId = reset_event.strreset_stream_list[i];
				recv(make_message(0, Message::Reset, streamId));
			}
		}
		break;
	}

	default:
		// Ignore
		break;
	}
}

void SctpTransport::clearStats() {
	mBytesReceived = 0;
	mBytesSent = 0;
}

size_t SctpTransport::bytesSent() { return mBytesSent; }

size_t SctpTransport::bytesReceived() { return mBytesReceived; }

optional<milliseconds> SctpTransport::rtt() {
	if (state() != State::Connected)
		return nullopt;

	struct sctp_status status = {};
	socklen_t len = sizeof(status);
	if (usrsctp_getsockopt(mSock, IPPROTO_SCTP, SCTP_STATUS, &status, &len))
		return nullopt;

	return milliseconds(status.sstat_primary.spinfo_srtt);
}

void SctpTransport::UpcallCallback(struct socket *,void* arg,int /*flags*/) {
#if RTC_SUPERPOS_PROFILE
    auto locked=Instances->lookup(arg);
    if(locked){auto* transport=locked->transport;
#else
    auto* transport=static_cast<SctpTransport*>(arg);
    if(auto locked=Instances->lock(transport)){
#endif
#if RTC_SUPERPOS_PROFILE
        NativeSctpCallbackScope scope(transport);
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
        if(transport->testPauseUpcall.exchange(false)){
            if(transport->testCloseFromUpcall.load()){
                transport->close();transport->testCallbackCloseReturned.store(true,std::memory_order_release);
            }
            transport->testUpcallEntered.store(true,std::memory_order_release);
            const auto deadline=std::chrono::steady_clock::now()+2s;
            while(!transport->testUpcallRelease.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        }
#endif
#endif
        transport->handleUpcall();
    }
}

int SctpTransport::WriteCallback(void *ptr, void *data, size_t len, uint8_t tos, uint8_t set_df) {
#if RTC_SUPERPOS_PROFILE
    auto locked=Instances->lookup(ptr);
    if(!locked)return -1;
    auto* transport=locked->transport;
    NativeSctpCallbackScope scope(transport);
#else
    auto* transport=static_cast<SctpTransport*>(ptr);
#endif
#ifndef SCTP_ACCEPT_ZERO_CHECKSUM
	// Set the CRC32 ourselves as we have enabled CRC32 offloading
	if (len >= 12) {
		uint32_t *checksum = reinterpret_cast<uint32_t *>(data) + 2;
		*checksum = 0;
		*checksum = usrsctp_crc32c(data, len);
	}
#endif

	// Workaround for sctplab/usrsctp#405: Send callback is invoked on already closed socket
	// https://github.com/sctplab/usrsctp/issues/405
#if RTC_SUPERPOS_PROFILE
    return transport->handleWrite(static_cast<byte*>(data),len,tos,set_df);
#else
    if(auto locked=Instances->lock(transport))return transport->handleWrite(static_cast<byte*>(data),len,tos,set_df);
    return -1;
#endif
}

void SctpTransport::DebugCallback(const char *format, ...) {
	const size_t bufferSize = 1024;
	char buffer[bufferSize];
	va_list va;
	va_start(va, format);
	int len = std::vsnprintf(buffer, bufferSize, format, va);
	va_end(va);
	if (len <= 0)
		return;

	len = std::min(len, int(bufferSize - 1));
	buffer[len - 1] = '\0'; // remove newline

	PLOG_VERBOSE << "usrsctp: " << buffer; // usrsctp debug as verbose
}

} // namespace rtc::impl

