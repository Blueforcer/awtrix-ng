#include <unity.h>

#include <string>
#include <vector>

#include "core/mqtt/ButtonEdges.h"
#include "platform/linux/mqtt/KnobEvents.h"

using ButtonEdges = awtrix::ha::ControlEdges<4>;

namespace {

std::string drain(ButtonEdges& edges) {
  std::string out;
  ButtonEdges::Edge e;
  while (edges.pop(e)) out += std::to_string(e.control) + (e.down ? "+" : "-");
  return out;
}

void test_only_the_changed_control_is_reported() {
  ButtonEdges edges;
  edges.observe({false, true, false, false});
  edges.observe({false, false, false, false});
  TEST_ASSERT_EQUAL_STRING("1+1-", drain(edges).c_str());
  edges.observe({false, false, false, false});
  TEST_ASSERT_EQUAL_STRING("", drain(edges).c_str());
}

void test_a_press_and_release_between_two_drains_both_arrive_in_order() {
  ButtonEdges edges;
  edges.observe({true, false, false, false});
  edges.observe({true, false, true, false});
  edges.observe({false, false, true, false});
  edges.observe({false, false, false, true});
  TEST_ASSERT_EQUAL_STRING("0+2+0-2-3+", drain(edges).c_str());
}

void test_resync_reports_the_current_state_of_the_announced_controls() {
  ButtonEdges edges;
  edges.observe({true, false, false, false});
  edges.resync({true, false, false, false}, 3);
  TEST_ASSERT_EQUAL_STRING("0+1-2-", drain(edges).c_str());
  edges.resync({false, false, false, true}, 4);
  TEST_ASSERT_EQUAL_STRING("0-1-2-3+", drain(edges).c_str());
  edges.observe({false, false, false, false});
  TEST_ASSERT_EQUAL_STRING("3-", drain(edges).c_str());
}

void test_a_full_buffer_drops_new_edges_but_keeps_tracking() {
  ButtonEdges edges;
  for (std::size_t i = 0; i < ButtonEdges::kCapacity + 6; ++i)
    edges.observe({i % 2 == 0, false, false, false});
  std::size_t count = 0;
  ButtonEdges::Edge e;
  while (edges.pop(e)) {
    TEST_ASSERT_EQUAL(count % 2 == 0, e.down);
    ++count;
  }
  TEST_ASSERT_EQUAL_size_t(ButtonEdges::kCapacity, count);
  edges.observe({false, true, false, false});
  TEST_ASSERT_EQUAL_STRING("1+", drain(edges).c_str());
}

void test_knob_messages_preserve_edges_and_wrap_and_resync_on_connect() {
  awtrix::ha::KnobEvents events;
  awtrix::net::LinkStatus link;
  std::vector<std::string> messages;
  const auto publish = [&](const char* topic, const std::string& body, bool retained) {
    messages.push_back(std::string(topic) + ":" + body + (retained ? ":retained" : ""));
  };
  events.observe(true);
  events.tick(link, true, 7, publish);
  TEST_ASSERT_TRUE(messages.empty());

  link.phase = awtrix::net::LinkPhase::Connected;
  ++link.connects;
  events.tick(link, true, 7, publish);
  TEST_ASSERT_EQUAL_size_t(2, messages.size());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob::retained", messages[0].c_str());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob:1", messages[1].c_str());
  messages.clear();
  events.observe(false);
  events.observe(true);
  events.observe(false);
  events.tick(link, false, 4, publish);
  TEST_ASSERT_EQUAL_size_t(4, messages.size());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob:0", messages[0].c_str());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob:1", messages[1].c_str());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob:0", messages[2].c_str());
  TEST_ASSERT_EQUAL_STRING("event/knob:{\"turn\":-3}", messages[3].c_str());

  messages.clear();
  link.phase = awtrix::net::LinkPhase::Offline;
  events.observe(true);
  events.tick(link, true, UINT32_MAX, publish);
  TEST_ASSERT_TRUE(messages.empty());
  link.phase = awtrix::net::LinkPhase::Connected;
  ++link.connects;
  events.tick(link, false, UINT32_MAX, publish);
  TEST_ASSERT_EQUAL_size_t(2, messages.size());
  TEST_ASSERT_EQUAL_STRING("state/buttons/knob:0", messages[1].c_str());
  messages.clear();
  events.tick(link, false, 0, publish);
  TEST_ASSERT_EQUAL_size_t(1, messages.size());
  TEST_ASSERT_EQUAL_STRING("event/knob:{\"turn\":1}", messages[0].c_str());
  messages.clear();
  events.tick(link, false, 0, publish);
  TEST_ASSERT_TRUE(messages.empty());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_the_changed_control_is_reported);
  RUN_TEST(test_a_press_and_release_between_two_drains_both_arrive_in_order);
  RUN_TEST(test_resync_reports_the_current_state_of_the_announced_controls);
  RUN_TEST(test_a_full_buffer_drops_new_edges_but_keeps_tracking);
  RUN_TEST(test_knob_messages_preserve_edges_and_wrap_and_resync_on_connect);
  return UNITY_END();
}
