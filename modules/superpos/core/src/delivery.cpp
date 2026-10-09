#include "superpos/delivery.hpp"
#include "superpos/codec.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace superpos {
namespace {

constexpr std::uint64_t maximum_counter = std::numeric_limits<std::uint64_t>::max();

bool valid_mode(DeliveryMode mode) noexcept {
    return mode == DeliveryMode::ReliableOrdered || mode == DeliveryMode::ReliableUnordered ||
        mode == DeliveryMode::UnreliableLatest || mode == DeliveryMode::Unreliable;
}
bool unreliable(DeliveryMode mode) noexcept { return mode==DeliveryMode::UnreliableLatest || mode==DeliveryMode::Unreliable; }
bool sliding_seen(std::uint64_t id,std::uint64_t newest,std::uint64_t bits) noexcept {
    return id<=newest && newest-id<64 && (bits&(std::uint64_t{1}<<(newest-id)))!=0;
}
void sliding_mark(std::uint64_t id,std::uint64_t& newest,std::uint64_t& bits) noexcept {
    if (id>newest) { auto d=id-newest; bits=d>=64?1:(bits<<d)|1; newest=id; }
    else if (newest-id<64) bits|=std::uint64_t{1}<<(newest-id);
}

Status validate(DeliveryMode mode, const DeliveryLimits &limits,
    std::span<DeliverySlot> slots, std::span<std::byte> arena,
    std::uint64_t first) noexcept {
    if (!valid_mode(mode) || first == 0 || limits.max_message_bytes > 65'536 ||
        limits.max_message_bytes == 0 || limits.fragment_payload_bytes == 0 ||
        limits.fragment_payload_bytes > 896 || limits.max_messages == 0 ||
        limits.max_messages > 64 || slots.size() < limits.max_messages ||
        limits.retry_ticks == 0 || limits.timeout_ticks < limits.retry_ticks || limits.progress_timeout_ticks == 0 ||
        arena.size() > std::numeric_limits<std::uint32_t>::max()) {
        return fail(Error::InvalidArgument);
    }
    const auto fragments = (limits.max_message_bytes - 1U) / limits.fragment_payload_bytes + 1U;
    if (fragments > 128) {
        return fail(Error::InvalidArgument);
    }
    return {};
}

std::uint16_t fragment_count(std::uint32_t size, std::uint16_t payload) noexcept {
    return static_cast<std::uint16_t>(size == 0 ? 1 : (size - 1U) / payload + 1U);
}
std::array<std::uint64_t,2> fragment_mask(std::uint16_t count) noexcept {
    std::array<std::uint64_t,2> bits{};
    for(unsigned i=0;i<2;++i) {
        const unsigned offset=64*i;
        bits[i]=count>=offset+64?UINT64_MAX:count<=offset?0:(std::uint64_t{1}<<(count-offset))-1;
    } return bits;
}
bool valid_receipt(const FragmentReceipt& receipt) noexcept {
    if(!receipt.message || receipt.channel>=32 || !receipt.fragments || receipt.fragments>128 || !(receipt.bits[0]|receipt.bits[1]))return false;
    const auto mask=fragment_mask(receipt.fragments);
    return !(receipt.bits[0]&~mask[0]) && !(receipt.bits[1]&~mask[1]);
}
std::uint16_t first_missing(const DeliverySlot& slot,std::uint16_t start) noexcept {
    for(auto i=start;i<slot.fragments;++i)if(!(slot.received_bits[i/64]&(std::uint64_t{1}<<(i%64))))return i;
    return slot.fragments;
}

Result<std::uint32_t> reserve_offset(std::span<const DeliverySlot> slots,
    std::size_t arena_size, std::uint32_t size,std::uint16_t ignored_channel=UINT16_MAX) noexcept {
    std::uint64_t candidate = 0;
    for (std::size_t pass = 0; pass <= slots.size(); ++pass) {
        if (candidate + size > arena_size) {
            return fail(Error::CapacityExceeded);
        }
        bool collided = false;
        for (const auto &slot : slots) {
            if (!slot.occupied || !slot.reserved || slot.size == 0 || slot.channel==ignored_channel) {
                continue;
            }
            const std::uint64_t end = static_cast<std::uint64_t>(slot.offset) + slot.size;
            if (candidate < end && candidate + size > slot.offset) {
                candidate = end;
                collided = true;
                break;
            }
        }
        if (!collided) {
            return static_cast<std::uint32_t>(candidate);
        }
    }
    return fail(Error::CapacityExceeded);
}

std::size_t reserved(std::span<const DeliverySlot> slots,std::uint16_t channel) noexcept {
    std::size_t result = 0;
    for (const auto &slot : slots) {
        if (slot.occupied && slot.reserved && slot.channel==channel) {
            result += slot.size;
        }
    }
    return result;
}

std::size_t occupied(std::span<const DeliverySlot> slots,std::uint16_t channel) noexcept {
    return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(),
        [channel](const DeliverySlot &slot) { return slot.occupied && slot.channel==channel; }));
}

