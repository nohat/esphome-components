#include "CppUTest/TestHarness.h"
#include "components/wiz_client/wiz_client.h"
#include <string>

// A test subclass to expose internal behavior
class TestWizClient : public WizClient {
 public:
  std::string last_sent;

  // Override send_udp to capture the sent data instead of sending it over UDP
  void send_udp(const char* data, size_t len) override {
    last_sent.assign(data, len);
  }

  // Expose the UDP port and the number of bulbs for testing.
  int get_udp_port() { return udp_.get_port(); }
  size_t bulb_count() { return ips_.size(); }
};

TEST_GROUP(WizClient)
{
};

TEST(WizClient, TestCase1)
{
    CHECK(true);
}

TEST(WizClient, TestCase2)
{
    CHECK_EQUAL(1, 1);
}

TEST_GROUP(WizClientBasic)
{
};

TEST(WizClientBasic, BasicTruth)
{
    CHECK(true);
}

TEST(WizClientBasic, BasicEquality)
{
    CHECK_EQUAL(1, 1);
}

TEST_GROUP(WizClientLogic)
{
  TestWizClient client;
  void setup() override {
    client.last_sent.clear();
  }
};

TEST(WizClientLogic, SetupInitializesUDP)
{
    client.setup();
    // Verify that setup initializes the UDP port to 38899
    CHECK_EQUAL(38899, client.get_udp_port());
}

TEST(WizClientLogic, AddBulbValid)
{
    client.add_bulb("192.168.1.42");
    // With a valid IP, bulb should be added.
    CHECK_EQUAL(1, client.bulb_count());
}

TEST(WizClientLogic, AddBulbInvalid)
{
    size_t count_before = client.bulb_count();
    // With an empty IP, fromString returns false in our stub.
    client.add_bulb("");
    // Expect no additional bulb.
    CHECK_EQUAL(count_before, client.bulb_count());
}

TEST(WizClientLogic, SetBrightness)
{
    client.set_brightness(50);
    // Expect JSON message with brightness 50.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"brightness\":50}}", client.last_sent.c_str());
}

TEST(WizClientLogic, SetBrightnessClamped)
{
    client.set_brightness(150);
    // Value clamped to 100.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"brightness\":100}}", client.last_sent.c_str());

    client.set_brightness(-20);
    // Value clamped to 0.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"brightness\":0}}", client.last_sent.c_str());
}

TEST(WizClientLogic, SetColorTemperature)
{
    client.set_brightness(80); // Set brightness first.
    client.set_color_temperature(3000);
    // Expect JSON message with temp 3000 and brightness 80.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":3000,\"brightness\":80}}", client.last_sent.c_str());
}

TEST(WizClientLogic, SetColorTemperatureClamped)
{
    client.set_brightness(80);
    client.set_color_temperature(1000);
    // Temperature clamped to 1700.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":1700,\"brightness\":80}}", client.last_sent.c_str());

    client.set_color_temperature(7000);
    // Temperature clamped to 6500.
    STRCMP_EQUAL("{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":6500,\"brightness\":80}}", client.last_sent.c_str());
}