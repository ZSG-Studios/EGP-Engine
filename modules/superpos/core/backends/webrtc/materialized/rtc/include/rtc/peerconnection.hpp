/**
 * Copyright (c) 2019 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_PEER_CONNECTION_H
#define RTC_PEER_CONNECTION_H

#include "candidate.hpp"
#include "common.hpp"
#include "configuration.hpp"
#include "datachannel.hpp"
#include "description.hpp"
#include "reliability.hpp"
#include "track.hpp"

#include <array>
#include <cstddef>
#include <chrono>
#include <functional>

namespace rtc {

namespace impl {

struct PeerConnection;
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
struct SuperposPeerProfileTestAccess;
#endif

}

struct RTC_CPP_EXPORT DataChannelInit {
	Reliability reliability = {};
	bool negotiated = false;
	optional<uint16_t> id = nullopt;
	string protocol = "";
};

struct RTC_CPP_EXPORT LocalDescriptionInit {
    optional<string> iceUfrag;
    optional<string> icePwd;
};

class RTC_CPP_EXPORT PeerConnection final : CheshireCat<impl::PeerConnection> {
#ifdef RTC_SUPERPOS_PEER_PROFILE_TESTING
    friend struct impl::SuperposPeerProfileTestAccess;
#endif
public:
	enum class State : int {
		New = RTC_NEW,
		Connecting = RTC_CONNECTING,
		Connected = RTC_CONNECTED,
		Disconnected = RTC_DISCONNECTED,
		Failed = RTC_FAILED,
		Closed = RTC_CLOSED
	};

	enum class IceState : int {
		New = RTC_ICE_NEW,
		Checking = RTC_ICE_CHECKING,
		Connected = RTC_ICE_CONNECTED,
		Completed = RTC_ICE_COMPLETED,
		Failed = RTC_ICE_FAILED,
		Disconnected = RTC_ICE_DISCONNECTED,
		Closed = RTC_ICE_CLOSED
	};

	enum class GatheringState : int {
		New = RTC_GATHERING_NEW,
		InProgress = RTC_GATHERING_INPROGRESS,
		Complete = RTC_GATHERING_COMPLETE
	};

	enum class SignalingState : int {
		Stable = RTC_SIGNALING_STABLE,
		HaveLocalOffer = RTC_SIGNALING_HAVE_LOCAL_OFFER,
		HaveRemoteOffer = RTC_SIGNALING_HAVE_REMOTE_OFFER,
		HaveLocalPranswer = RTC_SIGNALING_HAVE_LOCAL_PRANSWER,
		HaveRemotePranswer = RTC_SIGNALING_HAVE_REMOTE_PRANSWER,
	};

	PeerConnection();
	PeerConnection(Configuration config);
	~PeerConnection();

	void close();
	::superpos::processor_admission::Admission superposTaskPoll() noexcept;
    bool superposCloseComplete()const noexcept;
    using SuperposRetirement=impl::retirement::Token;
    SuperposRetirement superposRetirement()const noexcept;
    // Full wrapper lifetime is tracked only through this private-profile factory.
    static shared_ptr<PeerConnection> superposCreate(Configuration config);
    struct SuperposIdentity {
        enum class Status { Ready, NotReady, Busy, Unsupported, VerificationDisabled };
        Status status = Status::NotReady;
        std::array<std::byte,32> local{}, remote{};
    };
    // Actual local certificate and successfully verified remote DTLS certificate.
    // No identity is returned before connection or after close is requested.
    SuperposIdentity superposVerifiedIdentity() const noexcept;

	const Configuration *config() const;
	State state() const;
	IceState iceState() const;
	GatheringState gatheringState() const;
	SignalingState signalingState() const;
	bool negotiationNeeded() const;
	bool hasMedia() const;
	optional<Description> localDescription() const;
	optional<Description> remoteDescription() const;
	size_t remoteMaxMessageSize() const;
	optional<string> localAddress() const;
	optional<string> remoteAddress() const;
	uint16_t maxDataChannelId() const;
	bool getSelectedCandidatePair(Candidate *local, Candidate *remote);

	void setLocalDescription(Description::Type type = Description::Type::Unspec, LocalDescriptionInit init = {});
	void gatherLocalCandidates(std::vector<IceServer> additionalIceServers = {});
	void setRemoteDescription(Description description);
	void addRemoteCandidate(Candidate candidate);

	// For specific use cases only
	Description createOffer();
	Description createAnswer();

	void setMediaHandler(shared_ptr<MediaHandler> handler);
	shared_ptr<MediaHandler> getMediaHandler();

	[[nodiscard]] shared_ptr<DataChannel> createDataChannel(string label,
	                                                        DataChannelInit init = {});
	void onDataChannel(std::function<void(std::shared_ptr<DataChannel> dataChannel)> callback);

	[[nodiscard]] shared_ptr<Track> addTrack(Description::Media description);
	void onTrack(std::function<void(std::shared_ptr<Track> track)> callback);

	void onLocalDescription(std::function<void(Description description)> callback);
	void onLocalCandidate(std::function<void(Candidate candidate)> callback);
	void onStateChange(std::function<void(State state)> callback);
	void onIceStateChange(std::function<void(IceState state)> callback);
	void onGatheringStateChange(std::function<void(GatheringState state)> callback);
	void onSignalingStateChange(std::function<void(SignalingState state)> callback);

	void resetCallbacks();
	CertificateFingerprint remoteFingerprint();

	// Stats
	void clearStats();
	size_t bytesSent();
	size_t bytesReceived();
	optional<std::chrono::milliseconds> rtt();
};

RTC_CPP_EXPORT std::ostream &operator<<(std::ostream &out, PeerConnection::State state);
RTC_CPP_EXPORT std::ostream &operator<<(std::ostream &out, PeerConnection::IceState state);
RTC_CPP_EXPORT std::ostream &operator<<(std::ostream &out, PeerConnection::GatheringState state);
RTC_CPP_EXPORT std::ostream &operator<<(std::ostream &out, PeerConnection::SignalingState state);

} // namespace rtc

#endif
