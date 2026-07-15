#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

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

void mix_byte(uint64_t &hash, uint8_t value)
{
	hash ^= value;
	hash *= FNV_PRIME;
}

void mix_u64(uint64_t &hash, uint64_t value)
{
	for (int byte = 0; byte < 8; ++byte)
		mix_byte(hash, static_cast<uint8_t>(value >> (8 * byte)));
}

}

#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) \
	capture_ready(matrix, hard, soft, permutation, length, dimension, width)

#include "bitman.hh"
#include "osd.hh"

namespace {

template <int K, int O, int DEPTH = 0, typename Consumer>
inline void enumerate_nonzero(
	uint64_t prefix,
	int start,
	Consumer &consumer
)
{
	for (int index = start; index < K; ++index) {
		const uint64_t mask = prefix | (uint64_t{1} << index);
		consumer(mask);
		if constexpr (DEPTH + 1 < O)
			enumerate_nonzero<K, O, DEPTH + 1>(
				mask,
				index + 1,
				consumer
			);
	}
}

template <int K, int O, typename Consumer>
void enumerate_teps(Consumer &consumer)
{
	consumer(0);
	if constexpr (O > 0)
		enumerate_nonzero<K, O>(0, 0, consumer);
}

struct PageResult {
	int best = 0;
	int next = -1;
	uint64_t best_mask = 0;
	std::vector<int8_t> best_parity;
	uint64_t candidates = 0;
	uint64_t pages = 0;
	std::size_t last_page_size = 0;
	std::size_t max_live_payload_bytes = 0;
	uint64_t mask_hash = FNV_OFFSET;
	uint64_t content_hash = FNV_OFFSET;
};

class ResultAccumulator {
	const Context &context;
	int base_systematic_metric = 0;
	std::vector<int> systematic_delta;

public:
	PageResult result;

	explicit ResultAccumulator(const Context &input):
		context(input),
		systematic_delta(
			static_cast<std::size_t>(input.dimension)
		)
	{
		for (int i = 0; i < context.dimension; ++i) {
			const int base_term =
				(1 - 2 * context.base[static_cast<std::size_t>(i)]) *
				context.soft[static_cast<std::size_t>(i)];
			base_systematic_metric += base_term;
			systematic_delta[static_cast<std::size_t>(i)] =
				-2 * base_term;
		}
	}

	int systematic_metric(uint64_t mask) const
	{
		int metric = base_systematic_metric;
		while (mask) {
			const int index = __builtin_ctzll(mask);
			metric += systematic_delta[static_cast<std::size_t>(index)];
			mask &= mask - 1;
		}
		return metric;
	}

