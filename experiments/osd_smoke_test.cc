#include <cassert>
#include <cstdint>
#include <initializer_list>

#include "bitman.hh"
#include "osd.hh"

int main()
{
	constexpr int N = 15;
	constexpr int K = 5;
	constexpr int O = 1;
	constexpr int NW = (N + 7) / 8;
	constexpr int KW = (K + 7) / 8;

	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		{0b10011, 0b11111, 0b00111}
	);

	uint8_t message[KW] = {};
	CODE::set_be_bit(message, 0, 1);
	CODE::set_be_bit(message, 2, 1);
	CODE::set_be_bit(message, 4, 1);

	uint8_t encoded[NW] = {};
	CODE::LinearEncoder<N, K> encoder;
	encoder(encoded, message, genmat);

	int8_t soft[N];
	for (int i = 0; i < N; ++i)
		soft[i] = CODE::get_be_bit(encoded, i) ? -64 : 64;

	uint8_t decoded[NW] = {};
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	const bool unique = decoder(decoded, soft, genmat);

	assert(unique);
	for (int i = 0; i < N; ++i)
		assert(CODE::get_be_bit(decoded, i) == CODE::get_be_bit(encoded, i));

	// A second call checks that traversal backtracking restored decoder state.
	uint8_t decoded_again[NW] = {};
	const bool unique_again = decoder(decoded_again, soft, genmat);
	assert(unique_again);
	for (int i = 0; i < N; ++i)
		assert(CODE::get_be_bit(decoded_again, i) == CODE::get_be_bit(encoded, i));

	return 0;
}
