/*
Ordered statistics decoding

Copyright 2020 Ahmet Inan <inan@aicodix.de>
*/

#pragma once

#include <initializer_list>
#include "bitman.hh"
#include "sort.hh"

#ifndef CODE_OSD_TRACE_RESET
#define CODE_OSD_TRACE_RESET() ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_TRACE_RESET
#endif

#ifndef CODE_OSD_TRACE_FLIP
#define CODE_OSD_TRACE_FLIP(index) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_TRACE_FLIP
#endif

#ifndef CODE_OSD_TRACE_CANDIDATE
#define CODE_OSD_TRACE_CANDIDATE(hard, soft, length, width, value) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_TRACE_CANDIDATE
#endif

#ifndef CODE_OSD_TRACE_READY
#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_TRACE_READY
#endif

#ifndef CODE_OSD_PROFILE_BEGIN
#define CODE_OSD_PROFILE_BEGIN(stage) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_PROFILE_BEGIN
#endif

#ifndef CODE_OSD_PROFILE_END
#define CODE_OSD_PROFILE_END(stage) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_PROFILE_END
#endif

#ifndef CODE_OSD_PROFILE_FLIP
#define CODE_OSD_PROFILE_FLIP(width) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_PROFILE_FLIP
#endif

#ifndef CODE_OSD_PROFILE_METRIC
#define CODE_OSD_PROFILE_METRIC(width) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_PROFILE_METRIC
#endif

#ifndef CODE_OSD_PROFILE_UPDATE
#define CODE_OSD_PROFILE_UPDATE(outcome) ((void)0)
#define CODE_OSD_INTERNAL_UNDEF_PROFILE_UPDATE
#endif

