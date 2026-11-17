/**
 * PET Puller LCD screens.
 * Custom code. GPL-3.0-or-later, same terms as Marlin.
 */
#include "../../inc/MarlinConfigPre.h"

#if ENABLED(PET_PULLER) && HAS_MARLINUI_MENU

#include "menu_item.h"
#include "../../MarlinCore.h"
#include "../../feature/pet_puller.h"
#include "../../gcode/queue.h"
#include "../../module/temperature.h"

#if ENABLED(PID_EDIT_MENU)
  void menu_advanced_temperature();
#endif

namespace {

void copy_pgm(char *dst, size_t n, PGM_P src) {
  if (!n) return;
  strncpy_P(dst, src, n - 1);
  dst[n - 1] = '\0';
}

void line_pair(char *dst, size_t n, const int a, const int b) {
  snprintf_P(dst, n, PSTR("%d/%d"), a, b);
}

void pet_autotune() {
  if (!PetPuller::hardware_confirmed) {
    ui.set_status(F("Confirm hardware first"));
    ui.goto_screen(menu_pet_confirm);
    return;
  }
  if (PetPuller::state == PetState::FAULT || !IsRunning()) {
    ui.set_status(F("Clear the fault first"));
    return;
  }
  PetPuller::request_stop();
  char cmd[28];
  snprintf_P(cmd, sizeof(cmd), PSTR("M303 U1 E0 S%d"), int(PetPuller::autotune_c));
  queue.inject(cmd);
  ui.set_status(F("PID tune. Save after."));
  ui.return_to_status();
}

} // namespace

void pet_puller_status_screen() {
  ENCODER_RATE_MULTIPLY(true);

  const int16_t steps = int16_t(ui.encoderPosition);
  ui.encoderPosition = 0;
  if (steps) PetPuller::adjust_speed(steps);

  if (ui.use_click()) {
    ui.goto_screen(menu_pet_puller);
    return;
  }

  char state_text[12];
  char line0[22], line1[22], line2[22], line3[22];
  copy_pgm(state_text, sizeof(state_text), PetPuller::state_pgm());
  snprintf_P(line0, sizeof(line0), PSTR("%3d/%3dC %.8s"),
             int(thermalManager.wholeDegHotend(0)), int(PetPuller::target_c), state_text);
  snprintf_P(line1, sizeof(line1), PSTR("Speed %d mm/min"), int(PetPuller::speed_mm_min));
  snprintf_P(line2, sizeof(line2), PSTR("Dir %s"),
             PetPuller::direction_reversed ? "reverse" : "forward");

  const bool pulling = PetPuller::run_requested
                    || PetPuller::state == PetState::RUNNING
                    || PetPuller::state == PetState::STOPPING;

  if (ui.status_message[0])
    strncpy(line3, ui.status_message, sizeof(line3) - 1);
  else if (!PetPuller::hardware_confirmed)
    strncpy_P(line3, PSTR("HEATER LOCKED"), sizeof(line3) - 1);
  else if (pulling)
    strncpy_P(line3, PSTR("Click for Stop"), sizeof(line3) - 1);
  else if (PetPuller::target_c <= 0)
    strncpy_P(line3, PSTR("Heater off"), sizeof(line3) - 1);
  else
    strncpy_P(line3, PSTR("Knob = speed"), sizeof(line3) - 1);
  line3[sizeof(line3) - 1] = '\0';

  START_SCREEN();
  STATIC_ITEM_F(F(""), SS_LEFT, line0);
  STATIC_ITEM_F(F(""), SS_LEFT, line1);
  STATIC_ITEM_F(F(""), SS_LEFT, line2);
  STATIC_ITEM_F(F(""), SS_LEFT, line3);
  END_SCREEN();
}

void menu_pet_confirm() {
  char sensor[8];
  snprintf_P(sensor, sizeof(sensor), PSTR("%d"), int(TEMP_SENSOR_0));

  START_MENU();
  STATIC_ITEM_F(F("CONFIRM HARDWARE"), SS_CENTER);
  STATIC_ITEM_F(F("Sensor type"), SS_LEFT, sensor);
  STATIC_ITEM_F(F("E0 driver"), SS_LEFT, PET_E0_DRIVER_LABEL);
  STATIC_ITEM_F(F("Room temp must"), SS_CENTER);
  STATIC_ITEM_F(F("look correct"), SS_CENTER);
  STATIC_ITEM_F(F("PSU must match"), SS_CENTER);
  STATIC_ITEM_F(F("the cartridge"), SS_CENTER);
  ACTION_ITEM_F(F("I have confirmed"), PetPuller::confirm_hardware);
  ACTION_ITEM_F(F("Not yet"), ui.return_to_status);
  END_MENU();
}

void menu_pet_temperature() {
  if (!PetPuller::hardware_confirmed) {
    ui.goto_screen(menu_pet_confirm);
    return;
  }
  char now[12];
  snprintf_P(now, sizeof(now), PSTR("%d C"), int(thermalManager.wholeDegHotend(0)));

  START_MENU();
  BACK_ITEM_F(F("PET PULLER"));
  STATIC_ITEM_F(F("Actual"), SS_LEFT, now);
  EDIT_ITEM_FAST_F(int3, F("Target C"), &PetPuller::preset_c, 0, thermalManager.hotend_max_target(0), PetPuller::commit_target);
  END_MENU();
}

void menu_pet_speed() {
  const int16_t floor = PetPuller::run_requested ? PET_MIN_RUN_MM_MIN : 0;
  START_MENU();
  BACK_ITEM_F(F("PET PULLER"));
  EDIT_ITEM_FAST_F(int4, F("Speed mm/min"), &PetPuller::speed_mm_min, floor, PetPuller::max_speed_mm_min, PetPuller::commit_speed);
  END_MENU();
}