	void consume(
		uint64_t mask,
		const int8_t *parity
	)
	{
		assert(
			context.dimension == 64 ||
			!(mask >> context.dimension)
		);
		int metric = systematic_metric(mask);
		const int parity_length = context.length - context.dimension;
		for (int offset = 0; offset < parity_length; ++offset)
			metric +=
				(1 - 2 * parity[offset]) *
				context.soft[
					static_cast<std::size_t>(
						context.dimension + offset
					)
				];

		if (!result.candidates) {
			result.best = metric;
			result.best_mask = mask;
			result.best_parity.assign(
				parity,
				parity + parity_length
			);
		} else if (metric > result.best) {
			result.next = result.best;
			result.best = metric;
			result.best_mask = mask;
			result.best_parity.assign(
				parity,
				parity + parity_length
			);
		} else if (metric > result.next) {
			result.next = metric;
		}

		++result.candidates;
		mix_u64(result.mask_hash, mask);
		mix_u64(result.content_hash, mask);
		for (int offset = 0; offset < parity_length; ++offset)
			mix_byte(
				result.content_hash,
				static_cast<uint8_t>(parity[offset])
			);
		mix_u64(
			result.content_hash,
			static_cast<uint32_t>(metric)
		);
	}
};

void xor_selected_parity_rows(
	const Context &context,
	uint64_t selected_rows,
	int8_t *destination
)
{
	const int parity_length = context.length - context.dimension;
	while (selected_rows) {
		const int row = __builtin_ctzll(selected_rows);
		for (int offset = 0; offset < parity_length; ++offset)
			destination[offset] ^=
				context.matrix[
					static_cast<std::size_t>(
						row * context.length +
						context.dimension + offset
					)
				];
		selected_rows &= selected_rows - 1;
	}
}

void process_independent_page(
	const Context &context,
	const std::vector<uint64_t> &masks,
	ResultAccumulator &accumulator
)
{
	const int parity_length = context.length - context.dimension;
	std::vector<int8_t> candidates(
		masks.size() * static_cast<std::size_t>(parity_length)
	);
	for (std::size_t lane = 0; lane < masks.size(); ++lane) {
		int8_t *candidate =
			candidates.data() + lane * static_cast<std::size_t>(parity_length);
		std::copy(
			context.base.begin() + context.dimension,
			context.base.end(),
			candidate
		);
		xor_selected_parity_rows(context, masks[lane], candidate);
		accumulator.consume(masks[lane], candidate);
	}

	const std::size_t payload =
		masks.size() * sizeof(uint64_t) +
		candidates.size() * sizeof(int8_t);
	accumulator.result.max_live_payload_bytes =
		std::max(accumulator.result.max_live_payload_bytes, payload);
}

struct PageBoundary {
	uint64_t mask = 0;
	std::vector<int8_t> parity;
};

void process_prefix_delta_page(
	const Context &context,
	const std::vector<uint64_t> &masks,
	PageBoundary &boundary,
	ResultAccumulator &accumulator
)
{
	const int parity_length = context.length - context.dimension;
	const std::size_t page_elements =
		masks.size() * static_cast<std::size_t>(parity_length);
	std::vector<int8_t> edge_deltas(page_elements);
	std::vector<int8_t> candidates(page_elements);

	uint64_t previous_mask = boundary.mask;
	for (std::size_t lane = 0; lane < masks.size(); ++lane) {
		const uint64_t edge_mask = previous_mask ^ masks[lane];
		int8_t *edge =
			edge_deltas.data() +
			lane * static_cast<std::size_t>(parity_length);
		xor_selected_parity_rows(context, edge_mask, edge);
		previous_mask = masks[lane];
	}

	std::vector<int8_t> prefix(static_cast<std::size_t>(parity_length));
	for (std::size_t lane = 0; lane < masks.size(); ++lane) {
		const int8_t *edge =
			edge_deltas.data() +
			lane * static_cast<std::size_t>(parity_length);
		int8_t *candidate =
			candidates.data() +
			lane * static_cast<std::size_t>(parity_length);
		for (int offset = 0; offset < parity_length; ++offset) {
			prefix[static_cast<std::size_t>(offset)] ^= edge[offset];
			candidate[offset] =
				boundary.parity[static_cast<std::size_t>(offset)] ^
				prefix[static_cast<std::size_t>(offset)];
		}
		accumulator.consume(masks[lane], candidate);
	}

	boundary.mask = masks.back();
	boundary.parity.assign(
		candidates.end() - parity_length,
		candidates.end()
	);

	const std::size_t payload =
		masks.size() * sizeof(uint64_t) +
		edge_deltas.size() * sizeof(int8_t) +
		candidates.size() * sizeof(int8_t) +
		prefix.size() * sizeof(int8_t) +
		boundary.parity.size() * sizeof(int8_t);
	accumulator.result.max_live_payload_bytes =
		std::max(accumulator.result.max_live_payload_bytes, payload);
}

enum class Mode {
	Independent,
	PrefixDelta,
};

template <int K, int O>
PageResult run_pages(
	const Context &context,
	std::size_t page_size,
	Mode mode
)
{
	assert(page_size > 0);
	ResultAccumulator accumulator(context);
	PageBoundary boundary;
	boundary.parity.assign(
		context.base.begin() + context.dimension,
		context.base.end()
	);
	std::vector<uint64_t> page;
	page.reserve(page_size);

	auto process = [&]() {
		assert(!page.empty());
		if (mode == Mode::Independent)
			process_independent_page(context, page, accumulator);
		else
			process_prefix_delta_page(
				context,
				page,
				boundary,
				accumulator
			);
		++accumulator.result.pages;
		accumulator.result.last_page_size = page.size();
		page.clear();
	};

	auto consume_mask = [&](uint64_t mask) {
		page.push_back(mask);
		if (page.size() == page_size)
			process();
	};
	enumerate_teps<K, O>(consume_mask);
	if (!page.empty())
		process();

	if (mode == Mode::PrefixDelta) {
		std::vector<int8_t> expected(
			context.base.begin() + context.dimension,
			context.base.end()
		);
		xor_selected_parity_rows(context, boundary.mask, expected.data());
		assert(boundary.parity == expected);
	}

	return accumulator.result;
}

void validate_production(
	const Context &context,
	const PageResult &result
)
{
	assert(context.production_unique == (result.best != result.next));
	std::vector<int8_t> expected(
		static_cast<std::size_t>(context.length)
	);
	for (int i = 0; i < context.dimension; ++i)
		expected[static_cast<std::size_t>(i)] =
			context.base[static_cast<std::size_t>(i)] ^
			static_cast<int>((result.best_mask >> i) & 1);
	for (int i = context.dimension; i < context.length; ++i)
		expected[static_cast<std::size_t>(i)] =
			result.best_parity[
				static_cast<std::size_t>(i - context.dimension)
			];
	for (int i = 0; i < context.length; ++i) {
		const int original_index =
			context.permutation[static_cast<std::size_t>(i)];
		assert(
			CODE::get_be_bit(
				context.production_decoded.data(),
				original_index
			) == expected[static_cast<std::size_t>(i)]
		);
	}
}

const char *mode_name(Mode mode)
{
	return mode == Mode::Independent ? "independent" : "prefix_delta";
}

}