namespace CODE {

template <int N, int K>
class LinearEncoder
{
public:
	void operator()(uint8_t *codeword, const uint8_t *message, const int8_t *genmat)
	{
		for (int i = 0; i < N; ++i)
			set_be_bit(codeword, i, get_be_bit(message, 0) & genmat[i]);
		for (int j = 1; j < K; ++j)
			if (get_be_bit(message, j))
				for (int i = 0; i < N; ++i)
					xor_be_bit(codeword, i, genmat[N*j+i]);
	}
};

template <int N, int K>
class BoseChaudhuriHocquenghemGenerator
{
	static const int NP = N - K;
public:
	static void poly(int8_t *genpoly, std::initializer_list<int> minimal_polynomials)
	{
		// $genpoly(x) = \prod_i(minpoly_i(x))$
		int genpoly_degree = 1;
		for (int i = 0; i < NP; ++i)
			genpoly[i] = 0;
		genpoly[NP] = 1;
		for (auto m: minimal_polynomials) {
			assert(0 < m);
			assert(m & 1);
			int m_degree = 0;
			while (m>>m_degree)
				++m_degree;
			--m_degree;
			assert(genpoly_degree + m_degree <= NP + 1);
			for (int i = genpoly_degree; i >= 0; --i) {
				if (!genpoly[NP-i])
					continue;
				genpoly[NP-i] = m&1;
				for (int j = 1; j <= m_degree; ++j)
					genpoly[NP-(i+j)] ^= (m>>j)&1;
			}
			genpoly_degree += m_degree;
		}
		assert(genpoly_degree == NP + 1);
		assert(genpoly[0]);
		assert(genpoly[NP]);
		if (0) {
			std::cerr << "generator polynomial =" << std::endl;
			for (int i = 0; i <= NP; ++i)
				std::cerr << (int)genpoly[i];
			std::cerr << std::endl;
		}
	}
	static void matrix(int8_t *genmat, bool systematic, std::initializer_list<int> minimal_polynomials)
	{
		poly(genmat, minimal_polynomials);
		for (int i = NP+1; i < N; ++i)
			genmat[i] = 0;
		for (int j = 1; j < K; ++j) {
			for (int i = 0; i < j; ++i)
				genmat[N*j+i] = 0;
			for (int i = 0; i <= NP; ++i)
				genmat[(N+1)*j+i] = genmat[i];
			for (int i = j+NP+1; i < N; ++i)
				genmat[N*j+i] = 0;
		}
		if (systematic)
			for (int k = K-1; k; --k)
				for (int j = 0; j < k; ++j)
					if (genmat[N*j+k])
						for (int i = k; i < N; ++i)
							genmat[N*j+i] ^= genmat[N*k+i];
		if (0) {
			std::cerr << "generator matrix =" << std::endl;
			for (int j = 0; j < K; ++j) {
				for (int i = 0; i < N; ++i)
					std::cerr << (int)genmat[N*j+i];
				std::cerr << std::endl;
			}
		}
	}
};

template <int N, int K, int O>
class OrderedStatisticsDecoder
{
	static const int S = sizeof(size_t);
	static const int W = (N+S-1) & ~(S-1);
	int8_t G[W*K];
	int8_t codeword[W], candidate[W];
	int8_t softperm[W];
	int16_t perm[W];
	MergeSort<int16_t, N> sort;
	void row_echelon()
	{
		for (int k = 0; k < K; ++k) {
			// find pivot in this column
			for (int j = k; j < K; ++j) {
				if (G[W*j+k]) {
					for (int i = k; j != k && i < N; ++i)
						std::swap(G[W*j+i], G[W*k+i]);
					break;
				}
			}
			// keep searching for suitable column for pivot
			// beware: this will use columns >= K if necessary.
			for (int j = k + 1; !G[W*k+k] && j < N; ++j) {
				for (int h = k; h < K; ++h) {
					if (G[W*h+j]) {
						// account column swap
						std::swap(perm[k], perm[j]);
						for (int i = 0; i < K; ++i)
							std::swap(G[W*i+k], G[W*i+j]);
						for (int i = k; h != k && i < N; ++i)
							std::swap(G[W*h+i], G[W*k+i]);
						break;
					}
				}
			}
			assert(G[W*k+k]);
			// zero out column entries below pivot
			for (int j = k + 1; j < K; ++j)
				if (G[W*j+k])
					for (int i = k; i < N; ++i)
						G[W*j+i] ^= G[W*k+i];
		}
	}
	void systematic()
	{
		for (int k = K-1; k; --k)
			for (int j = 0; j < k; ++j)
				if (G[W*j+k])
					for (int i = k; i < N; ++i)
						G[W*j+i] ^= G[W*k+i];
	}
	void encode()
	{
		for (int i = K; i < N; ++i)
			codeword[i] = codeword[0] & G[i];
		for (int j = 1; j < K; ++j)
			for (int i = K; i < N; ++i)
				codeword[i] ^= codeword[j] & G[W*j+i];
	}
	void flip(int j)
	{
		CODE_OSD_PROFILE_FLIP(W);
		for (int i = 0; i < W; ++i)
			codeword[i] ^= G[W*j+i];
		CODE_OSD_TRACE_FLIP(j);
	}
	static int metric(const int8_t *hard, const int8_t *soft)
	{
		CODE_OSD_PROFILE_METRIC(W);
		int sum = 0;
		for (int i = 0; i < W; ++i)
			sum += (1 - 2 * hard[i]) * soft[i];
		return sum;
	}
public:
	bool operator()(uint8_t *hard, const int8_t *soft, const int8_t *genmat)
	{
		CODE_OSD_PROFILE_BEGIN(0);
		for (int i = 0; i < N; ++i)
			perm[i] = i;
		for (int i = 0; i < N; ++i)
			softperm[i] = std::abs(std::max<int8_t>(soft[i], -127));
		CODE_OSD_PROFILE_END(0);
		CODE_OSD_PROFILE_BEGIN(1);
		sort(perm, N, [this](int a, int b){ return softperm[a] > softperm[b]; });
		CODE_OSD_PROFILE_END(1);
		CODE_OSD_PROFILE_BEGIN(2);
		for (int j = 0; j < K; ++j)
			for (int i = 0; i < N; ++i)
				G[W*j+i] = genmat[N*j+perm[i]];
		CODE_OSD_PROFILE_END(2);
		CODE_OSD_PROFILE_BEGIN(3);
		row_echelon();
		CODE_OSD_PROFILE_END(3);
		CODE_OSD_PROFILE_BEGIN(4);
		systematic();
		CODE_OSD_PROFILE_END(4);
		CODE_OSD_PROFILE_BEGIN(5);
		for (int i = 0; i < N; ++i)
			softperm[i] = std::max<int8_t>(soft[perm[i]], -127);
		for (int i = N; i < W; ++i)
			softperm[i] = 0;
		CODE_OSD_PROFILE_END(5);
		CODE_OSD_PROFILE_BEGIN(6);
		for (int i = 0; i < K; ++i)
			codeword[i] = softperm[i] < 0;
		encode();
		CODE_OSD_PROFILE_END(6);
		CODE_OSD_PROFILE_BEGIN(7);
		CODE_OSD_TRACE_READY(G, codeword, softperm, perm, N, K, W);
		CODE_OSD_TRACE_RESET();
		for (int i = 0; i < N; ++i)
			candidate[i] = codeword[i];
		int best = metric(codeword, softperm);
		CODE_OSD_TRACE_CANDIDATE(codeword, softperm, N, W, best);
		int next = -1;
		auto update = [this, &best, &next]() {
			int met = metric(codeword, softperm);
			CODE_OSD_TRACE_CANDIDATE(codeword, softperm, N, W, met);
			if (met > best) {
				CODE_OSD_PROFILE_UPDATE(2);
				next = best;
				best = met;
				for (int i = 0; i < N; ++i)
					candidate[i] = codeword[i];
			} else if (met > next) {
				CODE_OSD_PROFILE_UPDATE(1);
				next = met;
			} else {
				CODE_OSD_PROFILE_UPDATE(0);
			}
		};
		for (int a = 0; O >= 1 && a < K; ++a) {
			flip(a);
			update();
			for (int b = a + 1; O >= 2 && b < K; ++b) {
				flip(b);
				update();
				for (int c = b + 1; O >= 3 && c < K; ++c) {
					flip(c);
					update();
					for (int d = c + 1; O >= 4 && d < K; ++d) {
						flip(d);
						update();
						for (int e = d + 1; O >= 5 && e < K; ++e) {
							flip(e);
							update();
							for (int f = e + 1; O >= 6 && f < K; ++f) {
								flip(f);
								update();
								flip(f);
							}
							flip(e);
						}
						flip(d);
					}
					flip(c);
				}
				flip(b);
			}
			flip(a);
		}
		CODE_OSD_PROFILE_END(7);
		CODE_OSD_PROFILE_BEGIN(8);
		for (int i = 0; i < N; ++i)
			set_be_bit(hard, perm[i], candidate[i]);
		CODE_OSD_PROFILE_END(8);
		return best != next;
	}
};

template <int N, int K, int O, int L>
class OrderedStatisticsListDecoder
{
	static const int S = sizeof(size_t);
	static const int W = (N+S-1) & ~(S-1);
	int8_t G[W*K];
	int8_t codeword[W], candidate[W*L];
	int8_t softperm[W];
	int16_t perm[W];
	int score[L], cperm[L];
	MergeSort<int16_t, N> sort;
	void row_echelon()
	{
		for (int k = 0; k < K; ++k) {
			// find pivot in this column
			for (int j = k; j < K; ++j) {
				if (G[W*j+k]) {
					for (int i = k; j != k && i < N; ++i)
						std::swap(G[W*j+i], G[W*k+i]);
					break;
				}
			}
			// keep searching for suitable column for pivot
			// beware: this will use columns >= K if necessary.
			for (int j = k + 1; !G[W*k+k] && j < N; ++j) {
				for (int h = k; h < K; ++h) {
					if (G[W*h+j]) {
						// account column swap
						std::swap(perm[k], perm[j]);
						for (int i = 0; i < K; ++i)
							std::swap(G[W*i+k], G[W*i+j]);
						for (int i = k; h != k && i < N; ++i)
							std::swap(G[W*h+i], G[W*k+i]);
						break;
					}
				}
			}
			assert(G[W*k+k]);
			// zero out column entries below pivot
			for (int j = k + 1; j < K; ++j)
				if (G[W*j+k])
					for (int i = k; i < N; ++i)
						G[W*j+i] ^= G[W*k+i];
		}
	}
	void systematic()
	{
		for (int k = K-1; k; --k)
			for (int j = 0; j < k; ++j)
				if (G[W*j+k])
					for (int i = k; i < N; ++i)
						G[W*j+i] ^= G[W*k+i];
	}
	void encode()
	{
		for (int i = K; i < N; ++i)
			codeword[i] = codeword[0] & G[i];
		for (int j = 1; j < K; ++j)
			for (int i = K; i < N; ++i)
				codeword[i] ^= codeword[j] & G[W*j+i];
	}
	void flip(int j)
	{
		for (int i = 0; i < W; ++i)
			codeword[i] ^= G[W*j+i];
	}
	int metric()
	{
		int sum = 0;
		for (int i = 0; i < W; ++i)
			sum += (1 - 2 * codeword[i]) * softperm[i];
		return sum;
	}
	void update()
	{
		int j = L-1;
		int met = metric();
		if (met <= score[j])
			return;
		int pos = cperm[j];
		for (int i = 0; i < W; ++i)
			candidate[pos*W+i] = codeword[i];
		for (; j > 0 && met > score[j-1]; --j) {
			score[j] = score[j-1];
			cperm[j] = cperm[j-1];
		}
		score[j] = met;
		cperm[j] = pos;
	}
public:
	void operator()(int *rank, uint8_t *hard, const int8_t *soft, const int8_t *genmat)
	{
		for (int i = 0; i < N; ++i)
			perm[i] = i;
		for (int i = 0; i < N; ++i)
			softperm[i] = std::abs(std::max<int8_t>(soft[i], -127));
		sort(perm, N, [this](int a, int b){ return softperm[a] > softperm[b]; });
		for (int j = 0; j < K; ++j)
			for (int i = 0; i < N; ++i)
				G[W*j+i] = genmat[N*j+perm[i]];
		row_echelon();
		systematic();
		for (int i = 0; i < N; ++i)
			softperm[i] = std::max<int8_t>(soft[perm[i]], -127);
		for (int i = N; i < W; ++i)
			softperm[i] = 0;
		for (int i = 0; i < K; ++i)
			codeword[i] = softperm[i] < 0;
		encode();
		for (int i = 0; i < W; ++i)
			candidate[i] = codeword[i];
		score[0] = metric();
		for (int i = 1; i < L; ++i)
			score[i] = -1;
		for (int i = 0; i < L; ++i)
			cperm[i] = i;
		for (int a = 0; O >= 1 && a < K; ++a) {
			flip(a);
			update();
			for (int b = a + 1; O >= 2 && b < K; ++b) {
				flip(b);
				update();
				for (int c = b + 1; O >= 3 && c < K; ++c) {
					flip(c);
					update();
					for (int d = c + 1; O >= 4 && d < K; ++d) {
						flip(d);
						update();
						for (int e = d + 1; O >= 5 && e < K; ++e) {
							flip(e);
							update();
							for (int f = e + 1; O >= 6 && f < K; ++f) {
								flip(f);
								update();
								flip(f);
							}
							flip(e);
						}
						flip(d);
					}
					flip(c);
				}
				flip(b);
			}
			flip(a);
		}
		for (int j = 0, r = 0; j < L; ++j) {
			if (j > 0 && score[j-1] != score[j])
				++r;
			rank[j] = r;
			for (int i = 0; i < N; ++i)
				set_be_bit(hard+j*((N+7)/8), perm[i], candidate[cperm[j]*W+i]);
		}
	}
};

}

