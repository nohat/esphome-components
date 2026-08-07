// Production and standard headers must come first: CppUTest's leak detector
// redefines `new`, which breaks libc++'s placement-new usage if it wins.
#include "components/wiz_client/wiz_client.h"
#include <string>
#include <vector>

#include "CppUTest/TestHarness.h"

// A test subclass to expose internal behavior
class TestWizClient : public wiz_client::WizClient {
 public:
  std::string last_sent;

  // Override send_udp to capture the sent data instead of sending it over UDP
  void send_udp(const char* data, size_t len) override {
    last_sent.assign(data, len);
    // Still record which targets would have been addressed.
    for (auto& ip : ips_) {
      udp_.beginPacket(ip, 38899);
      udp_.endPacket();
    }
  }

  // Expose the UDP port and the packet log for testing.
  int get_udp_port() { return udp_.get_port(); }
  const std::vector<IPAddress>& packets() { return udp_.packets(); }
  void clear_packets() { udp_.clear_packets(); }
};

TEST_GROUP(WizClientLogic) {
  TestWizClient client;
  void setup() override {
    client.last_sent.clear();
    client.clear_targets();
    client.clear_packets();
  }
};

TEST(WizClientLogic, SetupInitializesUDP) {
  client.setup();
  CHECK_EQUAL(38899, client.get_udp_port());
}

// --- target list -----------------------------------------------------------

TEST(WizClientLogic, AddTargetValid) {
  CHECK_TRUE(client.add_target("192.168.1.42"));
  CHECK_EQUAL(1, client.target_count());
  STRCMP_EQUAL("192.168.1.42", client.get_targets().c_str());
}

TEST(WizClientLogic, AddTargetRejectsMalformed) {
  CHECK_FALSE(client.add_target(""));
  CHECK_FALSE(client.add_target("   "));
  CHECK_FALSE(client.add_target("192.168.1"));
  CHECK_FALSE(client.add_target("192.168.1.256"));
  CHECK_FALSE(client.add_target("cc:40:85:64:07:b0"));  // MAC — not supported yet
  CHECK_FALSE(client.add_target("bathroom-light-1"));
  CHECK_EQUAL(0, client.target_count());
}

TEST(WizClientLogic, AddTargetTrimsWhitespace) {
  CHECK_TRUE(client.add_target("  192.168.1.42\t"));
  STRCMP_EQUAL("192.168.1.42", client.get_targets().c_str());
}

TEST(WizClientLogic, AddTargetDeduplicates) {
  CHECK_TRUE(client.add_target("192.168.1.42"));
  CHECK_FALSE(client.add_target("192.168.1.42"));
  CHECK_EQUAL(1, client.target_count());
}

TEST(WizClientLogic, AddTargetHonoursLimit) {
  for (size_t i = 0; i < wiz_client::MAX_TARGETS; i++) {
    std::string ip = "10.0.0." + std::to_string(i + 1);
    CHECK_TRUE(client.add_target(ip));
  }
  CHECK_EQUAL(wiz_client::MAX_TARGETS, client.target_count());
  CHECK_FALSE(client.add_target("10.0.1.1"));
  CHECK_EQUAL(wiz_client::MAX_TARGETS, client.target_count());
}

TEST(WizClientLogic, AddBulbStillWorks) {
  // The `bulbs:` codegen path funnels through add_bulb().
  client.add_bulb("192.168.1.169");
  client.add_bulb(nullptr);
  client.add_bulb("");
  CHECK_EQUAL(1, client.target_count());
}

TEST(WizClientLogic, SetTargetsCommaSeparated) {
  CHECK_EQUAL(2, client.set_targets("192.168.1.169,192.168.1.139"));
  STRCMP_EQUAL("192.168.1.169,192.168.1.139", client.get_targets().c_str());
}

TEST(WizClientLogic, SetTargetsAcceptsMixedSeparators) {
  CHECK_EQUAL(3, client.set_targets(" 10.0.0.1, 10.0.0.2 ;10.0.0.3\n"));
  STRCMP_EQUAL("10.0.0.1,10.0.0.2,10.0.0.3", client.get_targets().c_str());
}

TEST(WizClientLogic, SetTargetsReplacesRatherThanAppends) {
  client.set_targets("10.0.0.1,10.0.0.2");
  CHECK_EQUAL(1, client.set_targets("10.0.0.9"));
  STRCMP_EQUAL("10.0.0.9", client.get_targets().c_str());
}

TEST(WizClientLogic, SetTargetsSkipsInvalidEntriesButKeepsValidOnes) {
  CHECK_EQUAL(2, client.set_targets("10.0.0.1,not-an-ip,10.0.0.2,999.1.1.1"));
  STRCMP_EQUAL("10.0.0.1,10.0.0.2", client.get_targets().c_str());
}

TEST(WizClientLogic, SetTargetsKeepsPreviousWhenNothingParses) {
  client.set_targets("10.0.0.1,10.0.0.2");
  // A typo should not silently leave the dimmer with no targets at all.
  CHECK_EQUAL(2, client.set_targets("garbage,also-garbage"));
  STRCMP_EQUAL("10.0.0.1,10.0.0.2", client.get_targets().c_str());
}

TEST(WizClientLogic, SetTargetsEmptyStringClearsList) {
  client.set_targets("10.0.0.1,10.0.0.2");
  // An explicitly emptied field means "no targets", not "keep the old ones".
  CHECK_EQUAL(0, client.set_targets(""));
  STRCMP_EQUAL("", client.get_targets().c_str());
}

TEST(WizClientLogic, SendsToEveryTarget) {
  client.set_targets("10.0.0.1,10.0.0.2,10.0.0.3");
  client.clear_packets();
  client.set_brightness(50);
  CHECK_EQUAL(3, client.packets().size());
}

// --- commands --------------------------------------------------------------

TEST(WizClientLogic, SetBrightness) {
  client.set_brightness(50);
  STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":50}}",
               client.last_sent.c_str());
}

TEST(WizClientLogic, SetBrightnessClamped) {
  client.set_brightness(150);
  STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":100}}",
               client.last_sent.c_str());

  // WiZ bulbs treat dimming below 10 as invalid, so the floor is 10, not 0.
  client.set_brightness(-20);
  STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":10}}",
               client.last_sent.c_str());
}

TEST(WizClientLogic, SetPower) {
  client.set_power(true);
  STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"state\":true}}", client.last_sent.c_str());
  client.set_power(false);
  STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"state\":false}}", client.last_sent.c_str());
}

TEST(WizClientLogic, SetColorTemperature) {
  client.set_brightness(80);
  client.set_color_temperature(3000);
  STRCMP_EQUAL(
      "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":3000,\"dimming\":80}}",
      client.last_sent.c_str());
}

TEST(WizClientLogic, SetColorTemperatureClamped) {
  client.set_brightness(80);
  client.set_color_temperature(1000);
  STRCMP_EQUAL(
      "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":1700,\"dimming\":80}}",
      client.last_sent.c_str());

  client.set_color_temperature(7000);
  STRCMP_EQUAL(
      "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":6500,\"dimming\":80}}",
      client.last_sent.c_str());
}
