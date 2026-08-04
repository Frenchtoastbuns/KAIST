#pragma once
#include <array>
#include <cstdint>
namespace experiment {
template<int N>
struct BasicOrbgrandSchedule {
    template<class Visitor>
    static uint64_t run(uint64_t budget, Visitor&&) { return budget; }
};
template<int N, int K>
class BasicOrbgrandDecoder {
public:
    struct Result {
        std::array<uint8_t, (N + 7) / 8> decoded{};
        bool found = false;
        int metric = 0;
        uint64_t queries = 0;
    };
    template<class Generator>
    explicit BasicOrbgrandDecoder(const Generator&) {}
    Result decode(const std::array<int8_t, N>&, uint64_t budget) const { Result r; r.queries = budget; return r; }
    Result decode_one_line(const std::array<int8_t, N>&, uint64_t budget) const { Result r; r.queries = budget; return r; }
};
}
