#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace minirouter {

using PortId = uint32_t;
using NextHopId = uint32_t;
using NextHopGroupId = uint32_t;
using VlanId = uint16_t;

constexpr PortId INVALID_PORT_ID = 0xFFFFFFFF;
constexpr NextHopId INVALID_NEXT_HOP_ID = 0;
constexpr NextHopGroupId INVALID_NEXT_HOP_GROUP_ID = 0;

enum class ResultCode : int32_t {
    SUCCESS = 0,
    ERROR_INVALID_PARAM = -1,
    ERROR_NOT_FOUND = -2,
    ERROR_ALREADY_EXISTS = -3,
    ERROR_NO_MEMORY = -4,
    ERROR_TABLE_FULL = -5,
    ERROR_PACKET_MALFORMED = -6,
    ERROR_TTL_EXPIRED = -7,
    ERROR_CHECKSUM_INVALID = -8,
    ERROR_NO_ROUTE = -9,
    ERROR_INTERFACE_DOWN = -10,
    ERROR_NOT_SUPPORTED = -11,
    ERROR_GENERIC = -12
};

inline const char* result_code_to_string(ResultCode code) {
    switch (code) {
        case ResultCode::SUCCESS: return "SUCCESS";
        case ResultCode::ERROR_INVALID_PARAM: return "ERROR_INVALID_PARAM";
        case ResultCode::ERROR_NOT_FOUND: return "ERROR_NOT_FOUND";
        case ResultCode::ERROR_ALREADY_EXISTS: return "ERROR_ALREADY_EXISTS";
        case ResultCode::ERROR_NO_MEMORY: return "ERROR_NO_MEMORY";
        case ResultCode::ERROR_TABLE_FULL: return "ERROR_TABLE_FULL";
        case ResultCode::ERROR_PACKET_MALFORMED: return "ERROR_PACKET_MALFORMED";
        case ResultCode::ERROR_TTL_EXPIRED: return "ERROR_TTL_EXPIRED";
        case ResultCode::ERROR_CHECKSUM_INVALID: return "ERROR_CHECKSUM_INVALID";
        case ResultCode::ERROR_NO_ROUTE: return "ERROR_NO_ROUTE";
        case ResultCode::ERROR_INTERFACE_DOWN: return "ERROR_INTERFACE_DOWN";
        case ResultCode::ERROR_NOT_SUPPORTED: return "ERROR_NOT_SUPPORTED";
        case ResultCode::ERROR_GENERIC: return "ERROR_GENERIC";
    }
    return "UNKNOWN_ERROR";
}

} // namespace minirouter
