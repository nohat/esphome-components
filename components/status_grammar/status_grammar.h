#pragma once

#include "esphome/components/api/api_server.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esphome::status_grammar {

enum class BaseState { BOOT, WIFI_SEARCH, WIFI_ONLY, API_READY };
enum class TransactionState { NONE, RECEIVED, EXECUTING, COMPLETED, FAILED };
enum class HoldDirection { NONE, INCREASE, DECREASE };
enum class FaultState { NONE, WARNING, FATAL };

struct LedFrame {
  float normal{0.0f};
  float exception{0.0f};
};

class Renderer {
 public:
  static constexpr float PI = 3.14159265358979323846f;

  void begin(uint32_t now) { boot_started_ = now; base_ = BaseState::BOOT; }
  void set_connectivity(bool wifi, bool subscribed_api, uint32_t now) {
    BaseState next = !wifi ? BaseState::WIFI_SEARCH
                           : (subscribed_api ? BaseState::API_READY : BaseState::WIFI_ONLY);
    if (base_ != next) {
      if (next == BaseState::API_READY && base_ == BaseState::WIFI_ONLY)
        start_cue_(TransactionState::COMPLETED, now, transaction_generation_);
      base_ = next;
      base_started_ = now;
    }
  }
  void set_idle_brightness(float value) { idle_ = clamp_(value); }

  uint32_t command_received(uint32_t generation, bool remote, uint32_t now) {
    transaction_generation_ = generation;
    transaction_ = TransactionState::RECEIVED;
    transaction_started_ = now;
    if (remote && (uint32_t) (now - last_receipt_) >= 150) {
      cue_ = TransactionState::RECEIVED;
      cue_started_ = now;
      cue_generation_ = generation;
      last_receipt_ = now;
    }
    return transaction_generation_;
  }
  void execution_started(uint32_t generation, uint32_t now) {
    if (generation != transaction_generation_) return;
    transaction_ = TransactionState::EXECUTING;
    transaction_started_ = now;
  }
  void execution_completed(uint32_t generation, uint32_t now) {
    if (generation != transaction_generation_) return;
    transaction_ = TransactionState::NONE;
    start_cue_(TransactionState::COMPLETED, now, generation);
  }
  void execution_failed(uint32_t generation, uint32_t now) {
    if (generation != transaction_generation_) return;
    transaction_ = TransactionState::NONE;
    start_cue_(TransactionState::FAILED, now, generation);
  }

  void hold_started(HoldDirection direction, uint32_t now) {
    hold_ = direction; hold_limit_ = false; hold_started_ = now;
  }
  void hold_limit_reached(HoldDirection direction) {
    if (hold_ == direction) hold_limit_ = true;
  }
  void hold_ended(bool success, uint32_t now) {
    hold_ = HoldDirection::NONE; hold_limit_ = false;
    start_cue_(success ? TransactionState::COMPLETED : TransactionState::FAILED,
               now, transaction_generation_);
  }

  void vacancy_timeout_sync(uint32_t total_ms, uint32_t remaining_ms,
                            bool automation_owned, bool presence_clear, uint32_t now) {
    vacancy_total_ = total_ms; vacancy_remaining_ = remaining_ms;
    vacancy_sync_ = now; vacancy_owned_ = automation_owned;
    vacancy_presence_clear_ = presence_clear;
  }
  void vacancy_timeout_cancel() { vacancy_total_ = 0; }
  void warning_set(bool active) { fault_ = active ? FaultState::WARNING : FaultState::NONE; }
  void fatal_fault_set(bool active) { fault_ = active ? FaultState::FATAL : FaultState::NONE; }

