#pragma once
#include "box2d/id.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool spB2ParticipantQuiescent(b2WorldId world);
bool spB2ParticipantSameTopology(b2WorldId current, b2WorldId candidate);
bool spB2ParticipantCopyWiring(b2WorldId current, b2WorldId candidate);
#ifdef __cplusplus
}
#endif
