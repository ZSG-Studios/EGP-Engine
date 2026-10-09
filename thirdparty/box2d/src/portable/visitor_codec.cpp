// SPDX-License-Identifier: MIT
#include "visitor_codec.hpp"
#include <climits>
namespace superpos::box2d_portable {using namespace canonical;
namespace {constexpr std::array<FieldSpec,1> specs{{{1,AtomType::Reference,shape_kind,1,1,false}}};}
VisitorImage::VisitorImage()noexcept{field={1,std::span(&atom,1)};record.fields=std::span(&field,1);}
std::span<const FieldSpec> visitor_fields()noexcept{return specs;}
Status capture_visitor(const SpVisitor &v,Identity identity,const VersionedIdentityMap &map,VisitorImage &out)noexcept{
 if(v.shape<0||identity.kind!=visitor_kind||!identity.simulation||!identity.generation)return fail(Error::InvalidArgument);auto token=map.canonical({uint32_t(v.shape),v.generation});if(!token)return fail(token.error());if(token->kind!=shape_kind)return fail(Error::IncompatibleSchema);out.atom={0,*token};out.record.identity=identity;return {};
}
Status restore_visitor(const Record &r,const VersionedIdentityMap &map,SpVisitor &out)noexcept{
 if(r.identity.kind!=visitor_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=1||r.fields[0].id!=1||r.fields[0].atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=r.fields[0].atoms[0];if(a.bits||a.identity.kind!=shape_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);auto v=map.native(a.identity);if(!v)return fail(v.error());if(v->slot>INT_MAX||!v->generation||v->generation>UINT16_MAX)return fail(Error::InvalidArgument);out={int(v->slot),uint16_t(v->generation)};return {};
}
}