void acknowledge(std::uint64_t message, std::uint64_t &through, std::uint64_t &bits) noexcept {
    if (message <= through) {
        return;
    }
    bits |= std::uint64_t{1} << (message - through - 1U);
    while ((bits & 1U) != 0) {
        ++through;
        bits >>= 1;
    }
}

} // namespace

Result<std::size_t> encode_fragment_receipt(const FragmentReceipt& receipt,std::span<std::byte> output) noexcept {
    if(!valid_receipt(receipt))return fail(Error::NonCanonical);
    if(output.size()<40)return fail(Error::Truncated);
    std::array<std::byte,40> bytes{}; Writer w(bytes);
    const auto packed=(std::uint64_t(receipt.channel)<<32)|receipt.fragments;
    if(!w.u64(receipt.epoch) || !w.u64(receipt.message) || !w.u64(packed) || !w.u64(receipt.bits[0]) || !w.u64(receipt.bits[1]))return fail(Error::ProtocolViolation);
    std::memcpy(output.data(),bytes.data(),bytes.size()); return bytes.size();
}
Result<FragmentReceipt> decode_fragment_receipt(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()<40)return fail(Error::Truncated); if(bytes.size()!=40)return fail(Error::NonCanonical);
    Reader r(bytes); auto epoch=r.u64(),message=r.u64(),packed=r.u64(),first=r.u64(),last=r.u64();
    if(!epoch || !message || !packed || !first || !last)return fail(Error::Truncated);
    if((*packed>>32)>=32 || (*packed&UINT32_MAX)>128)return fail(Error::NonCanonical);
    FragmentReceipt receipt{*epoch,*message,static_cast<std::uint16_t>(*packed>>32),static_cast<std::uint16_t>(*packed),{*first,*last}};
    if(!valid_receipt(receipt))return fail(Error::NonCanonical); return receipt;
}

Result<std::size_t> encode_fragment(const DeliveryFragment &fragment,
    std::span<std::byte> output, std::uint16_t fragment_payload_bytes) noexcept {
    if (!valid_mode(fragment.mode) || fragment.message == 0 || fragment.channel>=32 || fragment_payload_bytes == 0 ||
        fragment_payload_bytes > 896 || fragment.total_bytes > 65'536 ||
        fragment.count != fragment_count(fragment.total_bytes, fragment_payload_bytes) ||
        fragment.count > 128 || fragment.index >= fragment.count || (fragment.mode==DeliveryMode::Unreliable && fragment.count!=1)) {
        return fail(Error::NonCanonical);
    }
    const auto offset = static_cast<std::uint32_t>(fragment.index) * fragment_payload_bytes;
    const auto size = std::min<std::uint32_t>(fragment_payload_bytes, fragment.total_bytes - offset);
    if (fragment.payload.size() != size) {
        return fail(Error::NonCanonical);
    }
    if (output.size() < 48U + size) {
        return fail(Error::Truncated);
    }
    std::array<std::byte, 48> header{};
    Writer writer(header);
    const auto packed = (static_cast<std::uint64_t>(fragment.index) << 48) |
        (static_cast<std::uint64_t>(fragment.count) << 32) |
        (static_cast<std::uint64_t>(size) << 16) | fragment_payload_bytes;
    if (auto status = writer.u64(0x5350444652414d01ULL); !status) { return fail(status.error()); }
    if (auto status = writer.u64(fragment.epoch); !status) { return fail(status.error()); }
    if (auto status = writer.u64(fragment.message); !status) { return fail(status.error()); }
    if (auto status = writer.u64(static_cast<std::uint64_t>(fragment.mode)|(std::uint64_t(fragment.channel)<<8)); !status) { return fail(status.error()); }
    if (auto status = writer.u64(fragment.total_bytes); !status) { return fail(status.error()); }
    if (auto status = writer.u64(packed); !status) { return fail(status.error()); }
    if (size != 0) {
        std::memmove(output.data() + header.size(), fragment.payload.data(), size);
    }
    std::memcpy(output.data(), header.data(), header.size());
    return header.size() + size;
}

