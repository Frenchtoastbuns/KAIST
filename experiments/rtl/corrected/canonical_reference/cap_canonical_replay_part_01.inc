    uint64_t payload_checksum=0;
};

uint64_t fnv_byte(uint64_t h,uint8_t v){return (h^v)*FNV_PRIME;}
template<class T> void hash_le(uint64_t& h,T value){
    using U=typename std::make_unsigned<T>::type; U u=static_cast<U>(value);
    for(std::size_t i=0;i<sizeof(T);++i){h=fnv_byte(h,uint8_t(u&0xffU));u>>=8;}
}
template<class T> T read_le(std::istream& in){
    using U=typename std::make_unsigned<T>::type; U value=0;
    for(std::size_t i=0;i<sizeof(T);++i){int c=in.get();if(c==EOF)throw std::runtime_error("unexpected end of trace");value|=U(uint8_t(c))<<(8*i);} return static_cast<T>(value);
}
uint64_t frame_hash(const Frame& f){
    uint64_t h=FNV_OFFSET;hash_le(h,f.index);hash_le(h,f.soft_checksum);
    for(const auto& g:f.rows)for(uint8_t v:g)hash_le(h,v);
    for(const auto& g:f.phi)for(uint16_t v:g)hash_le(h,v);
    for(uint8_t v:f.info)hash_le(h,v);
    hash_le(h,f.base_states);hash_le(h,f.seed_metric);hash_le(h,f.seed_mask);hash_le(h,f.seed_tie);
    hash_le(h,f.reference_metric);hash_le(h,f.reference_mask);hash_le(h,f.reference_tie);return h;
}
Header read_header(std::istream& in){
    char magic[8]{};in.read(magic,8);if(!in||std::string(magic,7)!="CAPCAN1")throw std::runtime_error("bad canonical trace magic");
    Header h;h.version=read_le<uint32_t>(in);h.header_bytes=read_le<uint32_t>(in);h.record_bytes=read_le<uint32_t>(in);h.frames=read_le<uint32_t>(in);
    h.n=read_le<uint32_t>(in);h.k=read_le<uint32_t>(in);h.order=read_le<uint32_t>(in);h.groups=read_le<uint32_t>(in);h.states=read_le<uint32_t>(in);h.group_bits=read_le<uint32_t>(in);h.soft_scale=read_le<uint32_t>(in);h.ebn0_milli=read_le<int32_t>(in);
    h.seed=read_le<uint64_t>(in);h.stream_checksum=read_le<uint64_t>(in);h.generator_checksum=read_le<uint64_t>(in);h.trace_checksum=read_le<uint64_t>(in);h.tie_frames=read_le<uint64_t>(in);
    if(h.version!=1||h.header_bytes!=EXPECTED_HEADER_BYTES||h.record_bytes!=EXPECTED_RECORD_BYTES||h.n!=128||h.k!=64||h.order!=4||h.groups!=13||h.states!=32||h.group_bits!=5||h.soft_scale!=32||h.ebn0_milli!=0)throw std::runtime_error("unexpected canonical trace geometry");
    if(h.seed!=CANONICAL_SEED||h.stream_checksum!=CANONICAL_STREAM_CHECKSUM)throw std::runtime_error("trace is not the frozen canonical stream");
    in.seekg(h.header_bytes,std::ios::beg);return h;
}
Frame read_frame(std::istream& in,const Header& h,uint64_t index){
    in.seekg(std::streamoff(h.header_bytes)+std::streamoff(index)*h.record_bytes,std::ios::beg);if(!in)throw std::runtime_error("trace seek failed");
    Frame f;f.index=read_le<uint32_t>(in);f.soft_checksum=read_le<uint64_t>(in);
    for(auto& g:f.rows)for(uint8_t& v:g)v=read_le<uint8_t>(in);
    for(auto& g:f.phi)for(uint16_t& v:g)v=read_le<uint16_t>(in);
    for(uint8_t& v:f.info)v=read_le<uint8_t>(in);
    f.base_states=read_le<uint64_t>(in);f.seed_metric=read_le<uint16_t>(in);f.seed_mask=read_le<uint64_t>(in);f.seed_tie=read_le<uint8_t>(in);
    f.reference_metric=read_le<uint16_t>(in);f.reference_mask=read_le<uint64_t>(in);f.reference_tie=read_le<uint8_t>(in);f.payload_checksum=read_le<uint64_t>(in);
    if(f.index!=index)throw std::runtime_error("trace frame index mismatch");if(frame_hash(f)!=f.payload_checksum)throw std::runtime_error("trace frame checksum mismatch");return f;
}
unsigned popcount4(uint8_t v){return unsigned(v&1U)+unsigned((v>>1)&1U)+unsigned((v>>2)&1U)+unsigned((v>>3)&1U);}
uint64_t parse_u64(const char* text){char* end=nullptr;auto v=std::strtoull(text,&end,0);if(end==text||*end!='\0')throw std::runtime_error("invalid integer");return uint64_t(v);}

class Simulator {
public:
    struct Result {
        uint64_t normal_issues=0,s45_issues=0;
        uint32_t normal_build=0,s45_build=0,normal_decode=0,s45_decode=0;
        uint32_t normal_bound=0,s45_bound=0,normal_wait=0,s45_wait=0;
        uint32_t normal_fifo_full=0,s45_fifo_full=0,normal_fifo_empty=0,s45_fifo_empty=0;
        uint32_t normal_b_n=0,normal_b_r=0,normal_b_exp=0,normal_b_tri=0,normal_b_prefix=0;
