// SPDX-License-Identifier: MIT
#pragma once
#include "tree_node_codec.hpp"
#include <algorithm>
#include <cmath>
namespace superpos::box2d_portable {
// Caller-owned candidate memory only. No traversal follows native pointers.
// Successful topology validation does not qualify whole-world continuation.
inline Status validate_tree_topology(std::span<const b2TreeNode> nodes,int root,int free_head,
 uint32_t node_count,uint32_t proxy_count,std::span<uint8_t> marks,std::span<uint32_t> stack)noexcept{
 if(nodes.size()>100000||node_count>nodes.size()||proxy_count>node_count||marks.size()<nodes.size()||stack.size()<nodes.size())return fail(Error::InvalidArgument);
 auto overlap=[](auto a,auto b){auto x=reinterpret_cast<uintptr_t>(a.data()),y=reinterpret_cast<uintptr_t>(b.data());return a.size_bytes()&&b.size_bytes()&&(x<=y?y-x<a.size_bytes():x-y<b.size_bytes());};
 if(overlap(nodes,marks)||overlap(nodes,stack)||overlap(marks,stack))return fail(Error::InvalidArgument);
 auto valid=[&](int n){return n>=0&&size_t(n)<nodes.size();};
 if((root!=-1&&!valid(root))||(free_head!=-1&&!valid(free_head))||(node_count==0)!=(root==-1))return fail(Error::InvalidArgument);
 std::fill_n(marks.begin(),nodes.size(),uint8_t(0));size_t pending=0,live=0,leaves=0,free=0;
 if(root!=-1){if(!(nodes[root].flags&b2_allocatedNode)||nodes[root].parent!=-1)return fail(Error::InvalidArgument);stack[pending++]=uint32_t(root);}
 while(pending){auto i=stack[--pending];if(marks[i])return fail(Error::InvalidArgument);marks[i]=1;++live;const auto &n=nodes[i];
  if(!(n.flags&b2_allocatedNode)||(n.flags&~uint16_t(7)))return fail(Error::InvalidArgument);
  const auto &b=n.aabb;if(!std::isfinite(b.lowerBound.x)||!std::isfinite(b.lowerBound.y)||!std::isfinite(b.upperBound.x)||!std::isfinite(b.upperBound.y)||b.lowerBound.x>b.upperBound.x||b.lowerBound.y>b.upperBound.y)return fail(Error::InvalidArgument);
  if(n.flags&b2_leafNode){if(n.height)return fail(Error::InvalidArgument);++leaves;continue;}
  auto a=n.children.child1,c=n.children.child2;if(!valid(a)||!valid(c)||a==c||a==int(i)||c==int(i)||(nodes[a].flags&~uint16_t(7))||(nodes[c].flags&~uint16_t(7))||!(nodes[a].flags&b2_allocatedNode)||!(nodes[c].flags&b2_allocatedNode)||nodes[a].parent!=int(i)||nodes[c].parent!=int(i))return fail(Error::InvalidArgument);
  if(n.height!=1u+std::max(nodes[a].height,nodes[c].height)||n.categoryBits!=(nodes[a].categoryBits|nodes[c].categoryBits))return fail(Error::InvalidArgument);
  if(((nodes[a].flags|nodes[c].flags)&b2_enlargedNode)&&!(n.flags&b2_enlargedNode))return fail(Error::InvalidArgument);
  for(auto child:{a,c}){const auto &q=nodes[child].aabb;if(b.lowerBound.x>q.lowerBound.x||b.lowerBound.y>q.lowerBound.y||b.upperBound.x<q.upperBound.x||b.upperBound.y<q.upperBound.y)return fail(Error::InvalidArgument);}
  if(pending+2>stack.size())return fail(Error::CapacityExceeded);stack[pending++]=uint32_t(a);stack[pending++]=uint32_t(c);
 }
 for(int i=free_head;i!=-1;){if(!valid(i)||marks[i]||nodes[i].flags)return fail(Error::InvalidArgument);marks[i]=2;++free;i=nodes[i].next;}
 if(live!=node_count||leaves!=proxy_count||live+free!=nodes.size())return fail(Error::InvalidArgument);
 return {};
}
}