#ifdef CODE_OSD_INTERNAL_UNDEF_TRACE_RESET
#undef CODE_OSD_TRACE_RESET
#undef CODE_OSD_INTERNAL_UNDEF_TRACE_RESET
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_TRACE_FLIP
#undef CODE_OSD_TRACE_FLIP
#undef CODE_OSD_INTERNAL_UNDEF_TRACE_FLIP
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_TRACE_CANDIDATE
#undef CODE_OSD_TRACE_CANDIDATE
#undef CODE_OSD_INTERNAL_UNDEF_TRACE_CANDIDATE
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_TRACE_READY
#undef CODE_OSD_TRACE_READY
#undef CODE_OSD_INTERNAL_UNDEF_TRACE_READY
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_PROFILE_BEGIN
#undef CODE_OSD_PROFILE_BEGIN
#undef CODE_OSD_INTERNAL_UNDEF_PROFILE_BEGIN
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_PROFILE_END
#undef CODE_OSD_PROFILE_END
#undef CODE_OSD_INTERNAL_UNDEF_PROFILE_END
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_PROFILE_FLIP
#undef CODE_OSD_PROFILE_FLIP
#undef CODE_OSD_INTERNAL_UNDEF_PROFILE_FLIP
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_PROFILE_METRIC
#undef CODE_OSD_PROFILE_METRIC
#undef CODE_OSD_INTERNAL_UNDEF_PROFILE_METRIC
#endif

#ifdef CODE_OSD_INTERNAL_UNDEF_PROFILE_UPDATE
#undef CODE_OSD_PROFILE_UPDATE
#undef CODE_OSD_INTERNAL_UNDEF_PROFILE_UPDATE
#endif