int main()
{
	constexpr int N = 127;
	constexpr int K = 64;
	constexpr int O = 4;
	constexpr uint64_t EXPECTED_CANDIDATES = 679121;
	constexpr uint64_t EXPECTED_MASK_HASH = 0x9717451b3bb8a575ULL;
	const std::vector<std::size_t> page_sizes = {1, 2, 4, 8, 16};

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

	uint64_t random_state = 0x50414745445f4f53ULL;
	int8_t soft[N];
	for (int i = 0; i < N; ++i) {
		const uint64_t value = next_random(random_state);
		const int magnitude = 1 + static_cast<int>(value % 127);
		soft[i] = value & (uint64_t{1} << 63)
			? static_cast<int8_t>(-magnitude)
			: static_cast<int8_t>(magnitude);
	}
	uint8_t decoded[(N + 7) / 8] = {};
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	captured.production_unique = decoder(decoded, soft, genmat);
	captured.production_decoded.assign(decoded, decoded + sizeof(decoded));
	const Context context = captured;

	uint64_t expected_content_hash = 0;
	int expected_best = 0;
	int expected_next = 0;

	for (const Mode mode: {Mode::Independent, Mode::PrefixDelta}) {
		for (const std::size_t page_size: page_sizes) {
			const auto start = Clock::now();
			const PageResult result =
				run_pages<K, O>(context, page_size, mode);
			const auto end = Clock::now();
			const uint64_t elapsed_ns = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					end - start
				).count()
			);

			assert(result.candidates == EXPECTED_CANDIDATES);
			assert(result.mask_hash == EXPECTED_MASK_HASH);
			assert(
				result.pages ==
				(EXPECTED_CANDIDATES + page_size - 1) / page_size
			);
			const std::size_t expected_last =
				EXPECTED_CANDIDATES % page_size
				? EXPECTED_CANDIDATES % page_size
				: page_size;
			assert(result.last_page_size == expected_last);
			validate_production(context, result);

			if (!expected_content_hash) {
				expected_content_hash = result.content_hash;
				expected_best = result.best;
				expected_next = result.next;
			} else {
				assert(result.content_hash == expected_content_hash);
				assert(result.best == expected_best);
				assert(result.next == expected_next);
			}

			std::cout << "PAGE_PASS "
				<< mode_name(mode) << ' '
				<< page_size << ' '
				<< result.candidates << ' '
				<< result.pages << ' '
				<< result.last_page_size << ' '
				<< result.max_live_payload_bytes << ' '
				<< elapsed_ns << ' '
				<< std::hex << result.content_hash
				<< std::dec << '\n';
		}
	}

	return 0;
}
