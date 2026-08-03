#include <unity.h>

#include <string>
#include <vector>

#include "core/radio/IcyMetadata.h"
#include "core/radio/IcyStream.h"
#include "core/radio/PlaylistParser.h"
#include "core/radio/StationList.h"
#include "core/render/TextEncoding.h"

using namespace awtrix;

namespace {

std::string titleOf(const std::string& block) {
  std::string title;
  TEST_ASSERT_TRUE(radio::parseStreamTitle(block, title));
  return title;
}

void test_parses_a_plain_stream_title() {
  TEST_ASSERT_EQUAL_STRING("Artist - Song",
                           titleOf("StreamTitle='Artist - Song';StreamUrl='';").c_str());
}

void test_title_may_contain_an_apostrophe() {
  TEST_ASSERT_EQUAL_STRING("Rock'n'Roll Hits",
                           titleOf("StreamTitle='Rock'n'Roll Hits';StreamUrl='';").c_str());
}

void test_title_may_contain_a_semicolon() {
  TEST_ASSERT_EQUAL_STRING("A; B; C", titleOf("StreamTitle='A; B; C';").c_str());
}

void test_accepts_a_block_without_a_trailing_url_field() {
  TEST_ASSERT_EQUAL_STRING("Solo", titleOf("StreamTitle='Solo';").c_str());
}

void test_ignores_a_block_with_no_title() {
  std::string title = "untouched";
  TEST_ASSERT_FALSE(radio::parseStreamTitle("StreamUrl='http://x/';", title));
  TEST_ASSERT_EQUAL_STRING("untouched", title.c_str());
}

void test_ignores_garbage_and_empty_blocks() {
  std::string title;
  TEST_ASSERT_FALSE(radio::parseStreamTitle("", title));
  TEST_ASSERT_FALSE(radio::parseStreamTitle(std::string(64, '\0'), title));
  TEST_ASSERT_FALSE(radio::parseStreamTitle("StreamTitle=no quotes here", title));
}

void test_empty_title_parses_as_empty() {
  std::string title = "old";
  TEST_ASSERT_TRUE(radio::parseStreamTitle("StreamTitle='';", title));
  TEST_ASSERT_EQUAL_STRING("", title.c_str());
}

void test_block_length_is_sixteen_byte_units() {
  TEST_ASSERT_EQUAL_INT(0, radio::metadataBlockLength(0));
  TEST_ASSERT_EQUAL_INT(16, radio::metadataBlockLength(1));
  TEST_ASSERT_EQUAL_INT(4080, radio::metadataBlockLength(255));
}

void test_tracker_normalises_to_utf8() {
  radio::TitleTracker tracker;
  TEST_ASSERT_TRUE(tracker.update("StreamTitle='Bj\xF6rk - J\xF3ga';"));
  TEST_ASSERT_TRUE(text::isValidUtf8(tracker.title()));
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk - J\xC3\xB3ga", tracker.title().c_str());

  radio::TitleTracker utf8;
  TEST_ASSERT_TRUE(utf8.update("StreamTitle='Bj\xC3\xB6rk';"));
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", utf8.title().c_str());
}

void test_tracker_reports_only_changes() {
  radio::TitleTracker tracker;
  TEST_ASSERT_TRUE(tracker.update("StreamTitle='One';"));
  TEST_ASSERT_EQUAL_STRING("One", tracker.title().c_str());
  TEST_ASSERT_FALSE(tracker.update("StreamTitle='One';"));
  TEST_ASSERT_TRUE(tracker.update("StreamTitle='Two';"));
  TEST_ASSERT_EQUAL_STRING("Two", tracker.title().c_str());
  TEST_ASSERT_FALSE(tracker.update("StreamUrl='x';"));
  TEST_ASSERT_EQUAL_STRING("Two", tracker.title().c_str());
}

radio::Url urlOf(const std::string& text) {
  radio::Url u;
  TEST_ASSERT_TRUE_MESSAGE(radio::parseUrl(text, u), text.c_str());
  return u;
}

void test_parses_urls() {
  radio::Url u = urlOf("http://stream.example/live");
  TEST_ASSERT_EQUAL_STRING("stream.example", u.host.c_str());
  TEST_ASSERT_EQUAL_STRING("/live", u.path.c_str());
  TEST_ASSERT_EQUAL_INT(80, u.port);
  TEST_ASSERT_FALSE(u.tls);

  u = urlOf("https://a.example:8443/x/y?z=1");
  TEST_ASSERT_EQUAL_INT(8443, u.port);
  TEST_ASSERT_TRUE(u.tls);
  TEST_ASSERT_EQUAL_STRING("/x/y?z=1", u.path.c_str());

  u = urlOf("http://1.2.3.4:8000");
  TEST_ASSERT_EQUAL_STRING("/", u.path.c_str());
  TEST_ASSERT_EQUAL_INT(8000, u.port);
  TEST_ASSERT_EQUAL_INT(443, urlOf("https://a.example/").port);
}

void test_rejects_unusable_urls() {
  radio::Url u;
  TEST_ASSERT_FALSE(radio::parseUrl("ftp://a/", u));
  TEST_ASSERT_FALSE(radio::parseUrl("/relative", u));
  TEST_ASSERT_FALSE(radio::parseUrl("http://", u));
  TEST_ASSERT_FALSE(radio::parseUrl("http://a:0/", u));
  TEST_ASSERT_FALSE(radio::parseUrl("http://a:99999/", u));
  TEST_ASSERT_FALSE(radio::parseUrl("http://a:pop/", u));
}

void test_resolves_redirects() {
  const radio::Url base = urlOf("http://a.example:8000/live");
  TEST_ASSERT_EQUAL_STRING("http://b.example/x",
                           radio::resolveRedirect(base, "http://b.example/x").c_str());
  TEST_ASSERT_EQUAL_STRING("http://a.example:8000/other",
                           radio::resolveRedirect(base, "/other").c_str());
  const radio::Url plain = urlOf("https://c.example/live");
  TEST_ASSERT_EQUAL_STRING("https://c.example/next",
                           radio::resolveRedirect(plain, "next").c_str());
}

void test_parses_an_http_response_head() {
  radio::ResponseHead head;
  TEST_ASSERT_TRUE(radio::parseResponseHead(
      "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nicy-metaint: 16000\r\n"
      "icy-name: Example FM\r\n\r\n",
      head));
  TEST_ASSERT_EQUAL_INT(200, head.status);
  TEST_ASSERT_EQUAL_INT(16000, head.metaInt);
  TEST_ASSERT_EQUAL_STRING("audio/mpeg", head.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("Example FM", head.stationName.c_str());
}

void test_accepts_the_shoutcast_status_line() {
  radio::ResponseHead head;
  TEST_ASSERT_TRUE(radio::parseResponseHead("ICY 200 OK\r\nicy-metaint:8192\r\n\r\n", head));
  TEST_ASSERT_EQUAL_INT(200, head.status);
  TEST_ASSERT_EQUAL_INT(8192, head.metaInt);
}

void test_reads_redirects_and_errors() {
  radio::ResponseHead head;
  TEST_ASSERT_TRUE(radio::parseResponseHead(
      "HTTP/1.1 302 Found\r\nLocation: http://b/x\r\n\r\n", head));
  TEST_ASSERT_EQUAL_INT(302, head.status);
  TEST_ASSERT_EQUAL_STRING("http://b/x", head.location.c_str());

  radio::ResponseHead notFound;
  TEST_ASSERT_TRUE(radio::parseResponseHead("HTTP/1.1 404 Not Found\r\n\r\n", notFound));
  TEST_ASSERT_EQUAL_INT(404, notFound.status);
  TEST_ASSERT_EQUAL_INT(0, notFound.metaInt);

  radio::ResponseHead broken;
  TEST_ASSERT_FALSE(radio::parseResponseHead("", broken));
  TEST_ASSERT_FALSE(radio::parseResponseHead("garbage\r\n\r\n", broken));
}

void test_request_asks_for_metadata() {
  const std::string request = radio::buildRequest(urlOf("http://a.example:8000/live"), "awtrix");
  TEST_ASSERT_TRUE(request.find("GET /live HTTP/1.1") != std::string::npos);
  TEST_ASSERT_TRUE(request.find("Host: a.example:8000") != std::string::npos);
  TEST_ASSERT_TRUE(request.find("Icy-MetaData: 1") != std::string::npos);
  TEST_ASSERT_TRUE(radio::buildRequest(urlOf("http://a.example/x"), "awtrix")
                       .find("Host: a.example\r\n") != std::string::npos);
}

struct Split {
  std::string audio;
  std::vector<std::string> blocks;
};

Split split(radio::MetadataSplitter& splitter, const std::string& chunk) {
  Split out;
  splitter.feed(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size(),
                [&](const uint8_t* d, std::size_t n) {
                  out.audio.append(reinterpret_cast<const char*>(d), n);
                },
                [&](const std::string& b) { out.blocks.push_back(b); });
  return out;
}

std::string metaBlock(const std::string& text) {
  std::string padded = text;
  while (padded.size() % 16) padded.push_back('\0');
  return std::string(1, static_cast<char>(padded.size() / 16)) + padded;
}

void test_splitter_passes_everything_through_without_metadata() {
  radio::MetadataSplitter splitter;
  splitter.reset(0);
  const Split out = split(splitter, "abcdef");
  TEST_ASSERT_EQUAL_STRING("abcdef", out.audio.c_str());
  TEST_ASSERT_EQUAL_size_t(0, out.blocks.size());
}

void test_splitter_extracts_a_block() {
  radio::MetadataSplitter splitter;
  splitter.reset(4);
  const Split out = split(splitter, "AAAA" + metaBlock("StreamTitle='X';") + "BBBB");
  TEST_ASSERT_EQUAL_STRING("AAAABBBB", out.audio.c_str());
  TEST_ASSERT_EQUAL_size_t(1, out.blocks.size());
  TEST_ASSERT_EQUAL_size_t(0, out.blocks[0].find("StreamTitle='X';"));
}

void test_splitter_handles_the_empty_block_between_changes() {
  radio::MetadataSplitter splitter;
  splitter.reset(4);
  std::string stream = "AAAA";
  stream.push_back('\0');
  stream += "BBBB";
  const Split out = split(splitter, stream);
  TEST_ASSERT_EQUAL_STRING("AAAABBBB", out.audio.c_str());
  TEST_ASSERT_EQUAL_size_t(0, out.blocks.size());
}

void test_splitter_survives_chunk_boundaries_anywhere() {
  const std::string stream =
      "AAAA" + metaBlock("StreamTitle='Long enough to straddle';") + "BBBBAAAA";
  radio::MetadataSplitter whole;
  whole.reset(4);
  const Split reference = split(whole, stream);

  radio::MetadataSplitter byByte;
  byByte.reset(4);
  Split accumulated;
  for (char c : stream) {
    const Split piece = split(byByte, std::string(1, c));
    accumulated.audio += piece.audio;
    for (const auto& b : piece.blocks) accumulated.blocks.push_back(b);
  }
  TEST_ASSERT_EQUAL_STRING(reference.audio.c_str(), accumulated.audio.c_str());
  TEST_ASSERT_EQUAL_size_t(reference.blocks.size(), accumulated.blocks.size());
  TEST_ASSERT_EQUAL_STRING(reference.blocks[0].c_str(), accumulated.blocks[0].c_str());
}

std::string resolved(const std::string& body) {
  std::string url;
  TEST_ASSERT_TRUE(radio::parsePlaylist(body, url));
  return url;
}

void test_m3u_with_comments() {
  TEST_ASSERT_EQUAL_STRING(
      "http://stream.example/live",
      resolved("#EXTM3U\n#EXTINF:-1,Station\nhttp://stream.example/live\n").c_str());
}

void test_m3u_bare() {
  TEST_ASSERT_EQUAL_STRING("https://a.example/x",
                           resolved("https://a.example/x\nhttps://b.example/y\n").c_str());
}

void test_pls_file_entries() {
  TEST_ASSERT_EQUAL_STRING(
      "http://stream.example/128",
      resolved("[playlist]\nNumberOfEntries=2\nFile1=http://stream.example/128\n"
               "Title1=Example\nLength1=-1\nFile2=http://stream.example/64\n")
          .c_str());
}

void test_pls_ignores_non_file_keys() {
  std::string url;
  TEST_ASSERT_FALSE(radio::parsePlaylist("[playlist]\nTitle1=http://not.a.stream/\n", url));
}

void test_rejects_empty_and_html() {
  std::string url;
  TEST_ASSERT_FALSE(radio::parsePlaylist("", url));
  TEST_ASSERT_FALSE(radio::parsePlaylist("<html><body>404</body></html>", url));
  TEST_ASSERT_FALSE(radio::parsePlaylist("#EXTM3U\n#EXTINF:-1,Nothing\n", url));
}

void test_does_not_follow_a_playlist_pointing_at_a_playlist() {
  std::string url;
  TEST_ASSERT_FALSE(radio::parsePlaylist("http://example/other.m3u\n", url));
  TEST_ASSERT_EQUAL_STRING(
      "http://example/real", resolved("http://example/other.pls\nhttp://example/real\n").c_str());
}

void test_kind_from_url_looks_at_the_path_only() {
  TEST_ASSERT_TRUE(radio::kindFromUrl("http://x/y.m3u") == radio::PlaylistKind::M3u);
  TEST_ASSERT_TRUE(radio::kindFromUrl("http://x/y.PLS") == radio::PlaylistKind::Pls);
  TEST_ASSERT_TRUE(radio::kindFromUrl("http://x/stream") == radio::PlaylistKind::None);
  TEST_ASSERT_TRUE(radio::kindFromUrl("http://x/live?file=a.m3u") == radio::PlaylistKind::None);
}

std::vector<radio::Station> parsedStations(const std::string& json) {
  std::vector<radio::Station> stations;
  radio::StationError error;
  TEST_ASSERT_TRUE_MESSAGE(radio::parseStations(json, stations, error), error.message.c_str());
  return stations;
}

bool rejectedWith(const std::string& json, const char* field) {
  std::vector<radio::Station> stations;
  radio::StationError error;
  if (radio::parseStations(json, stations, error)) return false;
  return error.field == field;
}

void test_parses_the_canonical_object() {
  const auto stations = parsedStations(
      "{\"stations\":[{\"name\":\"SWR3\",\"url\":\"https://a.example/swr3\"}]}");
  TEST_ASSERT_EQUAL_size_t(1, stations.size());
  TEST_ASSERT_EQUAL_STRING("SWR3", stations[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("https://a.example/swr3", stations[0].url.c_str());
}

void test_accepts_a_bare_array() {
  const auto stations = parsedStations("[{\"name\":\"A\",\"url\":\"http://a/\"}]");
  TEST_ASSERT_EQUAL_size_t(1, stations.size());
}

void test_empty_list_is_valid() {
  TEST_ASSERT_EQUAL_size_t(0, parsedStations("{\"stations\":[]}").size());
}

void test_rejects_bad_entries() {
  TEST_ASSERT_TRUE(rejectedWith("{\"stations\":[{\"name\":\"\",\"url\":\"http://a/\"}]}", "name"));
  TEST_ASSERT_TRUE(rejectedWith("{\"stations\":[{\"name\":\"A\"}]}", "url"));
  TEST_ASSERT_TRUE(rejectedWith("{\"stations\":[{\"name\":\"A\",\"url\":\"ftp://a/\"}]}", "url"));
  TEST_ASSERT_TRUE(rejectedWith("{\"stations\":[\"nope\"]}", "stations"));
  TEST_ASSERT_TRUE(rejectedWith("{\"stations\":{}}", "stations"));
  TEST_ASSERT_TRUE(rejectedWith("not json at all", ""));
}

void test_rejects_an_over_long_name() {
  const std::string tooLong(25, 'x');
  TEST_ASSERT_TRUE(rejectedWith(
      "{\"stations\":[{\"name\":\"" + tooLong + "\",\"url\":\"http://a/\"}]}", "name"));
}

void test_rejects_duplicate_names() {
  TEST_ASSERT_TRUE(rejectedWith(
      "{\"stations\":[{\"name\":\"A\",\"url\":\"http://a/\"},"
      "{\"name\":\"A\",\"url\":\"http://b/\"}]}",
      "name"));
}

void test_rejects_more_than_the_limit() {
  std::string json = "{\"stations\":[";
  for (std::size_t i = 0; i <= radio::kMaxStations; ++i) {
    if (i) json += ',';
    json += "{\"name\":\"s" + std::to_string(i) + "\",\"url\":\"http://a/\"}";
  }
  json += "]}";
  TEST_ASSERT_TRUE(rejectedWith(json, "stations"));
}

void test_a_rejected_list_leaves_the_previous_one_intact() {
  std::vector<radio::Station> stations{{"Keep", "http://keep/"}};
  radio::StationError error;
  TEST_ASSERT_FALSE(radio::parseStations(
      "{\"stations\":[{\"name\":\"New\",\"url\":\"http://new/\"},{\"name\":\"\"}]}", stations,
      error));
  TEST_ASSERT_EQUAL_size_t(1, stations.size());
  TEST_ASSERT_EQUAL_STRING("Keep", stations[0].name.c_str());
}

void test_round_trips_through_json() {
  const std::vector<radio::Station> stations{{"A \"quoted\"", "http://a/"}, {"B", "http://b/"}};
  const auto again = parsedStations(radio::stationsToJson(stations));
  TEST_ASSERT_EQUAL_size_t(2, again.size());
  TEST_ASSERT_EQUAL_STRING("A \"quoted\"", again[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("http://b/", again[1].url.c_str());
}

void test_finds_a_station_by_name() {
  const std::vector<radio::Station> stations{{"A", "http://a/"}, {"B", "http://b/"}};
  TEST_ASSERT_EQUAL_INT(1, radio::indexOfStation(stations, "B"));
  TEST_ASSERT_EQUAL_INT(-1, radio::indexOfStation(stations, "b"));
}

void test_detects_well_formed_utf8() {
  TEST_ASSERT_TRUE(text::isValidUtf8("plain ascii"));
  TEST_ASSERT_TRUE(text::isValidUtf8("Bj\xC3\xB6rk"));
  TEST_ASSERT_FALSE(text::isValidUtf8("Bj\xF6rk"));
  TEST_ASSERT_FALSE(text::isValidUtf8("\xC3"));
  TEST_ASSERT_FALSE(text::isValidUtf8("\x80\x80"));
  TEST_ASSERT_FALSE(text::isValidUtf8("\xC0\xAF"));
}

void test_utf8_titles_pass_through_unchanged() {
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", text::fromStreamBytes("Bj\xC3\xB6rk").c_str());
}

void test_latin1_titles_are_widened_to_utf8() {
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", text::fromStreamBytes("Bj\xF6rk").c_str());
}

void test_every_stream_result_is_valid_utf8() {
  const char* inputs[] = {"plain",     "Bj\xF6rk",           "Bj\xC3\xB6rk", "\xFF\xFE\x80",
                          "mixed \xE4\xC3\xA4", "\x01\x02", ""};
  for (const char* in : inputs)
    TEST_ASSERT_TRUE_MESSAGE(text::isValidUtf8(text::fromStreamBytes(in)), in);
}

void test_control_bytes_are_dropped() {
  TEST_ASSERT_EQUAL_STRING("AB", text::fromStreamBytes("A\x01\x0A" "B").c_str());
  TEST_ASSERT_EQUAL_STRING("AB", text::fromStreamBytes("A\x7F" "B").c_str());
}

void test_empty_stream_text_stays_empty() {
  TEST_ASSERT_EQUAL_STRING("", text::fromStreamBytes("").c_str());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_plain_stream_title);
  RUN_TEST(test_title_may_contain_an_apostrophe);
  RUN_TEST(test_title_may_contain_a_semicolon);
  RUN_TEST(test_accepts_a_block_without_a_trailing_url_field);
  RUN_TEST(test_ignores_a_block_with_no_title);
  RUN_TEST(test_ignores_garbage_and_empty_blocks);
  RUN_TEST(test_empty_title_parses_as_empty);
  RUN_TEST(test_block_length_is_sixteen_byte_units);
  RUN_TEST(test_tracker_normalises_to_utf8);
  RUN_TEST(test_tracker_reports_only_changes);

  RUN_TEST(test_parses_urls);
  RUN_TEST(test_rejects_unusable_urls);
  RUN_TEST(test_resolves_redirects);
  RUN_TEST(test_parses_an_http_response_head);
  RUN_TEST(test_accepts_the_shoutcast_status_line);
  RUN_TEST(test_reads_redirects_and_errors);
  RUN_TEST(test_request_asks_for_metadata);
  RUN_TEST(test_splitter_passes_everything_through_without_metadata);
  RUN_TEST(test_splitter_extracts_a_block);
  RUN_TEST(test_splitter_handles_the_empty_block_between_changes);
  RUN_TEST(test_splitter_survives_chunk_boundaries_anywhere);
  RUN_TEST(test_m3u_with_comments);
  RUN_TEST(test_m3u_bare);
  RUN_TEST(test_pls_file_entries);
  RUN_TEST(test_pls_ignores_non_file_keys);
  RUN_TEST(test_rejects_empty_and_html);
  RUN_TEST(test_does_not_follow_a_playlist_pointing_at_a_playlist);
  RUN_TEST(test_kind_from_url_looks_at_the_path_only);

  RUN_TEST(test_parses_the_canonical_object);
  RUN_TEST(test_accepts_a_bare_array);
  RUN_TEST(test_empty_list_is_valid);
  RUN_TEST(test_rejects_bad_entries);
  RUN_TEST(test_rejects_an_over_long_name);
  RUN_TEST(test_rejects_duplicate_names);
  RUN_TEST(test_rejects_more_than_the_limit);
  RUN_TEST(test_a_rejected_list_leaves_the_previous_one_intact);
  RUN_TEST(test_round_trips_through_json);
  RUN_TEST(test_finds_a_station_by_name);

  RUN_TEST(test_detects_well_formed_utf8);
  RUN_TEST(test_utf8_titles_pass_through_unchanged);
  RUN_TEST(test_latin1_titles_are_widened_to_utf8);
  RUN_TEST(test_every_stream_result_is_valid_utf8);
  RUN_TEST(test_control_bytes_are_dropped);
  RUN_TEST(test_empty_stream_text_stays_empty);
  return UNITY_END();
}
