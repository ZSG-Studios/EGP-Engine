// SPDX-License-Identifier: MIT
#include "world_events.hpp"
#include "runtime_scratch_bridge.h"
#include <box2d/types.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Col{AtomType type;uint32_t kind;bool nullable;};
constexpr Col RefShape{AtomType::Reference,shape_kind,false},RefContact{AtomType::Reference,contact_kind,false},RefBody{AtomType::Reference,body_kind,false},RefJoint{AtomType::Reference,joint_kind,false},RefMove{AtomType::Reference,body_move_kind,false},RefBinding{AtomType::Reference,binding_kind,true},F64{AtomType::Float64,0,false},F32{AtomType::Float32,0,false},Flag{AtomType::Boolean,0,false};
constexpr std::array<Col,8> move_cols{RefMove,RefBody,F64,F64,F32,F32,RefBinding,Flag};
constexpr std::array<Col,2> sensor_cols{RefShape,RefShape};
constexpr std::array<Col,3> contact_cols{RefShape,RefShape,RefContact};
constexpr std::array<Col,8> hit_cols{RefShape,RefShape,RefContact,F64,F64,F32,F32,F32};
constexpr std::array<Col,2> joint_cols{RefJoint,RefBinding};
std::span<const Col> cols(EventKind k)noexcept{switch(k){case EventKind::BodyMove:return move_cols;case EventKind::SensorBegin:case EventKind::SensorEnd:return sensor_cols;case EventKind::ContactBegin:case EventKind::ContactEnd:return contact_cols;case EventKind::ContactHit:return hit_cols;case EventKind::Joint:return joint_cols;}return {};}
bool nil(Identity i)noexcept{return !i.kind&&!i.simulation&&!i.generation;}
using Pos=decltype(b2BodyMoveEvent{}.transform.p.x);
// Event field accessors over the native element types.
template<class Id>Result<Identity> ref(const VersionedIdentityMap&m,Id id,uint16_t world0)noexcept{if(id.index1<1||id.world0!=world0)return fail(Error::InvalidArgument);return m.canonical({uint32_t(id.index1-1),uint32_t(id.generation)});}
template<class Id>Status unref(const VersionedIdentityMap&m,Identity identity,uint16_t world0,uint32_t max_generation,Id&out)noexcept{auto n=m.native(identity);if(!n)return fail(n.error());if(n->slot>=uint32_t(INT32_MAX)||!n->generation||n->generation>max_generation)return fail(Error::InvalidArgument);out={};out.index1=int32_t(n->slot+1);out.world0=world0;out.generation=decltype(out.generation)(n->generation);return {};}
Atom f32(float v)noexcept{return {std::bit_cast<uint32_t>(v),{}};}
Atom f64(double v)noexcept{return {std::bit_cast<uint64_t>(v),{}};}
bool finite(const Atom&a,AtomType t)noexcept{return t==AtomType::Float32?std::isfinite(std::bit_cast<float>(uint32_t(a.bits))):std::isfinite(std::bit_cast<double>(a.bits));}
Status pos(const Atom&a,Pos&out)noexcept{double d=std::bit_cast<double>(a.bits);Pos p=Pos(d);if(double(p)!=d)return fail(Error::Overflow);out=p;return {};}
float flt(const Atom&a)noexcept{return std::bit_cast<float>(uint32_t(a.bits));}
}
EventArrayImage::EventArrayImage(std::span<Atom> s)noexcept:storage(s){}
size_t event_columns(EventKind k)noexcept{return cols(k).size();}
size_t event_element_size(EventKind k)noexcept{switch(k){case EventKind::BodyMove:return sizeof(b2BodyMoveEvent);case EventKind::SensorBegin:return sizeof(b2SensorBeginTouchEvent);case EventKind::SensorEnd:return sizeof(b2SensorEndTouchEvent);case EventKind::ContactBegin:return sizeof(b2ContactBeginTouchEvent);case EventKind::ContactEnd:return sizeof(b2ContactEndTouchEvent);case EventKind::ContactHit:return sizeof(b2ContactHitEvent);case EventKind::Joint:return sizeof(b2JointEvent);}return 0;}
Status capture_event_array(EventKind k,const SpEventArray&a,Identity identity,const EventMaps&m,const IdentityMap*move_events,EventArrayImage&out)noexcept{
 const auto c=cols(k);const size_t n=a.count;if(c.empty()||identity.kind!=uint32_t(k)||!identity.simulation||!identity.generation||a.count>a.capacity||a.capacity>100000||(a.count&&!a.data)||(k==EventKind::BodyMove&&!move_events))return fail(Error::InvalidArgument);
 if(out.storage.size()<1+c.size()*n)return fail(Error::CapacityExceeded);std::fill(out.storage.begin(),out.storage.begin()+std::ptrdiff_t(1+c.size()*n),Atom{});
 auto at=[&](size_t col,size_t i)->Atom&{return out.storage[1+col*n+i];};out.storage[0].bits=a.capacity;
 for(size_t i=0;i<n;++i){
  auto put_ref=[&](size_t col,Result<Identity> r)->Status{if(!r)return fail(r.error());at(col,i).identity=*r;return {};};
  auto put_ptr=[&](size_t col,void*p)->Status{auto r=m.bindings.canonical(p);if(!r)return fail(r.error());at(col,i).identity=*r;return {};};
  Status s{};
  switch(k){
   case EventKind::BodyMove:{const auto&e=static_cast<const b2BodyMoveEvent*>(a.data)[i];if(auto x=move_events->canonical(uint32_t(i));!x)return fail(x.error());else at(0,i).identity=*x;if(s=put_ref(1,ref(m.body,e.bodyId,m.world0));!s)return s;at(2,i)=f64(double(e.transform.p.x));at(3,i)=f64(double(e.transform.p.y));at(4,i)=f32(e.transform.q.c);at(5,i)=f32(e.transform.q.s);if(s=put_ptr(6,e.userData);!s)return s;at(7,i).bits=e.fellAsleep?1:0;break;}
   case EventKind::SensorBegin:{const auto&e=static_cast<const b2SensorBeginTouchEvent*>(a.data)[i];if(s=put_ref(0,ref(m.shape,e.sensorShapeId,m.world0));!s)return s;if(s=put_ref(1,ref(m.shape,e.visitorShapeId,m.world0));!s)return s;break;}
   case EventKind::SensorEnd:{const auto&e=static_cast<const b2SensorEndTouchEvent*>(a.data)[i];if(s=put_ref(0,ref(m.shape,e.sensorShapeId,m.world0));!s)return s;if(s=put_ref(1,ref(m.shape,e.visitorShapeId,m.world0));!s)return s;break;}
   case EventKind::ContactBegin:{const auto&e=static_cast<const b2ContactBeginTouchEvent*>(a.data)[i];if(s=put_ref(0,ref(m.shape,e.shapeIdA,m.world0));!s)return s;if(s=put_ref(1,ref(m.shape,e.shapeIdB,m.world0));!s)return s;if(s=put_ref(2,ref(m.contact,e.contactId,m.world0));!s)return s;break;}
   case EventKind::ContactEnd:{const auto&e=static_cast<const b2ContactEndTouchEvent*>(a.data)[i];if(s=put_ref(0,ref(m.shape,e.shapeIdA,m.world0));!s)return s;if(s=put_ref(1,ref(m.shape,e.shapeIdB,m.world0));!s)return s;if(s=put_ref(2,ref(m.contact,e.contactId,m.world0));!s)return s;break;}
   case EventKind::ContactHit:{const auto&e=static_cast<const b2ContactHitEvent*>(a.data)[i];if(s=put_ref(0,ref(m.shape,e.shapeIdA,m.world0));!s)return s;if(s=put_ref(1,ref(m.shape,e.shapeIdB,m.world0));!s)return s;if(s=put_ref(2,ref(m.contact,e.contactId,m.world0));!s)return s;at(3,i)=f64(double(e.point.x));at(4,i)=f64(double(e.point.y));at(5,i)=f32(e.normal.x);at(6,i)=f32(e.normal.y);at(7,i)=f32(e.approachSpeed);break;}
   case EventKind::Joint:{const auto&e=static_cast<const b2JointEvent*>(a.data)[i];if(s=put_ref(0,ref(m.joint,e.jointId,m.world0));!s)return s;if(s=put_ptr(1,e.userData);!s)return s;break;}
  }
  for(size_t col=0;col<c.size();++col)if((c[col].type==AtomType::Float32||c[col].type==AtomType::Float64)&&!finite(at(col,i),c[col].type))return fail(Error::InvalidArgument);
 }
 out.fields[0]={1,out.storage.first(1)};for(size_t col=0;col<c.size();++col)out.fields[col+1]={uint32_t(col+2),out.storage.subspan(1+col*n,n)};out.record={identity,std::span<const Field>(out.fields).first(1+c.size())};return {};
}
Status restore_event_array(EventKind k,const Record&r,const EventMaps&m,SpEventArray&dst,std::span<Identity> move_ids)noexcept{
 const auto c=cols(k);if(c.empty()||r.identity.kind!=uint32_t(k)||!r.identity.simulation||!r.identity.generation||r.fields.size()!=1+c.size()||r.fields[0].id!=1||r.fields[0].atoms.size()!=1||!nil(r.fields[0].atoms[0].identity))return fail(Error::IncompatibleSchema);
 const uint64_t capacity=r.fields[0].atoms[0].bits;const size_t n=r.fields[1].atoms.size();if(capacity>100000||n>capacity||dst.capacity!=capacity||(capacity&&!dst.data)||(k==EventKind::BodyMove&&move_ids.size()<n))return fail(Error::CapacityExceeded);
 for(size_t col=0;col<c.size();++col){const auto&f=r.fields[col+1];if(f.id!=col+2||f.atoms.size()!=n)return fail(Error::IncompatibleSchema);
  for(const auto&a:f.atoms)switch(c[col].type){
   case AtomType::Reference:if(a.bits||(nil(a.identity)?!c[col].nullable:(a.identity.kind!=c[col].kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);break;
   case AtomType::Boolean:if(!nil(a.identity)||a.bits>1)return fail(Error::InvalidArgument);break;
   case AtomType::Float32:if(!nil(a.identity)||a.bits>UINT32_MAX||!finite(a,AtomType::Float32))return fail(Error::InvalidArgument);break;
   case AtomType::Float64:if(!nil(a.identity)||!finite(a,AtomType::Float64))return fail(Error::InvalidArgument);break;
   default:return fail(Error::IncompatibleSchema);}}
 auto at=[&](size_t col,size_t i)->const Atom&{return r.fields[col+1].atoms[i];};
 auto ptr=[&](size_t col,size_t i,void*&out)->Status{auto p=m.bindings.native(at(col,i).identity);if(!p)return fail(p.error());out=*p;return {};};
 for(size_t i=0;i<n;++i){Status s{};switch(k){
  case EventKind::BodyMove:{b2BodyMoveEvent e{};move_ids[i]=at(0,i).identity;if(s=unref(m.body,at(1,i).identity,m.world0,UINT16_MAX,e.bodyId);!s)return s;if(s=pos(at(2,i),e.transform.p.x);!s)return s;if(s=pos(at(3,i),e.transform.p.y);!s)return s;e.transform.q.c=flt(at(4,i));e.transform.q.s=flt(at(5,i));if(s=ptr(6,i,e.userData);!s)return s;e.fellAsleep=at(7,i).bits!=0;static_cast<b2BodyMoveEvent*>(dst.data)[i]=e;break;}
  case EventKind::SensorBegin:{b2SensorBeginTouchEvent e{};if(s=unref(m.shape,at(0,i).identity,m.world0,UINT16_MAX,e.sensorShapeId);!s)return s;if(s=unref(m.shape,at(1,i).identity,m.world0,UINT16_MAX,e.visitorShapeId);!s)return s;static_cast<b2SensorBeginTouchEvent*>(dst.data)[i]=e;break;}
  case EventKind::SensorEnd:{b2SensorEndTouchEvent e{};if(s=unref(m.shape,at(0,i).identity,m.world0,UINT16_MAX,e.sensorShapeId);!s)return s;if(s=unref(m.shape,at(1,i).identity,m.world0,UINT16_MAX,e.visitorShapeId);!s)return s;static_cast<b2SensorEndTouchEvent*>(dst.data)[i]=e;break;}
  case EventKind::ContactBegin:{b2ContactBeginTouchEvent e{};if(s=unref(m.shape,at(0,i).identity,m.world0,UINT16_MAX,e.shapeIdA);!s)return s;if(s=unref(m.shape,at(1,i).identity,m.world0,UINT16_MAX,e.shapeIdB);!s)return s;if(s=unref(m.contact,at(2,i).identity,m.world0,UINT32_MAX,e.contactId);!s)return s;static_cast<b2ContactBeginTouchEvent*>(dst.data)[i]=e;break;}
  case EventKind::ContactEnd:{b2ContactEndTouchEvent e{};if(s=unref(m.shape,at(0,i).identity,m.world0,UINT16_MAX,e.shapeIdA);!s)return s;if(s=unref(m.shape,at(1,i).identity,m.world0,UINT16_MAX,e.shapeIdB);!s)return s;if(s=unref(m.contact,at(2,i).identity,m.world0,UINT32_MAX,e.contactId);!s)return s;static_cast<b2ContactEndTouchEvent*>(dst.data)[i]=e;break;}
  case EventKind::ContactHit:{b2ContactHitEvent e{};if(s=unref(m.shape,at(0,i).identity,m.world0,UINT16_MAX,e.shapeIdA);!s)return s;if(s=unref(m.shape,at(1,i).identity,m.world0,UINT16_MAX,e.shapeIdB);!s)return s;if(s=unref(m.contact,at(2,i).identity,m.world0,UINT32_MAX,e.contactId);!s)return s;if(s=pos(at(3,i),e.point.x);!s)return s;if(s=pos(at(4,i),e.point.y);!s)return s;e.normal={flt(at(5,i)),flt(at(6,i))};e.approachSpeed=flt(at(7,i));static_cast<b2ContactHitEvent*>(dst.data)[i]=e;break;}
  case EventKind::Joint:{b2JointEvent e{};if(s=unref(m.joint,at(0,i).identity,m.world0,UINT16_MAX,e.jointId);!s)return s;if(s=ptr(1,i,e.userData);!s)return s;static_cast<b2JointEvent*>(dst.data)[i]=e;break;}
 }}
 dst.count=uint32_t(n);return {};
}
OwnedWorldEvents::OwnedWorldEvents(OwnedWorldEvents&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),world_(std::exchange(o.world_,nullptr)),move_bindings_(std::exchange(o.move_bindings_,{})),move_map_(o.move_map_){o.move_map_.reset();if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedWorldEvents::~OwnedWorldEvents(){if(block_)allocator_->deallocate(block_);}
namespace {struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}template<class T>size_t add(size_t n)noexcept{return raw(n,sizeof(T),alignof(T));}};}
Result<OwnedWorldEvents>restore_owned_events(OwnedGeometryRoot&&prior,const WorldEventRecords&in,const EventMaps&maps,Allocator&allocator)noexcept{
 if(!prior.has_storage())return fail(Error::InvalidArgument);
 const std::array<std::pair<EventKind,const Record*>,9> arrays{{{EventKind::BodyMove,in.move},{EventKind::SensorBegin,in.sensor_begin},{EventKind::ContactBegin,in.contact_begin},{EventKind::SensorEnd,in.sensor_end[0]},{EventKind::SensorEnd,in.sensor_end[1]},{EventKind::ContactEnd,in.contact_end[0]},{EventKind::ContactEnd,in.contact_end[1]},{EventKind::ContactHit,in.hit},{EventKind::Joint,in.joint}}};
 std::array<size_t,9> capacity{};for(size_t a=0;a<9;++a){const Record*r=arrays[a].second;if(!r||r->fields.empty()||r->fields[0].atoms.size()!=1||r->fields[0].atoms[0].bits>100000)return fail(Error::IncompatibleSchema);capacity[a]=size_t(r->fields[0].atoms[0].bits);}
 const size_t moves=in.move->fields.size()>1?in.move->fields[1].atoms.size():0;if(moves>capacity[0])return fail(Error::CapacityExceeded);
 const auto root=spWorldRootLayout();Layout plan;plan.raw(1,root.size,root.alignment);for(size_t a=0;a<9;++a)plan.raw(capacity[a],event_element_size(arrays[a].first),alignof(std::max_align_t));plan.add<Identity>(moves);plan.add<NativeBinding>(moves);plan.add<uint32_t>(moves);if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedWorldEvents out;out.allocator_=&allocator;out.bytes_=plan.bytes;out.block_=allocator.allocate(plan.bytes,std::max(alignof(std::max_align_t),size_t(root.alignment)),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout c;auto*base=static_cast<std::byte*>(out.block_);out.world_=base+c.raw(1,root.size,root.alignment);std::array<SpEventArray,9> native{};for(size_t a=0;a<9;++a){native[a]={base+c.raw(capacity[a],event_element_size(arrays[a].first),alignof(std::max_align_t)),0,uint32_t(capacity[a])};if(!capacity[a])native[a].data=nullptr;}
 auto*ids=reinterpret_cast<Identity*>(base+c.add<Identity>(moves));auto*bindings=reinterpret_cast<NativeBinding*>(base+c.add<NativeBinding>(moves));auto*order=reinterpret_cast<uint32_t*>(base+c.add<uint32_t>(moves));for(size_t i=0;i<moves;++i){std::construct_at(ids+i);std::construct_at(bindings+i);order[i]=0;}
 for(size_t a=0;a<9;++a)if(auto s=restore_event_array(arrays[a].first,*arrays[a].second,maps,native[a],std::span(ids,moves));!s)return fail(s.error());
 for(size_t i=0;i<moves;++i)bindings[i]={uint32_t(i),ids[i]};auto map=IdentityMap::prepare(std::span(bindings,moves),std::span(order,moves),body_move_kind);if(!map)return fail(map.error());
 // Body-move events and cold bodyMoveIndex must agree in both directions.
 const auto&view=prior.ownership_view();const auto*bodies=static_cast<const b2Body*>(view.bodies);const auto*events=static_cast<const b2BodyMoveEvent*>(native[0].data);
 for(size_t i=0;i<native[0].count;++i){const int slot=events[i].bodyId.index1-1;if(slot<0||uint32_t(slot)>=view.body_count||bodies[slot].id!=slot||bodies[slot].bodyMoveIndex!=int(i)||bodies[slot].generation!=events[i].bodyId.generation)return fail(Error::InvalidArgument);}
 for(uint32_t b=0;b<view.body_count;++b)if(bodies[b].id!=-1&&bodies[b].bodyMoveIndex!=-1&&(bodies[b].bodyMoveIndex<0||uint32_t(bodies[b].bodyMoveIndex)>=native[0].count))return fail(Error::InvalidArgument);
 SpWorldEventArrays bound{native[0],native[1],native[2],{native[3],native[4]},{native[5],native[6]},native[7],native[8],maps.world0};
 if(!spCopyWorldRoot(out.world_,prior.native_world())||!spBindWorldEvents(out.world_,&bound)||!spValidateNormalizedRuntimeScratch(out.world_))return fail(Error::InvalidArgument);
 out.move_bindings_={bindings,moves};out.move_map_.emplace(*map);out.prior_.emplace(std::move(prior));return out;
}
}
