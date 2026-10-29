#pragma once

#include <cstdint>
#include <cstring>

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#elif defined(_WIN32)
#include <winsock2.h>
#endif

namespace minirouter {

inline uint16_t host_to_net16(uint16_t val) {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    return htons(val);
#else
    return (uint16_t)(((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8));
#endif
}

inline uint16_t net_to_host16(uint16_t val) {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    return ntohs(val);
#else
    return (uint16_t)(((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8));
#endif
}

inline uint32_t host_to_net32(uint32_t val) {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    return htonl(val);
#else
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
#endif
}

inline uint32_t net_to_host32(uint32_t val) {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    return ntohl(val);
#else
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
#endif
}

// Unaligned memory reading/writing to avoid undefined behavior
template <typename T>
inline T read_unaligned(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}

template <typename T>
inline void write_unaligned(void* ptr, const T& val) {
    std::memcpy(ptr, &val, sizeof(T));
}

} // namespace minirouter
