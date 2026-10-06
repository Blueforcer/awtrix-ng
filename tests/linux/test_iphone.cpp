#include "../support.h"
// The iPhone link: the ANCS and AMS formats, the link against a phone faked on the fake radio, the
// payloads it puts on the display, and the main loop's settings, routes and scheduling.
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include "FakeBleRadio.h"
#include "core/api/JsonReader.h"
#include "platform/linux/ble/Ams.h"
#include "platform/linux/ble/Ancs.h"
#include "platform/linux/ble/BleHub.h"
#include "platform/linux/ble/IphoneLink.h"
#include "platform/linux/iphone/IphoneBridge.h"
#include "platform/linux/iphone/IphoneCovers.h"
#include "platform/linux/iphone/IphonePayload.h"
#include "platform/linux/iphone/IphoneSettings.h"
#include "platform/posix/Files.h"

using namespace awtrix;
using namespace awtrix::ble;
using namespace awtrix::ble::testing;
using namespace awtrix::iphone;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes answer(uint32_t uid, const std::string& app, const std::string& title, const std::string& message) {
  Bytes out{0, static_cast<uint8_t>(uid), static_cast<uint8_t>(uid >> 8), static_cast<uint8_t>(uid >> 16),
            static_cast<uint8_t>(uid >> 24)};
  const std::pair<uint8_t, const std::string*> parts[] = {{0, &app}, {1, &title}, {3, &message}};
  for (const auto& [id, text] : parts) {
    out.push_back(id);
    put16(out, static_cast<uint16_t>(text->size()));
    out.insert(out.end(), text->begin(), text->end());
  }
  return out;
}

Bytes notice(uint8_t event, uint8_t flags, uint32_t uid) {
  return Bytes{event, flags, 0, 1, static_cast<uint8_t>(uid), static_cast<uint8_t>(uid >> 8),
               static_cast<uint8_t>(uid >> 16), static_cast<uint8_t>(uid >> 24)};
}

void ancsFormats() {
  ancs::Notice n;
  check(ancs::parseNotice(notice(0, 16, 1), n) && n.event == 0 && n.flags == 16 && n.uid == 1 && n.fresh(),
        "a new notification with a negative action is fresh");
  check(ancs::parseNotice(notice(0, ancs::kPreExisting, 2), n) && !n.fresh(), "one the phone already had is not");
  check(ancs::parseNotice(notice(0, ancs::kSilent, 3), n) && !n.fresh(), "nor is a silent one");
  check(ancs::parseNotice(notice(1, 0, 4), n) && !n.fresh(), "nor a modified one");
  check(!ancs::parseNotice(Bytes(7, 0), n), "a short value is no notice");
  check(ancs::attributesRequest(0x01020304) == Bytes({0, 4, 3, 2, 1, 0, 1, 48, 0, 3, 120, 0}),
        "the request asks for app, title (48) and message (120), uid little-endian");

  ancs::Assembler a;
  const Bytes whole = answer(7, "com.apple.shortcuts", "Neuer Kurzbefehl", "Hallo Welt");
  check(a.feed(whole) == ancs::Assembler::Result::Waiting, "nothing is assembled before a request");
  a.begin(7);
  check(a.feed(whole) == ancs::Assembler::Result::Done && a.result().app == "com.apple.shortcuts" &&
            a.result().title == "Neuer Kurzbefehl" && a.result().message == "Hallo Welt" && !a.active(),
        "an answer in one piece");
  for (const std::size_t cut : {std::size_t{3}, std::size_t{6}, std::size_t{12}, whole.size() - 1}) {
    a.begin(7);
    const Bytes head(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(cut));
    const Bytes tail(whole.begin() + static_cast<std::ptrdiff_t>(cut), whole.end());
    const bool first = a.feed(head) == ancs::Assembler::Result::Waiting;
    check(first && a.feed(tail) == ancs::Assembler::Result::Done && a.result().message == "Hallo Welt",
          "an answer split after byte " + std::to_string(cut));
  }
  a.begin(8);
  check(a.feed(answer(7, "late", "", "")) == ancs::Assembler::Result::Waiting && a.active(),
        "an answer for another uid is dropped");
  check(a.feed(Bytes{'x', 'y'}) == ancs::Assembler::Result::Waiting, "and so is the tail of one");
  check(a.feed(answer(8, "a", "", "")) == ancs::Assembler::Result::Done && a.result().app == "a",
        "the awaited answer still reads after them");
  a.begin(9);
  Bytes huge{0, 9, 0, 0, 0, 0};
  put16(huge, 2000);
  huge.resize(ancs::Assembler::kMaxBytes + 1);
  check(a.feed(huge) == ancs::Assembler::Result::Failed && !a.active(), "an answer too long fails");

  std::string cut = "gr\xc3\xbc\xc3";
  ancs::trimUtf8(cut);
  check(cut == "gr\xc3\xbc", "half a character at the end is cut");
  std::string whole8 = "gr\xc3\xbc";
  ancs::trimUtf8(whole8);
  check(whole8 == "gr\xc3\xbc", "a whole one is kept");
  std::string euro = "\xe2\x82";
  ancs::trimUtf8(euro);
  check(euro.empty(), "two bytes of three are cut");
}

