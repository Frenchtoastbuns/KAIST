#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr int K=64, ORDER=4, GROUPS=13, STATES=32, SPLIT=45;
constexpr int NORMAL_DEPTH=(K+1)*(ORDER+1)*STATES;
constexpr int RIGHT_DEPTH=(K-SPLIT+1)*(ORDER+1)*STATES;
constexpr int PAIRS=15;
constexpr int COMPACT_DEPTH=(SPLIT+1)*PAIRS*STATES;
constexpr uint16_t INF=0x3ff;
using Rows=std::array<std::array<uint8_t,K>,GROUPS>;
using Phi=std::array<std::array<uint16_t,STATES>,GROUPS>;
using Info=std::array<uint16_t,K>;

int normal_addr(int suffix,int weight,int state){return (suffix*(ORDER+1)+weight)*STATES+state;}
int right_addr(int suffix,int weight,int state){return ((suffix-SPLIT)*(ORDER+1)+weight)*STATES+state;}
int pair_index(int left,int right){int total=left+right;return total*(total+1)/2+left;}
int compact_addr(int suffix,int pair,int state){return (suffix*PAIRS+pair)*STATES+state;}
int group_state(uint64_t packed,int g){return g==12?int((packed>>60)&0xf):int((packed>>(g*5))&0x1f);}
uint64_t xor_row(uint64_t packed,int rank,const Rows& rows){
  for(int g=0;g<12;++g) packed^=uint64_t(rows[g][rank])<<(g*5);
  packed^=uint64_t(rows[12][rank]&0xf)<<60;
  return packed;
}
int popcount(uint64_t x){return __builtin_popcountll(x);}
bool canonical_before(uint64_t a,uint64_t b){
  int aw=popcount(a),bw=popcount(b);
  if(aw!=bw) return aw<bw;
  for(int i=0;i<K;++i) if(((a>>i)&1)!=((b>>i)&1)) return ((a>>i)&1)!=0;
  return false;
}
uint32_t metric(uint64_t mask,uint64_t states,uint32_t information,const Phi& phi){
  (void)mask;
  uint32_t value=information;
  for(int g=0;g<GROUPS;++g) value+=phi[g][group_state(states,g)];
  return value;
}
void write_hex(const std::filesystem::path& p,const std::vector<uint32_t>& v,int width){
  std::ofstream f(p); f<<std::hex<<std::setfill('0');
  for(uint32_t x:v) f<<std::setw(width)<<x<<'\n';
}
struct Query{int suffix,budget;uint32_t information;uint64_t states;uint32_t normal,compact;};
struct Score{uint64_t mask;uint32_t information;uint64_t states;uint32_t expected;};
}

