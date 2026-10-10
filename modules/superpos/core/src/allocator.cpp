#include "superpos/allocator.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
namespace superpos {
namespace { struct Header { void* base; std::size_t charge; unsigned domain; }; }
Status MemoryPlan::validate(std::size_t maximum_owned_bytes) const noexcept {
 std::size_t sum=0;
 for(auto limit:limits){if(limit>SIZE_MAX-sum)return fail(Error::Overflow);sum+=limit;}
 if(sum>maximum_owned_bytes)return fail(Error::CapacityExceeded);
 return {};
}
Result<std::size_t> BudgetAllocator::allocation_charge(std::size_t n, std::size_t alignment) noexcept {
 if(alignment==0 || (alignment&(alignment-1))!=0)return fail(Error::InvalidArgument);
 alignment=alignment<alignof(Header)?alignof(Header):alignment;
 if(alignment-1>SIZE_MAX-sizeof(Header) || n>SIZE_MAX-sizeof(Header)-(alignment-1))return fail(Error::Overflow);
 return n+sizeof(Header)+alignment-1;
}
void* BudgetAllocator::allocate(std::size_t n, std::size_t alignment, MemoryDomain domain) noexcept {
 auto i=static_cast<unsigned>(domain);
 if(!valid_ || i>=6)return nullptr;
 auto calculated=allocation_charge(n,alignment);
 if(!calculated)return nullptr;
 alignment=alignment<alignof(Header)?alignof(Header):alignment;
 const auto charge=*calculated;
 auto old=used_[i].load();
 do { if(charge>plan_.limits[i] || old>plan_.limits[i]-charge) return nullptr; }
 while(!used_[i].compare_exchange_weak(old,old+charge));
 void* base=std::malloc(charge);
 if(!base) { used_[i].fetch_sub(charge); return nullptr; }
 auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(Header)+alignment-1)&~(alignment-1);
 auto* header=reinterpret_cast<Header*>(address)-1;
 *header={base,charge,i};
 return reinterpret_cast<void*>(address);
}
void BudgetAllocator::deallocate(void* p) noexcept {
 if(!p) return;
 auto h=*(reinterpret_cast<Header*>(p)-1);
 std::free(h.base); used_[h.domain].fetch_sub(h.charge);
}
std::size_t BudgetAllocator::total() const noexcept { std::size_t n=0; for(auto& d:used_) n+=d.load(); return n; }
namespace {
struct QuotaHeader {
 QuotaAllocator* owner;
 BudgetAllocator* parent;
 void* raw;
 std::size_t charge;
 unsigned domain;
};
}
QuotaAllocator::~QuotaAllocator(){if(total())std::abort();}
Status QuotaAllocator::bind(BudgetAllocator& parent,MemoryPlan plan) noexcept {
 if(total())return fail(Error::Busy);
 auto valid=plan.validate();if(!valid)return valid;
 auto parent_valid=parent.configuration_status();if(!parent_valid)return parent_valid;
 parent_=&parent;plan_=plan;return {};
}
void* QuotaAllocator::allocate(std::size_t n,std::size_t alignment,MemoryDomain domain) noexcept {
 const auto i=static_cast<unsigned>(domain);
 if(!parent_ || i>=6 || !alignment || (alignment&(alignment-1)))return nullptr;
 alignment=alignment<alignof(QuotaHeader)?alignof(QuotaHeader):alignment;
 if(alignment-1>SIZE_MAX-sizeof(QuotaHeader) || n>SIZE_MAX-sizeof(QuotaHeader)-(alignment-1))return nullptr;
 const auto request=n+sizeof(QuotaHeader)+alignment-1;
 auto charge=BudgetAllocator::allocation_charge(request,alignment);
 if(!charge)return nullptr;
 auto old=used_[i].load();
 do {if(*charge>plan_.limits[i] || old>plan_.limits[i]-*charge)return nullptr;}
 while(!used_[i].compare_exchange_weak(old,old+*charge));
 void* raw=parent_->allocate(request,alignment,domain);
 if(!raw){used_[i].fetch_sub(*charge);return nullptr;}
 const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(QuotaHeader)+alignment-1)&~(alignment-1);
 auto* header=reinterpret_cast<QuotaHeader*>(address)-1;
 *header={this,parent_,raw,*charge,i};
 return reinterpret_cast<void*>(address);
}
void QuotaAllocator::deallocate(void* pointer) noexcept {
 if(!pointer)return;
 const auto header=*(reinterpret_cast<QuotaHeader*>(pointer)-1);
 if(header.owner!=this || header.parent!=parent_ || header.domain>=6 || header.charge>used_[header.domain].load())std::abort();
 header.parent->deallocate(header.raw);
 used_[header.domain].fetch_sub(header.charge);
}
std::size_t QuotaAllocator::used(MemoryDomain d) const noexcept {
 const auto i=static_cast<unsigned>(d);return i<6?used_[i].load():0;
}
std::size_t QuotaAllocator::total() const noexcept {
 std::size_t n=0;for(const auto& d:used_)n+=d.load();return n;
}
Buffer::Buffer(Buffer&& b) noexcept : allocator_(b.allocator_),domain_(b.domain_),data_(std::exchange(b.data_,nullptr)),size_(std::exchange(b.size_,0)),capacity_(std::exchange(b.capacity_,0)) {}
Buffer& Buffer::operator=(Buffer&& b) noexcept { if(this!=&b) { if(data_) allocator_->deallocate(data_); allocator_=b.allocator_;domain_=b.domain_;data_=std::exchange(b.data_,nullptr);size_=std::exchange(b.size_,0);capacity_=std::exchange(b.capacity_,0); } return *this; }
Status Buffer::reserve(std::size_t n) noexcept {
 if(n<=capacity_) return {};
 auto* p=static_cast<std::byte*>(allocator_->allocate(n,alignof(std::max_align_t),domain_));
 if(!p) return fail(Error::OutOfMemory);
 if(size_) std::memcpy(p,data_,size_);
 if(data_) allocator_->deallocate(data_);
 data_=p;capacity_=n;return {};
}
Status Buffer::resize(std::size_t n) noexcept { auto r=reserve(n);if(!r) return r; if(n>size_) std::memset(data_+size_,0,n-size_);size_=n;return {}; }
}
