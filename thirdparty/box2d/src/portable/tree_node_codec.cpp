// SPDX-License-Identifier: MIT
#include "tree_node_codec.hpp"
#include <bit>
#include <cmath>
#include <climits>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
// Canonical tag, flags, height, parent, next, child1, child2, shape, category, AABB.
constexpr std::array<FieldSpec,13> defs{{
 {1,AtomType::Unsigned,0,1,1,false},{2,AtomType::Unsigned,0,1,1,false},{3,AtomType::Unsigned,0,1,1,false},
 {4,AtomType::Reference,tree_node_kind,1,1,true},{5,AtomType::Reference,tree_node_kind,1,1,true},
 {6,AtomType::Reference,tree_node_kind,1,1,true},{7,AtomType::Reference,tree_node_kind,1,1,true},
 {8,AtomType::Reference,shape_kind,1,1,true},{9,AtomType::Unsigned,0,1,1,false},
 {10,AtomType::Float32,0,1,1,false},{11,AtomType::Float32,0,1,1,false},
 {12,AtomType::Float32,0,1,1,false},{13,AtomType::Float32,0,1,1,false}
}};
float scalar(const Atom &a)noexcept{return std::bit_cast<float>(uint32_t(a.bits));}
Status capture_ref(int n,const IdentityMap &map,Atom &a)noexcept{
 if(n==-1)return {};if(n<0)return fail(Error::InvalidArgument);auto r=map.canonical(uint32_t(n));if(!r)return fail(r.error());a.identity=r.value();return {};
}
Status restore_ref(Identity id,const IdentityMap &map,int &n)noexcept{
 if(!id.kind){if(id.simulation||id.generation)return fail(Error::InvalidArgument);n=-1;return {};}
 auto r=map.native(id);if(!r)return fail(r.error());if(r.value()>INT_MAX)return fail(Error::InvalidArgument);n=int(r.value());return {};
}
}
TreeNodeImage::TreeNodeImage()noexcept{for(size_t i=0;i<13;++i){fields[i].id=defs[i].id;fields[i].atoms=std::span(atoms).subspan(i,1);}record.fields=fields;}
std::span<const FieldSpec> tree_node_fields()noexcept{return defs;}
Status restore_tree_node(const Record &r,const IdentityMap &nodes,const IdentityMap &shapes,b2TreeNode &out)noexcept{
 if(r.identity.kind!=tree_node_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=13)return fail(Error::IncompatibleSchema);
 auto own=nodes.native(r.identity);if(!own)return fail(own.error());
 std::array<Atom,13>a{};
 for(size_t i=0;i<13;++i){if(r.fields[i].id!=defs[i].id||r.fields[i].atoms.size()!=1)return fail(Error::IncompatibleSchema);a[i]=r.fields[i].atoms[0];
  if(i>=3&&i<=7){if(a[i].bits)return fail(Error::InvalidArgument);auto id=a[i].identity;if(id.kind&&(id.kind!=defs[i].reference_kind||!id.simulation||!id.generation))return fail(Error::InvalidArgument);if(!id.kind&&(id.simulation||id.generation))return fail(Error::InvalidArgument);}
  else if(a[i].identity.kind||a[i].identity.simulation||a[i].identity.generation)return fail(Error::InvalidArgument);
 }
 if(a[0].bits>2||a[1].bits>7||a[2].bits>UINT16_MAX)return fail(Error::InvalidArgument);
 const auto tag=a[0].bits,flags=a[1].bits;
 if((tag==0&&flags!=0)||(tag==1&&((flags&5)!=5))||(tag==2&&((flags&5)!=1)))return fail(Error::InvalidArgument);
 b2TreeNode staged{};staged.flags=uint16_t(flags);staged.height=uint16_t(a[2].bits);
 if(tag==0){for(size_t i=2;i<13;++i)if(i!=4&&(a[i].bits||a[i].identity.kind))return fail(Error::NonCanonical);
  if(a[4].identity==r.identity)return fail(Error::InvalidArgument);if(auto s=restore_ref(a[4].identity,nodes,staged.next);!s)return s;
 }else{
  if(a[4].identity.kind)return fail(Error::NonCanonical);
  if(a[3].identity==r.identity)return fail(Error::InvalidArgument);if(auto s=restore_ref(a[3].identity,nodes,staged.parent);!s)return s;
  for(size_t i=9;i<13;++i)if(a[i].bits>UINT32_MAX||!std::isfinite(scalar(a[i])))return fail(Error::NonCanonical);
  staged.aabb={{scalar(a[9]),scalar(a[10])},{scalar(a[11]),scalar(a[12])}};
  if(staged.aabb.lowerBound.x>staged.aabb.upperBound.x||staged.aabb.lowerBound.y>staged.aabb.upperBound.y)return fail(Error::InvalidArgument);staged.categoryBits=a[8].bits;
  if(tag==1){if(a[2].bits||a[5].identity.kind||a[6].identity.kind||!a[7].identity.kind)return fail(Error::InvalidArgument);
   auto shape=shapes.native(a[7].identity);if(!shape)return fail(shape.error());staged.userData=shape.value();
  }else{if(!a[2].bits||a[7].identity.kind||!a[5].identity.kind||!a[6].identity.kind||a[5].identity==a[6].identity||a[5].identity==r.identity||a[6].identity==r.identity)return fail(Error::InvalidArgument);
   if(auto s=restore_ref(a[5].identity,nodes,staged.children.child1);!s)return s;if(auto s=restore_ref(a[6].identity,nodes,staged.children.child2);!s)return s;
  }
 }
 out=staged;return {};
}
Status capture_tree_node(const b2TreeNode &n,Identity id,const IdentityMap &nodes,const IdentityMap &shapes,TreeNodeImage &out)noexcept{
 if(n.flags & ~uint16_t(b2_allocatedNode|b2_enlargedNode|b2_leafNode))return fail(Error::InvalidArgument);
 if(n.flags && !(n.flags & b2_allocatedNode))return fail(Error::InvalidArgument);
 TreeNodeImage staged;staged.record.identity=id;auto &a=staged.atoms;a[1].bits=n.flags;
 if(!n.flags){a[0].bits=0;if(auto s=capture_ref(n.next,nodes,a[4]);!s)return s;}
 else{a[0].bits=(n.flags&b2_leafNode)?1:2;a[2].bits=n.height;if(auto s=capture_ref(n.parent,nodes,a[3]);!s)return s;
  a[8].bits=n.categoryBits;a[9].bits=std::bit_cast<uint32_t>(n.aabb.lowerBound.x);a[10].bits=std::bit_cast<uint32_t>(n.aabb.lowerBound.y);a[11].bits=std::bit_cast<uint32_t>(n.aabb.upperBound.x);a[12].bits=std::bit_cast<uint32_t>(n.aabb.upperBound.y);
  if(a[0].bits==1){if(n.userData>UINT32_MAX)return fail(Error::InvalidArgument);auto r=shapes.canonical(uint32_t(n.userData));if(!r)return fail(r.error());a[7].identity=r.value();}
  else{if(auto s=capture_ref(n.children.child1,nodes,a[5]);!s)return s;if(auto s=capture_ref(n.children.child2,nodes,a[6]);!s)return s;}
 }
 b2TreeNode checked{};if(auto s=restore_tree_node(staged.record,nodes,shapes,checked);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};
}
}
