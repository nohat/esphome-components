// Production and standard headers must come first: CppUTest's leak detector
// redefines `new`, which breaks libc++'s placement-new usage if it wins.
#include "components/wiz_client/wiz_client.h"
#include <string>
#include <vector>

#include "CppUTest/TestHarness.h"

// Definitions for the mock globals declared in ESP8266WiFi.h.
uint32_t mock_millis_value = 0;
MockWiFiClass WiFi;

// A test subclass to expose internal behavior
class TestWizClient : public wiz_client::WizClient {
 public:
  std::string last_sent;

  // Capture the payload, but let the real send_udp run so that the
  // resolved-only filtering and per-target addressing are exercised.
  void send_udp(const char* data, size_t len) override {
    last_sent.assign(data, len);
    wiz_client::WizClient::send_udp(data, len);
  }

  int get_udp_port() { return udp_.get_port(); }
  const std::vector<WiFiUDP::Sent>& sent() { return udp_.sent(); }
  void clear_sent() { udp_.clear_sent(); }
  void inject(const char* from, const std::string& payload) {
    udp_.inject(IPAddress(from), payload);
  }
  size_t resolved() { return resolved_count(); }
};

// A WiZ getPilot reply, trimmed to the shape the component scans for.
static std::string pilot_reply(const char* mac) {
  return std::string("{\"method\":\"getPilot\",\"env\":\"pro\",\"result\":{\"mac\":\"") + mac +
         "\",\"rssi\":-60,\"state\":true,\"dimming\":100}}";
}

