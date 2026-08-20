#include "components/status_grammar/status_grammar.h"
#include "CppUTest/TestHarness.h"

uint32_t millis() { return mock_millis_value; }

using esphome::status_grammar::HoldDirection;
using esphome::status_grammar::Renderer;

TEST_GROUP(StatusGrammar) {
  Renderer renderer;
  void setup() override { renderer.begin(0); }
};

TEST(StatusGrammar, SearchBreathStaysWithinSpecifiedPeak) {
  renderer.set_connectivity(false, false, 600);
  for (uint32_t t = 600; t < 6000; t += 20) {
    auto frame = renderer.render(t);
    CHECK(frame.normal >= 0.0f);
    CHECK(frame.normal <= 0.3501f);
    DOUBLES_EQUAL(0.0, frame.exception, 0.0001);
  }
}

TEST(StatusGrammar, ReadyIsSteadyIdle) {
  renderer.set_connectivity(true, true, 600);
  DOUBLES_EQUAL(0.10, renderer.render(1200).normal, 0.001);
  DOUBLES_EQUAL(0.10, renderer.render(9200).normal, 0.001);
}

TEST(StatusGrammar, DualBootTestsExceptionThenTurnsItOff) {
  STRCMP_EQUAL("boot.normal_test", renderer.phase_name(100));
  STRCMP_EQUAL("boot.exception_test", renderer.phase_name(400));
  CHECK(renderer.render(355).exception > 0.0f);
  DOUBLES_EQUAL(0.0, renderer.render(600).exception, 0.0001);
}

TEST(StatusGrammar, ConnectivityPhasesAreNamed) {
  renderer.set_connectivity(false, false, 600);
  STRCMP_EQUAL("connectivity.wifi_search", renderer.phase_name(700));
  renderer.set_connectivity(true, false, 800);
  STRCMP_EQUAL("connectivity.api_pending", renderer.phase_name(900));
  renderer.set_connectivity(true, true, 1000);
  STRCMP_EQUAL("cue.completion", renderer.phase_name(1050));
  STRCMP_EQUAL("connectivity.api_ready", renderer.phase_name(1500));
}

TEST(StatusGrammar, LoggerOnlyApiRemainsWifiOnly) {
  renderer.set_connectivity(true, false, 600);
  const float pulse = renderer.render(620).normal;
  const float baseline = renderer.render(1000).normal;
  CHECK(baseline != pulse);
}

TEST(StatusGrammar, VacancyWindowHasSpecifiedBounds) {
  DOUBLES_EQUAL(15000, Renderer::vacancy_window_ms(30000), 0.1);
  DOUBLES_EQUAL(30000, Renderer::vacancy_window_ms(120000), 0.1);
  DOUBLES_EQUAL(60000, Renderer::vacancy_window_ms(900000), 0.1);
}

TEST(StatusGrammar, VacancyCadenceIsMonotonicAndBounded) {
  float previous = Renderer::vacancy_cadence(0.0f);
  DOUBLES_EQUAL(0.12, previous, 0.001);
  for (int i = 1; i <= 100; i++) {
    float current = Renderer::vacancy_cadence(i / 100.0f);
    CHECK(current >= previous);
    CHECK(current <= 0.8001f);
    previous = current;
  }
  DOUBLES_EQUAL(0.80, previous, 0.001);
}

TEST(StatusGrammar, StaleTransactionCompletionIsDiscarded) {
  renderer.set_connectivity(true, true, 600);
  renderer.command_received(1, true, 1000);
  renderer.command_received(2, true, 1200);
  renderer.execution_completed(1, 1300);
  // No stale 75% completion bloom from generation 1.
  CHECK(renderer.render(1330).normal < 0.70f);
}

TEST(StatusGrammar, HoldDirectionReversesImmediately) {
  renderer.hold_started(HoldDirection::INCREASE, 1000);
  const float increasing = renderer.render(1300).normal;
  renderer.hold_started(HoldDirection::DECREASE, 1300);
  const float decreasing = renderer.render(1320).normal;
  CHECK(increasing > decreasing);
}

TEST(StatusGrammar, HoldEndpointsStopCycling) {
  renderer.hold_started(HoldDirection::INCREASE, 1000);
  renderer.hold_limit_reached(HoldDirection::INCREASE);
  DOUBLES_EQUAL(0.65, renderer.render(1500).normal, 0.001);
  DOUBLES_EQUAL(0.65, renderer.render(9000).normal, 0.001);
}

TEST(StatusGrammar, FatalFaultSuppressesNormalChannel) {
  renderer.set_connectivity(true, true, 600);
  renderer.fatal_fault_set(true);
  auto frame = renderer.render(1600);
  DOUBLES_EQUAL(0.0, frame.normal, 0.001);
  CHECK(frame.exception >= 0.30f);
}

