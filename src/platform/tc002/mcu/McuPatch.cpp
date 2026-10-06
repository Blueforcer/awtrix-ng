#include "core/payload/Crc.h"
// Fixed TC002 PCM recipe. The LFSR format follows kagaimiq/bluetrum-tools
// (MIT; see third-party notices).
#include "platform/tc002/mcu/McuPatch.h"
#include "platform/tc002/daemon/McuFirmware.h"

#include <lzma.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace awtrix::tc002::mcu {
using tc002d::mcu::firmwareHash;
using tc002d::mcu::kBaseHash;
namespace {
using Bytes = std::vector<uint8_t>;
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
uint32_t word(const Bytes& b, std::size_t p) {
  require(p <= b.size() && b.size()-p >= 4, "MCU word bounds");
  return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
uint16_t half(const Bytes& b, std::size_t p) {
  require(p < b.size() && b.size()-p >= 2, "MCU half bounds");
  return static_cast<uint16_t>(b[p] | b[p+1]<<8);
}
void put(Bytes& b, std::size_t p, uint32_t v, unsigned n = 4) {
  require(p <= b.size() && b.size()-p >= n, "MCU write bounds");
  for (unsigned i=0; i<n; ++i) b[p+i] = static_cast<uint8_t>(v>>(8*i));
}
uint16_t crc16(const Bytes& b, std::size_t start, std::size_t end, uint16_t c = 0xffff) {
  require(start <= end && end <= b.size(), "MCU CRC16 bounds");
  return crc16Ccitt(b.data() + start, end - start, c);
}
uint32_t crc32(const Bytes& b, std::size_t start, std::size_t end) {
  require(start <= end && end <= b.size(), "MCU CRC32 bounds");
  return crc32Update(0xffffffffu, b.data() + start, end - start); // OEM raw CRC (no final XOR).
}
void transform(Bytes& b, std::size_t start, std::size_t end, uint32_t key) {
  require(start <= end && end <= b.size(), "MCU transform bounds");
  for (std::size_t i=start; i<end; ++i) {
    b[i] ^= static_cast<uint8_t>(key);
    for (unsigned bit=0; bit<8; ++bit) key=(key>>1)^((key&1) ? 0xa3000000U : 0U);
  }
}
std::string text(const Bytes& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }
lzma_options_lzma options() {
  lzma_options_lzma o{};
  require(!lzma_lzma_preset(&o, 6), "LZMA preset");
  o.dict_size=16384; o.lc=0; o.lp=0; o.pb=2;
  return o;
}
Bytes unpack(const Bytes& package) {
  const auto end=word(package,0x78);
  require(word(package,0x60)==512 && end>=1024 && end<=package.size() && end%512==0, "MCU segment bounds");
  Bytes data(package.begin()+512,package.begin()+end);
  const auto key=word(package,0x90)^0x474b5058U;
  for (std::size_t p=0;p<data.size();p+=512) transform(data,p,p+512,key^static_cast<uint32_t>((p+512)>>9));
  require(data.size()>5 && data[0]==0x5a && word(data,1)==16384, "MCU LZMA properties");
  const auto size=word(package,0x94);
  require(size>=0xa000 && size<=0xb000, "MCU decoded size bound");
  Bytes result(size+1);
  auto o=options(); lzma_filter filters[]={{LZMA_FILTER_LZMA1,&o},{LZMA_VLI_UNKNOWN,nullptr}};
  std::size_t input=5,output=0;
  require(lzma_raw_buffer_decode(filters,nullptr,data.data(),&input,data.size(),result.data(),&output,result.size())==LZMA_OK &&
          output==size && std::all_of(data.begin()+static_cast<std::ptrdiff_t>(input),data.end(),[](uint8_t v){return v==0;}),
          "MCU LZMA decode/padding");
  result.resize(output);
  return result;
}
Bytes postTransform(const Bytes& flash,uint32_t key) {
  Bytes out=flash;
  const auto boot=word(flash,0x14), size=word(flash,0x18);
  const auto crc=half(flash,0x1c);
  transform(out,0,0x40,0x474d564c);
  transform(out,0x40,0x80,0x50504158U^(uint32_t(half(flash,0x80))*0x10001U));
  for (uint32_t p=boot,i=1;p<boot+size;p+=512,++i) transform(out,p,p+512,0x474d564cU^(uint32_t(crc)*0x10001U)^i);
  for (std::size_t table : {0x40U,0x50U}) {
    const auto start=word(flash,table), length=word(flash,table+4);
    const auto common=table==0x40 ? 0x50504158U^(uint32_t(crc)*0x10001U)^key : 0U;
    for (uint32_t i=0;i<length/512;++i) transform(out,start+512+i*512,start+1024+i*512,common^half(flash,start+16+i*2));
  }
  return out;
}
uint32_t chipCheck(uint32_t key) {
  Bytes b(8); put(b,0,key);
  for (unsigned i=0;i<4;++i) b[4+i]=static_cast<uint8_t>(key>>(24-8*i));
  return (uint32_t(crc16(b,0,6,0x5555))<<16)|crc16(b,2,8,0xaaaa);
}
uint32_t chipKey(const Bytes& base,const Bytes& flash) {
  std::array<uint32_t,32> basis{}, combos{};
  uint32_t nullspace=0; unsigned rank=0;
  const auto zero=chipCheck(0);
  for (unsigned i=0;i<32;++i) {
    uint32_t value=chipCheck(uint32_t(1)<<i)^zero, combo=uint32_t(1)<<i;
    for (int p=31;p>=0;--p) if (value&(uint32_t(1)<<p)) {
      if (!basis[static_cast<unsigned>(p)]) {
        basis[static_cast<unsigned>(p)]=value; combos[static_cast<unsigned>(p)]=combo; ++rank; break;
      }
      value^=basis[static_cast<unsigned>(p)]; combo^=combos[static_cast<unsigned>(p)];
    }
    if (!value) nullspace=combo;
  }
  require(rank==31 && nullspace, "MCU chip-check rank");
  uint32_t value=(word(base,0x20) ? word(base,0x20) : 0x09d4c0ccU)^zero, key=0;
  for (int p=31;p>=0;--p) if (value&(uint32_t(1)<<p)) {
    require(basis[static_cast<unsigned>(p)]!=0,"MCU chip-check range");
    value^=basis[static_cast<unsigned>(p)]; key^=combos[static_cast<unsigned>(p)];
  }
  unsigned matches=0; uint32_t found=0;
  for (const auto candidate : {key,key^nullspace}) {
    const auto transformed=postTransform(flash,candidate);
    if (crc32(transformed,0x2000,transformed.size())==word(base,0x98) && crc32(transformed,0,0x1000)==word(base,0x9c)) {
      found=candidate; ++matches;
    }
  }
  require(matches==1,"MCU chip-check CRC selection");
  return found;
}
}

std::string assembleFirmware(const std::string& baseText,const std::string& extension) {
  require(baseText.size()==35840 && firmwareHash(baseText)==kBaseHash,"MCU base SHA-256 mismatch");
  require(extension.size()>256 && extension.size()<=4096,"MCU extension size");
  const Bytes base(baseText.begin(),baseText.end());
  const auto original=unpack(base);
  require(firmwareHash(text(original))=="fd6ecde95c67d16dcde7a6d63e61effce1f72d8d0af36c26c42f3587e0afb73d", "MCU decoded base SHA-256 mismatch");
  const auto key=chipKey(base,original);
  const std::size_t extra=(extension.size()+511)&~std::size_t(511),end=0x9c00+extra,size=0x7a00+extra;
  Bytes flash=original;
  flash.insert(flash.begin()+0x9c00,extra,0);
  std::copy(extension.begin(),extension.end(),flash.begin()+0x9c00);
  for (unsigned i=0;i<4;++i) require(word(original,0x5de4+i*4)==0x10003ba0,"MCU dispatch table");
  for (const auto& entry : {std::pair<std::size_t,uint32_t>{0x5de4,0x10007a00},{0x5de8,0x10007b00},{0x5dec,0x10007b80},{0x5df0,0x10007bc0}})
    put(flash,entry.first,entry.second);
  put(flash,0x44,static_cast<uint32_t>(size)); put(flash,0x50,static_cast<uint32_t>(end)); put(flash,0x2008,static_cast<uint32_t>(size));
  put(flash,0x200e,crc16(flash,0x2000,0x200e),2);
  for (std::size_t i=0;i<size/512;++i) put(flash,0x2010+i*2,crc16(flash,0x2200+i*512,0x2400+i*512,static_cast<uint16_t>(i+1)),2);
  put(flash,0x4c,crc16(flash,0x2200,end),2); put(flash,0x80,crc16(flash,0x40,0x80),2);
  auto o=options(); lzma_filter filters[]={{LZMA_FILTER_LZMA1,&o},{LZMA_VLI_UNKNOWN,nullptr}};
  Bytes packed(65536); packed[0]=0x5a; put(packed,1,16384);
  std::size_t used=5;
  require(lzma_raw_buffer_encode(filters,nullptr,flash.data(),flash.size(),packed.data(),&used,packed.size())==LZMA_OK,"MCU LZMA encode");
  packed.resize(((used+16+511)/512)*512,0);
  const auto seed=crc32(packed,0,packed.size());
  for (std::size_t p=0;p<packed.size();p+=512) transform(packed,p,p+512,seed^0x474b5058U^static_cast<uint32_t>((p+512)>>9));
  Bytes result(base.begin(),base.begin()+512);
  result.insert(result.end(),packed.begin(),packed.end());
  result.insert(result.end(),base.begin()+word(base,0x78),base.end());
  require(result.size()<=word(base,0x6c),"MCU staging capacity");
  put(result,4,static_cast<uint32_t>(result.size()-8)); put(result,0x64,static_cast<uint32_t>(packed.size()));
  put(result,0x78,static_cast<uint32_t>(512+packed.size())); put(result,0x8c,crc32(packed,0,packed.size()));
  put(result,0x90,seed); put(result,0x94,static_cast<uint32_t>(flash.size()));
  const auto transformed=postTransform(flash,key);
  put(result,0x98,crc32(transformed,0x2000,transformed.size())); put(result,0x9c,crc32(transformed,0,0x1000));
  put(result,0xfe,crc16(result,0,0xfe),2);
  require(unpack(result)==flash,"MCU local round trip failed");
  return text(result);
}
}
