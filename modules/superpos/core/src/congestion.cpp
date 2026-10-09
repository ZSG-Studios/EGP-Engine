#include "superpos/congestion.hpp"
#include <algorithm>
#include <limits>

namespace superpos {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    return b > maximum - a ? maximum : a + b;
}
}
PacketCongestion::PacketCongestion(CongestionConfig config, std::span<PacketRecord> records) noexcept
    : config_(config), records_(records),
      window_(std::min<std::uint64_t>(10ULL * config.datagram_bytes,
          std::max<std::uint64_t>(2ULL * config.datagram_bytes, 14720))),
      threshold_(config.maximum_window), latest_rtt_(config.initial_rtt_us),
      smoothed_rtt_(config.initial_rtt_us), variance_(config.initial_rtt_us / 2) {
    for (auto &record : records_) record = {};
}
Result<PacketCongestion> PacketCongestion::create(CongestionConfig config,
    std::span<PacketRecord> records) noexcept {
    if (config.datagram_bytes < 256 || config.datagram_bytes > 1200 ||
        config.maximum_window < 10ULL * config.datagram_bytes ||
        config.maximum_window > 64ULL * 1024 * 1024 || records.empty() ||
        records.size() > 65536 || !config.initial_rtt_us ||
        config.initial_rtt_us > 60000000 || config.maximum_ack_delay_us > 1000000 ||
        !config.burst_datagrams || config.burst_datagrams > 16)
        return fail(Error::InvalidArgument);
    return PacketCongestion(config, records);
}
std::size_t PacketCongestion::outstanding_packets() const noexcept {
    return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(),
        [](const PacketRecord &record) { return record.occupied; }));
}
Status PacketCongestion::can_send(std::uint32_t bytes, std::uint64_t now) const noexcept {
    if (!bytes || bytes > config_.datagram_bytes || now < last_time_)
        return fail(Error::InvalidArgument);
    if (exhausted_) return fail(Error::CounterExhausted);
    if (!pacing_allows(now) || bytes > window_ - std::min(flight_, window_))
        return fail(Error::Busy);
    // Keep one record for a fresh PTO probe, except in the deliberately minimal
    // one-record profile (which cannot promise a full-window probe).
    const auto capacity=records_.size()>1?records_.size()-1:records_.size();
    if (outstanding_packets() >= capacity) return fail(Error::CapacityExceeded);
    return {};
}
Result<std::uint64_t> PacketCongestion::sent(std::uint32_t bytes, std::uint64_t now) noexcept {
    return admit(bytes,now,false);
}
Result<std::uint64_t> PacketCongestion::next_packet_number() const noexcept {
    return exhausted_?Result<std::uint64_t>(fail(Error::CounterExhausted)):Result<std::uint64_t>(next_number_);
}
std::uint64_t PacketCongestion::burst_us() const noexcept {
    if(config_.burst_datagrams<=1)return 0;
    const auto interval=(static_cast<std::uint64_t>(config_.datagram_bytes)*smoothed_rtt_+window_-1)/window_;
    return interval*(config_.burst_datagrams-1U);
}
bool PacketCongestion::pacing_allows(std::uint64_t now) const noexcept {
    return now>=next_send_ || next_send_-now<=burst_us();
}
void PacketCongestion::pace(std::uint32_t bytes,std::uint64_t now) noexcept {
    const auto interval=std::max<std::uint64_t>(1,
        (static_cast<std::uint64_t>(bytes)*smoothed_rtt_+window_-1)/window_);
    // Bucket debt accumulates from the later of now and the previous deadline;
    // with a one-datagram burst this is exactly now+interval as before.
    next_send_=saturating_add(std::max(next_send_,now),interval);last_time_=now;
}
Status PacketCongestion::sent_untracked(std::uint32_t bytes,std::uint64_t now) noexcept {
    if(!bytes||bytes>config_.datagram_bytes||now<last_time_)return fail(Error::InvalidArgument);
    if(!pacing_allows(now))return fail(Error::Busy);
    pace(bytes,now);return {};
}
Result<std::uint64_t> PacketCongestion::sent_probe(std::uint32_t bytes,std::uint64_t now) noexcept {
    return admit(bytes,now,true);
}
Result<std::uint64_t> PacketCongestion::admit(std::uint32_t bytes,std::uint64_t now,bool probe) noexcept {
    if(probe) {
        if(!bytes||bytes>config_.datagram_bytes||now<last_time_)return fail(Error::InvalidArgument);
        if(exhausted_)return fail(Error::CounterExhausted);
        if(!probe_credit_||!pacing_allows(now))return fail(Error::Busy);
        if(outstanding_packets()==records_.size())return fail(Error::CapacityExceeded);
    } else if (auto result = can_send(bytes, now); !result) return fail(result.error());
    const auto number = next_number_;
    for (auto &record : records_) if (!record.occupied) {
        record = {true, number, now, bytes};
        break;
    }
    flight_ += bytes;
    last_sent_ = number;
    last_time_ = now;
    pto_anchor_=now;
    if(probe)probe_credit_=false;
    if (number == maximum) exhausted_ = true; else ++next_number_;
    // Window / RTT pacing rate; capped burst is one admitted datagram. All
    // factors are bounded by configuration/validated RTT sample limits.
    pace(bytes,now);
    return number;
}
Result<CongestionReceipt> PacketCongestion::acknowledge(PacketAck ack, std::uint64_t now) noexcept {
    if (now < last_time_ || !ack.largest || ack.largest > last_sent_ || !(ack.bits & 1) ||
        ack.delay_us > config_.maximum_ack_delay_us ||
        (ack.largest < 64 && (ack.bits >> ack.largest) != 0))
        return fail(Error::ProtocolViolation);
    CongestionReceipt result{};
    const bool window_limited = flight_ + config_.datagram_bytes >= window_;
    PacketRecord *sample = nullptr;
    for (auto &record : records_) if (record.occupied && record.number <= ack.largest &&
        ack.largest - record.number < 64 && (ack.bits & (1ULL << (ack.largest - record.number)))) {
        if (now < record.sent_at) return fail(Error::InvalidArgument);
        if (!sample || record.number > sample->number) sample = &record;
    }
    // Compute RTT before records are retired, once per newly acknowledged frame.
    if (sample) {
        latest_rtt_ = std::min<std::uint64_t>(60000000, std::max<std::uint64_t>(1, now - sample->sent_at));
        minimum_rtt_ = rtt_sampled_ ? std::min(minimum_rtt_, latest_rtt_) : latest_rtt_;
        auto adjusted = latest_rtt_;
        if (latest_rtt_ - minimum_rtt_ >= ack.delay_us) adjusted -= ack.delay_us;
        if (!rtt_sampled_) {
            smoothed_rtt_ = latest_rtt_; variance_ = latest_rtt_ / 2; rtt_sampled_ = true;
        } else {
            const auto difference = smoothed_rtt_ > adjusted ? smoothed_rtt_ - adjusted : adjusted - smoothed_rtt_;
            variance_ = (3 * variance_ + difference) / 4;
            smoothed_rtt_ = std::max<std::uint64_t>(1, (7 * smoothed_rtt_ + adjusted) / 8);
        }
    }
    for (auto &record : records_) if (record.occupied && record.number <= ack.largest &&
        ack.largest - record.number < 64 && (ack.bits & (1ULL << (ack.largest - record.number)))) {
        ++result.acknowledged_packets; result.acknowledged_bytes += record.bytes;
        flight_ -= record.bytes;
        if (window_limited && record.number > recovery_number_) {
            if (window_ < threshold_) window_ = std::min(config_.maximum_window, window_ + record.bytes);
            else {
                increase_credit_ += record.bytes;
                if (increase_credit_ >= window_) {
                    increase_credit_ -= window_;
                    window_ = std::min(config_.maximum_window, window_ + config_.datagram_bytes);
                }
            }
        }
        record = {};
    }
    last_time_ = now;
    largest_acked_ = std::max(largest_acked_, ack.largest);
    if (result.acknowledged_packets) { pto_count_=0;probe_credit_=false;pto_anchor_=now; }
    auto losses = loss(now);
    if (!losses) return fail(losses.error());
    result.lost_packets = losses->lost_packets; result.lost_bytes = losses->lost_bytes;
    return result;
}
Result<CongestionReceipt> PacketCongestion::loss(std::uint64_t now) noexcept {
    if (now < last_time_) return fail(Error::InvalidArgument);
    CongestionReceipt result{};
    const auto delay = std::max<std::uint64_t>(1000,
        (9 * std::max(latest_rtt_, smoothed_rtt_) + 7) / 8);
    bool reduce = false;
    for (auto &record : records_) if (record.occupied && record.number <= largest_acked_) {
        if (now < record.sent_at) return fail(Error::InvalidArgument);
        if (largest_acked_ - record.number >= 3 || now - record.sent_at >= delay) {
            reduce |= record.number > recovery_number_;
            ++result.lost_packets; result.lost_bytes += record.bytes; flight_ -= record.bytes;
            record = {};
        }
    }
    if (reduce) {
        recovery_number_ = last_sent_;
        threshold_ = std::max<std::uint64_t>(2ULL * config_.datagram_bytes, window_ / 2);
        window_ = threshold_; increase_credit_ = 0;
    }
    last_time_ = now;
    return result;
}
Result<CongestionReceipt> PacketCongestion::detect_loss(std::uint64_t now) noexcept { return loss(now); }
Result<std::uint64_t> PacketCongestion::probe_timeout_us() const noexcept {
    const auto base = saturating_add(smoothed_rtt_,
        saturating_add(std::max<std::uint64_t>(4 * variance_, 1000), config_.maximum_ack_delay_us));
    if (pto_count_ >= 64 || base > (maximum >> pto_count_)) return fail(Error::Overflow);
    return base << pto_count_;
}
Status PacketCongestion::probe_timeout(std::uint64_t now) noexcept {
    if (now < last_time_ || !flight_) return fail(Error::InvalidArgument);
    // Owner schedules at the returned deadline; no retransmission or window
    // credit is manufactured by a timer notification.
    if (pto_count_ == 63) return fail(Error::CounterExhausted);
    const auto timeout=probe_timeout_us();if(!timeout)return fail(timeout.error());
    if(now<saturating_add(pto_anchor_,*timeout))return fail(Error::Busy);
    ++pto_count_;last_time_=now;pto_anchor_=now;probe_credit_=true;
    return {};
}
}