Result<DeliveryFragment> decode_fragment(std::span<const std::byte> frame,
    std::uint16_t fragment_payload_bytes) noexcept {
    if (frame.size() < 48) {
        return fail(Error::Truncated);
    }
    if (frame.size() > 960 || fragment_payload_bytes == 0 || fragment_payload_bytes > 896) {
        return fail(Error::NonCanonical);
    }
    Reader reader(frame);
    const auto tag = reader.u64();
    const auto epoch = reader.u64();
    const auto message = reader.u64();
    const auto mode = reader.u64();
    const auto total = reader.u64();
    const auto packed = reader.u64();
    if (!tag || !epoch || !message || !mode || !total || !packed) {
        return fail(Error::Truncated);
    }
    if (*tag != 0x5350444652414d01ULL || *message == 0 || (*mode&255)>3 || (*mode>>8)>=32 || *total > 65'536 ||
        static_cast<std::uint16_t>(*packed) != fragment_payload_bytes) {
        return fail(Error::NonCanonical);
    }
    const auto index = static_cast<std::uint16_t>(*packed >> 48);
    const auto count = static_cast<std::uint16_t>(*packed >> 32);
    const auto size = static_cast<std::uint16_t>(*packed >> 16);
    if (count != fragment_count(static_cast<std::uint32_t>(*total), fragment_payload_bytes) ||
        count > 128 || index >= count || ((*mode&255)==static_cast<unsigned>(DeliveryMode::Unreliable) && count!=1)) {
        return fail(Error::NonCanonical);
    }
    const auto offset = static_cast<std::uint32_t>(index) * fragment_payload_bytes;
    if (size != std::min<std::uint64_t>(fragment_payload_bytes, *total - offset)) {
        return fail(Error::NonCanonical);
    }
    const auto payload = reader.raw(size);
    if (!payload) {
        return fail(payload.error());
    }
    if (!reader.empty()) {
        return fail(Error::NonCanonical);
    }
    return DeliveryFragment{*epoch, *message, static_cast<DeliveryMode>(*mode&255),
        static_cast<std::uint32_t>(*total), index, count, *payload,static_cast<std::uint16_t>(*mode>>8)};
}

DeliveryReceiver::DeliveryReceiver(DeliveryMode mode, Epoch epoch, DeliveryLimits limits,
    std::span<DeliverySlot> slots, std::span<std::byte> arena, std::uint64_t first,std::uint16_t channel,bool shared) noexcept
    : mode_(mode), epoch_(epoch), limits_(limits), slots_(slots.first(limits.max_messages)),
      arena_(arena), applied_through_(first - 1), latest_admitted_(first - 1),channel_(channel) {
    if(mode_==DeliveryMode::Unreliable)seen_bits_=UINT64_MAX;
    if (!shared) for (auto &slot : slots_) {
        slot = {};
    }
}

Result<DeliveryReceiver> DeliveryReceiver::create(DeliveryMode mode, Epoch epoch,
    DeliveryLimits limits, std::span<DeliverySlot> slots, std::span<std::byte> arena,
    std::uint64_t first) noexcept {
    if (auto status = validate(mode, limits, slots, arena, first); !status) {
        return fail(status.error());
    }
    return DeliveryReceiver(mode, epoch, limits, slots, arena, first);
}
Result<DeliveryReceiver> DeliveryReceiver::create_shared(DeliveryMode mode,Epoch epoch,DeliveryLimits limits,
    std::span<DeliverySlot> slots,std::span<std::byte> arena,std::uint16_t channel,std::uint64_t first) noexcept {
    if(channel>=32)return fail(Error::InvalidArgument);
    if(auto status=validate(mode,limits,slots,arena,first);!status)return fail(status.error());
    return DeliveryReceiver(mode,epoch,limits,slots,arena,first,channel,true);
}

DeliverySlot *DeliveryReceiver::find(std::uint64_t message) noexcept {
    for (auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && slot.message == message) {
            return &slot;
        }
    }
    return nullptr;
}

