#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Context {
	int length = 0;
	int dimension = 0;
	std::vector<int8_t> matrix;
	std::vector<int8_t> base;
	std::vector<int8_t> soft;
	std::vector<int16_t> permutation;
	std::vector<uint8_t> production_decoded;
	bool production_unique = false;
};

Context captured;

void capture_ready(
	const int8_t *matrix,
	const int8_t *hard,
	const int8_t *soft,
	const int16_t *permutation,
	int length,
	int dimension,
	int width
)
{
	captured = {};
	captured.length = length;
	captured.dimension = dimension;
	captured.base.assign(hard, hard + length);
	captured.soft.assign(soft, soft + length);
	captured.permutation.assign(permutation, permutation + length);
	captured.matrix.resize(
		static_cast<std::size_t>(dimension * length)
	);
	for (int row = 0; row < dimension; ++row)
		for (int column = 0; column < length; ++column)
			captured.matrix[
				static_cast<std::size_t>(row * length + column)
			] = matrix[width * row + column];
}

uint64_t next_random(uint64_t &state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

}

#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) \
	capture_ready(matrix, hard, soft, permutation, length, dimension, width)

#include "bitman.hh"
#include "osd.hh"

namespace {

struct FullResult {
	int best = 0;
	int next = -1;
	uint64_t candidates = 0;
	std::vector<int8_t> best_word;
};

class FullWidthEngine {
	const Context &context;
	std::vector<int8_t> word;
	uint64_t mask = 0;
	FullResult result;

public:
	explicit FullWidthEngine(const Context &input):
		context(input),
		word(input.base)
	{
		evaluate();
	}

	void flip(int index)
	{
		mask ^= uint64_t{1} << index;
		for (int i = 0; i < context.length; ++i)
			word[static_cast<std::size_t>(i)] ^=
				context.matrix[
					static_cast<std::size_t>(
						index * context.length + i
					)
				];
	}

	void evaluate()
	{
		int metric = 0;
		for (int i = 0; i < context.length; ++i)
			metric +=
				(1 - 2 * word[static_cast<std::size_t>(i)]) *
				context.soft[static_cast<std::size_t>(i)];

		if (!result.candidates) {
			result.best = metric;
			result.best_word = word;
		} else if (metric > result.best) {
			result.next = result.best;
			result.best = metric;
			result.best_word = word;
		} else if (metric > result.next) {
			result.next = metric;
		}
		++result.candidates;
	}

	FullResult finish()
	{
		assert(mask == 0);
		assert(word == context.base);
		return result;
	}
};

struct ParityResult {
	int best = 0;
	int next = -1;
	uint64_t candidates = 0;
	uint64_t best_mask = 0;
	std::vector<int8_t> best_parity;
};

class ParityOnlyEngine {
	const Context &context;
	std::vector<int8_t> parity;
	uint64_t mask = 0;
	int systematic_metric = 0;
	const int initial_systematic_metric;
	ParityResult result;

public:
	explicit ParityOnlyEngine(const Context &input):
		context(input),
		parity(
			input.base.begin() + input.dimension,
			input.base.end()
		),
		initial_systematic_metric([&input]() {
			int metric = 0;
			for (int i = 0; i < input.dimension; ++i)
				metric +=
					(1 - 2 * input.base[static_cast<std::size_t>(i)]) *
					input.soft[static_cast<std::size_t>(i)];
			return metric;
		}())
	{
		systematic_metric = initial_systematic_metric;
		evaluate();
	}

	void flip(int index)
	{
		const int old_bit =
			context.base[static_cast<std::size_t>(index)] ^
			static_cast<int>((mask >> index) & 1);
		systematic_metric -=
			2 * (1 - 2 * old_bit) *
			context.soft[static_cast<std::size_t>(index)];
		mask ^= uint64_t{1} << index;

		for (int i = context.dimension; i < context.length; ++i)
			parity[static_cast<std::size_t>(i - context.dimension)] ^=
				context.matrix[
					static_cast<std::size_t>(
						index * context.length + i
					)
				];
	}

	void evaluate()
	{
		int metric = systematic_metric;
		for (int i = context.dimension; i < context.length; ++i)
			metric +=
				(
					1 - 2 *
					parity[static_cast<std::size_t>(i - context.dimension)]
				) * context.soft[static_cast<std::size_t>(i)];

		if (!result.candidates) {
			result.best = metric;
			result.best_mask = mask;
			result.best_parity = parity;
		} else if (metric > result.best) {
			result.next = result.best;
			result.best = metric;
			result.best_mask = mask;
			result.best_parity = parity;
		} else if (metric > result.next) {
			result.next = metric;
		}
		++result.candidates;
	}

