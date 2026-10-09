// SPDX-License-Identifier: MIT
#pragma once
#include "receiver_binding.hpp"
#include "core/object/object.h"

namespace superpos_egp::lifecycle_engine {
// Implementations are trusted native engine code, admitted explicitly with
// NativeTransactional. Object identity is resolved anew at every operation;
// no Object* survives a callback. Script/resource names are not accepted.
class WeakFactory : public NativeFactory {
public:
    superpos::Status prepare(lifecycle::Instance,superpos::ReplicaChangeKind,const superpos::CanonicalReplica&) noexcept final;
    void commit(lifecycle::Instance,superpos::ReplicaChangeKind) noexcept final;
    void abort(lifecycle::Instance,superpos::ReplicaChangeKind) noexcept final;
    void destroy(lifecycle::Instance) noexcept final;
protected:
    virtual bool accepts_native(const Object&) const noexcept=0;
    virtual superpos::Status prepare_native(Object&,superpos::ReplicaChangeKind,const superpos::CanonicalReplica&) noexcept=0;
    virtual void commit_native(Object&,superpos::ReplicaChangeKind) noexcept=0;
    virtual void abort_native(Object&,superpos::ReplicaChangeKind) noexcept=0;
    virtual void destroy_native(Object&) noexcept=0;
};
}