void amsFormats() {
  check(ams::trackAttributes() == Bytes({2, 0, 2, 3}) && ams::playerAttributes() == Bytes({0, 0, 1}),
        "the entity update writes ask for artist, title, duration and name, playback info");
  ams::Update u;
  check(ams::parseUpdate(Bytes{2, 2, 1, 'H', 'i'}, u) && u.entity == 2 && u.attribute == 2 && u.truncated &&
            u.value == "Hi",
        "an entity update splits into entity, attribute, flags and text");
  check(!ams::parseUpdate(Bytes{2, 2}, u), "two bytes are no update");
  ams::PlaybackInfo info;
  check(ams::parsePlaybackInfo("1,1.0,31.683", info) && info.state == 1 && info.rate == 1.0 && info.elapsed == 31.683,
        "playback info reads state, rate and elapsed");
  check(ams::parsePlaybackInfo("", info) && info.state == 0 && info.rate == 0 && info.elapsed == 0,
        "nothing loaded reads as paused at 0");
  check(!ams::parsePlaybackInfo("7,1,0", info), "an unknown state is refused");

  ams::Playback p;
  const auto apply = [&](uint8_t entity, uint8_t attribute, const std::string& text, int64_t now) {
    Bytes v{entity, attribute, 0};
    v.insert(v.end(), text.begin(), text.end());
    ams::Update update;
    ams::parseUpdate(v, update);
    return p.apply(update, now);
  };
  check(apply(0, 0, "Musik", 0) && p.player == "Musik", "the player's name");
  check(!apply(0, 0, "Musik", 0), "the same name again changes nothing");
  apply(2, 0, "Roger Cicero", 0);
  apply(2, 2, "Wenn ich dich los w\xc3\xa4r (Live)", 0);
  apply(2, 3, "249.835", 0);
  check(!p.playing(), "a track that has not started is not playing");
  check(apply(0, 1, "1,1.0,31.683", 1000) && p.playing(), "it plays once the playback info says so");
  check(p.elapsedAt(3000) > 33.68 && p.elapsedAt(3000) < 33.69, "the position moves on with the time since the report");
  apply(0, 1, "1,2.0,10", 1000);
  check(p.elapsedAt(2000) == 12, "at the rate the phone plays");
  check(p.elapsedAt(1000000) == p.duration, "never past the end of the track");
  apply(0, 1, "0,0.0,40", 1000);
  check(!p.playing() && p.elapsedAt(9000) == 40, "paused, it stays put");
}

// ---- the link, against a phone on the fake radio ------------------------------------------

struct Phone {
  FakeRadio radio;
  std::unique_ptr<IphoneLink> link;
  BleHub hub{radio, [this](BleEvent e) {
               if (e.script == IphoneLink::kOwner) link->onEvent(std::move(e));
             }};
  std::vector<uint16_t> ancsHandles, amsHandles;
  std::vector<std::pair<uint16_t, Bytes>> writes;
  std::vector<std::string> logs;
  int64_t now = 1000;
  const Address address = mkaddr("AC:E4:B5:D6:D0:57");

  Phone() {
    link = std::make_unique<IphoneLink>(hub, "AWTRIX NG", [this](const std::string& l) { logs.push_back(l); });
    radio.peerDb.setDeviceName("iPhone von Stephan");
    radio.peerDb.addService(uuid(ancs::kService),
                            {{uuid(ancs::kNotificationSource), att::kPropNotify, true, {}, {}},
                             {uuid(ancs::kControlPoint), att::kPropWrite, true, {}, {}},
                             {uuid(ancs::kDataSource), att::kPropNotify, true, {}, {}}},
                            ancsHandles);
    const auto both = static_cast<uint8_t>(att::kPropWrite | att::kPropNotify);
    radio.peerDb.addService(uuid(ams::kService),
                            {{uuid("9b3c81d8-57b1-4a8a-b8df-0e56f7ca51c2"), both, true, {}, {}},
                             {uuid(ams::kEntityUpdate), both, true, {}, {}},
                             {uuid(ams::kEntityAttribute), static_cast<uint8_t>(att::kPropRead | att::kPropWrite), true, {}, {}}},
                            amsHandles);
    radio.peerDb.drain();
    radio.peerDb.onWrite = [this](int, uint16_t handle, const Bytes& value) { writes.emplace_back(handle, value); };
  }
  void step() {
    for (int i = 0; i < 40; ++i) {
      radio.pump(hub, now);
      link->tick(now);
    }
  }
  void connect(int id, const Address& who) {
    radio.peers[id] = who;
    radio.levels[id] = 1;
    radio.peerDb.linkUp(id);
    radio.events.accepted(id, who);
    step();
  }
  void enable(bool on = true) {
    IphoneConfig c;
    c.enabled = on;
    link->configure(c, now);
    step();
  }
  std::vector<IphoneEvent> events() {
    const std::deque<IphoneEvent> all = link->take();
    return {all.begin(), all.end()};
  }
  std::vector<Bytes> writesTo(uint16_t handle) const {
    std::vector<Bytes> out;
    for (const auto& [h, v] : writes)
      if (h == handle) out.push_back(v);
    return out;
  }
};