	ParityResult finish()
	{
		assert(mask == 0);
		assert(systematic_metric == initial_systematic_metric);
		assert(
			parity ==
			std::vector<int8_t>(
				context.base.begin() + context.dimension,
				context.base.end()
			)
		);
		return result;
	}
};

template <int K, int O, int DEPTH = 0, typename Engine>
inline void traverse(Engine &engine, int start = 0)
{
	for (int index = start; index < K; ++index) {
		engine.flip(index);
		engine.evaluate();
		if constexpr (DEPTH + 1 < O)
			traverse<K, O, DEPTH + 1>(engine, index + 1);
		engine.flip(index);
	}
}

template <int K, int O>
FullResult run_full(const Context &context)
{
	FullWidthEngine engine(context);
	traverse<K, O>(engine);
	return engine.finish();
}

template <int K, int O>
ParityResult run_parity(const Context &context)
{
	ParityOnlyEngine engine(context);
	traverse<K, O>(engine);
	return engine.finish();
}

std::vector<int8_t> reconstruct_parity_word(
	const Context &context,
	const ParityResult &result
)
{
	std::vector<int8_t> word(static_cast<std::size_t>(context.length));
	for (int i = 0; i < context.dimension; ++i)
		word[static_cast<std::size_t>(i)] =
			context.base[static_cast<std::size_t>(i)] ^
			static_cast<int>((result.best_mask >> i) & 1);
	for (int i = context.dimension; i < context.length; ++i)
		word[static_cast<std::size_t>(i)] =
			result.best_parity[
				static_cast<std::size_t>(i - context.dimension)
			];
	return word;
}

void validate(
	const Context &context,
	const FullResult &full,
	const ParityResult &parity
)
{
	assert(full.best == parity.best);
	assert(full.next == parity.next);
	assert(full.candidates == parity.candidates);
	assert(full.best_word == reconstruct_parity_word(context, parity));
	assert(context.production_unique == (full.best != full.next));
	assert(
		context.production_decoded.size() ==
		static_cast<std::size_t>((context.length + 7) / 8)
	);
	for (int i = 0; i < context.length; ++i) {
		const int original_index =
			context.permutation[static_cast<std::size_t>(i)];
		assert(
			CODE::get_be_bit(
				context.production_decoded.data(),
				original_index
			) == full.best_word[static_cast<std::size_t>(i)]
		);
	}
}

uint64_t median(std::vector<uint64_t> values)
{
	std::sort(values.begin(), values.end());
	return values[values.size() / 2];
}

template <typename Function>
uint64_t benchmark(Function &&function, int repeats)
{
	std::vector<uint64_t> times;
	times.reserve(static_cast<std::size_t>(repeats));
	for (int repeat = 0; repeat < repeats; ++repeat) {
		const auto start = Clock::now();
		function();
		const auto end = Clock::now();
		times.push_back(static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				end - start
			).count()
		));
	}
	return median(std::move(times));
}

}

int main()
{
	constexpr int N = 127;
	constexpr int K = 64;
	constexpr int O = 4;
	constexpr int FRAMES = 3;
	constexpr int REPEATS = 7;

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
	uint64_t random_state = 0x43414e4449444154ULL;
	std::vector<Context> contexts;

	for (int frame = 0; frame < FRAMES; ++frame) {
		int8_t soft[N];
		for (int i = 0; i < N; ++i) {
			const uint64_t value = next_random(random_state);
			const int magnitude = 1 + static_cast<int>(value % 127);
			soft[i] = value & (uint64_t{1} << 63)
				? static_cast<int8_t>(-magnitude)
				: static_cast<int8_t>(magnitude);
		}
		uint8_t decoded[(N + 7) / 8] = {};
		const bool unique = decoder(decoded, soft, genmat);
		captured.production_decoded.assign(decoded, decoded + sizeof(decoded));
		captured.production_unique = unique;
		contexts.push_back(captured);
	}

	for (int frame = 0; frame < FRAMES; ++frame) {
		const Context &context = contexts[static_cast<std::size_t>(frame)];
		const FullResult full_reference = run_full<K, O>(context);
		const ParityResult parity_reference = run_parity<K, O>(context);
		validate(context, full_reference, parity_reference);
		assert(full_reference.candidates == 679121);

		// Untimed warm-ups.
		validate(context, run_full<K, O>(context), run_parity<K, O>(context));

		const uint64_t full_ns = benchmark(
			[&context, &parity_reference]() {
				const auto result = run_full<K, O>(context);
				assert(result.best == parity_reference.best);
			},
			REPEATS
		);
		const uint64_t parity_ns = benchmark(
			[&context, &full_reference]() {
				const auto result = run_parity<K, O>(context);
				assert(result.best == full_reference.best);
			},
			REPEATS
		);

		std::cout << "CANDIDATE_BENCH "
			<< frame << ' '
			<< full_ns << ' '
			<< parity_ns << ' '
			<< static_cast<double>(full_ns) /
				static_cast<double>(parity_ns) << '\n';
	}

	return 0;
}