TEST_GROUP(WizClientLogic) {
  TestWizClient client;
  void setup() override {
    mock_millis_value = 0;
    WiFi.set_status(WL_CONNECTED);
    WiFi.set_network("192.168.1.106", "255.255.255.0");
    client.last_sent.clear();
    client.clear_targets();
    client.clear_sent();
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
  client.clear_sent();
  client.set_brightness(50);
  CHECK_EQUAL(3, client.sent().size());
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

// --- MAC targets and discovery ---------------------------------------------

TEST(WizClientLogic, AcceptsMacInSeveralForms) {
  CHECK_TRUE(client.add_target("cc:40:85:64:07:b0"));
  CHECK_TRUE(client.add_target("CC-40-85-64-14-B8"));
  CHECK_TRUE(client.add_target("cc40853a9dd6"));
  CHECK_EQUAL(3, client.target_count());
  // Canonical form is bare lowercase hex, which is also the most compact
  // thing to store in the length-limited text entity.
  STRCMP_EQUAL("cc40856407b0,cc40856414b8,cc40853a9dd6", client.get_targets().c_str());
}

TEST(WizClientLogic, MacFormsDeduplicateAgainstEachOther) {
  CHECK_TRUE(client.add_target("cc:40:85:64:07:b0"));
  CHECK_FALSE(client.add_target("cc40856407b0"));
  CHECK_EQUAL(1, client.target_count());
}

TEST(WizClientLogic, DottedQuadIsNeverReadAsMac) {
  // 123.123.123.123 is 12 hex digits once the dots are stripped, so the IPv4
  // form has to be tried first.
  CHECK_TRUE(client.add_target("123.123.123.123"));
  STRCMP_EQUAL("123.123.123.123", client.get_targets().c_str());
}

TEST(WizClientLogic, RejectsShortAndLongMacs) {
  CHECK_FALSE(client.add_target("cc:40:85:64:07"));
  CHECK_FALSE(client.add_target("cc40856407b0aa"));
  CHECK_FALSE(client.add_target("gg:40:85:64:07:b0"));
  CHECK_EQUAL(0, client.target_count());
}

TEST(WizClientLogic, MacTargetIsUnresolvedUntilABulbAnswers) {
  client.set_targets("cc40856407b0");
  CHECK_EQUAL(0, client.resolved());
  STRCMP_EQUAL("cc40856407b0=?", client.get_status().c_str());

  // An unresolved target must not be sent to.
  client.clear_sent();
  client.set_brightness(50);
  CHECK_EQUAL(0, client.sent().size());
}

TEST(WizClientLogic, DiscoveryReplyResolvesMacTarget) {
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.169", pilot_reply("cc40856407b0"));
  client.loop();

  CHECK_EQUAL(1, client.resolved());
  STRCMP_EQUAL("cc40856407b0=192.168.1.169", client.get_status().c_str());

  client.clear_sent();
  client.set_brightness(50);
  CHECK_EQUAL(1, client.sent().size());
  STRCMP_EQUAL("192.168.1.169", client.sent()[0].to.toString().c_str());
}

TEST(WizClientLogic, IgnoresRepliesFromBulbsWeDoNotTarget) {
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.19", pilot_reply("cc40853a9dd6"));
  client.loop();
  CHECK_EQUAL(0, client.resolved());
}

TEST(WizClientLogic, IgnoresRepliesWithoutAMac) {
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.169",
                "{\"method\":\"setPilot\",\"env\":\"pro\",\"result\":{\"success\":true}}");
  client.loop();
  CHECK_EQUAL(0, client.resolved());
}

TEST(WizClientLogic, ReresolvesWhenABulbChangesAddress) {
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.169", pilot_reply("cc40856407b0"));
  client.loop();
  STRCMP_EQUAL("cc40856407b0=192.168.1.169", client.get_status().c_str());

  // Same bulb, new DHCP lease. This is the failure that killed the CA House
  // bedroom dimmer's fast-dim path when it was pinned to literal IPs.
  client.inject("192.168.1.207", pilot_reply("cc40856407b0"));
  client.loop();
  STRCMP_EQUAL("cc40856407b0=192.168.1.207", client.get_status().c_str());
}

TEST(WizClientLogic, BroadcastsToTheSubnetBroadcastAddress) {
  client.set_targets("cc40856407b0");
  client.clear_sent();
  client.loop();

  CHECK_EQUAL(1, client.sent().size());
  STRCMP_EQUAL("192.168.1.255", client.sent()[0].to.toString().c_str());
  STRCMP_EQUAL("{\"method\":\"getPilot\",\"params\":{}}", client.sent()[0].payload.c_str());
}

TEST(WizClientLogic, DoesNotBroadcastWhenAllTargetsAreLiteralIPs) {
  client.set_targets("192.168.1.169,192.168.1.139");
  client.clear_sent();
  client.loop();
  CHECK_EQUAL(0, client.sent().size());
}

TEST(WizClientLogic, DoesNotBroadcastBeforeTheNetworkIsUp) {
  WiFi.set_status(0);
  client.set_targets("cc40856407b0");
  client.clear_sent();
  client.loop();
  CHECK_EQUAL(0, client.sent().size());

  WiFi.set_status(WL_CONNECTED);
  client.loop();
  CHECK_EQUAL(1, client.sent().size());
}

TEST(WizClientLogic, RetriesQuicklyWhileUnresolvedThenBacksOff) {
  client.set_targets("cc40856407b0");
  client.clear_sent();

  client.loop();
  CHECK_EQUAL(1, client.sent().size());

  // Still unresolved, but inside the retry window: no second probe.
  mock_millis_value += wiz_client::DISCOVERY_RETRY_MS - 1;
  client.loop();
  CHECK_EQUAL(1, client.sent().size());

  // Retry window elapsed.
  mock_millis_value += 2;
  client.loop();
  CHECK_EQUAL(2, client.sent().size());

  // Once resolved it falls back to the long interval.
  client.inject("192.168.1.169", pilot_reply("cc40856407b0"));
  client.loop();
  client.clear_sent();
  mock_millis_value += wiz_client::DISCOVERY_RETRY_MS * 10;
  client.loop();
  CHECK_EQUAL(0, client.sent().size());
}

TEST(WizClientLogic, RediscoversOnTheConfiguredInterval) {
  client.set_discovery_interval(60000);
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.169", pilot_reply("cc40856407b0"));
  client.loop();
  client.clear_sent();

  mock_millis_value += 59000;
  client.loop();
  CHECK_EQUAL(0, client.sent().size());

  mock_millis_value += 2000;
  client.loop();
  CHECK_EQUAL(1, client.sent().size());
}

TEST(WizClientLogic, DiscoveryCanBeDisabled) {
  client.set_discovery(false);
  client.set_targets("cc40856407b0");
  client.clear_sent();
  client.loop();
  CHECK_EQUAL(0, client.sent().size());
}

TEST(WizClientLogic, EditingTheListKeepsAlreadyResolvedAddresses) {
  client.set_targets("cc40856407b0");
  client.inject("192.168.1.169", pilot_reply("cc40856407b0"));
  client.loop();

  // Adding a second bulb should not cost us the first one's address.
  client.set_targets("cc40856407b0,cc40856414b8");
  STRCMP_EQUAL("cc40856407b0=192.168.1.169,cc40856414b8=?", client.get_status().c_str());
  CHECK_EQUAL(1, client.resolved());
}

TEST(WizClientLogic, MixedIpAndMacTargets) {
  client.set_targets("192.168.1.169,cc40856414b8");
  client.inject("192.168.1.139", pilot_reply("cc40856414b8"));
  client.loop();

  STRCMP_EQUAL("192.168.1.169,cc40856414b8=192.168.1.139", client.get_status().c_str());
  client.clear_sent();
  client.set_brightness(50);
  CHECK_EQUAL(2, client.sent().size());
}

TEST(WizClientLogic, StatusIsCappedForHomeAssistant) {
  std::string list;
  for (int i = 0; i < 20; i++) {
    char mac[16];
    snprintf(mac, sizeof(mac), "cc408564%04x", i);
    if (i) list += ",";
    list += mac;
  }
  client.set_targets(list);
  CHECK_TRUE(client.get_status().size() <= wiz_client::MAX_STATUS_LEN);
}