Status DeliveryReceiver::conflict() noexcept {
    failed_ = true;
    for (auto &slot : slots_) {
        if(slot.occupied && slot.channel==channel_)slot = {};
    }
    return fail(Error::ProtocolViolation);
}

Result<FragmentAdmission> DeliveryReceiver::receive(const DeliveryFragment &fragment, Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (fragment.epoch != epoch_) {
        return fail(Error::StaleEpoch);
    }
    if (fragment.mode != mode_ || fragment.message == 0 || fragment.channel!=channel_ ||
        fragment.total_bytes > limits_.max_message_bytes ||
        fragment.count != fragment_count(fragment.total_bytes, limits_.fragment_payload_bytes) ||
        fragment.index >= fragment.count || (mode_==DeliveryMode::Unreliable && fragment.count!=1)) {
        return fail(Error::NonCanonical);
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(fragment.index) * limits_.fragment_payload_bytes;
    const auto expected_size = std::min<std::uint32_t>(limits_.fragment_payload_bytes,
        fragment.total_bytes - offset);
    if (fragment.payload.size() != expected_size) {
        return fail(Error::NonCanonical);
    }

    if (mode_ == DeliveryMode::UnreliableLatest) {
        if (fragment.message < latest_admitted_ || fragment.message <= applied_through_) {
            return FragmentAdmission::Superseded;
        }
    } else if(mode_==DeliveryMode::Unreliable) {
        if(fragment.message<=latest_admitted_ && latest_admitted_-fragment.message>=64)return FragmentAdmission::Superseded;
        if(sliding_seen(fragment.message,latest_admitted_,seen_bits_) && !find(fragment.message))return FragmentAdmission::Duplicate;
    } else {
        if (fragment.message <= applied_through_) {
            return FragmentAdmission::Duplicate;
        }
        const auto distance = fragment.message - applied_through_;
        if (distance > 64) {
            return fail(Error::CapacityExceeded);
        }
        if ((applied_bits_ & (std::uint64_t{1} << (distance - 1U))) != 0) {
            return FragmentAdmission::Duplicate;
        }
    }

    auto *slot = find(fragment.message);
    if (mode_ == DeliveryMode::UnreliableLatest && slot == nullptr &&
        fragment.message == latest_admitted_) {
        return FragmentAdmission::Superseded;
    }
    if (slot != nullptr) {
        if (now < slot->admitted_at || now < slot->last_progress_at) {
            return fail(Error::InvalidArgument);
        }
        if (slot->size != fragment.total_bytes || slot->fragments != fragment.count) {
            return fail(conflict().error());
        }
    } else {
        if (mode_ == DeliveryMode::UnreliableLatest && fragment.message > latest_admitted_) {
            if (!reserve_offset(slots_,arena_.size(),fragment.total_bytes,channel_))return fail(Error::CapacityExceeded);
            // Validation and the whole-message capacity check precede supersession.
            for (auto &old : slots_) {
                if(old.occupied && old.channel==channel_)old = {};
            }
        }
        for (auto &candidate : slots_) {
            if (!candidate.occupied) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) {
            return fail(Error::CapacityExceeded);
        }
        const auto allocation = reserve_offset(slots_, arena_.size(), fragment.total_bytes);
        if (!allocation) {
            return fail(allocation.error());
        }
        *slot = {};
        slot->occupied = true;
        slot->channel = channel_;
        slot->reserved = true;
        slot->message = fragment.message;
        slot->offset = *allocation;
        slot->size = fragment.total_bytes;
        slot->fragments = fragment.count;
        slot->admitted_at = now;
        slot->last_progress_at = now;
        if (mode_ == DeliveryMode::UnreliableLatest) {
            latest_admitted_ = fragment.message;
        }
        if(mode_==DeliveryMode::Unreliable)sliding_mark(fragment.message,latest_admitted_,seen_bits_);
    }

    const auto word = fragment.index / 64U;
    const auto bit = std::uint64_t{1} << (fragment.index % 64U);
    auto destination = arena_.subspan(static_cast<std::size_t>(slot->offset) + offset, expected_size);
    if ((slot->received_bits[word] & bit) != 0) {
        if (!std::equal(fragment.payload.begin(), fragment.payload.end(), destination.begin())) {
            return fail(conflict().error());
        }
        return FragmentAdmission::Duplicate;
    }
    if (!fragment.payload.empty()) {
        std::memmove(destination.data(), fragment.payload.data(), fragment.payload.size());
    }
    slot->received_bits[word] |= bit;
    ++slot->received_fragments;
    slot->last_progress_at = now;
    slot->complete = slot->received_fragments == slot->fragments;
    if (slot->complete) {
        slot->stage = DeliveryStage::Received;
        return FragmentAdmission::Complete;
    }
    return FragmentAdmission::Accepted;
}

Result<DeliveryView> DeliveryReceiver::ready() const noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    const DeliverySlot *chosen = nullptr;
    for (const auto &slot : slots_) {
        if (!slot.occupied || slot.channel!=channel_ || !slot.complete) {
            continue;
        }
        if (mode_ == DeliveryMode::ReliableOrdered &&
            (applied_through_ == maximum_counter || slot.message != applied_through_ + 1U)) {
            continue;
        }
        if (chosen == nullptr || slot.message < chosen->message) {
            chosen = &slot;
        }
    }
    if (chosen == nullptr) {
        return fail(Error::NotReady);
    }
    return DeliveryView{chosen->message, arena_.subspan(chosen->offset, chosen->size), DeliveryStage::Received,channel_};
}

