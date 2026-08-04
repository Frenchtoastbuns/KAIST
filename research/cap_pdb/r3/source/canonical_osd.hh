#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <vector>
#include "bitman.hh"

namespace CODE {

template<int N, int K>
struct BoseChaudhuriHocquenghemGenerator {
    static void matrix(int8_t* output, bool systematic, std::initializer_list<int> factors) {
        static_assert(N <= 255);
        std::vector<uint8_t> polynomial(1, 1);
        for (int factor : factors) {
            int degree = 0;
            for (int value = factor; value > 1; value >>= 1) ++degree;
            std::vector<uint8_t> next(polynomial.size() + degree, 0);
            for (std::size_t i = 0; i < polynomial.size(); ++i)
                if (polynomial[i])
                    for (int j = 0; j <= degree; ++j)
                        if ((factor >> (degree - j)) & 1)
                            next[i + static_cast<std::size_t>(j)] ^= 1;
            polynomial.swap(next);
        }
        assert(static_cast<int>(polynomial.size()) == N - K + 1);
        std::fill(output, output + N * K, int8_t{0});
        for (int row = 0; row < K; ++row)
            for (int offset = 0; offset <= N - K; ++offset)
                output[row * N + row + offset] = static_cast<int8_t>(polynomial[static_cast<std::size_t>(offset)]);
        if (!systematic) return;
        for (int pivot = 0; pivot < K; ++pivot) {
            int pivot_row = pivot;
            while (pivot_row < K && !output[pivot_row * N + pivot]) ++pivot_row;
            assert(pivot_row < K);
            if (pivot_row != pivot)
                for (int column = 0; column < N; ++column)
                    std::swap(output[pivot * N + column], output[pivot_row * N + column]);
            for (int row = 0; row < K; ++row) {
                if (row == pivot || !output[row * N + pivot]) continue;
                for (int column = 0; column < N; ++column)
                    output[row * N + column] ^= output[pivot * N + column];
            }
        }
    }
};

template<int N, int K>
struct LinearEncoder {
    void operator()(uint8_t* output, const uint8_t* message, const int8_t* generator) const {
        std::fill(output, output + (N + 7) / 8, uint8_t{0});
        for (int row = 0; row < K; ++row) {
            if (!get_be_bit(message, row)) continue;
            for (int column = 0; column < N; ++column)
                if (generator[row * N + column])
                    set_be_bit(output, column, !get_be_bit(output, column));
        }
    }
};

template<int N, int K, int O>
class OrderedStatisticsDecoder {
    static constexpr int W = (N + static_cast<int>(sizeof(std::size_t)) - 1) & ~(static_cast<int>(sizeof(std::size_t)) - 1);
    static int metric(const int8_t* candidate, const int8_t* soft) {
        int value = 0;
        for (int index = 0; index < N; ++index)
            value += (1 - 2 * static_cast<int>(candidate[index] & 1)) * static_cast<int>(soft[index]);
        return value;
    }
public:
    bool operator()(uint8_t* decoded, const int8_t* input_soft, const int8_t* input_generator) {
        std::array<int, N> order{};
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            const int a = std::abs(static_cast<int>(input_soft[left]));
            const int b = std::abs(static_cast<int>(input_soft[right]));
            if (a != b) return a > b;
            return left < right;
        });
        std::array<int8_t, K * W> matrix{};
        std::array<int8_t, W> soft{};
        std::array<int16_t, W> permutation{};
        for (int column = 0; column < N; ++column) {
            const int original = order[static_cast<std::size_t>(column)];
            soft[static_cast<std::size_t>(column)] = input_soft[original];
            permutation[static_cast<std::size_t>(column)] = static_cast<int16_t>(original);
            for (int row = 0; row < K; ++row)
                matrix[static_cast<std::size_t>(row * W + column)] = input_generator[row * N + original] & 1;
        }
        for (int pivot = 0; pivot < K; ++pivot) {
            int pivot_column = pivot;
            int pivot_row = pivot;
            while (true) {
                pivot_row = pivot;
                while (pivot_row < K && !matrix[static_cast<std::size_t>(pivot_row * W + pivot_column)]) ++pivot_row;
                if (pivot_row < K) break;
                ++pivot_column;
                assert(pivot_column < N);
            }
            if (pivot_column != pivot) {
                for (int row = 0; row < K; ++row)
                    std::swap(matrix[static_cast<std::size_t>(row * W + pivot)], matrix[static_cast<std::size_t>(row * W + pivot_column)]);
                std::swap(soft[static_cast<std::size_t>(pivot)], soft[static_cast<std::size_t>(pivot_column)]);
                std::swap(permutation[static_cast<std::size_t>(pivot)], permutation[static_cast<std::size_t>(pivot_column)]);
            }
            if (pivot_row != pivot)
                for (int column = 0; column < W; ++column)
                    std::swap(matrix[static_cast<std::size_t>(pivot * W + column)], matrix[static_cast<std::size_t>(pivot_row * W + column)]);
            for (int row = 0; row < K; ++row) {
                if (row == pivot || !matrix[static_cast<std::size_t>(row * W + pivot)]) continue;
                for (int column = 0; column < W; ++column)
                    matrix[static_cast<std::size_t>(row * W + column)] ^= matrix[static_cast<std::size_t>(pivot * W + column)];
            }
        }
        std::array<int8_t, W> base{};
        for (int row = 0; row < K; ++row) {
            if (soft[static_cast<std::size_t>(row)] >= 0) continue;
            for (int column = 0; column < W; ++column)
                base[static_cast<std::size_t>(column)] ^= matrix[static_cast<std::size_t>(row * W + column)];
        }
        std::array<int8_t, W> candidate = base;
        int best = 0, next = -1;
        bool overridden = false;
#ifdef CODE_OSD_SEARCH_OVERRIDE
        overridden = CODE_OSD_SEARCH_OVERRIDE(matrix.data(), base.data(), candidate.data(), soft.data(), permutation.data(), N, K, W, O, best, next);
#endif
        if (!overridden) {
            int absolute = 0;
            for (int index = 0; index < N; ++index) absolute += std::abs(static_cast<int>(soft[static_cast<std::size_t>(index)]));
            int best_distance = (absolute - metric(base.data(), soft.data())) / 2;
            int next_distance = std::numeric_limits<int>::max();
            candidate = base;
            std::array<int8_t, W> trial = base;
            auto evaluate = [&]() {
                int distance = 0;
                for (int index = 0; index < N; ++index)
                    if ((trial[static_cast<std::size_t>(index)] != 0) != (soft[static_cast<std::size_t>(index)] < 0))
                        distance += std::abs(static_cast<int>(soft[static_cast<std::size_t>(index)]));
                if (distance < best_distance) { next_distance = best_distance; best_distance = distance; candidate = trial; }
                else if (distance < next_distance) next_distance = distance;
            };
            auto recurse = [&](auto&& self, int start, int depth, int target) -> void {
                if (depth == target) { evaluate(); return; }
                for (int rank = start; rank <= K - (target - depth); ++rank) {
                    for (int column = 0; column < W; ++column) trial[static_cast<std::size_t>(column)] ^= matrix[static_cast<std::size_t>(rank * W + column)];
                    self(self, rank + 1, depth + 1, target);
                    for (int column = 0; column < W; ++column) trial[static_cast<std::size_t>(column)] ^= matrix[static_cast<std::size_t>(rank * W + column)];
                }
            };
            for (int weight = 1; weight <= O; ++weight) recurse(recurse, 0, 0, weight);
            best = absolute - 2 * best_distance;
            next = next_distance == std::numeric_limits<int>::max() ? -1 : absolute - 2 * next_distance;
        }
#ifdef CODE_OSD_SEARCH_COMPLETE
        CODE_OSD_SEARCH_COMPLETE(best, next, candidate.data(), permutation.data(), N, W);
#endif
        std::fill(decoded, decoded + (N + 7) / 8, uint8_t{0});
        for (int column = 0; column < N; ++column)
            set_be_bit(decoded, permutation[static_cast<std::size_t>(column)], candidate[static_cast<std::size_t>(column)] != 0);
        return next != best;
    }
};
}
