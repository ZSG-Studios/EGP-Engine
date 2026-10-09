// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct SpContactEdge {int bodyId,prevKey,nextKey;} SpContactEdge;
typedef struct SpColdContact {SpContactEdge edges[2];int islandId,islandIndex,setIndex,colorIndex,localIndex,shapeIdA,shapeIdB,contactId;uint32_t flags,generation;} SpColdContact;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportColdContact(const void *,SpColdContact *);
bool spImportColdContact(const SpColdContact *,void *);
bool spRoundTripColdContact(const SpColdContact *,SpColdContact *);
int spContactColorCount(void);
#ifdef __cplusplus
}
#endif