void linkComesUp() {
  Phone p;
  check(p.link->status().state == IphoneState::Off && p.radio.powerCalls.empty(), "off, it leaves Bluetooth alone");
  p.enable();
  check(p.link->status().state == IphoneState::Waiting && p.radio.adverts.size() == 1, "enabled, it advertises");
  const Bytes adv = p.radio.adverts.begin()->second;
  check(adv.size() >= 18 && adv[1] == 0x15 && adv[2] == 0xd0 && adv[17] == 0x79,
        "asking for the notification service, as a 128-bit solicitation");

  p.connect(7, p.address);
  const IphoneStatus s = p.link->status();
  check(s.state == IphoneState::Ready && s.notifications && s.music, "the phone's link is set up");
  check(p.radio.levels[7] == 2, "pairing on the way, as the phone keeps its services for bonded devices");
  check(p.radio.connectCalls == 0, "over the phone's own link");
  const auto setup = p.writesTo(p.amsHandles[1]);
  check(setup.size() == 2 && setup[0] == ams::trackAttributes() && setup[1] == ams::playerAttributes(),
        "the player is asked for the track and its playback");
  auto events = p.events();
  check(events.size() == 1 && events[0].kind == IphoneEvent::Kind::Phone && events[0].phone.b == p.address.b &&
            events[0].name == "iPhone von Stephan",
        "the phone is remembered, with its name");

  p.radio.notifyPeer(p.ancsHandles[0], notice(0, 16, 1));
  p.step();
  auto requests = p.writesTo(p.ancsHandles[1]);
  check(requests.size() == 1 && requests[0] == ancs::attributesRequest(1), "a new notification's attributes are asked for");
  p.radio.notifyPeer(p.ancsHandles[0], notice(0, 0, 2));
  p.step();
  check(p.writesTo(p.ancsHandles[1]).size() == 1, "one question at a time");
  const Bytes reply = answer(1, "com.apple.shortcuts", "Neuer Kurzbefehl", "Hallo Welt");
  p.radio.notifyPeer(p.ancsHandles[2], Bytes(reply.begin(), reply.begin() + 9));
  p.step();
  p.radio.notifyPeer(p.ancsHandles[2], Bytes(reply.begin() + 9, reply.end()));
  p.step();
  events = p.events();
  check(events.size() == 1 && events[0].kind == IphoneEvent::Kind::Notification &&
            events[0].notification.app == "com.apple.shortcuts" && events[0].notification.message == "Hallo Welt",
        "the answer, split in two, arrives whole");
  requests = p.writesTo(p.ancsHandles[1]);
  check(requests.size() == 2 && requests[1] == ancs::attributesRequest(2), "then the next one is asked for");
  p.now += IphoneLink::kRequestTimeoutMs;
  p.step();
  check(p.events().empty(), "an unanswered question is given up");
  p.radio.notifyPeer(p.ancsHandles[0], notice(0, ancs::kPreExisting, 3));
  p.radio.notifyPeer(p.ancsHandles[0], notice(0, ancs::kSilent, 4));
  p.step();
  check(p.writesTo(p.ancsHandles[1]).size() == 2, "old and silent notifications are not asked for");

  p.radio.notifyPeer(p.amsHandles[1], Bytes{2, 2, 0, 'S', 'o', 'n', 'g'});
  p.radio.notifyPeer(p.amsHandles[1], bytes(std::string("\x00\x01\x00", 3) + "1,1.0,31.683"));
  p.step();
  events = p.events();
  check(!events.empty() && events.back().kind == IphoneEvent::Kind::Music && events.back().music.title == "Song" &&
            events.back().music.playing() && events.back().music.info.elapsed >= 31.68,
        "the player's updates arrive as music");

  p.radio.events.disconnected(7);
  p.step();
  events = p.events();
  check(p.link->status().state == IphoneState::Waiting && !p.link->status().notifications,
        "a phone that leaves is waited for");
  check(!events.empty() && events.back().kind == IphoneEvent::Kind::Music && !events.back().music.playing(),
        "and its music ends");

  p.connect(8, mkaddr("11:22:33:44:55:66", true));
  check(p.link->status().state == IphoneState::Waiting && p.radio.connectCalls == 0, "another phone is not taken");
  p.radio.events.disconnected(8);
  p.step();
  p.connect(9, p.address);
  check(p.link->status().state == IphoneState::Ready, "the remembered one comes back");
  check(p.events().empty(), "and is not remembered twice");

  p.link->forget(p.now);
  p.step();
  check(p.radio.forgotten == std::vector<std::string>{"AC:E4:B5:D6:D0:57"} &&
            std::count(p.radio.disconnected.begin(), p.radio.disconnected.end(), 9) == 1 &&
            p.link->status().state == IphoneState::Waiting,
        "forgetting it removes the bond and drops the link");

  p.enable(false);
  check(p.link->status().state == IphoneState::Off && p.radio.adverts.empty(), "disabled, it stops advertising");
}

