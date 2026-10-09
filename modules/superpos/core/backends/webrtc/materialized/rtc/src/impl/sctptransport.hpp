/**
 * Copyright (c) 2019-2021 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_SCTP_TRANSPORT_H
#define RTC_IMPL_SCTP_TRANSPORT_H

#include "common.hpp"
#ifdef RTC_SUPERPOS_PROFILE
#include "superpos_record.hpp"
#include "superpos_send.hpp"
#endif
#include "configuration.hpp"
#include "global.hpp"
#include "processor.hpp"
#include "queue.hpp"
#include "transport.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

#include "usrsctp.h"

namespace rtc::impl {

class SctpTransport final : private TransportProcessorAdmission, public Transport, public std::enable_shared_from_this<SctpTransport> {
public:
	static void Init();
	static void SetSettings(const SctpSettings &s);
	static void Cleanup();

	using amount_callback = std::function<void(uint16_t streamId, size_t amount)>;

	struct Ports {
		uint16_t local = DEFAULT_SCTP_PORT;
		uint16_t remote = DEFAULT_SCTP_PORT;
	};

	SctpTransport(shared_ptr<Transport> lower, const Configuration &config, Ports ports,
	              message_callback recvCallback, amount_callback bufferedAmountCallback,
	              state_callback stateChangeCallback);
	~SctpTransport();

	void onBufferedAmount(amount_callback callback);

	void start() override;
	void stop() override;
	bool send(message_ptr message) override; // false if buffered
	bool flush();
	void closeStream(unsigned int stream);
	void close();
	pa::Admission pollTasks() noexcept;
    bool superposLocalStopComplete()const noexcept {
        // Quiescent publishes both producer barriers. Seeing the final release
        // of StopQueued also publishes terminal flush admission/dirty intent.
        return mSuperposProducersQuiescent.load(std::memory_order_acquire)&&
            !mStopQueued.load(std::memory_order_acquire)&&!mStopDirty.load(std::memory_order_acquire)&&
            !mRecvQueued.load(std::memory_order_acquire)&&!mRecvDirty.load(std::memory_order_acquire)&&
            !mFlushQueued.load(std::memory_order_acquire)&&!mFlushDirty.load(std::memory_order_acquire);
    }
#ifdef RTC_SUPERPOS_PROCESSOR_TESTING
    std::array<unsigned,8> testStopStatus()const noexcept{return {unsigned(mSuperposStopRequested.load()),unsigned(mRecvQueued.load()),unsigned(mRecvDirty.load()),unsigned(mFlushQueued.load()),unsigned(mFlushDirty.load()),unsigned(mStopQueued.load()),unsigned(mStopDirty.load()),unsigned(mSuperposProducersQuiescent.load())};}
    std::atomic<bool> testPauseUpcall{},testUpcallEntered{},testUpcallRelease{},testCloseFromUpcall{},testCallbackCloseReturned{},testProducerSealEntered{};
    std::atomic<unsigned> testUpcallVisits{},testWriteVisits{};
    std::uint64_t testNativeIdentity()const noexcept{return mSuperposCallbackIdentity;}
    void testNativeUpcall(){UpcallCallback(mSock,callbackAddress(),0);}
    int testNativeWrite(){return testNativeWriteToken(mSuperposCallbackIdentity);}
    static void testNativeUpcallToken(std::uint64_t identity){UpcallCallback(nullptr,reinterpret_cast<void*>(static_cast<std::uintptr_t>(identity)),0);}
    static int testNativeWriteToken(std::uint64_t identity){std::array<std::byte,12> data{};return WriteCallback(reinterpret_cast<void*>(static_cast<std::uintptr_t>(identity)),data.data(),data.size(),0,0);}
#endif
#if RTC_SUPERPOS_PROFILE
    // Private backend accounting only, with no admission or state mutation.
    SuperposSendSnapshot superposSendSnapshot();
#endif

	unsigned int maxStream() const;

	// Stats
	void clearStats();
	size_t bytesSent();
	size_t bytesReceived();
	optional<std::chrono::milliseconds> rtt();

private:
	// Order seems wrong but these are the actual values
	// See https://datatracker.ietf.org/doc/html/draft-ietf-rtcweb-data-channel-13#section-8
	enum PayloadId : uint32_t {
		PPID_CONTROL = 50,
		PPID_STRING = 51,
		PPID_BINARY_PARTIAL = 52,
		PPID_BINARY = 53,
		PPID_STRING_PARTIAL = 54,
		PPID_STRING_EMPTY = 56,
		PPID_BINARY_EMPTY = 57
	};

    struct sockaddr_conn getSockAddrConn(uint16_t port);
#if RTC_SUPERPOS_PROFILE
    void* callbackAddress()const noexcept{return reinterpret_cast<void*>(static_cast<std::uintptr_t>(mSuperposCallbackIdentity));}
#endif

	void connect();
	void shutdown();
	void incoming(message_ptr message) override;
	bool outgoing(message_ptr message) override;

	void doRecv();
	void doFlush();
	pa::Admission enqueueRecv();
	pa::Admission enqueueFlush(bool terminal=false);
#if RTC_SUPERPOS_PROFILE
    pa::Admission enqueueStop();
    void doStop();
    void sealProducers();
#endif
	bool trySendQueue();
	bool trySendMessage(message_ptr message);
	void updateBufferedAmount(uint16_t streamId, ptrdiff_t delta);
	void triggerBufferedAmount(uint16_t streamId, size_t amount);
	bool sendReset(uint16_t streamId);

	void handleUpcall() noexcept;
	int handleWrite(byte *data, size_t len, uint8_t tos, uint8_t set_df) noexcept;

	void processData(binary &&data, uint16_t streamId, PayloadId ppid);
	void processNotification(const union sctp_notification *notify, size_t len);

	const size_t mMaxMessageSize;
	const Ports mPorts;
    struct socket *mSock{};
#if RTC_SUPERPOS_PROFILE
    const std::uint64_t mSuperposCallbackIdentity;
#endif
	std::optional<uint16_t> mNegotiatedStreamsCount;

	std::atomic<bool> mRecvQueued{},mFlushQueued{},mRecvDirty{},mFlushDirty{};
    std::atomic<bool> mSuperposStopRequested{};
    std::atomic<bool> mStopQueued{},mStopDirty{},mSuperposProducersQuiescent{};
	bool mFlushContinuation{};
	std::mutex mRecvMutex;
	std::recursive_mutex mSendMutex; // buffered amount callback is synchronous
	Queue<message_ptr> mSendQueue;
	bool mSendShutdown = false;
#if RTC_SUPERPOS_PROFILE
    std::size_t mSuperposBufferedAmount{};
    bool mSuperposClosing{},mSuperposResetPending{},mSuperposCallbackFailed{},mSuperposCallbackActive{};
#else
	std::map<uint16_t, size_t> mBufferedAmount;
#endif
	amount_callback mBufferedAmountCallback;

	std::mutex mWriteMutex;
	std::condition_variable mWrittenCondition;
	std::atomic<bool> mWritten = false;     // written outside lock
	std::atomic<bool> mWrittenOnce = false; // same

	binary mPartialMessage, mPartialNotification;
#ifdef RTC_SUPERPOS_PROFILE
    SuperposRecord<960> mSuperposMessage;
    SuperposRecord<4096> mSuperposNotification;
#endif
	binary mPartialStringData, mPartialBinaryData;

	// Stats
	std::atomic<size_t> mBytesSent = 0, mBytesReceived = 0;

	static void UpcallCallback(struct socket *sock, void *arg, int flags);
	static int WriteCallback(void *sctp_ptr, void *data, size_t len, uint8_t tos, uint8_t set_df);
	static void DebugCallback(const char *format, ...);

	class InstancesSet;
	static InstancesSet* Instances;
};

} // namespace rtc::impl

#endif
