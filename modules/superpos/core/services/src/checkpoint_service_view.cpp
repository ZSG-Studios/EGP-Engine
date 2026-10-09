// SPDX-License-Identifier: MIT
#include "superpos/checkpoint_service_view.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <climits>

namespace superpos {
namespace {
struct Shape { std::uint8_t columns,keys; std::array<int,8> bounds; };
constexpr std::array<Shape,9> shapes{{
    {8,2,{8,8,8,8,8,8,8,8}}, {6,4,{8,8,8,8,8,-4096}},
    {7,4,{8,8,8,8,-4096,-4096,8}}, {5,2,{8,0,8,8,-4096}},
    {3,1,{8,-4096,8}}, {3,2,{8,8,-4096}}, {2,1,{8,-4096}},
    {2,0,{-4096,8}}, {4,0,{8,32,147,8}}
}};
Result<std::uint64_t> value(const CheckpointCellView& cell) noexcept {
    if(cell.kind==CheckpointCellKind::Unsigned)return cell.number;
    if(cell.kind!=CheckpointCellKind::Bytes||cell.bytes.size()!=8)return fail(Error::RecoveryUnavailable);
    Reader reader(cell.bytes);return reader.u64();
}
Status validate_row(const CheckpointServiceRowView& row,std::size_t table) noexcept {
    if(table<=2){auto kind=value(row.cells[0]),epoch=value(row.cells[2]);
        if(!kind||*kind>2||!epoch||!*epoch)return fail(Error::RecoveryUnavailable);}
    if(table==0){
        auto first=value(row.cells[4]),bits=value(row.cells[7]);if(!first||!bits)return fail(Error::RecoveryUnavailable);
        const bool through=row.cells[5].kind!=CheckpointCellKind::Null,issued=row.cells[6].kind!=CheckpointCellKind::Null;
        auto t=through?value(row.cells[5]):Result<std::uint64_t>{0};
        auto i=issued?value(row.cells[6]):Result<std::uint64_t>{0};
        if(!t||!i||(through&&(!issued||*t>*i||*t<*first))||(issued&&*i<*first))return fail(Error::RecoveryUnavailable);
        if(!issued||(through&&*t==UINT64_MAX)){if(*bits)return fail(Error::RecoveryUnavailable);}
        else {const auto base=through?*t+1:*first;if(*bits&1)return fail(Error::RecoveryUnavailable);
            if(*i<base){if(*bits)return fail(Error::RecoveryUnavailable);}
            else {const auto distance=*i-base;if(distance>=64||(distance<63&&(*bits>>(distance+1))))return fail(Error::RecoveryUnavailable);}}
    }
    if(table==2){auto order=value(row.cells[6]);if(!order||!*order)return fail(Error::RecoveryUnavailable);}
    if(table==3){auto sequence=value(row.cells[2]);if(row.cells[1].number>=64||!sequence||!*sequence)return fail(Error::RecoveryUnavailable);}
    return {};
}
Result<std::array<std::uint64_t,4>> key(const CheckpointServiceRowView& row,std::size_t table) noexcept {
    std::array<std::uint64_t,4> result{};for(unsigned i=0;i<shapes[table].keys;++i){auto n=value(row.cells[i]);if(!n)return fail(n.error());result[i]=*n;}return result;
}
}
Result<CheckpointServicePageView> decode_checkpoint_service_page(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()<32||bytes.size()>checkpoint_chunk_bytes)return fail(Error::InvalidArgument);
    Reader reader(bytes);auto version=reader.u64(),table=reader.u64(),count=reader.u64(),done=reader.u64();
    if(!version||!table||!count||!done)return fail(Error::Truncated);
    if(*version!=1)return fail(Error::Unsupported);
    if(*table>=shapes.size()||*count>64||*done>1||(!*done&&!*count))return fail(Error::NonCanonical);
    const auto index=static_cast<std::size_t>(*table);const auto& shape=shapes[index];
    if(!shape.keys&&(*count>1||!*done))return fail(Error::NonCanonical);
    CheckpointServicePageView page;page.table=static_cast<CheckpointServiceTable>(index);page.count=static_cast<std::uint8_t>(*count);page.table_complete=bool(*done);
    std::array<std::uint64_t,4> previous{};bool has_previous=false;
    for(unsigned r=0;r<page.count;++r){
        auto length=reader.u64();if(!length)return fail(length.error());if(*length>16*1024)return fail(Error::CapacityExceeded);
        auto data=reader.raw(static_cast<std::size_t>(*length));if(!data)return fail(data.error());Reader row_reader(*data);
        auto columns=row_reader.u64();if(!columns||*columns!=shape.columns)return fail(Error::NonCanonical);
        auto& row=page.rows[r];row.count=shape.columns;
        for(unsigned c=0;c<shape.columns;++c){
            auto tag=row_reader.u64(),size=row_reader.u64();if(!tag||!size)return fail(Error::Truncated);
            auto& cell=row.cells[c];const auto bound=shape.bounds[c];
            if(*tag==0){if(*size||index!=0||(c!=5&&c!=6))return fail(Error::NonCanonical);cell.kind=CheckpointCellKind::Null;continue;}
            if(bound==0){if(*tag!=2||*size!=8)return fail(Error::NonCanonical);auto number=row_reader.u64();if(!number)return fail(number.error());if(*number>INT64_MAX)return fail(Error::NonCanonical);cell.kind=CheckpointCellKind::Unsigned;cell.number=*number;}
            else {const auto maximum=static_cast<std::uint64_t>(bound<0?-bound:bound);if(*tag!=1||*size>maximum||(bound>0&&*size!=maximum))return fail(Error::NonCanonical);
                auto payload=row_reader.raw(static_cast<std::size_t>(*size));if(!payload)return fail(payload.error());cell.kind=CheckpointCellKind::Bytes;cell.bytes=*payload;}
        }
        if(!row_reader.empty())return fail(Error::NonCanonical);
        if(auto valid=validate_row(row,index);!valid)return fail(valid.error());
        if(shape.keys){auto current=key(row,index);if(!current)return fail(current.error());if(has_previous&&!std::lexicographical_compare(previous.begin(),previous.end(),current->begin(),current->end()))return fail(Error::NonCanonical);previous=*current;has_previous=true;}
    }
    if(!reader.empty())return fail(Error::NonCanonical);return page;
}
CheckpointServiceInspector::CheckpointServiceInspector(const CheckpointRecoveryReceipt& expected) noexcept:expected_(expected){progress_.ticket=expected.content.ticket;}
Result<CheckpointServiceInspector> CheckpointServiceInspector::create(const CheckpointRecoveryReceipt& r) noexcept {
    if(r.content.state!=CheckpointContentState::RecoveryVerified||r.content.ticket!=r.service.ticket||
       !r.content.ticket.match||!r.content.ticket.id||!r.content.ticket.generation||
       r.content.manifest.match!=r.content.ticket.match||r.content.manifest.id!=r.content.ticket.id||!r.service.complete||r.service.next_table!=9||
       r.service.pages<9||r.service.pages>checkpoint_maximum_chunks||r.service.bytes<32ULL*r.service.pages||
       r.service.bytes>checkpoint_maximum_bytes||r.content.manifest.bytes>checkpoint_maximum_bytes-r.service.bytes||
       !known_fingerprint(r.service.commitment))return fail(Error::InvalidArgument);
    return CheckpointServiceInspector(r);
}
Status CheckpointServiceInspector::accept(std::uint32_t ordinal,std::span<const std::byte> bytes) noexcept {
    if(ordinal!=progress_.pages)return fail(Error::StaleGeneration);
    if(progress_.pages>=expected_.service.pages||progress_.tables>=9)return fail(Error::CapacityExceeded);
    auto page=decode_checkpoint_service_page(bytes);if(!page)return fail(page.error());const auto table=static_cast<std::size_t>(page->table);
    if(table!=progress_.tables)return fail(Error::NonCanonical);
    if(bytes.size()>expected_.service.bytes-progress_.bytes)return fail(Error::CapacityExceeded);
    auto next_key=previous_key_;bool next_has_key=has_key_;
    if(shapes[table].keys&&page->count){auto first=key(page->rows[0],table),last=key(page->rows[page->count-1],table);if(!first||!last)return fail(Error::RecoveryUnavailable);
        if(has_key_&&!std::lexicographical_compare(previous_key_.begin(),previous_key_.end(),first->begin(),first->end()))return fail(Error::NonCanonical);
        next_key=*last;next_has_key=true;}
    if(page->table_complete){next_has_key=false;next_key={};}
    const auto next_tables=progress_.tables+unsigned(page->table_complete);
    if(next_tables==9&&progress_.pages+1!=expected_.service.pages)return fail(Error::RecoveryUnavailable);
    if(progress_.pages+1==expected_.service.pages&&(next_tables!=9||progress_.bytes+bytes.size()!=expected_.service.bytes))return fail(Error::RecoveryUnavailable);
    previous_key_=next_key;has_key_=next_has_key;++progress_.pages;progress_.bytes+=bytes.size();progress_.rows+=page->count;progress_.tables=next_tables;return {};
}
Result<CheckpointServiceInspection> CheckpointServiceInspector::finish() const noexcept {
    if(progress_.pages!=expected_.service.pages||progress_.bytes!=expected_.service.bytes||progress_.tables!=9)return fail(Error::NotReady);return progress_;
}
namespace {
struct VerifierGuard {
    bool& busy;
    explicit VerifierGuard(bool& value) noexcept:busy(value){busy=true;}
    ~VerifierGuard(){busy=false;}
};
}
CheckpointServiceVerifier::CheckpointServiceVerifier(const CheckpointRecoveryReceipt& r,
    const CheckpointServiceInspector& inspector,CryptographicDigest& digest,Fingerprint chain) noexcept
    :expected_(r),inspector_(inspector),digest_(&digest),chain_(chain){}