// A phone that drops the link while pairing, as the first link does, comes back by itself; a
// device without the notification service is let go.
void linkTraps() {
  Phone p;
  p.enable();
  p.radio.silent = true;
  p.connect(7, mkaddr("5A:11:22:33:44:55", true));
  check(p.link->status().state == IphoneState::Connecting, "a silent phone stays connecting");
  p.radio.silent = false;
  p.radio.events.disconnected(7);
  p.step();
  check(p.link->status().state == IphoneState::Waiting && p.events().empty(), "dropped during setup, nothing is remembered");
  p.connect(8, p.address);
  check(p.link->status().state == IphoneState::Ready, "the reconnect with its identity address works");

  Phone other;
  other.radio.peerDb = GattServer();
  other.enable();
  other.connect(7, mkaddr("66:55:44:33:22:11"));
  check(other.link->status().state == IphoneState::Waiting && other.radio.disconnected.empty(),
        "a device without notifications is let go, its link left alone");
}

// ---- payloads -------------------------------------------------------------------------------

Panel tall() {
  Panel p;
  p.width = 52;
  p.height = 16;
  p.layouts = true;
  p.appFont = true;
  return p;
}

struct Region {
  std::string body;
  int x = 0, y = 0, width = 0, height = 0;
  std::string text(const char* key) const {
    std::string value;
    api::memberValue(api::JsonReader(body), key).appendString(value);
    return value;
  }
};

std::map<std::string, Region> regions(const std::string& body, const Panel& panel) {
  auto list = api::memberValue(api::memberValue(api::JsonReader(body), "layout"), "regions");
  awtrix::test::require(list.enterArray(), "layout has regions");
  std::map<std::string, Region> out;
  while (list.nextElement()) {
    Region region;
    region.body = list.valueText();
    auto box = api::memberValue(list, "box");
    awtrix::test::require(box.enterArray(), "region has a box");
    for (int* value : {&region.x, &region.y, &region.width, &region.height}) {
      long long number = 0;
      awtrix::test::require(box.nextElement() && box.asLong(number) && box.skipValue(), "box has integer dimensions");
      *value = static_cast<int>(number);
    }
    check(region.x >= 0 && region.y >= 0 && region.width > 0 && region.height > 0 &&
              region.x + region.width <= panel.width && region.y + region.height <= panel.height,
          "regions remain inside the panel");
    const std::string id = region.text("id");
    check(!id.empty() && out.emplace(id, region).second, "regions have unique identities");
    awtrix::test::require(list.skipValue(), "region is valid JSON");
  }
  return out;
}

