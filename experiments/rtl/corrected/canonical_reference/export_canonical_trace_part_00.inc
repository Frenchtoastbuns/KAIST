#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace canonical_capture {
constexpr int N = 128;
constexpr int K = 64;
constexpr int O = 4;
constexpr int W = 128;
struct State {
    std::array<int8_t, K * W> matrix{};
    std::array<int8_t, W> base{};
    std::array<int8_t, W> soft{};
    std::array<int16_t, W> permutation{};
    bool ready = false;
};
inline State captured;

template<int TN, int TK, int TO>
bool capture(const int8_t* matrix, const int8_t* base, int8_t*,
             const int8_t* soft, const int16_t* permutation,
             int length, int dimension, int width, int order,
             int&, int&) {
    static_assert(TN == N && TK == K && TO == O);
    if (length != N || dimension != K || width != W || order != O)
        throw std::runtime_error("unexpected OSD geometry");
    std::copy(matrix, matrix + K * W, captured.matrix.begin());
    std::copy(base, base + W, captured.base.begin());
    std::copy(soft, soft + W, captured.soft.begin());
    std::copy(permutation, permutation + W, captured.permutation.begin());
    captured.ready = true;
    return false;
}
}
#define CODE_OSD_SEARCH_OVERRIDE(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next) \
    canonical_capture::capture<N,K,O>(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next)
#include "canonical_osd.hh"

namespace {
constexpr int N = 128;
constexpr int K = 64;
constexpr int O = 4;
constexpr int GROUPS = 13;
constexpr int STATES = 32;
constexpr int GROUP_BITS = 5;
constexpr int DEFAULT_FRAMES = 1000;
constexpr uint64_t CANONICAL_SEED = 0x5245434f4e5f3131ULL;
constexpr uint64_t CANONICAL_STREAM_CHECKSUM = 11508365490867720138ULL;
constexpr uint32_t TRACE_VERSION = 1;
constexpr uint32_t HEADER_BYTES = 96;
constexpr uint32_t RECORD_BYTES = 4 + 8 + GROUPS*K + GROUPS*STATES*2 + K + 8 + 2 + 8 + 1 + 2 + 8 + 1 + 8;
constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

using Rows = std::array<std::array<uint8_t, K>, GROUPS>;
using Phi = std::array<std::array<uint16_t, STATES>, GROUPS>;
using Info = std::array<uint8_t, K>;

struct Frame {
    uint32_t index = 0;
    uint64_t soft_checksum = FNV_OFFSET;
    Rows rows{};
    Phi phi{};
    Info info{};
    uint64_t base_states = 0;
    uint16_t seed_metric = 0;
    uint64_t seed_mask = 0;
    uint8_t seed_tie = 0;
    uint16_t reference_metric = 0;
    uint64_t reference_mask = 0;
    uint8_t reference_tie = 0;
    uint64_t payload_checksum = FNV_OFFSET;
};

uint64_t fnv_byte(uint64_t hash, uint8_t value) {
    return (hash ^ value) * FNV_PRIME;
}

template<class T>
void hash_le(uint64_t& hash, T value) {
    static_assert(std::is_integral<T>::value, "integral required");
    using U = typename std::make_unsigned<T>::type;
    U u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        hash = fnv_byte(hash, static_cast<uint8_t>(u & 0xffU));
        u >>= 8;
    }
}

template<class T>
void write_le(std::ostream& out, T value) {
    static_assert(std::is_integral<T>::value, "integral required");
    using U = typename std::make_unsigned<T>::type;
    U u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.put(static_cast<char>(u & 0xffU));
        u >>= 8;
    }
    if (!out) throw std::runtime_error("trace write failed");
}

int popcount64(uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int result = 0;
    while (value) { result += int(value & 1ULL); value >>= 1; }
    return result;
#endif
}

bool canonical_before(uint64_t left, uint64_t right) {
    const int lw = popcount64(left), rw = popcount64(right);
