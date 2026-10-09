// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/result.hpp"
#include <cstddef>
#include <span>

namespace superpos::service::net {
// Nonblocking native byte transport. Busy accepts no bytes; zero receive is
// stream EOF. Authentication and message framing belong to the caller.
class StreamIO {
public:
    virtual ~StreamIO()=default;
    virtual Result<std::size_t> send(std::span<const std::byte>) noexcept=0;
    virtual Result<std::size_t> receive(std::span<std::byte>) noexcept=0;
};
}