  LedFrame render(uint32_t now) const {
    LedFrame frame{base_wave_(now), 0.0f};
    const uint32_t boot_t = now - boot_started_;
    if (boot_t >= 300 && boot_t < 520) {
      const uint32_t t = boot_t - 300;
      frame.exception = t < 110 ? 0.35f * t / 110.0f
                                : 0.35f * (220 - t) / 110.0f;
    }
    const float vacancy = vacancy_wave_(now);
    if (vacancy >= 0.0f) frame.normal = vacancy;

    if (cue_ != TransactionState::NONE) apply_cue_(frame, now);
    if (transaction_ == TransactionState::EXECUTING &&
        (uint32_t) (now - transaction_started_) >= 250)
      frame.normal = raised_cosine_(now - transaction_started_, 900, 0.18f, 0.34f);
    if (hold_ != HoldDirection::NONE) frame.normal = hold_wave_(now);

    if (fault_ == FaultState::WARNING)
      frame.exception = warning_wave_(now);
    else if (fault_ == FaultState::FATAL) {
      frame.normal = 0.0f;
      frame.exception = persistent_fault_wave_(now);
    }
    frame.normal = clamp_(frame.normal);
    frame.exception = clamp_(frame.exception);
    return frame;
  }

  static float vacancy_window_ms(uint32_t total_ms) {
    return std::min(60000.0f, std::max(15000.0f, total_ms * 0.25f));
  }
  static float vacancy_cadence(float progress) {
    progress = clamp_(progress);
    return 0.12f * std::pow(0.80f / 0.12f, progress * progress);
  }

 private:
  static float clamp_(float value) { return std::max(0.0f, std::min(1.0f, value)); }
  static float raised_cosine_(uint32_t elapsed, uint32_t period, float low, float high) {
    const float phase = (elapsed % period) / static_cast<float>(period);
    return low + (high - low) * (1.0f - std::cos(2.0f * PI * phase)) * 0.5f;
  }
  float base_wave_(uint32_t now) const {
    if ((uint32_t) (now - boot_started_) < 520) {
      const uint32_t t = now - boot_started_;
      if (t < 150) return 0.60f * t / 150.0f;
      if (t < 300) return 0.60f * (300 - t) / 150.0f;
      return 0.0f;
    }
    if (base_ == BaseState::WIFI_SEARCH)
      return raised_cosine_(now - base_started_, 2400, 0.0f, 0.35f);
    if (base_ == BaseState::WIFI_ONLY) {
      const uint32_t t = (now - base_started_) % 2000;
      if (t < 40) return 0.08f + 0.27f * t / 40.0f;
      if (t < 180) return 0.35f - 0.27f * (t - 40) / 140.0f;
      return 0.08f;
    }
    return idle_;
  }
  float vacancy_wave_(uint32_t now) const {
    if (!vacancy_total_ || !vacancy_owned_ || !vacancy_presence_clear_) return -1.0f;
    const uint32_t elapsed = now - vacancy_sync_;
    const uint32_t remaining = elapsed >= vacancy_remaining_ ? 0 : vacancy_remaining_ - elapsed;
    const float window = vacancy_window_ms(vacancy_total_);
    if (remaining > window || remaining == 0) return -1.0f;
    const float progress = clamp_(1.0f - remaining / window);
    // Deadline-derived phase avoids resetting progress after overlays/resyncs.
    const float cycles = (vacancy_remaining_ - remaining) * vacancy_cadence(progress) / 1000.0f;
    const float phase = cycles - std::floor(cycles);
    return 0.03f + 0.15f * (1.0f - std::cos(2.0f * PI * phase)) * 0.5f;
  }
  float hold_wave_(uint32_t now) const {
    if (hold_limit_) return hold_ == HoldDirection::INCREASE ? 0.65f : 0.08f;
    const uint32_t t = (now - hold_started_) % 750;
    if (hold_ == HoldDirection::INCREASE) {
      if (t < 600) return 0.08f + 0.57f * t / 600.0f;
      if (t < 660) return 0.65f - 0.57f * (t - 600) / 60.0f;
      return 0.08f;
    }
    if (t < 60) return 0.08f + 0.57f * t / 60.0f;
    if (t < 660) return 0.65f - 0.57f * (t - 60) / 600.0f;
    return 0.08f;
  }
  void apply_cue_(LedFrame &frame, uint32_t now) const {
    const uint32_t t = now - cue_started_;
    if (cue_ == TransactionState::RECEIVED && t < 130) {
      frame.normal = t < 20 ? 0.70f * t / 20.0f : (t < 70 ? 0.70f : 0.70f * (130 - t) / 60.0f);
    } else if (cue_ == TransactionState::COMPLETED && t < 450) {
      frame.normal = t < 30 ? 0.75f * t / 30.0f : (t < 150 ? 0.75f : 0.75f * (450 - t) / 300.0f);
    } else if (cue_ == TransactionState::FAILED && t < 320) {
      frame.exception = (t < 100 || (t >= 220 && t < 320)) ? 1.0f : 0.0f;
    }
  }
  float warning_wave_(uint32_t now) const {
    const uint32_t t = now % 3000;
    return (t < 100 || (t >= 220 && t < 320)) ? 1.0f : 0.0f;
  }
  float persistent_fault_wave_(uint32_t now) const {
    return now % 1500 < 120 ? 1.0f : 0.30f;
  }
  void start_cue_(TransactionState cue, uint32_t now, uint32_t generation) {
    cue_ = cue; cue_started_ = now; cue_generation_ = generation;
  }

