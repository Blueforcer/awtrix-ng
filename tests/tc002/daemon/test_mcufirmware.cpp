#include <sys/socket.h>
#include <sys/wait.h>

#include <algorithm>
#include <vector>

#include "platform/posix/sha256.h"
#include "platform/tc002/daemon/McuService.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using Bytes = std::vector<uint8_t>;

namespace {
void put32(Bytes& b, uint32_t n) { for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(n >> (8*i))); }
Bytes wire(Bytes b) { put32(b, mcu::Upgrade::crc(b.data(), b.size())); return b; }
Bytes config(uint8_t enabled = 1, uint32_t offset = 0, uint32_t block = 512, unsigned packet = 522) {
  Bytes b{0x92, 0, 2, 11};
  put32(b, offset); put32(b, block);
  b.push_back(static_cast<uint8_t>(packet)); b.push_back(static_cast<uint8_t>(packet >> 8)); b.push_back(enabled);
  return wire(b);
}
const Bytes hello{0xcc,0xaa,0x55,0xee,0x12,0x19,0xe4,0x21,0x69,0x3f,0x34};
Bytes normal(uint8_t cmd, Bytes payload = {}) {
  Bytes b{0xff,0x55,cmd,static_cast<uint8_t>(payload.size())};
  b.insert(b.end(), payload.begin(), payload.end());
  unsigned sum = 0;
  for (auto c : b) sum += c;
  b.push_back(static_cast<uint8_t>(sum >> 8)); b.push_back(static_cast<uint8_t>(sum));
  return b;
}
Bytes identity(uint32_t version = 3, uint32_t tag = 17) {
  Bytes b{'A','W','M','C',1,3,123,0}; put32(b,version); put32(b,tag); return b;
}
Bytes drain(int fd) {
  Bytes out;
  uint8_t b[8192];
  for (;;) {
    auto n = ::read(fd, b, sizeof b);
    if (n <= 0) break;
    out.insert(out.end(), b, b+n);
  }
  return out;
}

