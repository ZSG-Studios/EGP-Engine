// SPDX-License-Identifier: MIT
#include "moves_codec.hpp"
#include <algorithm>
#include <bit>
#include <climits>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,5> defs{{{1,AtomType::Unsigned,0,1,1,false},{2,AtomType::Unsigned,0,3,3,false},{3,AtomType::Unsigned,0,3,3,false},{4,AtomType::Reference,shape_proxy_kind,0,100000,false},{5,AtomType::Unsigned,0,0,100000,false}}};
struct Range{const void*p;size_t n;};template<class T>Range range(std::span<T>s)noexcept{return {s.data(),s.size_bytes()};}
bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.n&&b.n&&(x<=y?y-x<a.n:x-y<b.n);}
Status disjoint(std::span<const Range> ranges)noexcept{for(size_t i=0;i<ranges.size();++i)for(size_t j=i+1;j<ranges.size();++j)if(overlap(ranges[i],ranges[j]))return fail(Error::InvalidArgument);return {};}
Status unique(std::span<const Atom> refs,std::span<uint32_t> order)noexcept{if(order.size()<refs.size())return fail(Error::CapacityExceeded);for(size_t i=0;i<refs.size();++i)order[i]=uint32_t(i);order=order.first(refs.size());std::sort(order.begin(),order.end(),[&](auto a,auto b){return refs[a].identity.simulation<refs[b].identity.simulation;});for(size_t i=1;i<order.size();++i)if(refs[order[i-1]].identity.simulation==refs[order[i]].identity.simulation)return fail(Error::InvalidArgument);return {};}
}
MovesImage::MovesImage(std::span<Atom> refs,std::span<Atom> ts)noexcept:references(refs),types(ts){fields={Field{1,std::span(meta).subspan(0,1)},Field{2,std::span(meta).subspan(1,3)},Field{3,std::span(meta).subspan(4,3)},Field{4,{}},Field{5,{}}};record.fields=fields;}
std::span<const FieldSpec> moves_fields()noexcept{return defs;}
Status capture_moves(const MoveBufferView &n,Identity id,const MoveMappings &maps,std::span<uint32_t> order,MovesImage &out)noexcept{
 if(id.kind!=moves_kind||!id.simulation||!id.generation||n.count>n.capacity||n.capacity>100000||(!n.moves&&n.capacity)||out.references.size()<n.count||out.types.size()<n.count||order.size()<n.count)return fail(Error::InvalidArgument);
 for(size_t t=0;t<3;++t)if(!maps.proxies[t]||n.bits[t].blockCount>n.bits[t].blockCapacity||n.bits[t].blockCapacity>1563||(!n.bits[t].bits&&n.bits[t].blockCapacity))return fail(Error::InvalidArgument);
 const std::array<Range,9> ranges{{range(std::span(n.moves,n.capacity)),range(std::span(n.bits[0].bits,n.bits[0].blockCapacity)),range(std::span(n.bits[1].bits,n.bits[1].blockCapacity)),range(std::span(n.bits[2].bits,n.bits[2].blockCapacity)),range(out.references),range(out.types),range(order),{&out,sizeof(out)},{&n,sizeof(n)}}};if(auto s=disjoint(ranges);!s)return s;
 uint64_t total=0;for(size_t t=0;t<3;++t){for(uint32_t i=0;i<n.bits[t].blockCount;++i)total+=std::popcount(n.bits[t].bits[i]);for(uint32_t i=n.bits[t].blockCount;i<n.bits[t].blockCapacity;++i)if(n.bits[t].bits[i])return fail(Error::NonCanonical);}if(total!=n.count)return fail(Error::InvalidArgument);
 for(size_t i=0;i<n.count;++i){int key=n.moves[i];if(key<0)return fail(Error::InvalidArgument);unsigned type=unsigned(key)&3,slot=unsigned(key)>>2;if(type>=3||!b2GetBit(&n.bits[type],slot))return fail(Error::InvalidArgument);auto ref=maps.proxies[type]->canonical(slot);if(!ref)return fail(ref.error());if(ref->kind!=shape_proxy_kind)return fail(Error::IncompatibleSchema);out.references[i]={0,*ref};out.types[i]={type,{}};}
 if(auto s=unique(out.references.first(n.count),order);!s)return s;out.meta[0]={n.capacity,{}};for(size_t t=0;t<3;++t){out.meta[1+t]={n.bits[t].blockCapacity,{}};out.meta[4+t]={n.bits[t].blockCount,{}};}out.fields[3].atoms=out.references.first(n.count);out.fields[4].atoms=out.types.first(n.count);out.record.identity=id;return {};
}
Result<MoveBufferView> restore_moves(const Record&r,const MoveMappings &maps,std::span<uint32_t> order,MoveStorage dst)noexcept{
 if(r.identity.kind!=moves_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=5)return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<5;++i){if(r.fields[i].id!=defs[i].id||r.fields[i].atoms.size()<defs[i].minimum_atoms||r.fields[i].atoms.size()>defs[i].maximum_atoms)return fail(Error::IncompatibleSchema);}
 for(size_t f=0;f<3;++f)for(const auto&a:r.fields[f].atoms)if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>(f?1563:100000))return fail(Error::InvalidArgument);
 auto refs=r.fields[3].atoms,types=r.fields[4].atoms;const auto capacity=r.fields[0].atoms[0].bits;if(refs.size()!=types.size()||refs.size()>capacity||dst.moves.size()!=capacity||order.size()<refs.size())return fail(Error::CapacityExceeded);
 const std::array<Range,5> outputs{{range(dst.moves),range(dst.bits[0]),range(dst.bits[1]),range(dst.bits[2]),range(order)}};if(auto s=disjoint(outputs);!s)return fail(s.error());
 for(auto o:outputs){if(overlap(o,{&r,sizeof(r)})||overlap(o,range(r.fields)))return fail(Error::InvalidArgument);for(const auto&f:r.fields)if(overlap(o,range(f.atoms)))return fail(Error::InvalidArgument);}
 MoveBufferView view{};view.moves=dst.moves.data();view.capacity=uint32_t(capacity);view.count=uint32_t(refs.size());for(size_t t=0;t<3;++t){auto cap=r.fields[1].atoms[t].bits,count=r.fields[2].atoms[t].bits;if(!maps.proxies[t]||count>cap||dst.bits[t].size()!=cap)return fail(Error::InvalidArgument);view.bits[t]={dst.bits[t].data(),uint32_t(cap),uint32_t(count)};}
 for(size_t i=0;i<refs.size();++i){const auto&a=refs[i],b=types[i];if(a.bits||a.identity.kind!=shape_proxy_kind||!a.identity.simulation||!a.identity.generation||b.identity.kind||b.identity.simulation||b.identity.generation||b.bits>=3)return fail(Error::InvalidArgument);auto slot=maps.proxies[b.bits]->native(a.identity);if(!slot)return fail(slot.error());if(*slot>(unsigned(INT_MAX)>>2)||*slot/64>=view.bits[b.bits].blockCount)return fail(Error::InvalidArgument);dst.moves[i]=int((*slot<<2)|unsigned(b.bits));}
 if(auto s=unique(refs,order);!s)return fail(s.error());for(auto words:dst.bits)std::fill(words.begin(),words.end(),0);for(size_t i=0;i<refs.size();++i)b2SetBit(&view.bits[unsigned(dst.moves[i])&3],unsigned(dst.moves[i])>>2);return view;
}
}