void menu_pet_direction() {
  char now[12];
  strncpy_P(now, PetPuller::direction_reversed ? PSTR("reverse") : PSTR("forward"), sizeof(now) - 1);
  now[sizeof(now) - 1] = '\0';

  START_MENU();
  BACK_ITEM_F(F("PET PULLER"));
  STATIC_ITEM_F(F("Now"), SS_LEFT, now);
  ACTION_ITEM_F(F("Forward"), []{ PetPuller::set_direction(false); });
  ACTION_ITEM_F(F("Reverse"), []{ PetPuller::set_direction(true); });
  END_MENU();
}

void menu_pet_settings() {
  const bool idle = PetPuller::motion_idle();
  char steps[12], accel[12];
  snprintf_P(steps, sizeof(steps), PSTR("%d.%d"), int(PetPuller::steps_per_mm), int(PetPuller::steps_per_mm * 10) % 10);
  snprintf_P(accel, sizeof(accel), PSTR("%d"), int(PetPuller::accel_mm_s2));

  START_MENU();
  BACK_ITEM_F(F("PET PULLER"));
  ACTION_ITEM_F(F("Heater Off"), PetPuller::heater_off);
  EDIT_ITEM_FAST_F(int3, F("Min safe C"), &PetPuller::min_safe_c, PET_EXTRUDE_MINTEMP, thermalManager.hotend_max_target(0), PetPuller::commit_limits);
  EDIT_ITEM_FAST_F(int3, F("Window C"), &PetPuller::window_c, 1, 20, PetPuller::commit_limits);
  EDIT_ITEM_FAST_F(int3, F("Hysteresis C"), &PetPuller::hysteresis_c, 1, 15, PetPuller::commit_limits);
  EDIT_ITEM_FAST_F(int4, F("Max mm/min"), &PetPuller::max_speed_mm_min, PET_MIN_RUN_MM_MIN, PET_ABSOLUTE_MAX_MM_MIN, PetPuller::commit_limits);
  if (idle) {
    EDIT_ITEM_FAST_F(float61, F("Steps/mm"), &PetPuller::steps_per_mm, 1.0f, 9999.0f, PetPuller::commit_motion);
    EDIT_ITEM_FAST_F(float5, F("Accel mm/s2"), &PetPuller::accel_mm_s2, 5.0f, 5000.0f, PetPuller::commit_motion);
  }
  else {
    STATIC_ITEM_F(F("Steps/mm"), SS_LEFT, steps);
    STATIC_ITEM_F(F("Accel"), SS_LEFT, accel);
    STATIC_ITEM_F(F("Stop motor to edit"), SS_CENTER);
  }
  STATIC_ITEM_F(F("Tune at run temp"), SS_CENTER);
  EDIT_ITEM_FAST_F(int3, F("Autotune C"), &PetPuller::autotune_c, 150, thermalManager.hotend_max_target(0), PetPuller::save_soon);
  ACTION_ITEM_F(F("Run PID autotune"), pet_autotune);
  #if ENABLED(PID_EDIT_MENU)
    SUBMENU_F(F("PID values"), menu_advanced_temperature);
  #endif
  if (PetPuller::hardware_confirmed)
    ACTION_ITEM_F(F("Lock heater"), PetPuller::revoke_hardware);
  else
    ACTION_ITEM_F(F("Confirm hardware"), []{ ui.goto_screen(menu_pet_confirm); });
  ACTION_ITEM_F(F("Save settings"), PetPuller::save_all);
  END_MENU();
}

void menu_pet_puller() {
  char temp[16], speed[16], motor[12];
  line_pair(temp, sizeof(temp), thermalManager.wholeDegHotend(0), PetPuller::target_c);
  snprintf_P(speed, sizeof(speed), PSTR("%d mm/min"), int(PetPuller::speed_mm_min));
  copy_pgm(motor, sizeof(motor), PetPuller::state_pgm());

  const bool pulling = PetPuller::run_requested
                    || PetPuller::state == PetState::RUNNING
                    || PetPuller::state == PetState::STOPPING;

  START_MENU();
  STATIC_ITEM_F(F("PET PULLER"), SS_CENTER);
  STATIC_ITEM_F(F("Temperature:"), SS_LEFT, temp);
  STATIC_ITEM_F(F("Speed:"), SS_LEFT, speed);
  STATIC_ITEM_F(F("Motor:"), SS_LEFT, motor);

  if (PetPuller::state == PetState::FAULT)
    ACTION_ITEM_F(F("Clear Fault"), PetPuller::clear_fault);

  // Stop is the first action while a pull is requested so the next click halts.
  if (pulling)
    ACTION_ITEM_F(F("Stop Pulling"), PetPuller::request_stop);
  ACTION_ITEM_F(F("Start Pulling"), PetPuller::request_start);
  if (!pulling)
    ACTION_ITEM_F(F("Stop Pulling"), PetPuller::request_stop);

  SUBMENU_F(F("Set Temperature"), menu_pet_temperature);
  SUBMENU_F(F("Set Pulling Speed"), menu_pet_speed);
  SUBMENU_F(F("Motor Direction"), menu_pet_direction);
  ACTION_ITEM_F(F("Heater Off"), PetPuller::heater_off);
  SUBMENU_F(F("Settings"), menu_pet_settings);
  ACTION_ITEM_F(F("Back"), ui.return_to_status);
  END_MENU();
}

#endif // PET_PULLER && HAS_MARLINUI_MENU
