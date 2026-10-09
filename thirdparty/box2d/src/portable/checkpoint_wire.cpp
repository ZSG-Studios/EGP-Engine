// SPDX-License-Identifier: MIT
#include "checkpoint_wire.hpp"
#include <algorithm>
#include <cstring>
namespace superpos::box2d_portable::fixture {using namespace canonical;
namespace {
constexpr std::byte magic[8]={std::byte('S'),std::byte('P'),std::byte('B'),std::byte('2'),std::byte('W'),std::byte('C'),std::byte('K'),std::byte('1')};
constexpr size_t header_bytes=8+4+4+8+32+4,trailer_bytes=32,atom_bytes=8+4+8+8,identity_bytes=4+8+8;
constexpr uint32_t max_fields=128,max_atoms_per_field=262144,max_group=100000;
constexpr uint64_t max_total_atoms=uint64_t(16)<<20;
struct Writer{std::vector<std::byte> b;
 void u32(uint32_t v){for(int i=0;i<4;++i)b.push_back(std::byte(v>>(8*i)));}
 void u64(uint64_t v){for(int i=0;i<8;++i)b.push_back(std::byte(v>>(8*i)));}
 void id(const Identity&i){u32(i.kind);u64(i.simulation);u64(i.generation);}
 void record(const Record&r){id(r.identity);u32(uint32_t(r.fields.size()));for(const auto&f:r.fields){u32(f.id);u32(uint32_t(f.atoms.size()));for(const auto&a:f.atoms){u64(a.bits);id(a.identity);}}}
 void group(const std::vector<Record>&g){u32(uint32_t(g.size()));for(const auto&r:g)record(r);}};
struct Reader{const std::byte*p;size_t left;Error error=Error::None;
 bool need(size_t n){if(error!=Error::None)return false;if(n>left){error=Error::Truncated;return false;}return true;}
 uint32_t u32(){if(!need(4))return 0;uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(uint8_t(p[i]))<<(8*i);p+=4;left-=4;return v;}
 uint64_t u64(){if(!need(8))return 0;uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(uint8_t(p[i]))<<(8*i);p+=8;left-=8;return v;}
 Identity id(){Identity i;i.kind=u32();i.simulation=u64();i.generation=u64();return i;}
 void reject(Error e){if(error==Error::None)error=e;}};
bool before(const Identity&a,const Identity&b){if(a.kind!=b.kind)return a.kind<b.kind;if(a.simulation!=b.simulation)return a.simulation<b.simulation;return a.generation<b.generation;}
// Every vector group the checkpoint stores in canonical identity order.
template<class F> void each_group(WorldCheckpoint&cp,F f){
 for(auto*g:{&cp.sets,&cp.bodies,&cp.states,&cp.islands,&cp.cold_bodies,&cp.cold_contacts,&cp.cold_joints,&cp.set_contacts,&cp.set_joints,&cp.set_payloads,&cp.graph_contacts,&cp.graph_joints,&cp.graph_payloads,&cp.shapes,&cp.shape_geometry,&cp.chains,&cp.sensors})f(*g);
 for(auto&g:cp.nodes)f(g);for(auto&g:cp.pool_occupants)f(g);}
template<class F> void each_single(WorldCheckpoint&cp,F f){
 for(auto*r:{&cp.root,&cp.graph,&cp.moves,&cp.pairs,&cp.broadphase})f(*r);
 for(auto&r:cp.trees)f(r);for(auto&r:cp.colors)f(r);for(auto&r:cp.events)f(r);for(auto&r:cp.pool_records)f(r);}
Record read_record(Reader&in,WorldCheckpoint&cp,uint64_t&total_atoms){
 auto o=std::make_unique<OwnedRecord>();o->identity=in.id();const uint32_t nf=in.u32();if(nf>max_fields){in.reject(Error::CapacityExceeded);return {};}
 std::vector<std::pair<uint32_t,uint32_t>> shape;shape.reserve(nf);
 for(uint32_t f=0;f<nf&&in.error==Error::None;++f){const uint32_t fid=in.u32(),n=in.u32();if(n>max_atoms_per_field||(total_atoms+=n)>max_total_atoms){in.reject(Error::CapacityExceeded);return {};}if(!in.need(size_t(n)*atom_bytes))return {};
  shape.push_back({fid,n});for(uint32_t k=0;k<n;++k){Atom a;a.bits=in.u64();a.identity=in.id();o->atoms.push_back(a);}}
 if(in.error!=Error::None)return {};
 size_t at=0;for(auto[fid,n]:shape){o->fields.push_back({fid,std::span<const Atom>(o->atoms).subspan(at,n)});at+=n;}
 Record v{o->identity,o->fields};cp.owned.push_back(std::move(o));return v;}
}
uint32_t checkpoint_position_bits()noexcept{
#ifdef BOX2D_DOUBLE_PRECISION
 return 64;
#else
 return 32;
#endif
}
void reseal_checkpoint(std::vector<std::byte>&bytes){if(bytes.size()<header_bytes+trailer_bytes)return;Sha256 h;h.update(std::span<const std::byte>(bytes).first(bytes.size()-trailer_bytes));auto d=h.finish();std::memcpy(bytes.data()+bytes.size()-trailer_bytes,d.data(),trailer_bytes);}
std::vector<std::byte> encode_checkpoint(const WorldCheckpoint&source){
 auto&cp=const_cast<WorldCheckpoint&>(source);// read-only traversal through the shared helpers
 Writer w;w.b.insert(w.b.end(),std::begin(magic),std::end(magic));w.u32(checkpoint_wire_version);w.u32(checkpoint_position_bits());w.u64(0);
 w.b.insert(w.b.end(),cp.source_digest.begin(),cp.source_digest.end());w.u32(cp.record_count);
 each_single(cp,[&](Record&r){w.record(r);});each_group(cp,[&](std::vector<Record>&g){w.group(g);});
 std::vector<uint64_t> symbols;for(const auto&b:cp.bindings){if(b.identity.kind!=binding_kind||!b.identity.simulation||b.identity.generation!=1)return {};symbols.push_back(b.identity.simulation);}
 std::sort(symbols.begin(),symbols.end());if(std::adjacent_find(symbols.begin(),symbols.end())!=symbols.end())return {};
 w.u32(uint32_t(symbols.size()));for(uint64_t s:symbols)w.u64(s);
 w.u32(uint32_t(cp.retired.size()));for(const auto&r:cp.retired){w.u32(r.pool);w.id(r.identity);w.id(r.slot);w.u32(r.generation);}
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){w.u64(cp.slot_base[p]);w.u32(uint32_t(cp.slot_source[p].size()));for(uint32_t s:cp.slot_source[p])w.u32(s);}
 const uint64_t total=w.b.size()+trailer_bytes;if(total>checkpoint_wire_max_bytes)return {};for(int i=0;i<8;++i)w.b[16+i]=std::byte(total>>(8*i));
 w.b.resize(size_t(total));reseal_checkpoint(w.b);return std::move(w.b);
}
Status decode_checkpoint(std::span<const std::byte> bytes,const SymbolRegistry&symbols,WorldCheckpoint&cp){
 if(!cp.owned.empty())return fail(Error::InvalidArgument);
 if(bytes.size()<header_bytes+trailer_bytes)return fail(Error::Truncated);if(bytes.size()>checkpoint_wire_max_bytes)return fail(Error::CapacityExceeded);
 if(std::memcmp(bytes.data(),magic,8))return fail(Error::IncompatibleSchema);
 Reader in{bytes.data()+8,bytes.size()-8-trailer_bytes};
 if(in.u32()!=checkpoint_wire_version)return fail(Error::IncompatibleSchema);
 if(in.u32()!=checkpoint_position_bits())return fail(Error::IncompatibleSchema);// cross-precision restore is unsupported
 const uint64_t total=in.u64();if(total!=bytes.size())return fail(total>bytes.size()?Error::Truncated:Error::InvalidArgument);
 {Sha256 h;h.update(bytes.first(bytes.size()-trailer_bytes));auto d=h.finish();if(std::memcmp(d.data(),bytes.data()+bytes.size()-trailer_bytes,trailer_bytes))return fail(Error::AuthenticationFailed);}
 if(!in.need(32))return fail(in.error);std::memcpy(cp.source_digest.data(),in.p,32);in.p+=32;in.left-=32;cp.record_count=in.u32();
 uint64_t total_atoms=0;
 each_single(cp,[&](Record&r){if(in.error==Error::None)r=read_record(in,cp,total_atoms);});
 each_group(cp,[&](std::vector<Record>&g){if(in.error!=Error::None)return;const uint32_t n=in.u32();if(n>max_group||size_t(n)*(identity_bytes+4)>in.left){in.reject(n>max_group?Error::CapacityExceeded:Error::Truncated);return;}
  g.reserve(n);for(uint32_t i=0;i<n&&in.error==Error::None;++i){auto r=read_record(in,cp,total_atoms);if(in.error!=Error::None)return;if(!g.empty()&&before(r.identity,g.back().identity)){in.reject(Error::NonCanonical);return;}g.push_back(r);}});
 if(in.error!=Error::None)return fail(in.error);
 {const uint32_t n=in.u32();if(n>max_group||size_t(n)*8>in.left)return fail(n>max_group?Error::CapacityExceeded:Error::Truncated);uint64_t prior=0;
  for(uint32_t i=0;i<n;++i){const uint64_t s=in.u64();if(!s||s<=prior)return fail(Error::NonCanonical);prior=s;void*p=symbols.pointer(s);if(!p)return fail(Error::PermissionDenied);cp.bindings.push_back({p,{binding_kind,s,1}});}
  std::sort(cp.bindings.begin(),cp.bindings.end(),[](const PointerBinding&a,const PointerBinding&b){return reinterpret_cast<uintptr_t>(a.pointer)<reinterpret_cast<uintptr_t>(b.pointer);});}
 {const uint32_t n=in.u32();if(n>max_group||size_t(n)*(8+2*identity_bytes)>in.left)return fail(n>max_group?Error::CapacityExceeded:Error::Truncated);
  for(uint32_t i=0;i<n;++i){RetiredLifetime r;r.pool=in.u32();r.identity=in.id();r.slot=in.id();r.generation=in.u32();if(in.error!=Error::None)return fail(in.error);if(r.pool>=SP_POOL_COUNT)return fail(Error::InvalidArgument);cp.retired.push_back(r);}}
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){cp.slot_base[p]=in.u64();const uint32_t n=in.u32();if(n>max_group||size_t(n)*4>in.left)return fail(n>max_group?Error::CapacityExceeded:Error::Truncated);for(uint32_t i=0;i<n;++i)cp.slot_source[p].push_back(in.u32());}
 if(in.error!=Error::None)return fail(in.error);if(in.left)return fail(Error::InvalidArgument);
 // Singleton records carry exactly the admitted singleton identities.
 {WorldDigestMaps m;assign_singleton_ids(m);bool ok=cp.root.identity==m.root&&cp.graph.identity==m.graph&&cp.broadphase.identity==m.broadphase&&cp.moves.identity==m.broadphase_ids.moves&&cp.pairs.identity==m.broadphase_ids.pairs;
  for(uint32_t t=0;t<3;++t)ok=ok&&cp.trees[t].identity==m.broadphase_ids.trees[t];for(uint32_t c=0;c<24;++c)ok=ok&&cp.colors[c].identity==m.colors[c];for(uint32_t a=0;a<9;++a)ok=ok&&cp.events[a].identity==m.events[a];
  for(uint32_t p=0;p<SP_POOL_COUNT;++p)ok=ok&&cp.pool_records[p].identity.simulation==p+1&&cp.pool_records[p].identity.generation==1;if(!ok)return fail(Error::InvalidArgument);}
 // Slot layout: per pool, the fixture slot-identity base and a permutation of
 // the pool's slots.
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){if(cp.slot_base[p]!=uint64_t(100000)*(p+1)||cp.slot_source[p].size()!=cp.pool_occupants[p].size())return fail(Error::InvalidArgument);std::vector<uint8_t> seen(cp.slot_source[p].size());for(uint32_t s:cp.slot_source[p]){if(s>=seen.size()||seen[s])return fail(Error::InvalidArgument);seen[s]=1;}}
 // The header record count covers every visited record except the pool records.
 {size_t n=5+3+24+9;each_group(cp,[&](std::vector<Record>&g){n+=g.size();});for(const auto&g:cp.pool_occupants)n-=g.size();if(n!=cp.record_count)return fail(Error::InvalidArgument);}
 // Canonical form: the decoded checkpoint must re-encode to the same bytes.
 auto again=encode_checkpoint(cp);if(again.size()!=bytes.size()||std::memcmp(again.data(),bytes.data(),bytes.size()))return fail(Error::NonCanonical);
 return {};
}
}