Status DeliveryReceiver::applied(std::uint64_t message) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (message == 0) {
        return fail(Error::InvalidArgument);
    }
    if (mode_==DeliveryMode::Unreliable && sliding_seen(message,applied_through_,applied_bits_))return {};
    if (mode_==DeliveryMode::UnreliableLatest && message==applied_through_)return {};
    if (!unreliable(mode_) && message <= applied_through_) {
        return {};
    }
    if (!unreliable(mode_) && message > applied_through_ && message - applied_through_ <= 64 &&
        (applied_bits_ & (std::uint64_t{1} << (message - applied_through_ - 1U))) != 0) {
        return {};
    }
    auto *slot = find(message);
    if (slot == nullptr || !slot->complete) {
        return fail(Error::NotReady);
    }
    if (mode_ == DeliveryMode::ReliableOrdered && message != applied_through_ + 1U) {
        return fail(Error::NotReady);
    }
    if (mode_ == DeliveryMode::UnreliableLatest) {
        applied_through_ = message;
    } else if(mode_==DeliveryMode::Unreliable) {
        sliding_mark(message,applied_through_,applied_bits_);
    } else {
        acknowledge(message, applied_through_, applied_bits_);
    }
    *slot = {};
    return {};
}

Result<DeliveryStage> DeliveryReceiver::acknowledgement(std::uint64_t message) const noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (message == 0) {
        return fail(Error::InvalidArgument);
    }
    if ((mode_==DeliveryMode::UnreliableLatest && message==applied_through_) ||
        (mode_==DeliveryMode::Unreliable && sliding_seen(message,applied_through_,applied_bits_)) ||
        (!unreliable(mode_) && message <= applied_through_) || (!unreliable(mode_) && message>applied_through_ &&
        message - applied_through_ <= 64 &&
        (applied_bits_ & (std::uint64_t{1} << (message - applied_through_ - 1U))) != 0)) {
        return DeliveryStage::Applied;
    }
    for (const auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && slot.message == message && slot.complete) {
            return DeliveryStage::Received;
        }
    }
    return fail(Error::NotReady);
}
Result<FragmentReceipt> DeliveryReceiver::fragment_acknowledgement(std::uint64_t message) const noexcept {
    if(failed_)return fail(Error::ChannelFailed); if(!message)return fail(Error::InvalidArgument);
    if(unreliable(mode_))return fail(Error::Unsupported);
    for(const auto& slot:slots_)if(slot.occupied && slot.channel==channel_ && slot.message==message)
        return FragmentReceipt{epoch_,message,channel_,slot.fragments,slot.received_bits};
    return fail(Error::NotReady);
}

Status DeliveryReceiver::expire(Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    for (const auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && (now < slot.admitted_at || now < slot.last_progress_at)) {
            return fail(Error::InvalidArgument);
        }
    }
    bool timed_out = false;
    for (auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && (now - slot.admitted_at >= limits_.timeout_ticks ||
            now - slot.last_progress_at >= limits_.progress_timeout_ticks)) {
            slot = {};
            timed_out = true;
        }
    }
    if (timed_out && !unreliable(mode_)) {
        failed_ = true;
        for (auto &slot : slots_) {
            if(slot.occupied && slot.channel==channel_)slot = {};
        }
        return fail(Error::Timeout);
    }
    return {};
}

