    if (lw != rw) return lw < rw;
    for (int rank = 0; rank < K; ++rank) {
        const bool lb = ((left >> rank) & 1ULL) != 0;
        const bool rb = ((right >> rank) & 1ULL) != 0;
        if (lb != rb) return lb;
    }
    return false;
}

int group_state(uint64_t packed, int group) {
    return group == 12 ? int((packed >> 60) & 0x0fULL)
                       : int((packed >> (group * 5)) & 0x1fULL);
}

uint64_t xor_rank(uint64_t states, int rank, const Rows& rows) {
    for (int group = 0; group < 12; ++group)
        states ^= uint64_t(rows[group][rank]) << (group * 5);
    states ^= uint64_t(rows[12][rank] & 0x0fU) << 60;
    return states;
}

uint16_t trace_metric(uint64_t states, uint16_t info, const Phi& phi) {
    uint32_t value = info;
    for (int group = 0; group < GROUPS; ++group)
        value += phi[group][group_state(states, group)];
    if (value >= (1U << 14)) throw std::runtime_error("metric exceeds 14 bits");
    return static_cast<uint16_t>(value);
}

void consider(Frame& frame, uint64_t mask, uint16_t info, uint64_t states) {
    const uint16_t value = trace_metric(states, info, frame.phi);
    if (value < frame.reference_metric) {
        frame.reference_metric = value;
        frame.reference_mask = mask;
        frame.reference_tie = 0;
    } else if (value == frame.reference_metric && mask != frame.reference_mask) {
        frame.reference_tie = 1;
        if (canonical_before(mask, frame.reference_mask)) frame.reference_mask = mask;
    }
}

template<int DIMENSION>
std::array<int8_t, 128 * DIMENSION> extend_bch127(
    const std::array<int8_t, 127 * DIMENSION>& input) {
    std::array<int8_t, 128 * DIMENSION> output{};
    for (int row = 0; row < DIMENSION; ++row) {
        uint8_t parity = 0;
        for (int column = 0; column < 127; ++column) {
            const uint8_t value = static_cast<uint8_t>(input[row*127 + column] & 1);
            output[row*128 + column] = static_cast<int8_t>(value);
            parity ^= value;
        }
        output[row*128 + 127] = static_cast<int8_t>(parity);
    }
    return output;
}

std::array<int8_t, N*K> ebch128_64() {
    std::array<int8_t, 127*K> base{};
    CODE::BoseChaudhuriHocquenghemGenerator<127,K>::matrix(
        base.data(), true,
        {0b10001001,0b10001111,0b10011101,0b11110111,0b10111111,
         0b11010101,0b10000011,0b11101111,0b11001011});
    return extend_bch127<K>(base);
}

uint64_t generator_checksum(const std::array<int8_t, N*K>& generator) {
    uint64_t hash = FNV_OFFSET;
    for (int8_t value : generator) hash = fnv_byte(hash, static_cast<uint8_t>(value));
    return hash;
}

canonical_capture::State preprocess(
    const std::array<int8_t,N>& input_soft,
    const std::array<int8_t,N*K>& input_generator) {
    canonical_capture::State result;
    std::array<int,N> order{};
    for (int i=0;i<N;++i) order[i]=i;
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        const int a=std::abs(int(input_soft[left]));
        const int b=std::abs(int(input_soft[right]));
        if (a!=b) return a>b;
        return left<right;
    });
    for (int column=0;column<N;++column) {
        const int original=order[column];
        result.soft[column]=input_soft[original];
        result.permutation[column]=static_cast<int16_t>(original);
        for (int row=0;row<K;++row)
            result.matrix[row*canonical_capture::W+column]=input_generator[row*N+original]&1;
    }
    for (int pivot=0;pivot<K;++pivot) {
        int pivot_column=pivot;
        int pivot_row=pivot;
        while (true) {
            pivot_row=pivot;
            while (pivot_row<K && !result.matrix[pivot_row*canonical_capture::W+pivot_column]) ++pivot_row;
            if (pivot_row<K) break;
            ++pivot_column;
            if (pivot_column>=N) throw std::runtime_error("rank-deficient generator");
        }
        if (pivot_column!=pivot) {
            for (int row=0;row<K;++row)
                std::swap(result.matrix[row*canonical_capture::W+pivot],
                          result.matrix[row*canonical_capture::W+pivot_column]);
            std::swap(result.soft[pivot],result.soft[pivot_column]);
            std::swap(result.permutation[pivot],result.permutation[pivot_column]);
        }
        if (pivot_row!=pivot)
            for (int column=0;column<canonical_capture::W;++column)
                std::swap(result.matrix[pivot*canonical_capture::W+column],
                          result.matrix[pivot_row*canonical_capture::W+column]);
        for (int row=0;row<K;++row) {
            if (row==pivot || !result.matrix[row*canonical_capture::W+pivot]) continue;
            for (int column=0;column<canonical_capture::W;++column)
                result.matrix[row*canonical_capture::W+column]^=
                    result.matrix[pivot*canonical_capture::W+column];
        }
    }
    for (int row=0;row<K;++row) {
        if (result.soft[row]>=0) continue;
        for (int column=0;column<canonical_capture::W;++column)
            result.base[column]^=result.matrix[row*canonical_capture::W+column];
    }
    result.ready=true;