Result<CheckpointServiceVerifier> CheckpointServiceVerifier::create(const CheckpointRecoveryReceipt& input,CryptographicDigest& digest) noexcept {
    // Snapshot before the first provider callback. A trusted provider can still
    // synchronously invoke application code holding a mutable alias to input.
    const CheckpointRecoveryReceipt r=input;
    auto inspector=CheckpointServiceInspector::create(r);if(!inspector)return fail(inspector.error());
    if(!known_fingerprint(r.origin.boot_nonce))return fail(Error::InvalidArgument);
    if(digest.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    auto codec=checkpoint_service_codec(digest);if(!codec)return fail(codec.error());if(*codec!=r.content.manifest.service_codec)return fail(Error::IncompatibleSchema);
    const auto& m=r.content.manifest;const auto& t=r.content.ticket;
    std::array<std::byte,104> seed{};Writer writer(seed);
    for(auto n:{std::uint64_t{0x5350535356430001},t.match,t.id,t.generation,r.service.revision,r.origin.boot_term,m.epoch,m.sequence,m.tick})
        if(auto status=writer.u64(n);!status)return fail(status.error());
    if(auto status=writer.raw(r.origin.boot_nonce);!status)return fail(status.error());
    Fingerprint chain{};if(auto status=digest.hash(seed,chain);!status)return fail(status.error());
    if(digest.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    return CheckpointServiceVerifier(r,*inspector,digest,chain);
}
Status CheckpointServiceVerifier::accept(std::uint32_t ordinal,std::span<const std::byte> bytes) noexcept {
    if(busy_)return fail(Error::Busy);VerifierGuard guard(busy_);
    // Publish neither the structural cursor nor the hash chain until every
    // fallible digest operation succeeds. Retry after provider failure is exact.
    auto next=inspector_;if(auto status=next.accept(ordinal,bytes);!status)return status;
    if(digest_->algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    Fingerprint page{},chain{};if(auto status=digest_->hash(bytes,page);!status)return status;
    std::array<std::byte,72> link{};Writer writer(link);
    if(auto status=writer.raw(chain_);!status)return status;if(auto status=writer.u64(ordinal);!status)return status;
    if(auto status=writer.raw(page);!status)return status;
    if(auto status=digest_->hash(link,chain);!status)return status;
    if(digest_->algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    inspector_=next;chain_=chain;return {};
}
Result<CheckpointServiceInspection> CheckpointServiceVerifier::finish() noexcept {
    if(busy_)return fail(Error::Busy);VerifierGuard guard(busy_);auto progress=inspector_.finish();if(!progress)return fail(progress.error());
    if(digest_->algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    std::array<std::byte,96> terminal{};Writer writer(terminal);
    if(auto status=writer.raw(chain_);!status)return fail(status.error());
    for(auto n:{progress->bytes,std::uint64_t(progress->pages),expected_.service.cache_order,expected_.service.revision})
        if(auto status=writer.u64(n);!status)return fail(status.error());
    if(auto status=writer.raw(expected_.content.manifest.content);!status)return fail(status.error());
    Fingerprint result{};if(auto status=digest_->hash(terminal,result);!status)return fail(status.error());
    if(digest_->algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    if(result!=expected_.service.commitment)return fail(Error::RecoveryUnavailable);return progress;
}
}
