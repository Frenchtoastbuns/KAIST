#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int STAGE_COUNT = 9;

std::array<Clock::time_point, STAGE_COUNT> starts;
std::array<uint64_t, STAGE_COUNT> elapsed_ns;

void profile_begin(int stage)
{
	assert(stage >= 0 && stage < STAGE_COUNT);
	starts[static_cast<std::size_t>(stage)] = Clock::now();
}

void profile_end(int stage)
{
	assert(stage >= 0 && stage < STAGE_COUNT);
	const auto end = Clock::now();
	elapsed_ns[static_cast<std::size_t>(stage)] +=
		static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				end - starts[static_cast<std::size_t>(stage)]
			).count()
		);
}

uint64_t next_random(uint64_t &state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

}

#define CODE_OSD_PROFILE_BEGIN(stage) profile_begin(stage)
#define CODE_OSD_PROFILE_END(stage) profile_end(stage)

#include "bitman.hh"
#include "osd.hh"

int main()
{
	constexpr int N = 127;
	constexpr int K = 64;
	constexpr int O = 4;
	constexpr int WARMUPS = 2;
	constexpr int REPEATS = 9;

	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		{
			0b10001001, 0b10001111, 0b10011101,
			0b11110111, 0b10111111, 0b11010101,
			0b10000011, 0b11101111, 0b11001011
		}
	);

	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	uint64_t random_state = 0x4b414953545f4f53ULL;
	uint64_t output_hash = 14695981039346656037ULL;

	for (int iteration = -WARMUPS; iteration < REPEATS; ++iteration) {
		int8_t soft[N];
		for (int i = 0; i < N; ++i) {
			const uint64_t value = next_random(random_state);
			const int magnitude = 1 + static_cast<int>(value % 127);
			soft[i] = value & (uint64_t{1} << 63)
				? static_cast<int8_t>(-magnitude)
				: static_cast<int8_t>(magnitude);
		}
		uint8_t decoded[(N + 7) / 8] = {};
		elapsed_ns = {};
		const auto total_start = Clock::now();
		const bool unique = decoder(decoded, soft, genmat);
		const auto total_end = Clock::now();
		const uint64_t total_ns = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				total_end - total_start
			).count()
		);

		for (const uint8_t byte: decoded) {
			output_hash ^= byte;
			output_hash *= 1099511628211ULL;
		}
		output_hash ^= unique;
		output_hash *= 1099511628211ULL;

		if (iteration < 0)
			continue;

		uint64_t accounted_ns = 0;
		for (const uint64_t value: elapsed_ns)
			accounted_ns += value;
		assert(accounted_ns <= total_ns);

		std::cout << "PROFILE "
			<< iteration << ' '
			<< total_ns;
		for (const uint64_t value: elapsed_ns)
			std::cout << ' ' << value;
		std::cout << ' ' << output_hash << '\n';
	}

	return 0;
}