std::size_t DeliveryReceiver::reserved_bytes() const noexcept { return reserved(slots_,channel_); }
std::size_t DeliveryReceiver::pending_messages() const noexcept { return occupied(slots_,channel_); }

DeliverySender::DeliverySender(DeliveryMode mode, Epoch epoch, DeliveryLimits limits,
    std::span<DeliverySlot> slots, std::span<std::byte> arena, std::uint64_t first,std::uint16_t channel,bool shared) noexcept
    : mode_(mode), epoch_(epoch), limits_(limits), retry_ticks_(limits.retry_ticks), slots_(slots.first(limits.max_messages)),
      arena_(arena), next_message_(first), applied_through_(first - 1U),channel_(channel) {
    if (!shared) for (auto &slot : slots_) {
        slot = {};
    }
}

Result<DeliverySender> DeliverySender::create(DeliveryMode mode, Epoch epoch,
    DeliveryLimits limits, std::span<DeliverySlot> slots, std::span<std::byte> arena,
    std::uint64_t first) noexcept {
    if (auto status = validate(mode, limits, slots, arena, first); !status) {
        return fail(status.error());
    }
    return DeliverySender(mode, epoch, limits, slots, arena, first);
}
Result<DeliverySender> DeliverySender::create_shared(DeliveryMode mode,Epoch epoch,DeliveryLimits limits,
    std::span<DeliverySlot> slots,std::span<std::byte> arena,std::uint16_t channel,std::uint64_t first) noexcept {
    if(channel>=32)return fail(Error::InvalidArgument);
    if(auto status=validate(mode,limits,slots,arena,first);!status)return fail(status.error());
    return DeliverySender(mode,epoch,limits,slots,arena,first,channel,true);
}

DeliverySlot *DeliverySender::find(std::uint64_t message) noexcept {
    for (auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && slot.message == message) {
            return &slot;
        }
    }
    return nullptr;
}

Result<std::uint64_t> DeliverySender::admit(std::span<const std::byte> payload, Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (attempt_pending_) {
        return fail(Error::Busy);
    }
    if (sequence_exhausted_) {
        return fail(Error::CounterExhausted);
    }
    if (payload.size() > limits_.max_message_bytes || payload.size() > arena_.size() || (mode_==DeliveryMode::Unreliable && payload.size()>limits_.fragment_payload_bytes)) {
        return fail(Error::CapacityExceeded);
    }
    if (!unreliable(mode_) && next_message_ - applied_through_ > 64) {
        return fail(Error::CapacityExceeded);
    }
    if (mode_ == DeliveryMode::UnreliableLatest) {
        if(!reserve_offset(slots_,arena_.size(),static_cast<std::uint32_t>(payload.size()),channel_))return fail(Error::CapacityExceeded);
        for (auto &old : slots_) {
            if(old.occupied && old.channel==channel_)old = {};
        }
    }
    DeliverySlot *slot = nullptr;
    for (auto &candidate : slots_) {
        if (!candidate.occupied) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        return fail(Error::CapacityExceeded);
    }
    const auto size = static_cast<std::uint32_t>(payload.size());
    const auto allocation = reserve_offset(slots_, arena_.size(), size);
    if (!allocation) {
        return fail(allocation.error());
    }
    *slot = {};
    slot->occupied = true;
    slot->channel = channel_;
    slot->reserved = true;
    slot->message = next_message_;
    slot->offset = *allocation;
    slot->size = size;
    slot->fragments = fragment_count(size, limits_.fragment_payload_bytes);
    slot->admitted_at = now;
    slot->last_cycle_at = now;
    slot->last_progress_at = now;
    if (!payload.empty()) {
        std::memmove(arena_.subspan(slot->offset, slot->size).data(), payload.data(), payload.size());
    }
    if (next_message_ == maximum_counter) {
        sequence_exhausted_ = true;
    } else {
        ++next_message_;
    }
    return slot->message;
}

