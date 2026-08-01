#include <verilated.h>
#include "Vcap_canonical_replay_pair_top.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace {
constexpr int K=64, GROUPS=13, STATES=32;
constexpr uint64_t CANONICAL_SEED=0x5245434f4e5f3131ULL;
constexpr uint64_t CANONICAL_STREAM_CHECKSUM=11508365490867720138ULL;
constexpr uint64_t CLASSICAL_CANDIDATES=679121ULL;
constexpr uint64_t FNV_OFFSET=1469598103934665603ULL;
constexpr uint64_t FNV_PRIME=1099511628211ULL;
constexpr uint32_t EXPECTED_HEADER_BYTES=96;
constexpr uint32_t EXPECTED_RECORD_BYTES=1778;
constexpr uint64_t NORMAL_LUTS=39096ULL;
constexpr uint64_t S45_LUTS=43970ULL;

using Rows=std::array<std::array<uint8_t,K>,GROUPS>;
using Phi=std::array<std::array<uint16_t,STATES>,GROUPS>;
using Info=std::array<uint8_t,K>;

struct Header {
    uint32_t version=0,header_bytes=0,record_bytes=0,frames=0;
    uint32_t n=0,k=0,order=0,groups=0,states=0,group_bits=0,soft_scale=0;
    int32_t ebn0_milli=0;
    uint64_t seed=0,stream_checksum=0,generator_checksum=0,trace_checksum=0,tie_frames=0;
};
struct Frame {
    uint32_t index=0;
    uint64_t soft_checksum=0;
    Rows rows{};
    Phi phi{};
    Info info{};
    uint64_t base_states=0;
    uint16_t seed_metric=0;
    uint64_t seed_mask=0;
    uint8_t seed_tie=0;
    uint16_t reference_metric=0;
    uint64_t reference_mask=0;
    uint8_t reference_tie=0;
    uint64_t payload_checksum=0;
};