  BaseState base_{BaseState::BOOT};
  TransactionState transaction_{TransactionState::NONE};
  mutable TransactionState cue_{TransactionState::NONE};
  HoldDirection hold_{HoldDirection::NONE};
  FaultState fault_{FaultState::NONE};
  uint32_t boot_started_{0}, base_started_{0}, transaction_started_{0}, cue_started_{0};
  uint32_t hold_started_{0}, last_receipt_{UINT32_MAX - 150};
  uint32_t transaction_generation_{0}, cue_generation_{0};
  uint32_t vacancy_total_{0}, vacancy_remaining_{0}, vacancy_sync_{0};
  bool hold_limit_{false}, vacancy_owned_{false}, vacancy_presence_clear_{false};
  float idle_{0.10f};
};

class StatusGrammar : public Component {
 public:
  void setup() override { renderer_.begin(millis()); }
  void loop() override {
    const uint32_t now = millis();
    if ((uint32_t) (now - last_render_) < render_interval_) return;
    last_render_ = now;
    renderer_.set_connectivity(wifi::global_wifi_component->is_connected(),
                               api::global_api_server->is_connected_with_state_subscription(), now);
    const LedFrame frame = renderer_.render(now);
    if (normal_output_)
      normal_output_->set_level(std::pow(frame.normal, gamma_correct_) * normal_max_power_);
    if (exception_output_)
      exception_output_->set_level(std::pow(frame.exception, gamma_correct_) * exception_max_power_);
  }
  float get_setup_priority() const override { return setup_priority::HARDWARE - 1.0f; }
  void set_normal_output(output::FloatOutput *output) { normal_output_ = output; }
  void set_exception_output(output::FloatOutput *output) { exception_output_ = output; }
  void set_normal_max_power(float value) { normal_max_power_ = value; }
  void set_exception_max_power(float value) { exception_max_power_ = value; }
  void set_idle_brightness(float value) { renderer_.set_idle_brightness(value); }
  void set_render_interval(uint32_t value) { render_interval_ = value; }
  void set_gamma_correct(float value) { gamma_correct_ = value; }
  Renderer &renderer() { return renderer_; }

 protected:
  Renderer renderer_;
  output::FloatOutput *normal_output_{nullptr};
  output::FloatOutput *exception_output_{nullptr};
  float normal_max_power_{0.35f}, exception_max_power_{0.20f};
  float gamma_correct_{2.8f};
  uint32_t render_interval_{20}, last_render_{0};
};

}  // namespace esphome::status_grammar
