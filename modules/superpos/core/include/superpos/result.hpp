#pragma once
#include <expected>
namespace superpos {
enum class Error { None, InvalidArgument, OutOfMemory, CapacityExceeded, Unsupported,
 ProtocolViolation, NonCanonical, Overflow, Truncated, AuthenticationFailed,
 StaleEpoch, StaleGeneration, MissingBaseline, NotReady, PermissionDenied, Timeout,
 ChannelFailed, UnknownOutcome, RecoveryUnavailable, Io, Busy, IncompatibleSchema, CounterExhausted };
template<class T> using Result = std::expected<T, Error>;
using Status = Result<void>;
constexpr std::unexpected<Error> fail(Error e) noexcept { return std::unexpected(e); }
}
