// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
namespace rtc::impl::profile_limits {
inline constexpr std::size_t peers=256;
inline constexpr std::size_t pair_handshakes=8;
inline constexpr std::size_t associations=2*(peers+pair_handshakes);
// PeerConnection, DTLS and SCTP each own one independent Processor strand.
inline constexpr std::size_t processor_owners=3*associations;
inline constexpr std::size_t tasks=4096;
inline constexpr std::size_t control_tasks=1024;
// A reserved lifecycle record for every possible strand prevents data/control
// saturation from denying provider cleanup. This exceeds the 256 minimum.
inline constexpr std::size_t lifecycle_tasks=processor_owners;
inline constexpr std::size_t data_tasks=tasks-control_tasks-lifecycle_tasks;
static_assert(associations==528 && processor_owners==1584);
static_assert(data_tasks>0 && lifecycle_tasks>=256);
}
