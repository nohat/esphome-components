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
