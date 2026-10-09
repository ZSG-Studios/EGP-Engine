// SPDX-License-Identifier: MIT
#include "inactive_constraints.hpp"
#include <algorithm>
#include <cstring>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}};
void*raw_take(void*p,Layout&l,size_t n,size_t s,size_t a)noexcept{return static_cast<std::byte*>(p)+l.raw(n,s,a);}
bool less(Identity a,Identity b)noexcept{return a.kind<b.kind||(a.kind==b.kind&&a.simulation<b.simulation);}
bool sorted(std::span<const Record>rs,uint32_t kind=0)noexcept{if(rs.size()>100000)return false;for(size_t i=0;i<rs.size();++i)if((kind&&rs[i].identity.kind!=kind)||!rs[i].identity.kind||!rs[i].identity.simulation||!rs[i].identity.generation||(i&&!less(rs[i-1].identity,rs[i].identity)))return false;return true;}
const Record*find(std::span<const Record>rs,Identity id)noexcept{auto it=std::lower_bound(rs.begin(),rs.end(),id,[](const Record&r,Identity v){return less(r.identity,v);});return it!=rs.end()&&it->identity==id?&*it:nullptr;}
const Record*payload(const Record&r,std::span<const Record>all)noexcept{for(auto f:r.fields)for(auto a:f.atoms)if(a.identity.kind>=distance_joint_kind&&a.identity.kind<=wheel_joint_kind)return find(all,a.identity);return nullptr;}
bool endpoints(ConstraintEndpoints e)noexcept{return e.body_a>=0&&e.body_b>=0&&e.body_a!=e.body_b;}
}
OwnedInactiveConstraints::OwnedInactiveConstraints(OwnedInactiveConstraints&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),contacts_(std::exchange(o.contacts_,nullptr)),joints_(std::exchange(o.joints_,nullptr)),contact_count_(std::exchange(o.contact_count_,0)),contact_capacity_(std::exchange(o.contact_capacity_,0)),joint_count_(std::exchange(o.joint_count_,0)),joint_capacity_(std::exchange(o.joint_capacity_,0)),contact_stride_(o.contact_stride_),joint_stride_(o.joint_stride_),set_index_(std::exchange(o.set_index_,-1)){}
OwnedInactiveConstraints::~OwnedInactiveConstraints(){if(block_)allocator_->deallocate(block_);}
Result<OwnedInactiveConstraints>restore_inactive_constraints(const SpSolverMembership&m,InactiveConstraintRecords in,InactiveConstraintMaps maps,std::span<const ConstraintEndpoints>contact_ends,std::span<const ConstraintEndpoints>joint_ends,Allocator&allocator)noexcept{
 if(m.set_index<0||m.contact_count>m.contact_capacity||m.joint_count>m.joint_capacity||m.contact_capacity>100000||m.joint_capacity>100000||(m.contact_count&&!m.contact_ids)||(m.joint_count&&!m.joint_ids)||contact_ends.size()>100000||joint_ends.size()>100000)return fail(Error::InvalidArgument);
 // Static sets never hold contacts; awake joints always live in the graph.
 if((m.set_index==0&&m.contact_count)||(m.set_index==2&&m.joint_count))return fail(Error::InvalidArgument);
 if(!sorted(in.contacts,contact_sim_kind)||!sorted(in.joints,joint_sim_kind)||!sorted(in.joint_payloads)||in.contacts.size()!=m.contact_count||in.joints.size()!=m.joint_count)return fail(Error::InvalidArgument);
 const auto native=spInactiveConstraintLayout();if(!native.contact_size||!native.joint_size)return fail(Error::IncompatibleSchema);
 Layout plan;plan.raw(m.contact_capacity,native.contact_size,native.contact_alignment);plan.raw(m.joint_capacity,native.joint_size,native.joint_alignment);plan.raw(contact_ends.size(),1,1);plan.raw(joint_ends.size(),1,1);if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedInactiveConstraints out;out.allocator_=&allocator;out.bytes_=std::max(plan.bytes,size_t(1));out.block_=allocator.allocate(out.bytes_,std::max({alignof(std::max_align_t),size_t(native.contact_alignment),size_t(native.joint_alignment)}),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 std::memset(out.block_,0,out.bytes_);
 Layout cursor;out.contacts_=raw_take(out.block_,cursor,m.contact_capacity,native.contact_size,native.contact_alignment);out.joints_=raw_take(out.block_,cursor,m.joint_capacity,native.joint_size,native.joint_alignment);
 auto*contact_marks=static_cast<uint8_t*>(raw_take(out.block_,cursor,contact_ends.size(),1,1));auto*joint_marks=static_cast<uint8_t*>(raw_take(out.block_,cursor,joint_ends.size(),1,1));
 out.contact_capacity_=m.contact_capacity;out.joint_capacity_=m.joint_capacity;out.contact_stride_=native.contact_size;out.joint_stride_=native.joint_size;out.set_index_=m.set_index;
 for(uint32_t i=0;i<m.contact_count;++i){
  const int id=m.contact_ids[i];if(id<0||size_t(id)>=contact_ends.size()||contact_marks[id])return fail(Error::InvalidArgument);contact_marks[id]=1;const auto ends=contact_ends[size_t(id)];if(!endpoints(ends))return fail(Error::InvalidArgument);
  auto identity=maps.contact.canonical(uint32_t(id));if(!identity)return fail(identity.error());Identity key=*identity;key.kind=contact_sim_kind;const Record*record=find(in.contacts,key);if(!record)return fail(Error::StaleGeneration);
  // Retained labels resolve through the destination body map only to prove
  // they name the cold endpoint; they never select a destination solver slot.
  SpContactSim staged{};ContactMappings cm{maps.contact,maps.shape,maps.body,maps.body};if(auto s=restore_contact_sim(*record,cm,staged);!s)return fail(s.error());
  if(staged.contactId!=id||(staged.bodySimIndexA!=-1&&staged.bodySimIndexA!=ends.body_a)||(staged.bodySimIndexB!=-1&&staged.bodySimIndexB!=ends.body_b))return fail(Error::InvalidArgument);
  if(spContactValidationEnabled()&&(staged.bodyIdA!=ends.body_a||staged.bodyIdB!=ends.body_b))return fail(Error::InvalidArgument);
  if(!spInactiveContactAdmissible(&staged,m.set_index))return fail(Error::InvalidArgument);
  staged.bodySimIndexA=staged.bodySimIndexB=-1;// inactive prepared-index normalization
  if(!spImportContactSim(&staged,static_cast<std::byte*>(out.contacts_)+size_t(i)*native.contact_size))return fail(Error::InvalidArgument);++out.contact_count_;
 }
 size_t used_payloads=0;
 for(uint32_t i=0;i<m.joint_count;++i){
  const int id=m.joint_ids[i];if(id<0||size_t(id)>=joint_ends.size()||joint_marks[id])return fail(Error::InvalidArgument);joint_marks[id]=1;const auto ends=joint_ends[size_t(id)];if(!endpoints(ends))return fail(Error::InvalidArgument);
  auto identity=maps.joint.canonical(uint32_t(id));if(!identity)return fail(identity.error());Identity key=*identity;key.kind=joint_sim_kind;const Record*record=find(in.joints,key);if(!record)return fail(Error::StaleGeneration);
  const Record*child=payload(*record,in.joint_payloads);SpJointSim staged{};JointSimMappings jm{maps.joint,maps.body,maps.body};if(auto s=restore_joint_sim(*record,child,jm,staged);!s)return fail(s.error());
  if(staged.jointId!=id||staged.bodyIdA!=ends.body_a||staged.bodyIdB!=ends.body_b)return fail(Error::InvalidArgument);
  for(int e=0;e<2;++e)if(int*index=spInactiveJointIndex(&staged,e)){if(*index!=-1&&*index!=(e?ends.body_b:ends.body_a))return fail(Error::InvalidArgument);*index=-1;}// inactive prepared-index normalization
  if(!spImportJointSim(&staged,static_cast<std::byte*>(out.joints_)+size_t(i)*native.joint_size))return fail(Error::InvalidArgument);++out.joint_count_;if(child)++used_payloads;
 }
 if(cursor.bytes!=plan.bytes||used_payloads!=in.joint_payloads.size())return fail(Error::InvalidArgument);
 return out;
}
}
