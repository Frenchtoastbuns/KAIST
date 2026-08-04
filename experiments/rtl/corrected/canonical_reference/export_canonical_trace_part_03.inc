    return static_cast<uint64_t>(value);
}
}

int main(int argc, char** argv) {
    std::string output = "canonical_cap_trace.bin";
    std::string manifest = "canonical_cap_trace_manifest.json";
    int frames = DEFAULT_FRAMES;
    uint64_t seed = CANONICAL_SEED;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i+1 < argc) output = argv[++i];
        else if (arg == "--manifest" && i+1 < argc) manifest = argv[++i];
        else if (arg == "--frames" && i+1 < argc) frames = static_cast<int>(parse_u64(argv[++i]));
        else if (arg == "--seed" && i+1 < argc) seed = parse_u64(argv[++i]);
        else throw std::runtime_error("unknown or incomplete argument: " + arg);
    }
    if (frames <= 0) throw std::runtime_error("frames must be positive");

    const auto generator = ebch128_64();
    const uint64_t gen_checksum = generator_checksum(generator);
    CODE::LinearEncoder<N,K> encoder;
    CODE::OrderedStatisticsDecoder<N,K,O> decoder;
    std::mt19937_64 random(seed);
    std::uniform_int_distribution<int> bit(0,1);
    std::normal_distribution<double> noise(0.0,1.0); // rate=1/2, Eb/N0=0 dB
    uint64_t stream_checksum = FNV_OFFSET;
    uint64_t trace_checksum = FNV_OFFSET;
    std::vector<Frame> trace;
    trace.reserve(static_cast<std::size_t>(frames));
    uint64_t tie_frames = 0;

    for (int frame_index = 0; frame_index < frames; ++frame_index) {
        std::array<uint8_t,(K+7)/8> message{};
        for (int position = 0; position < K; ++position)
            CODE::set_be_bit(message.data(), position, bit(random));
        std::array<uint8_t,(N+7)/8> encoded{};
        encoder(encoded.data(), message.data(), generator.data());
        std::array<int8_t,N> soft{};
        for (int position = 0; position < N; ++position) {
            const double symbol = CODE::get_be_bit(encoded.data(), position) ? -1.0 : 1.0;
            const double received = symbol + noise(random);
            const int magnitude = std::max(1, std::min(127,
                static_cast<int>(std::lround(std::abs(received) * 32.0))));
            soft[position] = static_cast<int8_t>(received < 0 ? -magnitude : magnitude);
            stream_checksum = fnv_byte(stream_checksum, static_cast<uint8_t>(soft[position]));
        }
        const auto pre = preprocess(soft, generator);
        std::array<uint8_t,(N+7)/8> decoded{};
        const std::array<uint8_t,(N+7)/8>* decoded_check = nullptr;
        bool unique = true;
        if (frame_index < 3) {
            canonical_capture::captured.ready = false;
            unique = decoder(decoded.data(), soft.data(), generator.data());
            if (!canonical_capture::captured.ready ||
                canonical_capture::captured.matrix != pre.matrix ||
                canonical_capture::captured.base != pre.base ||
                canonical_capture::captured.soft != pre.soft ||
                canonical_capture::captured.permutation != pre.permutation)
                throw std::runtime_error("direct preprocessing differs from validated OSD preprocessing");
            decoded_check = &decoded;
        }
        Frame frame = build_frame(static_cast<uint32_t>(frame_index), soft, pre, decoded_check);
        if (frame_index < 3 && frame.reference_tie != static_cast<uint8_t>(!unique))
            throw std::runtime_error("baseline tie status differs from exhaustive TEP oracle");
        if (frame.reference_tie) ++tie_frames;
        hash_le(trace_checksum, frame.payload_checksum);
        trace.push_back(frame);
    }

    if (seed == CANONICAL_SEED && frames == DEFAULT_FRAMES &&
        stream_checksum != CANONICAL_STREAM_CHECKSUM) {
        std::ostringstream message;
        message << "canonical stream checksum mismatch: got " << stream_checksum
                << " expected " << CANONICAL_STREAM_CHECKSUM;
        throw std::runtime_error(message.str());
    }

    std::ofstream out(output, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open trace output");
    const char magic[8] = {'C','A','P','C','A','N','1','\0'};
    out.write(magic, sizeof(magic));
    write_le(out, TRACE_VERSION); write_le(out, HEADER_BYTES); write_le(out, RECORD_BYTES);
    write_le(out, static_cast<uint32_t>(frames));
    write_le(out, static_cast<uint32_t>(N)); write_le(out, static_cast<uint32_t>(K));
    write_le(out, static_cast<uint32_t>(O)); write_le(out, static_cast<uint32_t>(GROUPS));
    write_le(out, static_cast<uint32_t>(STATES)); write_le(out, static_cast<uint32_t>(GROUP_BITS));
    write_le(out, static_cast<uint32_t>(32)); write_le(out, static_cast<int32_t>(0));
    write_le(out, seed); write_le(out, stream_checksum); write_le(out, gen_checksum);
    write_le(out, trace_checksum); write_le(out, tie_frames);
    while (static_cast<uint32_t>(out.tellp()) < HEADER_BYTES) out.put('\0');
    if (static_cast<uint32_t>(out.tellp()) != HEADER_BYTES) throw std::runtime_error("header size mismatch");
    for (const Frame& frame : trace) write_frame(out, frame);
    out.close();

    std::ofstream json(manifest);
    if (!json) throw std::runtime_error("cannot open manifest output");
    json << "{\n"
         << "  \"configuration\": \"eBCH(128,64), OSD-4, BPSK-AWGN, 0 dB\",\n"
         << "  \"seed\": " << seed << ",\n"
         << "  \"frames\": " << frames << ",\n"
         << "  \"soft_quantization_scale\": 32,\n"
         << "  \"stream_checksum\": " << stream_checksum << ",\n"
         << "  \"expected_stream_checksum\": " << CANONICAL_STREAM_CHECKSUM << ",\n"
         << "  \"generator_checksum\": " << gen_checksum << ",\n"
         << "  \"trace_payload_checksum\": " << trace_checksum << ",\n"
         << "  \"tie_frames\": " << tie_frames << ",\n"
         << "  \"header_bytes\": " << HEADER_BYTES << ",\n"
         << "  \"record_bytes\": " << RECORD_BYTES << "\n"
         << "}\n";

    std::cout << "CANONICAL_TRACE_PASS frames=" << frames
              << " seed=" << seed
              << " stream_checksum=" << stream_checksum
              << " generator_checksum=" << gen_checksum
              << " trace_checksum=" << trace_checksum
              << " tie_frames=" << tie_frames
              << " record_bytes=" << RECORD_BYTES << '\n';
    return 0;
}
