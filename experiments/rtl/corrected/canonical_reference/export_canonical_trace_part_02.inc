    return result;
}

Frame build_frame(uint32_t frame_index,
                  const std::array<int8_t,N>& original_soft,
                  const canonical_capture::State& pre,
                  const std::array<uint8_t,(N+7)/8>* decoded_check) {
    Frame frame;
    frame.index = frame_index;
    for (int8_t value : original_soft)
        frame.soft_checksum = fnv_byte(frame.soft_checksum, static_cast<uint8_t>(value));

    for (int rank = 0; rank < K; ++rank) {
        frame.info[rank] = static_cast<uint8_t>(std::abs(int(pre.soft[rank])));
        for (int group = 0; group < GROUPS; ++group) {
            uint8_t effect = 0;
            const int bits = group == 12 ? 4 : 5;
            for (int bit = 0; bit < bits; ++bit) {
                const int parity = group*5 + bit;
                const int column = K + parity;
                if (pre.matrix[rank*canonical_capture::W + column] & 1)
                    effect |= static_cast<uint8_t>(1U << bit);
            }
            frame.rows[group][rank] = effect;
        }
    }

    for (int group = 0; group < GROUPS; ++group) {
        const int bits = group == 12 ? 4 : 5;
        for (int state = 0; state < STATES; ++state) {
            uint16_t cost = 0;
            for (int bit = 0; bit < bits; ++bit) {
                if ((state >> bit) & 1) {
                    const int column = K + group*5 + bit;
                    cost = static_cast<uint16_t>(cost + std::abs(int(pre.soft[column])));
                }
            }
            frame.phi[group][state] = cost;
        }
    }

    for (int group = 0; group < GROUPS; ++group) {
        const int bits = group == 12 ? 4 : 5;
        uint8_t state = 0;
        for (int bit = 0; bit < bits; ++bit) {
            const int column = K + group*5 + bit;
            const bool mismatch = (pre.base[column] != 0) != (pre.soft[column] < 0);
            if (mismatch) state |= static_cast<uint8_t>(1U << bit);
        }
        frame.base_states |= uint64_t(state) << (group*5);
    }

    frame.seed_metric = trace_metric(frame.base_states, 0, frame.phi);
    frame.seed_mask = 0;
    frame.seed_tie = 0;
    frame.reference_metric = frame.seed_metric;
    frame.reference_mask = 0;
    frame.reference_tie = 0;

    for (int i = 0; i < K; ++i) {
        const uint64_t s1 = xor_rank(frame.base_states, i, frame.rows);
        const uint16_t c1 = frame.info[i];
        const uint64_t m1 = 1ULL << i;
        consider(frame, m1, c1, s1);
        for (int j = i+1; j < K; ++j) {
            const uint64_t s2 = xor_rank(s1, j, frame.rows);
            const uint16_t c2 = static_cast<uint16_t>(c1 + frame.info[j]);
            const uint64_t m2 = m1 | (1ULL << j);
            consider(frame, m2, c2, s2);
            for (int k = j+1; k < K; ++k) {
                const uint64_t s3 = xor_rank(s2, k, frame.rows);
                const uint16_t c3 = static_cast<uint16_t>(c2 + frame.info[k]);
                const uint64_t m3 = m2 | (1ULL << k);
                consider(frame, m3, c3, s3);
                for (int l = k+1; l < K; ++l) {
                    const uint64_t s4 = xor_rank(s3, l, frame.rows);
                    const uint16_t c4 = static_cast<uint16_t>(c3 + frame.info[l]);
                    const uint64_t m4 = m3 | (1ULL << l);
                    consider(frame, m4, c4, s4);
                }
            }
        }
    }

    if (decoded_check) {
        std::array<int8_t, canonical_capture::W> candidate = pre.base;
        for (int rank = 0; rank < K; ++rank) {
            if (((frame.reference_mask >> rank) & 1ULL) == 0) continue;
            for (int column = 0; column < canonical_capture::W; ++column)
                candidate[column] ^= pre.matrix[rank*canonical_capture::W + column];
        }
        for (int column = 0; column < N; ++column) {
            const int original = pre.permutation[column];
            if (CODE::get_be_bit(decoded_check->data(), original) != (candidate[column] != 0))
                throw std::runtime_error("canonical TEP does not reproduce baseline decoded word");
        }
    }

    uint64_t hash = FNV_OFFSET;
    hash_le(hash, frame.index);
    hash_le(hash, frame.soft_checksum);
    for (const auto& group : frame.rows) for (uint8_t v : group) hash_le(hash, v);
    for (const auto& group : frame.phi) for (uint16_t v : group) hash_le(hash, v);
    for (uint8_t v : frame.info) hash_le(hash, v);
    hash_le(hash, frame.base_states);
    hash_le(hash, frame.seed_metric); hash_le(hash, frame.seed_mask); hash_le(hash, frame.seed_tie);
    hash_le(hash, frame.reference_metric); hash_le(hash, frame.reference_mask); hash_le(hash, frame.reference_tie);
    frame.payload_checksum = hash;
    return frame;
}
void write_frame(std::ostream& out, const Frame& frame) {
    write_le(out, frame.index); write_le(out, frame.soft_checksum);
    for (const auto& group : frame.rows) for (uint8_t v : group) write_le(out, v);
    for (const auto& group : frame.phi) for (uint16_t v : group) write_le(out, v);
    for (uint8_t v : frame.info) write_le(out, v);
    write_le(out, frame.base_states);
    write_le(out, frame.seed_metric); write_le(out, frame.seed_mask); write_le(out, frame.seed_tie);
    write_le(out, frame.reference_metric); write_le(out, frame.reference_mask); write_le(out, frame.reference_tie);
    write_le(out, frame.payload_checksum);
}

uint64_t parse_u64(const char* text) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0') throw std::runtime_error("invalid integer");
