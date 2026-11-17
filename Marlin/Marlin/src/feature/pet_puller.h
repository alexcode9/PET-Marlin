/**
 * PET filament puller controller.
 *
 * Custom code. Not part of upstream Marlin.
 * GPL-3.0-or-later, same terms as Marlin.
 */
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(PET_PULLER)

enum class PetState : uint8_t {
  IDLE,
  HEATING,
  READY,
  RUNNING,
  STOPPING,
  FAULT
};

class PetPuller {
public:
  static PetState state;
  static bool hardware_confirmed;
  static bool direction_reversed;
  static bool run_requested;

  static int16_t target_c;       // live heater setpoint, 0 = off
  static int16_t preset_c;       // last edited setpoint, restored only by the user
  static int16_t min_safe_c;
  static int16_t speed_mm_min;
  static int16_t max_speed_mm_min;
  static int16_t window_c;
  static int16_t hysteresis_c;
  static int16_t autotune_c;
  static float steps_per_mm;
  static float accel_mm_s2;

  static void init();
  static void tick();
  static void on_kill();
  static void on_stop();

  static void request_start();
  static void request_stop();
  static void halt_motion();
  static void heater_off();
  static void clear_fault();
  static void confirm_hardware();
  static void revoke_hardware();
  static void set_direction(bool reversed);
  static void adjust_speed(int16_t delta);
  static void commit_target();
  static void commit_speed();
  static void commit_limits();
  static void commit_motion();
  static void save_soon();
  static void save_all();

  static int16_t start_c();
  static int16_t stop_c();
  static bool setpoint_valid();
  static bool motion_idle();
  static PGM_P state_pgm();

private:
  static bool direction_latched;
  static bool motor_released;
  static millis_t save_at;

  static void sanitize();
  static void apply_heater();
  static void apply_limits();
  static void apply_motion_settings();
  static void load();
  static void save_record();
  static void enter_fault();
  static void update_state();
  static void service_motion();
  static bool enqueue_segment();
  static void release_motor();
};

void pet_puller_status_screen();
void menu_pet_puller();
void menu_pet_confirm();

#endif