void payloads() {
  check(content("Anna", "Hi") == "Anna: Hi", "title and message");
  check(content("Anna", " ") == "Anna" && content("", "Hi") == "Hi" && content("", "").empty(),
        "whichever of the two is there");
  check(content("A", "one\ntwo") == "A: one two", "line breaks read as spaces");
  check(clip("\xc3\xa4\xc3\xb6\xc3\xbc", 2) == "\xc3\xa4\xc3\xb6" && clip("abc", 5) == "abc", "clip counts characters");

  const Panel shortPanel;
  std::string n = notification(shortPanel, "WhatsApp", "Anna: Hi", "wa");
  check(has(n, "\"durationMs\":8000,\"repeat\":1,\"stack\":true,\"wakeup\":false"), "shown once for 8 s, stacked");
  check(has(n, "\"text\":\"Anna: Hi\"") && has(n, "\"icon\":\"wa\"") &&
            !has(n, "layout"),
        "a short panel shows notification text and its icon");
  n = notification(shortPanel, "WhatsApp", "Anna: Hi", "");
  check(has(n, "\"text\":\"WhatsApp \xc2\xb7 Anna: Hi\"") && !has(n, "icon"), "without an icon the app is named in the text");
  check(has(notification(shortPanel, "WhatsApp", "", "wa"), "\"text\":\"WhatsApp\""),
        "without content the app's name is the text");
  check(has(notification(shortPanel, "A", std::string(300, 'x'), "i"), std::string(240, 'x') + "\""),
        "the text is cut at 240 characters");

  n = notification(tall(), "WhatsApp", "Anna: Hi", "wa");
  const auto notice = regions(n, tall());
  const auto& icon = notice.at("icon");
  const auto& app = notice.at("app");
  const auto& message = notice.at("message");
  check(icon.text("icon") == "wa" && app.text("text") == "WhatsApp" && message.text("text") == "Anna: Hi",
        "icon, app and message retain their content");
  check(icon.x + icon.width <= app.x && icon.x + icon.width <= message.x && app.y + app.height <= message.y,
        "app above message and both beside the icon");
  check(!app.text("font").empty(), "a supported compact font is selected");
  Panel noFont = tall();
  noFont.appFont = false;
  check(!has(notification(noFont, "W", "x", ""), "font"), "no font named that the panel lacks");
  n = notification(tall(), "WhatsApp", "", "");
  const auto lone = regions(n, tall());
  const auto& only = lone.at("message");
  check(lone.count("app") == 0 && only.text("text") == "WhatsApp", "without content the app name is shown once");
  check(std::abs(only.y - (tall().height - only.y - only.height)) <= 1, "lone message is vertically centered");

  Track t;
  t.title = "Song";
  t.artist = "Band";
  t.durationMs = 200000;
  t.positionMs = 50000;
  check(t.progress() == 25 && barLeds(t, barWidth(tall())) == barWidth(tall()) / 4 &&
            barWidth(tall()) < tall().width && barWidth(shortPanel) == shortPanel.width,
        "the bar beside the cover lights a quarter");
  std::string m = music(shortPanel, t, "cov");
  check(has(m, "\"lifetimeMs\":65000") && has(m, "\"text\":\"Song\"") && has(m, "\"text\":\" \xc2\xb7 Band\"") &&
            has(m, "\"icon\":\"cov\"") && has(m, "\"progress\":25"),
        "a short panel shows title, artist, cover and progress");
  m = music(tall(), t, "cov");
  const auto song = regions(m, tall());
  const auto& cover = song.at("cover");
  const auto& title = song.at("title");
  const auto& artist = song.at("artist");
  const auto& progress = song.at("progress");
  check(cover.text("icon") == "cov" && title.text("text") == "Song" && artist.text("text") == "Band",
        "cover, title and artist retain their content");
  check(cover.x + cover.width <= title.x && title.y + title.height <= artist.y &&
            artist.y + artist.height <= progress.y, "music rows and progress bar do not overlap");
  long long percent = -1;
  check(api::memberValue(api::JsonReader(progress.body), "progress").asLong(percent) && percent == t.progress(),
        "bar reflects track progress");
  t.artist.clear();
  t.durationMs = 0;
  m = music(tall(), t, "cov");
  const auto bare = regions(m, tall());
  const auto& bareTitle = bare.at("title");
  check(bare.count("artist") == 0 && bare.count("progress") == 0 && bareTitle.text("text") == "Song",
        "missing artist and duration omit their rows");
  check(std::abs(bareTitle.y - (tall().height - bareTitle.y - bareTitle.height)) <= 1,
        "title alone is vertically centered");
}