Result<CarrierAttempt> DeliverySender::next(Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (attempt_pending_) {
        return fail(Error::Busy);
    }
    if (attempt_exhausted_) {
        return fail(Error::CounterExhausted);
    }
    DeliverySlot *chosen = nullptr;
    for (auto &slot : slots_) {
        if (!slot.occupied || slot.channel!=channel_ || slot.stage == DeliveryStage::Applied) {
            continue;
        }
        if (now < slot.admitted_at || now < slot.last_cycle_at || now < slot.last_progress_at) {
            return fail(Error::InvalidArgument);
        }
        const bool due = (unreliable(mode_)?slot.next_fragment<slot.fragments:first_missing(slot,slot.next_fragment)<slot.fragments) ||
            (!unreliable(mode_) && now - slot.last_cycle_at >= retry_ticks_);
        if (due && (chosen == nullptr || slot.message < chosen->message)) {
            chosen = &slot;
        }
    }
    if (chosen == nullptr) {
        return fail(Error::NotReady);
    }
    auto fragment=unreliable(mode_)?chosen->next_fragment:first_missing(*chosen,chosen->next_fragment);
    bool probe=false;
    if(fragment==chosen->fragments) {
        fragment=unreliable(mode_)?std::uint16_t{0}:first_missing(*chosen,0);
        if(fragment==chosen->fragments || chosen->stage>=DeliveryStage::Received) { fragment=0; probe=true; }
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(fragment) * limits_.fragment_payload_bytes;
    const auto size = std::min<std::uint32_t>(limits_.fragment_payload_bytes, chosen->size - offset);
    const auto attempt = next_attempt_;
    if (next_attempt_ == maximum_counter) {
        attempt_exhausted_ = true;
    } else {
        ++next_attempt_;
    }
    attempt_pending_ = true;
    attempt_slot_ = static_cast<std::size_t>(chosen - slots_.data());
    pending_attempt_ = attempt;
    attempt_fragment_=fragment; attempt_probe_=probe;
    return CarrierAttempt{DeliveryFragment{epoch_, chosen->message, mode_, chosen->size,
        fragment, chosen->fragments,
        arena_.subspan(static_cast<std::size_t>(chosen->offset) + offset, size),channel_}, attempt,probe};
}

Status DeliverySender::carrier_result(std::uint64_t attempt, bool accepted, Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (!attempt_pending_ || attempt != pending_attempt_) {
        return fail(Error::NotReady);
    }
    auto &slot = slots_[attempt_slot_];
    if (now < slot.admitted_at || now < slot.last_cycle_at || now < slot.last_progress_at) {
        return fail(Error::InvalidArgument);
    }
    attempt_pending_ = false;
    if (accepted) {
        if(!attempt_probe_ && slot.stage==DeliveryStage::Admitted && slot.sent_bits==std::array<std::uint64_t,2>{}) {
            // First fragment on the carrier: the message starts aging now.
            slot.admitted_at=now; slot.last_progress_at=now;
        }
        if(attempt_probe_) { slot.next_fragment=slot.fragments; slot.last_cycle_at=now; }
        else {
            slot.sent_bits[attempt_fragment_/64]|=std::uint64_t{1}<<(attempt_fragment_%64);
            slot.next_fragment=static_cast<std::uint16_t>(attempt_fragment_+1);
            if(unreliable(mode_)?slot.next_fragment==slot.fragments:first_missing(slot,slot.next_fragment)==slot.fragments) {
                slot.next_fragment=slot.fragments; slot.last_cycle_at=now;
            }
            if(slot.sent_bits==fragment_mask(slot.fragments) && slot.stage<DeliveryStage::CarrierAccepted)slot.stage=DeliveryStage::CarrierAccepted;
        }
    }
    return {};
}

Status DeliverySender::receipt(std::uint64_t message, DeliveryStage received_stage) noexcept {
    auto *slot = find(message);
    return receipt(message, received_stage, slot != nullptr ? slot->last_progress_at : 0);
}

Status DeliverySender::receipt(std::uint64_t message, DeliveryStage received_stage, Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    if (received_stage != DeliveryStage::Received && received_stage != DeliveryStage::Applied) {
        return fail(Error::InvalidArgument);
    }
    if(message==0)return fail(Error::InvalidArgument);
    if (!unreliable(mode_) && (message <= applied_through_ ||
        (message-applied_through_<=64 && (applied_bits_&(std::uint64_t{1}<<(message-applied_through_-1)))!=0))) {
        return {};
    }
    auto *slot = find(message);
    // Unreliable tickets may be retired after carrier acceptance or superseded.
    // A delayed actual receipt for an already allocated ID cannot affect reuse.
    if(!slot && unreliable(mode_) && (sequence_exhausted_ || message<next_message_))return {};
    if (slot == nullptr || slot->stage == DeliveryStage::Admitted ||
        (attempt_pending_ && &slots_[attempt_slot_] == slot)) {
        return fail(Error::NotReady);
    }
    if (now < slot->last_progress_at) {
        return fail(Error::InvalidArgument);
    }
    if (received_stage == DeliveryStage::Applied) {
        slot->reserved = false;
    }
    if (received_stage > slot->stage) {
        slot->stage = received_stage;
        slot->last_progress_at = now;
    }
    if(received_stage>=DeliveryStage::Received)slot->received_bits=fragment_mask(slot->fragments);
    if (received_stage == DeliveryStage::Applied && !unreliable(mode_)) {
        acknowledge(message, applied_through_, applied_bits_);
    }
    return {};
}
Status DeliverySender::fragment_receipt(const FragmentReceipt& receipt,Tick now) noexcept {
    if(failed_)return fail(Error::ChannelFailed);
    if(receipt.epoch!=epoch_)return fail(Error::StaleEpoch);
    if(receipt.channel!=channel_ || !valid_receipt(receipt))return fail(Error::NonCanonical);
    if(unreliable(mode_))return fail(Error::Unsupported);
    const auto message=receipt.message;
    if(message<=applied_through_ || (message-applied_through_<=64 && (applied_bits_&(std::uint64_t{1}<<(message-applied_through_-1)))!=0))return {};
    auto* slot=find(message); if(!slot)return fail(Error::NotReady);
    if(receipt.fragments!=slot->fragments)return fail(Error::ProtocolViolation);
    if((receipt.bits[0]&~slot->sent_bits[0]) || (receipt.bits[1]&~slot->sent_bits[1]))return fail(Error::ProtocolViolation);
    if(now<slot->last_progress_at)return fail(Error::InvalidArgument);
    const auto first=slot->received_bits[0]|receipt.bits[0],last=slot->received_bits[1]|receipt.bits[1];
    if(first!=slot->received_bits[0] || last!=slot->received_bits[1]) {
        slot->received_bits={first,last}; slot->received_fragments=static_cast<std::uint16_t>(std::popcount(first)+std::popcount(last)); slot->last_progress_at=now;
    }
    return {};
}

Result<DeliveryStage> DeliverySender::stage(std::uint64_t message) const noexcept {
    for (const auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && slot.message == message) {
            return slot.stage;
        }
    }
    return fail(Error::NotReady);
}

