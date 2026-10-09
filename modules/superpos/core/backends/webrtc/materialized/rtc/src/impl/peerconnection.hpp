/**
 * Copyright (c) 2019-2021 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_PEER_CONNECTION_H
#define RTC_IMPL_PEER_CONNECTION_H

#include "common.hpp"
#include "datachannel.hpp"
#include "dtlstransport.hpp"
#include "icetransport.hpp"
#include "init.hpp"
#include "processor.hpp"
#include "observed_callback.hpp"
#if RTC_SUPERPOS_PROFILE
#include "profile_boundary.hpp"
#endif
#include "sctptransport.hpp"
#include "track.hpp"

#include "rtc/peerconnection.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace rtc::impl {

struct PeerConnection : std::enable_shared_from_this<PeerConnection> {
	using State = rtc::PeerConnection::State;
	using IceState = rtc::PeerConnection::IceState;
	using GatheringState = rtc::PeerConnection::GatheringState;
	using SignalingState = rtc::PeerConnection::SignalingState;

	PeerConnection(Configuration config_);
	~PeerConnection();

	void close();
	::superpos::processor_admission::Admission superposTaskPoll() noexcept;
    bool superposCloseComplete()const noexcept{
#if RTC_SUPERPOS_PROFILE
        return mTaskCloseComplete.load(std::memory_order_acquire);
#else
        return state.load()==State::Closed;
#endif
    }
	rtc::PeerConnection::SuperposIdentity superposVerifiedIdentity() const noexcept;
	void remoteClose();

	optional<Description> localDescription() const;
	optional<Description> remoteDescription() const;
	size_t remoteMaxMessageSize() const;

	shared_ptr<IceTransport> initIceTransport();
	shared_ptr<DtlsTransport> initDtlsTransport();
	shared_ptr<SctpTransport> initSctpTransport();
	shared_ptr<IceTransport> getIceTransport() const;
	shared_ptr<DtlsTransport> getDtlsTransport() const;
	shared_ptr<SctpTransport> getSctpTransport() const;
	void closeTransports();
	enum TaskWork : unsigned {OpenData=1,OpenMedia=2,PendingData=4,PendingMedia=8,CloseData=16};
	pa::Admission requestTask(unsigned bits) noexcept;
	void doTaskWork();
	void finishTaskWork() noexcept;

	// Construction retries own only two bits and one coalesced control job.
	// They do not spin in provider callbacks or consume ordinary data credits.
	enum TransportInit : unsigned {InitDtls=1,InitSctp=2};
	pa::Admission requestTransportInit(unsigned bits) noexcept;
	void doTransportInit();
	std::atomic<unsigned> mTransportInitPending{};
	std::atomic<bool> mTransportInitQueued{};
	std::atomic<unsigned> mTaskWork{};
	std::atomic<bool> mTaskWorkQueued{};
	static constexpr unsigned StateEvents=32,StateEventCapacity=16;
	struct ScalarStateEvent {std::atomic<bool> ready{};unsigned kind{},value{};};
	std::array<ScalarStateEvent,StateEventCapacity> mStateEvents{};
	std::atomic<std::uint64_t> mStateHead{},mStateTail{};
	std::atomic<bool> mStateEventsClosed{};
    std::atomic<unsigned> mStateAdmissions{};
	struct ScalarReserved {pa::Admission status;std::uint64_t position;};
	ScalarReserved reserveStateEvent() noexcept;
	pa::Admission publishStateEvent(ScalarReserved,unsigned kind,unsigned value) noexcept;
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
	std::atomic<bool> mTestStatePause{},mTestStateEntered{},mTestStateRelease{};
    std::atomic<bool> mTestStateBeforePause{},mTestStateBeforeEntered{},mTestStateBeforeRelease{};
#endif
	pa::Admission queueStateEvent(unsigned kind,unsigned value) noexcept;
	void drainStateEvents();
#if RTC_SUPERPOS_PROFILE
    static constexpr unsigned SignalingCapacity=9, CandidateCapacity=8;
    struct SignalingRecord {
        std::atomic<bool> ready{};
        unsigned kind{}; // 0 Description, 1 Candidate, 2 cancelled Description, 3 cancelled Candidate
        optional<Description> description;
        optional<Candidate> candidate;
    };
    struct SignalingReserved {pa::Admission status;std::uint64_t position;unsigned kind;};
    std::array<SignalingRecord,SignalingCapacity> mSignaling{};
    std::atomic<std::uint64_t> mSignalingHead{},mSignalingTail{};
    std::atomic<unsigned> mSignalingDescriptions{},mSignalingCandidates{};
    std::atomic<bool> mSignalingClosed{},mSignalingQueued{};
    std::atomic_flag mSignalingGate=ATOMIC_FLAG_INIT;
    bool mProfileLocalDescriptionIssued{}; // protected by the description mutex
    unsigned mProfileLocalCandidates{}; // lifetime cap, not just pending work
    SignalingReserved reserveSignaling(unsigned kind) noexcept;
    void publishSignaling(SignalingReserved,optional<Description>,optional<Candidate>) noexcept;
    void cancelSignaling(SignalingReserved) noexcept;
    pa::Admission requestSignaling() noexcept;
    void finishSignaling() noexcept;
    void drainSignaling(bool discard=false);
    void doSignaling();
#endif

	void endLocalCandidates();
	void rollbackLocalDescription();
	bool checkFingerprint(const std::string &fingerprint);
	void forwardMessage(message_ptr message);
	void forwardMedia(message_ptr message);
	void forwardBufferedAmount(uint16_t stream, size_t amount);

	shared_ptr<DataChannel> emplaceDataChannel(string label, DataChannelInit init, shared_ptr<rtc::DataChannel>* publicResult=nullptr);
	std::pair<shared_ptr<DataChannel>, bool> findDataChannel(uint16_t stream);
	bool removeDataChannel(uint16_t stream);
	uint16_t maxDataChannelStream() const;
	void assignDataChannels();
	void iterateDataChannels(std::function<void(shared_ptr<DataChannel> channel)> func);
	void openDataChannels();
	void closeDataChannels();
	void remoteCloseDataChannels();

	shared_ptr<Track> emplaceTrack(Description::Media description);
	void iterateTracks(std::function<void(shared_ptr<Track> track)> func);
	void iterateRemoteTracks(std::function<void(shared_ptr<Track> track)> func);
	void openTracks();
	void closeTracks();

	void validateRemoteDescription(const Description &description);
	void populateLocalDescription(Description &description) const;
	void processLocalDescription(Description description);
	void processLocalCandidate(Candidate candidate);
	void processRemoteDescription(Description description);
	void processRemoteCandidate(Candidate candidate);
	string localBundleMid() const;

	bool negotiationNeeded() const;

	void setMediaHandler(shared_ptr<MediaHandler> handler);
	shared_ptr<MediaHandler> getMediaHandler() const;

	void triggerDataChannel(weak_ptr<DataChannel> weakDataChannel);
	void triggerTrack(weak_ptr<Track> weakTrack);

	void triggerPendingDataChannels();
	void triggerPendingTracks();

	void flushPendingDataChannels();
	void flushPendingTracks();

	bool changeState(State newState);
	bool changeIceState(IceState newState);
	bool changeGatheringState(GatheringState newState);
	bool changeSignalingState(SignalingState newState);

	void resetCallbacks();

	CertificateFingerprint remoteFingerprint();

	// Helper method for asynchronous callback invocation
	template <typename... Args> void trigger(synchronized_callback<Args...> *cb, Args... args) {
		try {
			(*cb)(std::move(args...));
		} catch (const std::exception &e) {
			PLOG_WARNING << "Uncaught exception in callback: " << e.what();
		}
	}

	const Configuration config;
	std::atomic<State> state = State::New;
	std::atomic<IceState> iceState = IceState::New;
	std::atomic<GatheringState> gatheringState = GatheringState::New;
	std::atomic<SignalingState> signalingState = SignalingState::Stable;
	std::atomic<bool> closing = false;
	std::atomic<bool> mTaskCloseRequested{},mTaskCloseQueued{};
    std::atomic<bool> mTaskClosePrepared{},mTaskCloseComplete{};
	std::mutex signalingMutex;

	synchronized_callback<shared_ptr<rtc::DataChannel>> dataChannelCallback;
	synchronized_callback<Description> localDescriptionCallback;
	synchronized_callback<Candidate> localCandidateCallback;
	observed_callback<State> stateChangeCallback;
	observed_callback<IceState> iceStateChangeCallback;
	observed_callback<GatheringState> gatheringStateChangeCallback;
	observed_callback<SignalingState> signalingStateChangeCallback;
	synchronized_callback<shared_ptr<rtc::Track>> trackCallback;

private:
#if RTC_SUPERPOS_PROFILE
    // A successful negotiated channel occupies the one lifetime reservation,
    // even after reset/close or destruction of its last public wrapper.
    std::atomic<::superpos::rtc_profile::ChannelState> mSuperposChannelAdmission{::superpos::rtc_profile::ChannelState::Empty};
#endif
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
    friend struct SuperposPeerProfileTestAccess;
#endif
	void dispatchMedia(message_ptr message);
	void updateTrackSsrcCache(const Description &description);

	const init_token mInitToken = Init::Instance().token();
	future_certificate_ptr mCertificate;

	Processor mProcessor;
	optional<Description> mLocalDescription;
	optional<Description> mCurrentLocalDescription;
	mutable std::mutex mLocalDescriptionMutex;

	optional<Description> mRemoteDescription;
	CertificateFingerprint::Algorithm mRemoteFingerprintAlgorithm = CertificateFingerprint::Algorithm::Sha256;
	optional<string> mRemoteFingerprint;
    std::array<std::byte,32> mSuperposLocalIdentity{}, mSuperposRemoteIdentity{};
    bool mSuperposLocalIdentityValid = false, mSuperposRemoteIdentityValid = false;
	mutable std::mutex mRemoteDescriptionMutex;

	shared_ptr<MediaHandler> mMediaHandler;
	mutable std::shared_mutex mMediaHandlerMutex;

	shared_ptr<IceTransport> mIceTransport;
	shared_ptr<DtlsTransport> mDtlsTransport;
	std::mutex mDtlsTransportInitMutex;
	std::mutex mSctpTransportInitMutex;
	shared_ptr<SctpTransport> mSctpTransport;

	std::unordered_map<uint16_t, weak_ptr<DataChannel>> mDataChannels; // by stream ID
	std::vector<weak_ptr<DataChannel>> mUnassignedDataChannels;
	mutable std::shared_mutex mDataChannelsMutex;

	std::unordered_map<string, weak_ptr<Track>> mTracks;         // by mid
	std::unordered_map<uint32_t, weak_ptr<Track>> mTracksBySsrc; // by SSRC
	std::vector<weak_ptr<Track>> mTrackLines;                    // by SDP order
	mutable std::shared_mutex mTracksMutex;

	Queue<shared_ptr<DataChannel>> mPendingDataChannels;
	Queue<shared_ptr<Track>> mPendingTracks;
};

} // namespace rtc::impl

#endif
