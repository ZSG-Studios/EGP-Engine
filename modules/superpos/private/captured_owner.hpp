#pragma once
#include <superpos/allocator.hpp>
#include <memory>
#include <type_traits>
namespace superpos_egp {
template<class T> struct CapturedDelete {
    static_assert(std::is_nothrow_destructible_v<T>);
    superpos::Allocator* allocator{};
    void operator()(T* object) const noexcept {
        if(object){std::destroy_at(object);allocator->deallocate(object);}
    }
};
template<class T> using CapturedOwner=std::unique_ptr<T,CapturedDelete<T>>;
template<class T,class... Args> superpos::Result<CapturedOwner<T>> captured_create(
        superpos::Allocator& allocator,superpos::MemoryDomain domain,Args&&... args) noexcept {
    static_assert(std::is_nothrow_constructible_v<T,Args...>);
    static_assert(std::is_nothrow_destructible_v<T>);
    void* memory=allocator.allocate(sizeof(T),alignof(T),domain);
    if(!memory)return superpos::fail(superpos::Error::OutOfMemory);
    return CapturedOwner<T>{std::construct_at(static_cast<T*>(memory),std::forward<Args>(args)...),CapturedDelete<T>{&allocator}};
}
}
