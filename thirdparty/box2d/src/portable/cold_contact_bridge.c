// SPDX-License-Identifier: MIT
#include "cold_contact_bridge.h"
#include "contact.h"
#include "box2d/constants.h"
int spContactColorCount(void){return B2_GRAPH_COLOR_COUNT;}
bool spExportColdContact(const void *pointer,SpColdContact *out){if(!pointer||!out)return false;const b2Contact *native=pointer;SpColdContact staged={0};
 staged.edges[0].bodyId=native->edges[0].bodyId;
 staged.edges[0].prevKey=native->edges[0].prevKey;
 staged.edges[0].nextKey=native->edges[0].nextKey;
 staged.edges[1].bodyId=native->edges[1].bodyId;
 staged.edges[1].prevKey=native->edges[1].prevKey;
 staged.edges[1].nextKey=native->edges[1].nextKey;
 staged.islandId=native->islandId;
 staged.islandIndex=native->islandIndex;
 staged.setIndex=native->setIndex;
 staged.colorIndex=native->colorIndex;
 staged.localIndex=native->localIndex;
 staged.shapeIdA=native->shapeIdA;
 staged.shapeIdB=native->shapeIdB;
 staged.contactId=native->contactId;
 staged.flags=native->flags;
 staged.generation=native->generation;
 *out=staged;return true;}
bool spImportColdContact(const SpColdContact *input,void *pointer){if(!pointer||!input)return false;b2Contact staged={0};
 staged.edges[0].bodyId=input->edges[0].bodyId;
 staged.edges[0].prevKey=input->edges[0].prevKey;
 staged.edges[0].nextKey=input->edges[0].nextKey;
 staged.edges[1].bodyId=input->edges[1].bodyId;
 staged.edges[1].prevKey=input->edges[1].prevKey;
 staged.edges[1].nextKey=input->edges[1].nextKey;
 staged.islandId=input->islandId;
 staged.islandIndex=input->islandIndex;
 staged.setIndex=input->setIndex;
 staged.colorIndex=input->colorIndex;
 staged.localIndex=input->localIndex;
 staged.shapeIdA=input->shapeIdA;
 staged.shapeIdB=input->shapeIdB;
 staged.contactId=input->contactId;
 staged.flags=input->flags;
 staged.generation=input->generation;
 *(b2Contact *)pointer=staged;return true;}
bool spRoundTripColdContact(const SpColdContact *in,SpColdContact *out){b2Contact native={0};return spImportColdContact(in,&native)&&spExportColdContact(&native,out);}
