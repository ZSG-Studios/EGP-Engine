// SPDX-License-Identifier: MIT
#include "pool_occupants.hpp"
#include <algorithm>
#include <new>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Range{const void*p;size_t bytes;};
bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.bytes&&b.bytes&&(x<=y?y-x<a.bytes:x-y<b.bytes);}
bool nil(Identity id)noexcept{return !id.kind&&!id.simulation&&!id.generation;}
bool identity(Identity id,uint32_t kind)noexcept{return id.kind==kind&&id.simulation&&id.generation;}
template<class Owner>Result<std::span<const Record>> capture(const WorldPoolSlots&slots,const Owner&objects,PoolOccupantStorage storage)noexcept{
 if(!slots.lease_active()||!objects.lease_active()||!spWorldCaptureMatches(&slots.source()))return fail(Error::NotReady);
 const auto&v=slots.source();auto d=pool_occupant_definition(slots.pool());if(!d)return fail(d.error());const uint32_t count=v.pools[slots.pool()].allocated_count;
 if(storage.cells.size()<count||storage.records.size()<count)return fail(Error::CapacityExceeded);
 const Range writes[]={{storage.cells.data(),storage.cells.size_bytes()},{storage.records.data(),storage.records.size_bytes()}};
 if(overlap(writes[0],writes[1]))return fail(Error::InvalidArgument);for(auto w:writes)if(slots.overlaps_storage(w.p,w.bytes)||objects.overlaps_storage(w.p,w.bytes)||overlap(w,{&objects,sizeof(objects)}))return fail(Error::InvalidArgument);
 for(uint32_t i=0;i<count;++i){SpNativeLifetime actual{};if(!spObserveWorldCaptureSlot(&v,slots.pool(),i,&actual)||actual.generation>d->native_generation_max)return fail(Error::InvalidArgument);
  auto slot=slots.map().canonical(i),object=objects.map().canonical(i);if(!slot||!identity(*slot,d->slot_kind))return fail(Error::StaleGeneration);
  if(actual.native_id==-1){if(object)return fail(Error::InvalidArgument);if(d->native_generation_max&&actual.generation==d->native_generation_max)return fail(Error::RecoveryUnavailable);}else if(actual.native_id!=int(i)||!object||!identity(*object,d->object_kind)||(d->native_generation_max&&!actual.generation))return fail(Error::StaleGeneration);
  auto&cell=storage.cells[i];cell.atoms[0]={};if(object)cell.atoms[0].identity=*object;cell.atoms[1]={actual.generation,{}};cell.fields={Field{1,std::span(cell.atoms).subspan(0,1)},Field{2,std::span(cell.atoms).subspan(1,1)}};storage.records[i]={*slot,cell.fields};
 }
 return std::span<const Record>(storage.records.first(count));
}
struct Layout {
 size_t bytes=0;bool valid=true;
 template<class T>size_t add(size_t count)noexcept{if(!valid||count>SIZE_MAX/sizeof(T)){valid=false;return 0;}const size_t padding=(alignof(T)-bytes%alignof(T))%alignof(T);if(bytes>SIZE_MAX-padding||bytes+padding>SIZE_MAX-count*sizeof(T)){valid=false;return 0;}bytes+=padding;size_t offset=bytes;bytes+=count*sizeof(T);return offset;}
};
template<class T>std::span<T> at(void*block,size_t offset,size_t count)noexcept{auto*p=reinterpret_cast<T*>(static_cast<std::byte*>(block)+offset);for(size_t i=0;i<count;++i)std::construct_at(p+i);return {p,count};}
}
Result<PoolOccupantDefinition> pool_occupant_definition(SpWorldPool pool)noexcept{
 if(pool<SP_POOL_BODY||pool>=SP_POOL_COUNT)return fail(Error::InvalidArgument);
 constexpr uint32_t kinds[]={0x1001,0x1006,0x1008,0x1402,0x1401,0x1007,0x1009};constexpr uint32_t maxima[]={UINT16_MAX,UINT32_MAX,UINT16_MAX,0,0,UINT16_MAX,UINT16_MAX};
 return PoolOccupantDefinition{world_pool_slot_kind(pool),kinds[pool],maxima[pool],{{{1,AtomType::Reference,kinds[pool],1,1,true},{2,AtomType::Unsigned,0,1,1,false}}}};
}
PoolOccupantCell::PoolOccupantCell()noexcept:fields{{{1,std::span(atoms).subspan(0,1)},{2,std::span(atoms).subspan(1,1)}}}{}
Result<std::span<const Record>> capture_pool_occupants(const WorldPoolSlots&s,const WorldOwnerMap&o,PoolOccupantStorage storage)noexcept{
 if(s.pool()>SP_POOL_SET||o.kind()!=SpOwnerKind(s.pool())||&o.source()!=&s.source().ownership)return fail(Error::InvalidArgument);return capture(s,o,storage);
}
Result<std::span<const Record>> capture_pool_occupants(const WorldPoolSlots&s,const GeometryOwnerMap&o,PoolOccupantStorage storage)noexcept{
 if((s.pool()!=SP_POOL_SHAPE&&s.pool()!=SP_POOL_CHAIN)||o.chain()!=(s.pool()==SP_POOL_CHAIN)||&o.source()!=&s.source().geometry)return fail(Error::InvalidArgument);return capture(s,o,storage);
}
OwnedPoolCandidate::OwnedPoolCandidate(OwnedPoolCandidate&&other)noexcept:allocator_(other.allocator_),block_(std::exchange(other.block_,nullptr)),bytes_(std::exchange(other.bytes_,0)),slots_(other.slots_),objects_(other.objects_),generations_(other.generations_),pool_(other.pool_){other.slots_.reset();other.objects_.reset();other.generations_={};other.pool_={};}
OwnedPoolCandidate::~OwnedPoolCandidate(){if(block_)allocator_->deallocate(block_);}
Result<OwnedPoolCandidate> restore_pool_occupants(SpWorldPool pool,std::span<const Record>records,const Record&pool_record,Allocator&allocator)noexcept{
 return restore_pool_occupants_placed(pool,records,pool_record,{},allocator);
}
Result<OwnedPoolCandidate> restore_pool_occupants_placed(SpWorldPool pool,std::span<const Record>records,const Record&pool_record,std::span<const uint32_t>placement,Allocator&allocator)noexcept{
 if(!placement.empty()&&placement.size()!=records.size())return fail(Error::InvalidArgument);
 auto d=pool_occupant_definition(pool);if(!d)return fail(d.error());auto pd=pool_definition(d->slot_kind,100000);if(!pd)return fail(pd.error());
 if(records.size()>100000||pool_record.identity.kind!=pd->record_kind||!pool_record.identity.simulation||!pool_record.identity.generation||pool_record.fields.size()!=3)return fail(Error::InvalidArgument);
 for(uint32_t i=0;i<3;++i)if(pool_record.fields[i].id!=i+1||pool_record.fields[i].atoms.size()<(i==2?0:1)||pool_record.fields[i].atoms.size()>(i==2?100000:1))return fail(Error::InvalidArgument);
 const auto&count_atom=pool_record.fields[0].atoms[0];const auto&capacity_atom=pool_record.fields[1].atoms[0];if(!nil(count_atom.identity)||!nil(capacity_atom.identity)||count_atom.bits!=records.size()||capacity_atom.bits>100000)return fail(Error::InvalidArgument);
 const size_t count=records.size(),capacity=size_t(capacity_atom.bits);if(pool_record.fields[2].atoms.size()>count||pool_record.fields[2].atoms.size()>capacity)return fail(Error::InvalidArgument);
 // Validate immutable record shape and generations before allocating anything.
 size_t live=0;for(size_t i=0;i<count;++i){const auto&r=records[i];if(!identity(r.identity,d->slot_kind)||(i&&records[i-1].identity.simulation>=r.identity.simulation)||r.fields.size()!=2||r.fields[0].id!=1||r.fields[1].id!=2||r.fields[0].atoms.size()!=1||r.fields[1].atoms.size()!=1)return fail(Error::InvalidArgument);
  const auto&object=r.fields[0].atoms[0];const auto&generation=r.fields[1].atoms[0];if(object.bits||!nil(generation.identity)||generation.bits>d->native_generation_max)return fail(Error::InvalidArgument);if(!nil(object.identity)){if(!identity(object.identity,d->object_kind)||(d->native_generation_max&&!generation.bits))return fail(Error::InvalidArgument);++live;}else if(d->native_generation_max&&generation.bits==d->native_generation_max)return fail(Error::RecoveryUnavailable);
 }
 Layout layout;size_t sb=layout.add<NativeBinding>(count),so=layout.add<uint32_t>(count),ob=layout.add<NativeBinding>(live),oo=layout.add<uint32_t>(live),ng=layout.add<uint32_t>(count),visited=layout.add<std::byte>(count),free=layout.add<int>(capacity);if(!layout.valid)return fail(Error::CapacityExceeded);
 OwnedPoolCandidate candidate;candidate.allocator_=&allocator;candidate.bytes_=std::max(layout.bytes,size_t(1));candidate.block_=allocator.allocate(candidate.bytes_,alignof(std::max_align_t),MemoryDomain::Recovery);if(!candidate.block_)return fail(Error::OutOfMemory);
 auto slot_bindings=at<NativeBinding>(candidate.block_,sb,count);auto slot_order=at<uint32_t>(candidate.block_,so,count);auto object_bindings=at<NativeBinding>(candidate.block_,ob,live);auto object_order=at<uint32_t>(candidate.block_,oo,live);candidate.generations_=at<uint32_t>(candidate.block_,ng,count);auto marks=at<std::byte>(candidate.block_,visited,count);auto free_order=at<int>(candidate.block_,free,capacity);
 for(uint32_t i=0;i<count;++i){uint32_t target=placement.empty()?i:placement[i];if(target>=count||marks[target]!=std::byte{})return fail(Error::InvalidArgument);marks[target]=std::byte{1};slot_bindings[target]={target,records[i].identity};candidate.generations_[target]=uint32_t(records[i].fields[1].atoms[0].bits);}
 size_t next=0;for(uint32_t target=0;target<count;++target){const auto&slot=slot_bindings[target].identity;auto it=std::lower_bound(records.begin(),records.end(),slot.simulation,[](const Record&r,uint64_t id){return r.identity.simulation<id;});if(it==records.end()||it->identity!=slot)return fail(Error::InvalidArgument);const auto id=it->fields[0].atoms[0].identity;if(!nil(id))object_bindings[next++]={target,id};}
 std::fill(marks.begin(),marks.end(),std::byte{});
 auto slots=IdentityMap::prepare(slot_bindings,slot_order,d->slot_kind),objects=IdentityMap::prepare(object_bindings,object_order,d->object_kind);if(!slots)return fail(slots.error());if(!objects)return fail(objects.error());
 auto native=restore_pool(pool_record,*pd,*slots,marks,free_order);if(!native)return fail(native.error());
 for(size_t i=0;i<count;++i){const auto target=placement.empty()?i:placement[i];if((marks[target]!=std::byte{})!=nil(records[i].fields[0].atoms[0].identity))return fail(Error::InvalidArgument);}
 candidate.slots_=*slots;candidate.objects_=*objects;candidate.pool_=*native;return candidate;
}
}
