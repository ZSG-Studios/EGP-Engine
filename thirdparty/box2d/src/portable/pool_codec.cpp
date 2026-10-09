// SPDX-License-Identifier: MIT
#include "pool_codec.hpp"
#include <algorithm>
namespace superpos::box2d_portable {
using namespace canonical;
namespace {
constexpr std::uint32_t allocated_field=1,capacity_field=2,free_order_field=3;
bool overlaps(const void *a,size_t na,const void *b,size_t nb) noexcept {
 if(!na||!nb)return false;auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
 return x<=y?y-x<na:x-y<nb;
}

Status complete_map(const IdentityMap &map,std::uint32_t count,std::uint32_t kind) noexcept {
 for(std::uint32_t slot=0;slot<count;++slot){auto token=map.canonical(slot);if(!token||token->kind!=kind)return fail(Error::RecoveryUnavailable);}
 return {};
}
}
PoolImage::PoolImage(std::span<Atom> free_order) noexcept:free_order_storage(free_order) {
 fields={Field{allocated_field,std::span(counts).subspan(0,1)},Field{capacity_field,std::span(counts).subspan(1,1)},Field{free_order_field,free_order}};record.fields=fields;
}
Result<PoolDefinition> pool_definition(std::uint32_t kind,std::uint32_t limit) noexcept {
 if(!kind||kind>UINT32_MAX-0x2000||!limit||limit>100000)return fail(Error::InvalidArgument);
 PoolDefinition d;d.record_kind=kind+0x2000;d.token_kind=kind;d.maximum_slots=limit;
 d.fields={FieldSpec{allocated_field,AtomType::Unsigned,0,1,1,false},FieldSpec{capacity_field,AtomType::Unsigned,0,1,1,false},FieldSpec{free_order_field,AtomType::Reference,kind,0,limit,false}};return d;
}
Status capture_pool(const SpPoolView &native,Identity id,const PoolDefinition &d,const IdentityMap &map,std::span<std::byte> visited,PoolImage &image) noexcept {
 if(id.kind!=d.record_kind||!id.simulation||!id.generation||native.allocated_count>d.maximum_slots||native.free_capacity>d.maximum_slots||native.free_count>native.allocated_count||native.free_count>native.free_capacity||(!native.free_entries&&native.free_count))return fail(Error::InvalidArgument);
 if(visited.size()<native.allocated_count||image.free_order_storage.size()<native.free_count)return fail(Error::CapacityExceeded);
 if(overlaps(visited.data(),visited.size_bytes(),native.free_entries,size_t(native.free_count)*sizeof(int)) ||
 overlaps(image.free_order_storage.data(),image.free_order_storage.size_bytes(),native.free_entries,size_t(native.free_count)*sizeof(int)) ||
 overlaps(image.free_order_storage.data(),image.free_order_storage.size_bytes(),visited.data(),visited.size_bytes()) ||
 overlaps(&image,sizeof(image),visited.data(),visited.size_bytes()))return fail(Error::InvalidArgument);
 if(auto v=complete_map(map,native.allocated_count,d.token_kind);!v)return v;
 std::fill_n(visited.begin(),native.allocated_count,std::byte{});
 for(std::uint32_t i=0;i<native.free_count;++i){auto slot=native.free_entries[i];if(slot<0||std::uint32_t(slot)>=native.allocated_count||visited[slot]!=std::byte{})return fail(Error::InvalidArgument);visited[slot]=std::byte{1};}
 // Image storage is private. Its free-order backing was supplied at construction.
 auto output=image.free_order_storage.data();
 for(std::uint32_t i=0;i<native.free_count;++i){auto token=map.canonical(std::uint32_t(native.free_entries[i]));if(!token)return fail(token.error());output[i]={0,*token};}
 image.counts[0]={native.allocated_count,{}};image.counts[1]={native.free_capacity,{}};
 image.fields[2].atoms={output,native.free_count};image.record.identity=id;return {};
}
Result<SpPoolView> restore_pool(const Record &r,const PoolDefinition &d,const IdentityMap &map,std::span<std::byte> visited,std::span<int> staging) noexcept {
 if(r.identity.kind!=d.record_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=3)return fail(Error::IncompatibleSchema);
 for(unsigned i=0;i<3;++i)if(r.fields[i].id!=d.fields[i].id)return fail(Error::IncompatibleSchema);
 if(r.fields[0].atoms.size()!=1||r.fields[1].atoms.size()!=1)return fail(Error::IncompatibleSchema);
 const auto &a=r.fields[0].atoms[0],&b=r.fields[1].atoms[0];
 if(a.identity.kind||a.identity.simulation||a.identity.generation||b.identity.kind||b.identity.simulation||b.identity.generation||a.bits>d.maximum_slots||b.bits>d.maximum_slots)return fail(Error::InvalidArgument);
 const auto count=std::uint32_t(a.bits),capacity=std::uint32_t(b.bits);const auto free=r.fields[2].atoms;
 if(free.size()>count||free.size()>capacity||visited.size()<count||staging.size()<free.size())return fail(Error::CapacityExceeded);
 if(overlaps(visited.data(),visited.size_bytes(),staging.data(),staging.size_bytes()) ||
 overlaps(&r,sizeof(r),visited.data(),visited.size_bytes()) || overlaps(&r,sizeof(r),staging.data(),staging.size_bytes()) ||
 overlaps(r.fields.data(),r.fields.size_bytes(),visited.data(),visited.size_bytes()) || overlaps(r.fields.data(),r.fields.size_bytes(),staging.data(),staging.size_bytes()))return fail(Error::InvalidArgument);
 for(const auto &field:r.fields)if(overlaps(field.atoms.data(),field.atoms.size_bytes(),visited.data(),visited.size_bytes()) || overlaps(field.atoms.data(),field.atoms.size_bytes(),staging.data(),staging.size_bytes()))return fail(Error::InvalidArgument);
 if(auto v=complete_map(map,count,d.token_kind);!v)return fail(v.error());
 std::fill_n(visited.begin(),count,std::byte{});
 for(size_t i=0;i<free.size();++i){if(free[i].bits||free[i].identity.kind!=d.token_kind)return fail(Error::InvalidArgument);
 auto slot=map.native(free[i].identity);if(!slot)return fail(slot.error());if(*slot>=count||visited[*slot]!=std::byte{})return fail(Error::InvalidArgument);visited[*slot]=std::byte{1};staging[i]=int(*slot);}
 return SpPoolView{staging.data(),std::uint32_t(free.size()),capacity,count};
}
}
