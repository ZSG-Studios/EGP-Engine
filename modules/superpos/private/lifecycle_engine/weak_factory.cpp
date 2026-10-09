// SPDX-License-Identifier: MIT
#include "weak_factory.hpp"
#include <cstdlib>
namespace superpos_egp::lifecycle_engine {
superpos::Status WeakFactory::prepare(lifecycle::Instance id,superpos::ReplicaChangeKind kind,const superpos::CanonicalReplica& value) noexcept {
    Object* object=ObjectDB::get_instance(ObjectID(id.value));
    if(!object || !accepts_native(*object))return superpos::fail(superpos::Error::StaleGeneration);
    return prepare_native(*object,kind,value);
}
void WeakFactory::commit(lifecycle::Instance id,superpos::ReplicaChangeKind kind) noexcept {
    Object* object=ObjectDB::get_instance(ObjectID(id.value));
    // No user callbacks between prepare and commit: disappearance is forbidden
    // by the admitted native transaction contract, never silently accepted.
    if(!object || !accepts_native(*object))std::abort();
    commit_native(*object,kind);
}
void WeakFactory::abort(lifecycle::Instance id,superpos::ReplicaChangeKind kind) noexcept {
    Object* object=ObjectDB::get_instance(ObjectID(id.value));if(object && accepts_native(*object))abort_native(*object,kind);
}
void WeakFactory::destroy(lifecycle::Instance id) noexcept {
    Object* object=ObjectDB::get_instance(ObjectID(id.value));if(object && accepts_native(*object))destroy_native(*object);
}
}