void covers() {
  check(coverSearchUrl("Roger Cicero", "Wenn ich dich los w\xc3\xa4r (Live)") ==
            "https://itunes.apple.com/search?term=Roger%20Cicero%20Wenn%20ich%20dich%20los%20w%C3%A4r%20%28Live%29"
            "&entity=song&limit=1",
        "the search names artist and title, encoded");
  const std::string found = R"({"resultCount":1,"results":[{"artworkUrl100":"https://is1-ssl.mzstatic.com/image/thumb/Music/x/100x100bb.jpg"}]})";
  check(coverImageUrl(found, 16) == "https://is1-ssl.mzstatic.com/image/thumb/Music/x/16x16bb.jpg" &&
            coverImageUrl(found, 8) == "https://is1-ssl.mzstatic.com/image/thumb/Music/x/8x8bb.jpg",
        "the cover in the icon's size");
  check(coverImageUrl(R"({"resultCount":0,"results":[]})", 16).empty() &&
            coverImageUrl(R"({"results":[{"artworkUrl100":"http://x/100x100bb.jpg"}]})", 16).empty(),
        "no result, or no HTTPS cover, is no cover");
  CoverCache cache;
  cache.put(CoverCache::key("a", "t0", 16), "");
  for (int i = 1; i <= 16; ++i) cache.put(CoverCache::key("a", "t" + std::to_string(i), 16), "url" + std::to_string(i));
  std::string url;
  check(!cache.find(CoverCache::key("a", "t0", 16), url), "the oldest of seventeen is forgotten");
  check(cache.find(CoverCache::key("a", "t1", 16), url) && url == "url1", "the others are kept");
  check(!cache.find(CoverCache::key("a", "t1", 8), url), "per size");
}

// ---- the main loop's side --------------------------------------------------------------------

struct FakeControl : IphoneControl {
  IphoneStatus status;
  std::deque<IphoneEvent> events;
  std::vector<IphoneConfig> configs;
  int forgets = 0;
  IphoneStatus iphoneStatus() const override { return status; }
  std::deque<IphoneEvent> takeIphoneEvents() override {
    std::deque<IphoneEvent> out;
    out.swap(events);
    return out;
  }
  void configureIphone(const IphoneConfig& config) override { configs.push_back(config); }
  void forgetIphone() override { ++forgets; }
};

struct FakeDisplay : INotifyService, IAppService {
  std::vector<std::string> notes, pushes, switches, deletes;
  DispatchResult notify(const std::string& json, uint8_t, DispatchDetail&) override {
    notes.push_back(json);
    return DispatchResult::Ok;
  }
  void dismiss() override {}
  bool dismissNamed(const std::string&) override { return false; }
  DispatchResult setPushedApp(const std::string& name, const std::string& json, DispatchDetail&) override {
    pushes.push_back(name + " " + json);
    return DispatchResult::Ok;
  }
  void deletePushedApp(const std::string& name) override { deletes.push_back(name); }
  bool setAppOrder(const std::string&) override { return true; }
  void setAppEnabled(const std::string&, bool) override {}
  DispatchResult switchApp(const std::string& json, DispatchDetail&) override {
    switches.push_back(json);
    return DispatchResult::Ok;
  }
  void nextApp() override {}
  void previousApp() override {}
};

struct FakeCovers : CoverSource {
  std::vector<std::string> asked;
  bool busy = false, done = false;
  std::string url;
  bool lookup(const std::string& artist, const std::string& title, int size) override {
    if (busy) return false;
    busy = true;
    asked.push_back(artist + "|" + title + "|" + std::to_string(size));
    return true;
  }
  bool result(std::string& out) override {
    if (!busy || !done) return false;
    busy = done = false;
    out = url;
    return true;
  }
};

struct Bridge {
  char directory[32] = "/tmp/awtrix-iphone-XXXXXX";
  std::string path;
  FakeControl control;
  FakeDisplay display;
  FakeCovers coverSource;
  long long unixSeconds = 1790700000;
  std::unique_ptr<IphoneBridge> bridge;
  int64_t now = 1000;
  Bridge() {
    check(::mkdtemp(directory) != nullptr, "bridge fixture gets a directory");
    path = std::string(directory) + "/iphone.json";
    fresh();
  }
  ~Bridge() {
    bridge.reset();
    ::unlink(path.c_str());
    ::rmdir(directory);
  }
  void fresh() {
    IphoneBridge::Options o;
    o.panel = tall();
    o.path = path;
    o.unixTime = [this] { return unixSeconds; };
    bridge = std::make_unique<IphoneBridge>(control, display, display, &coverSource, std::move(o));
  }
  int call(const char* method, const char* route, const std::string& request, std::string& body) {
    return bridge->handle(method, route, request, body);
  }
  int put(const std::string& request, std::string& body) { return call("PUT", "/api/v1/iphone", request, body); }
  void tick(int64_t by = 0) {
    now += by;
    bridge->tick(now);
  }
  void notification(const std::string& app, const std::string& title, const std::string& message) {
    IphoneEvent e;
    e.kind = IphoneEvent::Kind::Notification;
    e.notification.app = app;
    e.notification.title = title;
    e.notification.message = message;
    control.events.push_back(e);
    tick();
  }
  void playing(const std::string& player, const std::string& title, int state, double elapsed, double duration = 200) {
    IphoneEvent e;
    e.kind = IphoneEvent::Kind::Music;
    e.music.player = player;
    e.music.title = title;
    e.music.artist = "Band";
    e.music.duration = duration;
    e.music.info.state = state;
    e.music.info.rate = 1;
    e.music.info.elapsed = elapsed;
    control.events.push_back(e);
    tick();
  }
};

std::string field(const std::string& body) {
  std::string out;
  api::memberValue(api::memberValue(api::JsonReader(body), "error"), "field").appendString(out);
  return out;
}

void settingsApi() {
  Bridge b;
  check(b.control.configs.size() == 1 && !b.control.configs[0].enabled, "the link is told it is off at start");
  std::string body;
  check(b.call("GET", "/api/v1/iphone", "", body) == 200, "GET answers");
  check(has(body, "\"enabled\":false,\"state\":\"off\",\"phone\":null,\"notifications\":false,\"music\":false") &&
            has(body, "\"settings\":{\"music\":true,\"covers\":true,\"apps\":[]}") && has(body, "\"seen\":[]"),
        "off by default, music and covers on, no apps");
  check(b.call("GET", "/api/v1/other", "", body) == 0, "other paths are not the iPhone's");
  check(b.call("POST", "/api/v1/iphone", "{}", body) == 405 && has(body, "allowed: GET, PUT"), "only GET and PUT");
  check(b.call("GET", "/api/v1/iphone/phone", "", body) == 405, "the phone only goes by DELETE");

  check(b.put(R"({"enabled":true})", body) == 200 && has(body, "\"enabled\":true"), "a partial PUT switches it on");
  check(b.control.configs.size() == 2 && b.control.configs[1].enabled, "and tells the link");
  check(b.put(R"({"covers":false,"apps":[{"id":"net.whatsapp.WhatsApp","name":"WhatsApp"}]})", body) == 200,
        "apps are replaced");
  check(has(body, "\"enabled\":true") && has(body, "\"covers\":false") &&
            has(body, "{\"id\":\"net.whatsapp.WhatsApp\",\"name\":\"WhatsApp\"}"),
        "keeping what the PUT left out");
  check(b.control.configs.size() == 2, "the link hears only of a switch");

  const std::string icon = "data:image/gif;base64," + std::string(5980, 'A');
  const struct {
    std::string request;
    const char* field;
  } invalid[] = {
      {R"({"enabled":"yes"})", "enabled"},
      {R"({"music":1})", "music"},
      {R"({"apps":{}})", "apps"},
      {R"({"apps":[1]})", "apps[0]"},
      {R"({"apps":[{"id":"","name":"A"}]})", "apps[0].id"},
      {R"({"apps":[{"id":")" + std::string(129, 'a') + R"(","name":"A"}]})", "apps[0].id"},
      {R"({"apps":[{"id":"a","name":")" + std::string(65, 'n') + R"("}]})", "apps[0].name"},
      {R"({"apps":[{"id":"a"}]})", "apps[0].name"},
      {R"({"apps":[{"id":"a","name":"A","icon":"data:image/png;base64,AAAA"}]})", "apps[0].icon"},
      {R"({"apps":[{"id":"a","name":"A","icon":")" + icon + R"("}]})", "apps[0].icon"},
      {R"({"apps":[{"id":"a","name":"A"},{"id":"a","name":"B"}]})", "apps[1].id"},
  };
  for (const auto& c : invalid)
    check(b.put(c.request, body) == 422 && has(body, "\"validationFailed\"") && field(body) == c.field,
          "422 for " + c.request.substr(0, 60));
  std::string many = R"({"apps":[)";
  for (int i = 0; i < 33; ++i) many += std::string(i ? "," : "") + R"({"id":"a)" + std::to_string(i) + R"(","name":"A"})";
  check(b.put(many + "]}", body) == 422 && has(body, "at most 32 apps"), "33 apps are too many");
  check(b.put(many.substr(0, many.rfind(",{")) + "]}", body) == 200, "32 are fine");
  check(b.put("{", body) == 400 && has(body, "invalidJson"), "broken JSON is a 400");
  check(b.put(R"({"apps":[{"id":"net.whatsapp.WhatsApp","name":"WhatsApp","icon":"wa"}]})", body) == 200,
        "a rule with an icon name");
  check(b.call("GET", "/api/v1/iphone", "", body) == 200 && has(body, "\"icon\":\"wa\"}"),
        "and reads back as stored");

  IphoneEvent phone;
  phone.kind = IphoneEvent::Kind::Phone;
  Address::parse("AC:E4:B5:D6:D0:57", false, phone.phone);
  phone.name = "iPhone von Stephan";
  b.control.events.push_back(phone);
  b.tick();
  b.notification("com.apple.shortcuts", "Neuer Kurzbefehl", "Hallo Welt");
  b.bridge->flush();
  b.fresh();
  check(b.control.configs.back().enabled && b.control.configs.back().havePhone &&
            b.control.configs.back().phone.str() == "AC:E4:B5:D6:D0:57",
        "after a restart the link is told the phone and that it is on");
  check(b.call("GET", "/api/v1/iphone", "", body) == 200 &&
            has(body, "\"phone\":{\"addr\":\"AC:E4:B5:D6:D0:57\",\"name\":\"iPhone von Stephan\"}") &&
            has(body, "\"seen\":[{\"id\":\"com.apple.shortcuts\",\"title\":\"Neuer Kurzbefehl\",\"at\":1790700000}]") &&
            has(body, "\"icon\":\"wa\""),
        "phone, apps and seen apps are read back");
  check(b.call("DELETE", "/api/v1/iphone/phone", "", body) == 200 && has(body, "\"phone\":null") && b.control.forgets == 1,
        "DELETE forgets the phone");
  b.fresh();
  check(!b.control.configs.back().havePhone, "for good");
}

void notifications() {
  Bridge b;
  std::string body;
  b.put(R"({"enabled":true,"apps":[{"id":"net.whatsapp.WhatsApp","name":"WhatsApp","icon":"wa"}]})",
        body);
  b.notification("com.apple.mobilemail", "Invoice", "Due");
  check(b.display.notes.empty(), "an app without a rule is not shown");
  b.notification("net.whatsapp.WhatsApp", "Anna", "Hi");
  check(b.display.notes.size() == 1 && has(b.display.notes[0], "\"durationMs\":8000") &&
            has(b.display.notes[0], "\"text\":\"WhatsApp\"") && has(b.display.notes[0], "\"text\":\"Anna: Hi\"") &&
            has(b.display.notes[0], "\"icon\":\"wa\""),
        "a listed app is shown with its rule");
  b.notification("net.whatsapp.WhatsApp", "Anna", "Hi");
  check(b.display.notes.size() == 1, "the same notification again at once is dropped");
  b.now += IphoneBridge::kRepeatMs;
  b.notification("net.whatsapp.WhatsApp", "Anna", "Hi");
  check(b.display.notes.size() == 2, "two seconds later it is shown");
  b.notification("net.whatsapp.WhatsApp", "Anna", "Bye");
  check(b.display.notes.size() == 3, "other content at once is shown too");
  b.call("GET", "/api/v1/iphone", "", body);
  check(has(body, "\"seen\":[{\"id\":\"net.whatsapp.WhatsApp\",\"title\":\"Anna\"") && has(body, "com.apple.mobilemail"),
        "seen lists both, newest first");
  for (int i = 0; i < 25; ++i) b.notification("app." + std::to_string(i), std::string(60, 't'), "");
  check(b.bridge->settings().seen.size() == Settings::kMaxSeen && b.bridge->settings().seen[0].id == "app.24" &&
            b.bridge->settings().seen[0].title.size() == 48,
        "at most 20 seen apps, titles cut to 48 characters");
}

void nowPlaying() {
  Bridge b;
  std::string body;
  b.put(R"({"enabled":true,"covers":false,"apps":[{"id":"com.spotify.client","name":"Spotify","icon":"spot"}]})", body);
  b.playing("Spotify", "Song", 1, 0);
  check(b.display.pushes.empty(), "a new track settles first");
  b.tick(IphoneBridge::kSettleMs);
  check(b.display.pushes.size() == 1 && has(b.display.pushes[0], "nowplaying ") && has(b.display.pushes[0], "\"icon\":\"spot\"") &&
            b.display.switches == std::vector<std::string>{"{\"name\":\"nowplaying\"}"},
        "then it is pushed with the player's app icon and switched to");
  b.tick(100);
  check(b.display.pushes.size() == 1, "nothing new, nothing pushed");
  // 34 LEDs over 200 s: the first lights at 3 % = 6 s.
  b.tick(6000);
  check(b.display.pushes.size() == 2 && has(b.display.pushes[1], "\"progress\":3") && b.display.switches.size() == 1,
        "the next LED of the bar re-pushes it, without switching");
  b.playing("Spotify", "Song", 1, 6.5, 0);
  b.tick(IphoneBridge::kSettleMs);
  const std::size_t pushes = b.display.pushes.size();
  b.tick(IphoneBridge::kRefreshMs - 1);
  check(b.display.pushes.size() == pushes, "without a bar nothing is due for a while");
  b.tick(1);
  check(b.display.pushes.size() == pushes + 1, "but it is refreshed every 20 s");
  b.playing("Spotify", "Song", 0, 30);
  b.tick(IphoneBridge::kSettleMs);
  b.tick(IphoneBridge::kGraceMs - 1);
  check(b.display.deletes.empty(), "paused, it stays a moment");
  b.tick(1);
  check(b.display.deletes == std::vector<std::string>{"nowplaying"}, "then it goes");
  b.playing("Other", "Song", 1, 0);
  b.tick(IphoneBridge::kSettleMs);
  check(has(b.display.pushes.back(), IphoneBridge::noteIcon(16)) && b.display.switches.size() == 2,
        "a player without an app rule shows a note, and is switched to again");
  b.put(R"({"music":false})", body);
  b.tick();
  check(b.display.deletes.size() == 2, "switched off, the app goes at once");
}

void coverLookups() {
  Bridge b;
  std::string body;
  b.put(R"({"enabled":true})", body);
  b.playing("Musik", "Song", 1, 0);
  b.tick(IphoneBridge::kSettleMs);
  check(b.coverSource.asked == std::vector<std::string>{"Band|Song|16"}, "the cover is looked up in the icon's size");
  check(b.display.pushes.empty(), "a new track waits for it a moment");
  b.coverSource.url = "https://is1-ssl.mzstatic.com/image/thumb/Music/x/16x16bb.jpg";
  b.coverSource.done = true;
  b.tick(100);
  check(b.display.pushes.size() == 1 &&
            has(b.display.pushes[0], "\"icon\":\"https://is1-ssl.mzstatic.com/image/thumb/Music/x/16x16bb.jpg\""),
        "and shows its picture once it is there");
  b.playing("Musik", "Other", 1, 0);
  b.tick(IphoneBridge::kSettleMs);
  b.tick(IphoneBridge::kCoverWaitMs);
  check(b.display.pushes.size() == 2 && has(b.display.pushes[1], IphoneBridge::noteIcon(16)),
        "a cover that takes too long is not waited for");
  b.coverSource.url = "";
  b.coverSource.done = true;
  b.tick(100);
  b.playing("Musik", "Song", 1, 0);
  b.tick(IphoneBridge::kSettleMs);
  check(b.coverSource.asked.size() == 2 && has(b.display.pushes.back(), "16x16bb.jpg"), "a cover found is not looked up again");
  b.playing("Musik", "Other", 1, 0);
  b.tick(IphoneBridge::kSettleMs);
  check(b.coverSource.asked.size() == 2 && has(b.display.pushes.back(), IphoneBridge::noteIcon(16)),
        "nor is one that was not found");
}

}

int main() {
  ancsFormats();
  amsFormats();
  linkComesUp();
  linkTraps();
  payloads();
  covers();
  settingsApi();
  notifications();
  nowPlaying();
  coverLookups();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("iphone: all checks passed\n");
  return failures ? 1 : 0;
}
