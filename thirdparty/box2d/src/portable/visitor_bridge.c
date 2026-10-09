// SPDX-License-Identifier: MIT
#include "visitor_bridge.h"
#include "sensor.h"
bool spVisitorNativeRoundTrip(const SpVisitor *source,SpVisitor *out){if(!source||!out)return false;b2Visitor native={0};native.shapeId=source->shape;native.generation=source->generation;out->shape=native.shapeId;out->generation=native.generation;return true;}
