#include "superpos/journal_envelope.hpp"
#include "superpos/codec.hpp"
namespace superpos {
namespace {
Result<std::uint16_t> u16(Reader& reader) noexcept {
    auto bytes=reader.raw(2);if(!bytes)return fail(bytes.error());
    return static_cast<std::uint16_t>((std::to_integer<unsigned>((*bytes)[0])<<8)|std::to_integer<unsigned>((*bytes)[1]));
}
}
Result<JournalEnvelopeView> decode_journal_envelope(std::uint64_t match,std::span<const std::byte> encoded) noexcept {
    if(!match)return fail(Error::InvalidArgument);
    if(encoded.size()>1024*1024)return fail(Error::CapacityExceeded);
    JournalEnvelopeView view{};if(encoded.empty())return view;
    Reader reader(encoded);auto decisions=u16(reader),effects=u16(reader);
    if(!decisions||!effects)return fail(Error::Truncated);
    if(*decisions>64||*effects>64)return fail(Error::CapacityExceeded);
    // The producer uses an empty blob when both lists are empty.
    if(!*decisions&&!*effects)return fail(Error::NonCanonical);
    for(std::size_t i=0;i<*decisions;++i){
        auto id_match=reader.u64();auto kind=reader.raw(1);
        auto actor=reader.u64(),epoch=reader.u64(),sequence=reader.u64();
        auto request_size=u16(reader),result_size=u16(reader);
        if(!id_match||!kind||!actor||!epoch||!sequence||!request_size||!result_size)return fail(Error::Truncated);
        const auto actor_kind=std::to_integer<unsigned>((*kind)[0]);
        if(*id_match!=match||!*epoch||actor_kind>2)return fail(Error::ProtocolViolation);
        if(*request_size>4096||*result_size>4096)return fail(Error::CapacityExceeded);
        auto request=reader.raw(*request_size),result=reader.raw(*result_size);
        if(!request||!result)return fail(Error::Truncated);
        DurableOperationId id{*id_match,*actor,*epoch,*sequence,static_cast<OperationActorKind>(actor_kind)};
        for(std::size_t j=0;j<i;++j)if(view.decisions[j].id==id)return fail(Error::ProtocolViolation);
        view.decisions[i]={id,*request,*result};
    }
    for(std::size_t i=0;i<*effects;++i){
        auto recipient=reader.u64();auto size=u16(reader);
        if(!recipient||!size)return fail(Error::Truncated);
        if(*size>4096)return fail(Error::CapacityExceeded);
        auto payload=reader.raw(*size);if(!payload)return fail(payload.error());
        view.effects[i]={*recipient,*payload};
    }
    if(!reader.empty())return fail(Error::NonCanonical);
    view.decision_count=*decisions;view.effect_count=*effects;return view;
}
}