TEST(StatusGrammar, ClearingFaultRecomputesLiveBase) {
  renderer.set_connectivity(false, false, 600);
  renderer.fatal_fault_set(true);
  renderer.set_connectivity(true, true, 1000);
  renderer.fatal_fault_set(false);
  DOUBLES_EQUAL(0.10, renderer.render(2000).normal, 0.001);
}

TEST(StatusGrammar, MirrorMapsBothChannelsAtReady) {
  renderer.set_mirror_levels(0.22f, 0.03f, 0.04f, 0.61f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(false);
  auto off = renderer.render(1200);
  DOUBLES_EQUAL(0.22, off.normal, 0.001);
  DOUBLES_EQUAL(0.03, off.exception, 0.001);
  STRCMP_EQUAL("application.mirror_off", renderer.phase_name(1200));

  renderer.set_mirror_state(true);
  auto on = renderer.render(1300);
  DOUBLES_EQUAL(0.04, on.normal, 0.001);
  DOUBLES_EQUAL(0.61, on.exception, 0.001);
  STRCMP_EQUAL("application.mirror_on", renderer.phase_name(1300));
}

TEST(StatusGrammar, ConnectivityOverridesMirrorUntilApiReady) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_mirror_state(true);
  renderer.set_connectivity(false, false, 600);
  DOUBLES_EQUAL(0.0, renderer.render(600).exception, 0.001);
  STRCMP_EQUAL("connectivity.wifi_search", renderer.phase_name(600));
  renderer.set_connectivity(true, false, 700);
  DOUBLES_EQUAL(0.0, renderer.render(800).exception, 0.001);
  STRCMP_EQUAL("connectivity.api_pending", renderer.phase_name(800));
}

TEST(StatusGrammar, ClearingFaultRecomputesCurrentMirrorState) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(false);
  renderer.fatal_fault_set(true);
  renderer.set_mirror_state(true);
  renderer.fatal_fault_set(false);
  auto frame = renderer.render(1500);
  DOUBLES_EQUAL(0.0, frame.normal, 0.001);
  DOUBLES_EQUAL(0.60, frame.exception, 0.001);
}

TEST(StatusGrammar, NormalOverlaySuppressesMirrorExceptionChannel) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(true);
  renderer.command_received(1, true, 1000);
  auto receipt = renderer.render(1050);
  CHECK(receipt.normal > 0.0f);
  DOUBLES_EQUAL(0.0, receipt.exception, 0.001);

  renderer.vacancy_timeout_sync(900000, 30000, true, true, 2000);
  auto vacancy = renderer.render(2100);
  CHECK(vacancy.normal > 0.0f);
  DOUBLES_EQUAL(0.0, vacancy.exception, 0.001);
}

TEST(StatusGrammar, PresenceCueContrastsMirrorOffWithException) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(false);
  renderer.presence_acknowledged(1000);
  STRCMP_EQUAL("cue.presence", renderer.phase_name(1020));
  auto frame = renderer.render(1050);
  DOUBLES_EQUAL(0.0, frame.normal, 0.001);
  DOUBLES_EQUAL(0.70, frame.exception, 0.001);
  STRCMP_EQUAL("application.mirror_off", renderer.phase_name(1200));
}

TEST(StatusGrammar, PresenceCueContrastsMirrorOnWithNormal) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(true);
  renderer.presence_acknowledged(1000);
  auto frame = renderer.render(1050);
  DOUBLES_EQUAL(0.70, frame.normal, 0.001);
  DOUBLES_EQUAL(0.0, frame.exception, 0.001);
}

TEST(StatusGrammar, PresenceCueCoalescesWithinHalfSecond) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(true);
  renderer.presence_acknowledged(1000);
  renderer.presence_acknowledged(1200);
  // Second call coalesced; cue still ends at 1000+130.
  STRCMP_EQUAL("application.mirror_on", renderer.phase_name(1200));
  renderer.presence_acknowledged(1600);
  STRCMP_EQUAL("cue.presence", renderer.phase_name(1620));
}

TEST(StatusGrammar, CompletionCuePreemptsPresenceContrast) {
  renderer.set_mirror_levels(0.20f, 0.0f, 0.0f, 0.60f);
  renderer.set_mirror_enabled(true);
  renderer.set_connectivity(true, true, 600);
  renderer.set_mirror_state(false);
  renderer.presence_acknowledged(1000);
  renderer.command_received(1, false, 1010);
  renderer.execution_completed(1, 1010);
  auto frame = renderer.render(1050);
  CHECK(frame.normal > 0.50f);
  DOUBLES_EQUAL(0.0, frame.exception, 0.001);
  STRCMP_EQUAL("cue.completion", renderer.phase_name(1050));
}
