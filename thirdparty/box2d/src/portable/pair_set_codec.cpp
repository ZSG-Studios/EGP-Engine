// SPDX-License-Identifier: MIT
#include "pair_set_codec.hpp"
#include <algorithm>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,2> defs{{{1,AtomType::Unsigned,0,1,1,false},{2,AtomType::Reference,shape_kind,0,131072,false}}};
bool overlap(const void*a,size_t n,const void*b,size_t m)noexcept{auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return n&&m&&(x<=y?y-x<n:x-y<m);}
bool valid_capacity(uint64_t n)noexcept{return n>=16&&n<=131072&&(n&(n-1))==0;}
bool less_pair(std::span<const Atom>a,std::span<const Atom>b)noexcept{return a[0].identity.simulation<b[0].identity.simulation||(a[0].identity.simulation==b[0].identity.simulation&&a[1].identity.simulation<b[1].identity.simulation);}
}
PairSetImage::PairSetImage(std::span<Atom> s)noexcept:storage(s){fields={Field{1,std::span(&capacity,1)},Field{2,{}}};record.fields=fields;}
std::span<const FieldSpec> pair_set_fields()noexcept{return defs;}
Status capture_pair_set(const b2HashSet &n,Identity id,const IdentityMap &map,PairSetImage &out)noexcept{
 if(id.kind!=pair_set_kind||!id.simulation||!id.generation||!valid_capacity(n.capacity)||n.count>n.capacity/2||!n.items||out.storage.size()<size_t(n.count)*2)return fail(Error::InvalidArgument);
 if(overlap(&out,sizeof(out),out.storage.data(),out.storage.size_bytes())||overlap(&n,sizeof(n),out.storage.data(),out.storage.size_bytes())||overlap(n.items,size_t(n.capacity)*sizeof(b2SetItem),out.storage.data(),out.storage.size_bytes())||overlap(n.items,size_t(n.capacity)*sizeof(b2SetItem),&out,sizeof(out)))return fail(Error::InvalidArgument);
 size_t count=0;for(uint32_t i=0;i<n.capacity;++i)if(n.items[i].key)++count;if(count!=n.count)return fail(Error::InvalidArgument);
 count=0;for(uint32_t i=0;i<n.capacity;++i){uint64_t key=n.items[i].key;if(!key)continue;uint32_t x=uint32_t(key>>32),y=uint32_t(key);if(x>=y||y>INT32_MAX||!b2ContainsKey(&n,key))return fail(Error::InvalidArgument);auto a=map.canonical(x),b=map.canonical(y);if(!a)return fail(a.error());if(!b)return fail(b.error());if(a->kind!=shape_kind||b->kind!=shape_kind)return fail(Error::IncompatibleSchema);if(a->simulation>b->simulation)std::swap(a,b);out.storage[count*2]={0,*a};out.storage[count*2+1]={0,*b};++count;}
 // In-place heap sorting preserves bounded O(n log n) work without allocation.
 auto lower=[&](size_t a,size_t b){return less_pair(out.storage.subspan(a*2,2),out.storage.subspan(b*2,2));};
 auto swap_pair=[&](size_t a,size_t b){std::swap(out.storage[a*2],out.storage[b*2]);std::swap(out.storage[a*2+1],out.storage[b*2+1]);};
 auto sift=[&](size_t start,size_t end){while(start<end/2){size_t child=start*2+1;if(child+1<end&&lower(child,child+1))++child;if(!lower(start,child))break;swap_pair(start,child);start=child;}};
 for(size_t i=count/2;i>0;--i)sift(i-1,count);for(size_t end=count;end>1;--end){swap_pair(0,end-1);sift(0,end-1);}
 for(size_t i=1;i<count;++i)if(!less_pair(out.storage.subspan((i-1)*2,2),out.storage.subspan(i*2,2)))return fail(Error::InvalidArgument);
 out.capacity={n.capacity,{}};out.fields[1].atoms=out.storage.first(count*2);out.record.identity=id;return {};
}
Result<b2HashSet> restore_pair_set(const Record&r,const IdentityMap&map,std::span<uint64_t> keys,std::span<b2SetItem> items)noexcept{
 if(r.identity.kind!=pair_set_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=2||r.fields[0].id!=1||r.fields[1].id!=2||r.fields[0].atoms.size()!=1)return fail(Error::IncompatibleSchema);
 const auto cap=r.fields[0].atoms[0];auto pairs=r.fields[1].atoms;if(cap.identity.kind||cap.identity.simulation||cap.identity.generation||!valid_capacity(cap.bits)||items.size()!=cap.bits||(pairs.size()%2)||pairs.size()>cap.bits||keys.size()<pairs.size()/2)return fail(Error::InvalidArgument);
 if(overlap(items.data(),items.size_bytes(),keys.data(),keys.size_bytes())||overlap(&r,sizeof(r),items.data(),items.size_bytes())||overlap(&r,sizeof(r),keys.data(),keys.size_bytes())||overlap(r.fields.data(),r.fields.size_bytes(),items.data(),items.size_bytes())||overlap(r.fields.data(),r.fields.size_bytes(),keys.data(),keys.size_bytes()))return fail(Error::InvalidArgument);
 for(const auto&f:r.fields)if(overlap(f.atoms.data(),f.atoms.size_bytes(),items.data(),items.size_bytes())||overlap(f.atoms.data(),f.atoms.size_bytes(),keys.data(),keys.size_bytes()))return fail(Error::InvalidArgument);
 for(size_t i=0;i<pairs.size()/2;++i){auto pair=pairs.subspan(i*2,2);for(const auto&a:pair)if(a.bits||a.identity.kind!=shape_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);if(pair[0].identity.simulation>=pair[1].identity.simulation||(i&&!less_pair(pairs.subspan((i-1)*2,2),pair)))return fail(Error::NonCanonical);auto a=map.native(pair[0].identity),b=map.native(pair[1].identity);if(!a)return fail(a.error());if(!b)return fail(b.error());if(*a>INT32_MAX||*b>INT32_MAX||*a==*b)return fail(Error::InvalidArgument);keys[i]=(uint64_t(std::min(*a,*b))<<32)|std::max(*a,*b);}
 std::fill(items.begin(),items.end(),b2SetItem{});b2HashSet result{items.data(),uint32_t(items.size()),0};for(size_t i=0;i<pairs.size()/2;++i)if(b2AddKey(&result,keys[i]))return fail(Error::ProtocolViolation);return result;
}
}
