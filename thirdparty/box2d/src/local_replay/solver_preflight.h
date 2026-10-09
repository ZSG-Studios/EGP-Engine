#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private native-layout admission aid. This checks framing/counts and computes
// an upper bound for the decoder's payload allocations, aligned to 32 bytes.
// It excludes b2CreateWorld's shell, task/runtime storage, all engine wrappers,
// and future solver growth. It does not validate every internal graph index or
// make b2World_Restore transactional. Only locally captured compatible images
// may proceed to a separately reserved candidate-world restore.
typedef struct spB2ImageRequirements {
    size_t encoded_bytes;
    size_t payload_allocation_bound;
} spB2ImageRequirements;

// No allocation, no world mutation, output unchanged on failure. Result storage
// must not overlap the image. Source mutation during the call is prohibited.
bool spB2PreflightSolverImage(const uint8_t* image, size_t size,
                            spB2ImageRequirements* requirements);