Status DeliverySender::retire(std::uint64_t message) noexcept {
    if (attempt_pending_) {
        return fail(Error::Busy);
    }
    auto *slot = find(message);
    if (slot == nullptr) {
        return fail(Error::NotReady);
    }
    if ((unreliable(mode_) && slot->stage >= DeliveryStage::CarrierAccepted) ||
        slot->stage == DeliveryStage::Applied) {
        *slot = {};
        return {};
    }
    return fail(Error::NotReady);
}

Status DeliverySender::expire(Tick now) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    for (const auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && (now < slot.admitted_at || now < slot.last_progress_at)) {
            return fail(Error::InvalidArgument);
        }
    }
    bool timed_out = false;
    bool unknown_outcome = false;
    for (auto &slot : slots_) {
        if (slot.occupied && slot.channel==channel_ && slot.stage != DeliveryStage::Applied &&
            (now - slot.admitted_at >= limits_.timeout_ticks ||
                now - slot.last_progress_at >= limits_.progress_timeout_ticks)) {
            unknown_outcome = unknown_outcome || slot.stage != DeliveryStage::Admitted || slot.next_fragment != 0;
            slot = {};
            timed_out = true;
        }
    }
    if (timed_out) {
        attempt_pending_ = false;
        if (!unreliable(mode_)) {
            failed_ = true;
            for (auto &slot : slots_) {
                if(slot.occupied && slot.channel==channel_)slot = {};
            }
            return fail(unknown_outcome ? Error::UnknownOutcome : Error::Timeout);
        }
    }
    return {};
}

Status DeliverySender::set_retry_ticks(Tick ticks) noexcept {
    if (failed_) {
        return fail(Error::ChannelFailed);
    }
    retry_ticks_ = std::clamp(ticks, limits_.retry_ticks, std::max(limits_.retry_ticks, limits_.progress_timeout_ticks / 2));
    return {};
}

std::size_t DeliverySender::reserved_bytes() const noexcept { return reserved(slots_,channel_); }
std::size_t DeliverySender::pending_messages() const noexcept { return occupied(slots_,channel_); }

} // namespace superpos