struct Artifact {
  tc002d_test::TempDir dir;
  Bytes image = Bytes(512);
  mcu::Identity target{1,3,123,3,17};
  std::string digest, metadata, extension = std::string(512, 0);
  Artifact() {
    std::memcpy(image.data(), "POT\0", 4);
    image[4] = 0xf8; image[5] = 1; // 512 - 8
    std::memcpy(image.data()+0x10, "XBOX\1\0\1\0", 8);
    auto checksum = [&](unsigned start, unsigned end, unsigned at) {
      uint16_t c = 0xffff;
      for (unsigned i = start; i < end; ++i) {
        c ^= static_cast<uint16_t>(image[i] << 8);
        for (int j = 0; j < 8; ++j) c = static_cast<uint16_t>((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
      }
      image[at] = static_cast<uint8_t>(c); image[at+1] = static_cast<uint8_t>(c >> 8);
    };
    checksum(256,512,0xfc); checksum(0,0xfe,0xfe);
    sha256_state hash; uint8_t bytes[32]; char hex[65];
    sha256_init(&hash); sha256_update(&hash,image.data(),image.size()); sha256_final(&hash,bytes); sha256_hex(bytes,hex);
    digest = hex;
    const auto marker=identity();
    std::copy(marker.begin(),marker.end(),extension.begin()+32);
    awtrix::api::JsonWriter j(metadata);
    j.beginObject().member("schema",2).member("recipe","tc002-pcm-v1").member("base_sha256",mcu::kBaseHash).member("target","tc002").member("base_version","V1.0.17")
      .member("abi",1).member("features",3).member("family",123).member("version",3).member("build_tag",17)
      .member("file","extension.bin").member("bytes",512).member("sha256",mcu::firmwareHash(extension))
      .member("image_bytes",512).member("image_sha256",digest).endObject();
    tc002d_test::writeFile(dir/"manifest.json",metadata);
    tc002d_test::writeFile(dir/"extension.bin",extension);
    tc002d_test::writeFile(dir/"fixture.pot",std::string(reinterpret_cast<const char*>(image.data()),image.size()));
  }
  void load(mcu::Firmware& f) { f.load(dir.path(),dir/"journal.json"); }
};

void decisions() {
  Artifact a;
  mcu::Firmware f; a.load(f);
  check(f.valid() && f.image().empty(), "extension alone never becomes a flash image");
  check(f.loadPrepared(a.dir/"fixture.pot") && f.image()==a.image,"local image digest and headers verified");
  check(f.needed("V1.0.17",nullptr,false), "known legacy image eligible");
  check(!f.needed("V1.0.18",nullptr,false), "unknown vendor image blocked");
  check(!f.needed("V1.0.17",nullptr,true), "malformed identity is not treated as legacy");
  check(!f.needed("V1.0.17",&a.target,false) && f.status() == "current", "matching build skips flash");
  auto newer = a.target; newer.version++;
  check(!f.needed("V1.0.17",&newer,false), "newer build never downgraded");
  auto alien = a.target; alien.abi++; alien.version--;
  check(!f.needed("V1.0.17",&alien,false), "ABI mismatch blocks update");
  alien = a.target; alien.tag++;
  check(!f.needed("V1.0.17",&alien,false), "same version different tag blocked");
  auto old = a.target; old.version--;
  check(f.needed("V1.0.17",&old,false) && f.recordAttempt(), "older build records durable intent");
  mcu::Firmware boot; a.load(boot);
  check(boot.pending() && !boot.needed("V1.0.17",&old,false), "restart cannot repeat interrupted update");
  check(!boot.needed("V1.0.17",&a.target,false) && !boot.pending(), "running target verifies persisted intent");
  check(tc002d_test::readFile(a.dir/"journal.json").find("verified") != std::string::npos, "verification persisted");
  tc002d_test::writeFile(a.dir/"extension.bin","corrupt");
  a.load(f);
  check(!f.needed("V1.0.17",&old,false) && f.image().empty(), "damaged image cannot flash");
  tc002d_test::writeFile(a.dir/"journal.json","{");
  a.load(f);
  check(!f.needed("V1.0.17",&old,false), "broken journal fails closed");
}

struct Transport {
  int fds[2]{-1,-1};
  mcu::Upgrade update;
  Bytes image;
  Transport(std::size_t size = 1024) : image(size,0xa5) {
    check(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fds)==0,"transport socket pair");
    update.start(image,0);
    check(sent()==hello,"exact OEM handshake CRC");
  }
  ~Transport() { ::close(fds[0]); ::close(fds[1]); }
  Bytes sent(int64_t now = 0) { update.flush(fds[0],now); return drain(fds[1]); }
  void reply(const Bytes& b, int64_t now = 0) { update.receive(b.data(),b.size(),now); }
  void configure(const Bytes& c = config()) { reply(hello); auto b=sent(); check(b.size()==18 && b[0]==0x92,"upgrade request"); reply(c); }
};

void protocol() {
  const uint8_t input[] = "123456789";
  check(mcu::Upgrade::crc(input,9,true)==0xcbf43926, "file CRC has standard final XOR");
  check(mcu::Upgrade::crc(input,9)==0x340bc6d9, "frame CRC omits final XOR");
  {
    Transport t;
    // Replies can arrive one byte at a time.
    for (uint8_t c : hello) t.reply(Bytes{c});
    auto request=t.sent();
    check(request.size()==18 && request[4]==123 && request[5]==0,"hardware selector");
    t.reply(config());
    auto first=t.sent(); check(first.size()==526 && first[0]==0xa0 && first[1]==1,"packet size greater than block accepted");
    t.reply(wire({0x90,1,0}));
    auto second=t.sent(); check(second.size()==526 && second[1]==2,"next block sequence");
    t.reply(wire({0x90,2,0xff}));
    check(t.update.succeeded() && t.update.progress()==1024,"final ff status is success");
  }
  {
    Transport t(512); t.configure(config(1,0,512,128));
    auto packets=t.sent(); check(packets.size()==544,"multi-packet block framed without gap");
    t.reply(wire({0x90,1,0xfe}));
    auto retry=t.sent(); check(retry.size()==544 && retry[1]==5,"retry increments TX per actual packet");
    t.reply(wire({0x90,2,0xff})); check(t.update.succeeded(),"retry ACK consumes RX sequence");
  }
  for (const auto& c : {config(0),config(1,512),config(1,0,0),config(1,0,512,0)}) {
    Transport t; t.configure(c);
    check(!t.update.active() && !t.update.succeeded() && t.sent().empty(),"disabled/resume/invalid geometry sends no data");
  }
  for (const Bytes& ack : {wire({0x90,2,0}),wire({0x90,1,0xf7}),wire({0x90,1,0xfa}),Bytes{0x90,1,0,0,0,0,0}}) {
    Transport t; t.configure(); t.sent(); t.reply(ack);
    check(!t.update.active() && !t.update.succeeded(),"wrong sequence/fatal ACK/CRC stops transfer");
  }
  {
    Transport t; t.configure(); t.sent(); t.update.tick(15000);
    check(!t.update.active() && t.sent().empty(),"missing block ACK never triggers blind retransmission");
  }
  {
    Transport t;
    // A full socket forces EAGAIN without dropping the queued request.
    t.reply(hello);
    Bytes fill(4096,0);
    while (::write(t.fds[0],fill.data(),fill.size())>0) {}
    t.update.flush(t.fds[0],1);
    check(t.update.pending(),"EAGAIN retains pending transfer bytes");
    drain(t.fds[1]);
    auto request=t.sent(2); check(request.size()==18,"queued request survives EAGAIN");
  }
  {
    Transport t(32768);
    int small = 1024;
    ::setsockopt(t.fds[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof small);
    t.configure(config(1,0,32768,4096));
    Bytes received;
    for (unsigned i=0; i<100 && t.update.pending(); ++i) {
      auto part=t.sent(1);
      received.insert(received.end(),part.begin(),part.end());
    }
    check(received.size()==32824,"short writes preserve every byte of a multi-packet block");
    t.reply(wire({0x90,1,0xff}),2);
    check(t.update.succeeded(),"short-write transfer completes normally");
  }
}

struct Clock {
  Artifact artifact;
  DeviceState state;
  int master = -1;
  McuOptions options;
  Clock() {
    master=::posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    check(master>=0 && ::grantpt(master)==0 && ::unlockpt(master)==0,"PTY MCU");
    options.path=::ptsname(master);
      options.firmwareDirectory=artifact.dir.path();
    options.firmwareJournal=artifact.dir/"journal.json";
    options.firmwareCache=artifact.dir.path();
    options.firmwareHelper=artifact.dir/"prepare";
    tc002d_test::writeFile(options.firmwareHelper,"#!/bin/sh\n/bin/cp \"$1/fixture.pot\" \"$2/prepared.pot\"\n");
    ::chmod(options.firmwareHelper.c_str(),0700);
  }
  ~Clock() { ::close(master); }
  Bytes take() { pollfd p{master,POLLIN,0}; ::poll(&p,1,20); return drain(master); }
  void send(McuService& service,const Bytes& b,int64_t now) {
    check(::write(master,b.data(),b.size())==static_cast<ssize_t>(b.size()),"PTY reply write");
    std::vector<PollInterest> interests; service.pollInterest(interests);
    for (const auto& interest : interests) {
      pollfd p{interest.fd,interest.events,0};
      if (::poll(&p,1,100)>0) service.onReady(p.fd,p.revents,now);
    }
  }
  void probe(McuService& service, uint32_t version, int64_t now, uint8_t usb = 1) {
    check(take()==normal(0x11),"boot asks version");
    send(service,normal(0x11,{'V','1','.','0','.','1','7'}),now);
    check(take()==normal(8),"boot asks build identity");
    send(service,normal(8,identity(version)),now);
    check(take()==normal(2),"USB queried before install");
    send(service,normal(2,{usb}),now);
  }
};

void bootFlow(bool stopDuringTransfer) {
  Clock clock;
  McuService service(clock.state,clock.options);
  bool healthy=false;
  service.allowFirmwareUpdate([&]{ return healthy; });
  service.start(0);
  clock.probe(service,2,0);
  check(clock.take()==normal(3),"unhealthy runtime does not trigger MCU upgrade");
  clock.send(service,normal(3,{90,12,20}),0); clock.take();
  healthy=true; service.onTime(1);
  check(clock.take().empty() && service.firmwareBusy(),"healthy boot starts preparation without UART handshake");
  check(!tc002d_test::exists(clock.options.firmwareJournal),"preparation cannot record a flash attempt");
  service.onTime(1001);
  check(clock.take()==normal(2),"USB queries continue during local preparation");
  clock.send(service,normal(2,{1}),1001);
  int result=0; const auto child=::waitpid(-1,&result,0);
  check(child>0 && service.onChildExit(child,result,1001),"preparation child reaped");
  service.onTime(1001);
  check(clock.take()==normal(2),"power is checked again after preparation");
  check(clock.state.power().usbPower,"preparation preserves the published USB measurement");
  clock.send(service,normal(2,{1}),1002);
  check(clock.take()==hello && service.firmwareBusy(),"verified local image starts update with fresh USB");
  check(tc002d_test::readFile(clock.options.firmwareJournal).find("attempt")!=std::string::npos,"journal precedes first upgrade byte");
  if (stopDuringTransfer) { service.requestStop(1002); check(!service.stopped(),"stop defers UART close while flashing"); }
  clock.send(service,hello,1002);
  check(clock.take().size()==18,"service routes handshake");
  clock.send(service,config(),1002);
  check(clock.take().size()==526,"service sends image");
  clock.send(service,wire({0x90,1,0xff}),1002);
  if (stopDuringTransfer) {
    check(service.stopped(),"deferred stop completes after transfer");
    mcu::Firmware after; clock.artifact.load(after);
    check(after.pending(),"ACK alone leaves next boot verification pending");
    return;
  }
  check(service.firmwareBusy(),"ACK awaits running identity");
  service.onTime(2502);
  clock.probe(service,3,2502);
  check(!service.firmwareBusy(),"target identity completes upgrade");
  check(clock.take()==normal(3),"normal service resumes after verification");
  check(tc002d_test::readFile(clock.options.firmwareJournal).find("verified")!=std::string::npos,"success persists after actual identity reply");
  service.requestStop(2503);
}

void usbGate() {
  Clock clock; McuService service(clock.state,clock.options);
  service.allowFirmwareUpdate([]{return true;}); service.start(0); clock.probe(service,2,0,0);
  check(clock.take()==normal(3) && !service.firmwareBusy(),"battery-only boot cannot flash");
  check(!tc002d_test::exists(clock.options.firmwareJournal),"no attempt recorded until power available");
  service.requestStop(1);
}

void currentNeedsNoPreparation() {
  Clock clock; McuService service(clock.state,clock.options);
  service.allowFirmwareUpdate([]{return true;}); service.start(0); clock.probe(service,3,0);
  check(!service.firmwareBusy() && !tc002d_test::exists(clock.options.firmwareCache+"/prepared.pot"),
        "current build does not prepare or download anything");
  check(!tc002d_test::exists(clock.options.firmwareJournal),"current build does not record an attempt");
  service.requestStop(1);
}

void preparationFailure(bool shutdown) {
  Clock clock;
  tc002d_test::writeFile(clock.options.firmwareHelper, shutdown ? "#!/bin/sh\nexec /bin/sleep 20\n" : "#!/bin/sh\nexit 11\n");
  McuService service(clock.state,clock.options);
  service.allowFirmwareUpdate([]{return true;}); service.start(0); clock.probe(service,2,0);
  check(service.firmwareBusy(),"preparation is tracked as a child");
  if (shutdown) { service.requestStop(1); check(!service.stopped(),"shutdown waits for child reap"); }
  int result=0; const auto child=::waitpid(-1,&result,0);
  check(child>0 && service.onChildExit(child,result,2),"failed or cancelled preparation reaped");
  check(!tc002d_test::exists(clock.options.firmwareJournal),"failed preparation never touches flash journal");
  if (shutdown) check(service.stopped(),"cancelled preparation leaves no child or UART");
  else {
    check(!service.firmwareBusy(),"download failure leaves MCU update idle");
    service.onTime(1001);
    check(!service.firmwareBusy(),"download failure respects retry delay");
    service.requestStop(1002);
  }
}
}

int main() {
  tc002d_test::quietLogs();
  decisions(); protocol(); bootFlow(false); bootFlow(true); usbGate(); currentNeedsNoPreparation();
  preparationFailure(false); preparationFailure(true);
  return tc002d_test::finish("MCU firmware");
}