int main(int argc,char**argv){
  const std::filesystem::path out=argc>1?argv[1]:"experiments/results/corrected_vectors";
  std::filesystem::create_directories(out);
  std::mt19937_64 rng(0x4341505f4e423435ULL);
  Rows rows{}; Phi phi{}; Info info{};
  for(int g=0;g<GROUPS;++g) for(int r=0;r<K;++r)
    rows[g][r]=uint8_t(rng() & (g==12?0xf:0x1f));
  for(int g=0;g<GROUPS;++g) for(int s=0;s<STATES;++s)
    phi[g][s]=uint16_t((rng()%48)+(s==0?0:1));
  for(int i=0;i<K;++i) info[i]=uint16_t(1+i/2);
  std::array<uint32_t,K+1> prefix{};
  for(int i=0;i<K;++i) prefix[i+1]=prefix[i]+info[i];
  uint64_t base=rng(); base=(base&((uint64_t(1)<<60)-1))|((base>>60)&0xf)<<60;

  std::vector<uint16_t> normal(GROUPS*NORMAL_DEPTH,INF);
  for(int g=0;g<GROUPS;++g){
    for(int s=0;s<STATES;++s) normal[g*NORMAL_DEPTH+normal_addr(K,0,s)]=phi[g][s];
    for(int suffix=K-1;suffix>=0;--suffix) for(int w=0;w<=ORDER;++w) for(int s=0;s<STATES;++s){
      uint16_t best=normal[g*NORMAL_DEPTH+normal_addr(suffix+1,w,s)];
      if(w) best=std::min(best,normal[g*NORMAL_DEPTH+normal_addr(suffix+1,w-1,s^rows[g][suffix])]);
      normal[g*NORMAL_DEPTH+normal_addr(suffix,w,s)]=best;
    }
  }
  std::vector<uint16_t> right(GROUPS*RIGHT_DEPTH,INF);
  for(int g=0;g<GROUPS;++g){
    for(int s=0;s<STATES;++s) right[g*RIGHT_DEPTH+right_addr(K,0,s)]=phi[g][s];
    for(int suffix=K-1;suffix>=SPLIT;--suffix) for(int w=0;w<=ORDER;++w) for(int s=0;s<STATES;++s){
      uint16_t best=right[g*RIGHT_DEPTH+right_addr(suffix+1,w,s)];
      if(w) best=std::min(best,right[g*RIGHT_DEPTH+right_addr(suffix+1,w-1,s^rows[g][suffix])]);
      right[g*RIGHT_DEPTH+right_addr(suffix,w,s)]=best;
    }
  }
  std::vector<uint16_t> compact(GROUPS*COMPACT_DEPTH,INF);
  for(int g=0;g<GROUPS;++g){
    for(int total=0;total<=ORDER;++total) for(int left=0;left<=total;++left){
      int right_w=total-left,pair=pair_index(left,right_w);
      for(int s=0;s<STATES;++s) compact[g*COMPACT_DEPTH+compact_addr(SPLIT,pair,s)]=
        left==0?right[g*RIGHT_DEPTH+right_addr(SPLIT,right_w,s)]:INF;
    }
    for(int suffix=SPLIT-1;suffix>=0;--suffix) for(int total=0;total<=ORDER;++total)
      for(int left=0;left<=total;++left){int right_w=total-left,pair=pair_index(left,right_w);
        for(int s=0;s<STATES;++s){
          uint16_t best=compact[g*COMPACT_DEPTH+compact_addr(suffix+1,pair,s)];
          if(left) best=std::min(best,compact[g*COMPACT_DEPTH+compact_addr(suffix+1,pair_index(left-1,right_w),s^rows[g][suffix])]);
          compact[g*COMPACT_DEPTH+compact_addr(suffix,pair,s)]=best;
        }
      }
  }

  uint32_t best_metric=std::numeric_limits<uint32_t>::max(); uint64_t best_mask=0; bool tied=false;
  auto consider=[&](uint64_t mask,uint32_t information,uint64_t states){uint32_t m=metric(mask,states,information,phi);
    if(m<best_metric){best_metric=m;best_mask=mask;tied=false;}
    else if(m==best_metric && mask!=best_mask){tied=true;if(canonical_before(mask,best_mask)) best_mask=mask;}
  };
  consider(0,0,base);
  std::function<void(int,int,uint64_t,uint32_t,uint64_t)> enumerate;
  enumerate=[&](int start,int remain,uint64_t mask,uint32_t inf,uint64_t states){
    if(remain==0){consider(mask,inf,states);return;}
    for(int r=start;r<=K-remain;++r) enumerate(r+1,remain-1,mask|(uint64_t(1)<<r),inf+info[r],xor_row(states,r,rows));
  };
  for(int w=1;w<=ORDER;++w) enumerate(0,w,0,0,base);

  std::vector<uint32_t> row_hex,phi_hex,info_hex,normal_hex,right_hex,compact_hex;
  for(auto&g:rows) for(auto x:g) row_hex.push_back(x);
  for(auto&g:phi) for(auto x:g) phi_hex.push_back(x);
  for(auto x:info) info_hex.push_back(x);
  for(auto x:normal) normal_hex.push_back(x);
  for(auto x:right) right_hex.push_back(x);
  for(auto x:compact) compact_hex.push_back(x);
  write_hex(out/"rows.hex",row_hex,2); write_hex(out/"phi.hex",phi_hex,3); write_hex(out/"info.hex",info_hex,2);
  write_hex(out/"normal.hex",normal_hex,3); write_hex(out/"right.hex",right_hex,3); write_hex(out/"compact.hex",compact_hex,3);
  {std::ofstream f(out/"meta.txt");f<<std::hex<<std::setfill('0')<<std::setw(16)<<base<<' '<<best_metric<<' '<<std::setw(16)<<best_mask<<' '<<int(tied)<<'\n';}

  std::vector<Query> queries;
  for(int n=0;n<256;++n){
    int suffix=1+int(rng()%63), max_prefix=std::min(3,suffix), count=int(rng()%(max_prefix+1));
    std::vector<int> candidates(suffix); for(int i=0;i<suffix;++i)candidates[i]=i;
    std::shuffle(candidates.begin(),candidates.end(),rng); candidates.resize(count); std::sort(candidates.begin(),candidates.end());
    uint64_t states=base,mask=0;uint32_t inf=0;for(int r:candidates){mask|=uint64_t(1)<<r;states=xor_row(states,r,rows);inf+=info[r];}
    int max_budget=std::min({ORDER-count,K-suffix,4}); if(max_budget<1){--n;continue;} int budget=1+int(rng()%max_budget);
    uint32_t nb=std::numeric_limits<uint32_t>::max(),cb=std::numeric_limits<uint32_t>::max();
    for(int q=1;q<=budget && suffix+q<=K;++q){uint32_t value=inf+(prefix[suffix+q]-prefix[suffix]);
      for(int g=0;g<GROUPS;++g)value+=normal[g*NORMAL_DEPTH+normal_addr(suffix,q,group_state(states,g))]; nb=std::min(nb,value);}
    if(suffix<SPLIT){for(int q=1;q<=budget;++q)for(int l=0;l<=q;++l){int r=q-l;if(suffix+l>SPLIT||SPLIT+r>K)continue;
      uint32_t value=inf+(prefix[suffix+l]-prefix[suffix])+(prefix[SPLIT+r]-prefix[SPLIT]);
      int pair=pair_index(l,r);for(int g=0;g<GROUPS;++g)value+=compact[g*COMPACT_DEPTH+compact_addr(suffix,pair,group_state(states,g))];cb=std::min(cb,value);}}
    else {for(int q=1;q<=budget&&suffix+q<=K;++q){uint32_t value=inf+(prefix[suffix+q]-prefix[suffix]);
      for(int g=0;g<GROUPS;++g)value+=right[g*RIGHT_DEPTH+right_addr(suffix,q,group_state(states,g))];cb=std::min(cb,value);}}
    queries.push_back({suffix,budget,inf,states,nb,cb});
  }
  {std::ofstream f(out/"queries.txt");f<<queries.size()<<'\n'<<std::hex;for(auto&q:queries)f<<q.suffix<<' '<<q.budget<<' '<<q.information<<' '<<q.states<<' '<<q.normal<<' '<<q.compact<<'\n';}
  std::vector<Score> scores;
  for(int n=0;n<256;++n){int w=int(rng()%5);std::vector<int> ranks(K);for(int i=0;i<K;++i)ranks[i]=i;std::shuffle(ranks.begin(),ranks.end(),rng);ranks.resize(w);
    uint64_t mask=0,states=base;uint32_t inf=0;for(int r:ranks){mask|=uint64_t(1)<<r;states=xor_row(states,r,rows);inf+=info[r];}
    scores.push_back({mask,inf,states,metric(mask,states,inf,phi)});
  }
  {std::ofstream f(out/"scores.txt");f<<scores.size()<<'\n'<<std::hex;for(auto&s:scores)f<<s.mask<<' '<<s.information<<' '<<s.states<<' '<<s.expected<<'\n';}
  std::cout<<"VECTOR_OK best="<<best_metric<<" mask=0x"<<std::hex<<best_mask<<" tied="<<std::dec<<tied<<"\n";
}
