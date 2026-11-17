/**
 * PET filament puller.
 *
 * Continuous E0 motion is a short queue of extrude-only planner blocks.
 * The stepper ISR, temperature ISR, and LCD stay on Marlin's normal paths.
 * The queue depth is capped, so the planner cannot grow without bound and
 * buffer_line is never asked to wait.
 *
 * Pulling does not resume across reset or a latched fault. A saved setpoint
 * is not applied to the heater until Set Temperature or Start Pulling.
 */
#include "pet_puller.h"

#if ENABLED(PET_PULLER)

#include "../MarlinCore.h"
#include "../lcd/marlinui.h"
#include "../module/motion.h"
#include "../module/planner.h"
#include "../module/settings.h"
#include "../module/stepper.h"
#include "../module/temperature.h"
#include "../HAL/shared/eeprom_api.h"
#include "../libs/crc16.h"

#include <stdio.h>

#warning "PET Puller: confirm thermistor, E0 driver, LCD, and PSU voltage before powering the heater."

namespace {

struct PetPayload {
  uint32_t magic;
  uint8_t version;
  uint8_t hardware_confirmed;
  uint8_t direction_reversed;
  uint8_t reserved0;
  int16_t preset_c;
  int16_t min_safe_c;
  int16_t speed_mm_min;
  int16_t max_speed_mm_min;
  int16_t window_c;
  int16_t hysteresis_c;
  int16_t autotune_c;
  float steps_per_mm;
  float accel_mm_s2;
};

struct PetRecord {
  PetPayload payload;
  uint16_t crc;
};

constexpr uint32_t PET_MAGIC = 0x31544550UL; // 'PET1' on little-endian
constexpr uint8_t PET_RECORD_VERSION = 1;
constexpr int16_t PET_EEPROM_MARLIN_OFFSET = 100; // matches settings.cpp EEPROM_OFFSET

int16_t clamp16(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return int16_t(lo);
  if (v > hi) return int16_t(hi);
  return int16_t(v);
}

} // namespace

PetState PetPuller::state = PetState::IDLE;
bool PetPuller::hardware_confirmed = false;
bool PetPuller::direction_reversed = false;
bool PetPuller::run_requested = false;
int16_t PetPuller::target_c = 0;
int16_t PetPuller::preset_c = PET_DEFAULT_PRESET_C;
int16_t PetPuller::min_safe_c = PET_DEFAULT_MIN_SAFE_C;
int16_t PetPuller::speed_mm_min = PET_DEFAULT_SPEED_MM_MIN;
int16_t PetPuller::max_speed_mm_min = PET_DEFAULT_MAX_MM_MIN;
int16_t PetPuller::window_c = PET_DEFAULT_WINDOW_C;
int16_t PetPuller::hysteresis_c = PET_DEFAULT_HYSTERESIS_C;
int16_t PetPuller::autotune_c = PET_DEFAULT_AUTOTUNE_C;
float PetPuller::steps_per_mm = float(PET_DEFAULT_STEPS_PER_MM);
float PetPuller::accel_mm_s2 = float(PET_DEFAULT_ACCEL_MM_S2);
bool PetPuller::direction_latched = false;
bool PetPuller::motor_released = true;
millis_t PetPuller::save_at = 0;

PGM_P PetPuller::state_pgm() {
  switch (state) {
    case PetState::HEATING:  return PSTR("HEATING");
    case PetState::READY:    return PSTR("READY");
    case PetState::RUNNING:  return PSTR("RUNNING");
    case PetState::STOPPING: return PSTR("STOPPING");
    case PetState::FAULT:    return PSTR("FAULT");
    case PetState::IDLE:     break;
  }
  return PSTR("STOPPED");
}

int16_t PetPuller::stop_c() {
  int32_t band = int32_t(target_c) - window_c - hysteresis_c;
  if (band < min_safe_c) band = min_safe_c;
  if (band < 0) band = 0;
  return int16_t(band);
}

int16_t PetPuller::start_c() {
  int32_t band = int32_t(target_c) - window_c;
  const int32_t above = int32_t(stop_c()) + hysteresis_c;
  if (band < above) band = above;
  if (band > target_c) band = target_c;
  if (band < 0) band = 0;
  return int16_t(band);
}

bool PetPuller::setpoint_valid() {
  return target_c > 0
      && speed_mm_min >= PET_MIN_RUN_MM_MIN
      && target_c >= int16_t(min_safe_c + hysteresis_c)
      && start_c() <= target_c
      && start_c() > stop_c();
}

bool PetPuller::motion_idle() {
  return state != PetState::RUNNING
      && state != PetState::STOPPING
      && !planner.has_blocks_queued()
      && !planner.cleaning_buffer_counter;
}

void PetPuller::sanitize() {
  const int16_t max_t = thermalManager.hotend_max_target(0);
  preset_c = clamp16(preset_c, 0, max_t);
  target_c = clamp16(target_c, 0, max_t);
  min_safe_c = clamp16(min_safe_c, PET_EXTRUDE_MINTEMP, max_t);
  window_c = clamp16(window_c, 1, 20);
  hysteresis_c = clamp16(hysteresis_c, 1, 15);
  max_speed_mm_min = clamp16(max_speed_mm_min, PET_MIN_RUN_MM_MIN, PET_ABSOLUTE_MAX_MM_MIN);
  speed_mm_min = clamp16(speed_mm_min, 0, max_speed_mm_min);
  autotune_c = clamp16(autotune_c, 150, max_t);
  if (steps_per_mm < 1.0f) steps_per_mm = 1.0f;
  if (steps_per_mm > 20000.0f) steps_per_mm = 20000.0f;
  if (accel_mm_s2 < 5.0f) accel_mm_s2 = 5.0f;
  if (accel_mm_s2 > 5000.0f) accel_mm_s2 = 5000.0f;
}

void PetPuller::apply_limits() {
  sanitize();
  planner.settings.max_feedrate_mm_s[E_AXIS] = max_speed_mm_min / 60.0f;
  planner.flow_percentage[0] = 100;
  planner.e_factor[0] = 1.0f;
}

void PetPuller::apply_motion_settings() {
  if (planner.has_blocks_queued() || planner.cleaning_buffer_counter) return;
  sanitize();
  planner.settings.axis_steps_per_mm[E_AXIS] = steps_per_mm;
  planner.settings.max_acceleration_mm_per_s2[E_AXIS] = uint32_t(accel_mm_s2 + 0.5f);
  planner.settings.acceleration = accel_mm_s2;
  planner.settings.retract_acceleration = accel_mm_s2;
  planner.settings.travel_acceleration = accel_mm_s2;
  apply_limits();
  current_position.e = 0;
  planner.refresh_positioning();
}

void PetPuller::apply_heater() {
  celsius_t want = 0;
  if (hardware_confirmed && IsRunning() && state != PetState::FAULT)
    want = target_c;
  if (thermalManager.degTargetHotend(0) != want)
    thermalManager.setTargetHotend(want, 0);
}

void PetPuller::load() {
  const uint16_t marlin_end = uint16_t(PET_EEPROM_MARLIN_OFFSET + MarlinSettings::datasize());
  if (marlin_end > PET_EEPROM_BASE || persistentStore.capacity() < PET_EEPROM_BASE + sizeof(PetRecord)) {
    SERIAL_ECHOLNPGM("PET settings overlap Marlin EEPROM (", marlin_end, " bytes). Using compiled defaults.");
    return;
  }

  PetRecord rec;
  persistentStore.access_start();
  int pos = PET_EEPROM_BASE;
  uint16_t ignored = 0;
  persistentStore.read_data(pos, reinterpret_cast<uint8_t *>(&rec), sizeof(rec), &ignored);
  persistentStore.access_finish();

  uint16_t crc = 0;
  crc16(&crc, &rec.payload, sizeof(rec.payload));
  if (rec.payload.magic != PET_MAGIC || rec.payload.version != PET_RECORD_VERSION || crc != rec.crc)
    return;

  hardware_confirmed = rec.payload.hardware_confirmed != 0;
  direction_reversed = rec.payload.direction_reversed != 0;
  preset_c = rec.payload.preset_c;
  min_safe_c = rec.payload.min_safe_c;
  speed_mm_min = rec.payload.speed_mm_min;
  max_speed_mm_min = rec.payload.max_speed_mm_min;
  window_c = rec.payload.window_c;
  hysteresis_c = rec.payload.hysteresis_c;
  autotune_c = rec.payload.autotune_c;
  steps_per_mm = rec.payload.steps_per_mm;
  accel_mm_s2 = rec.payload.accel_mm_s2;
}

void PetPuller::save_record() {
  const uint16_t marlin_end = uint16_t(PET_EEPROM_MARLIN_OFFSET + MarlinSettings::datasize());
  if (marlin_end > PET_EEPROM_BASE || persistentStore.capacity() < PET_EEPROM_BASE + sizeof(PetRecord))
    return;

  PetRecord rec = {};
  rec.payload.magic = PET_MAGIC;
  rec.payload.version = PET_RECORD_VERSION;
  rec.payload.hardware_confirmed = hardware_confirmed ? 1 : 0;
  rec.payload.direction_reversed = direction_reversed ? 1 : 0;
  rec.payload.preset_c = preset_c;
  rec.payload.min_safe_c = min_safe_c;
  rec.payload.speed_mm_min = speed_mm_min;
  rec.payload.max_speed_mm_min = max_speed_mm_min;
  rec.payload.window_c = window_c;
  rec.payload.hysteresis_c = hysteresis_c;
  rec.payload.autotune_c = autotune_c;
  rec.payload.steps_per_mm = steps_per_mm;
  rec.payload.accel_mm_s2 = accel_mm_s2;
  crc16(&rec.crc, &rec.payload, sizeof(rec.payload));

  persistentStore.access_start();
  int pos = PET_EEPROM_BASE;
  uint16_t ignored = 0;
  persistentStore.write_data(pos, reinterpret_cast<const uint8_t *>(&rec), sizeof(rec), &ignored);
  persistentStore.access_finish();
}

void PetPuller::save_soon() {
  save_at = millis() + 5000UL;
}

void PetPuller::save_all() {
  sanitize();
  save_at = 0;
  save_record();
  settings.save();
  ui.set_status(F("Settings saved"));
}

void PetPuller::release_motor() {
  if (motor_released) return;
  if (planner.has_blocks_queued() || planner.cleaning_buffer_counter) return;
  stepper.disable_extruder();
  motor_released = true;
}

void PetPuller::enter_fault() {
  run_requested = false;
  target_c = 0;
  const bool first = state != PetState::FAULT;
  state = PetState::FAULT;
  if (thermalManager.degTargetHotend(0) != 0)
    thermalManager.setTargetHotend(0, 0);
  if (first) {
    planner.quick_stop();
    stepper.disable_extruder();
    motor_released = true;
  }
}

void PetPuller::on_kill() {
  run_requested = false;
  target_c = 0;
  state = PetState::FAULT;
  planner.quick_stop();
}

void PetPuller::on_stop() {
  enter_fault();
}

bool PetPuller::enqueue_segment() {
  if (planner.cleaning_buffer_counter) return false;
  if (planner.moves_free() < 2) return false;
  if (thermalManager.tooColdToExtrude(0)) return false;
  if (speed_mm_min < PET_MIN_RUN_MM_MIN) return false;

  // Keep the logical E coordinate at 0 between segments. Doing that while
  // blocks are queued would desynchronize the planner, so refuse until the
  // queue has drained and then resync.
  if (planner.position.e != 0 || current_position.e != 0) {
    if (planner.has_blocks_queued()) return false;
    current_position.e = 0;
    planner.set_e_position_mm(0);
  }

  const float mm_s = speed_mm_min / 60.0f;
  float seg = mm_s * (PET_SEGMENT_MS / 1000.0f);
  if (seg < 0.5f) seg = 0.5f;
  if (seg > 5.0f) seg = 5.0f;

  const float steps_per = planner.settings.axis_steps_per_mm[E_AXIS];
  if (steps_per < 1.0f) return false;
  const float min_mm = float(MIN_STEPS_PER_SEGMENT + 1) / steps_per;
  if (seg < min_mm) seg = min_mm;
  if (seg > EXTRUDE_MAXLENGTH) return false;
  if (direction_latched) seg = -seg;

  xyze_pos_t target = current_position;
  target.e = seg;

  const int32_t e_before = planner.position.e;
  const uint8_t queued_before = planner.movesplanned();
  const bool accepted = planner.buffer_line(target, mm_s, 0);

  // The block already stores its step delta. Put the absolute E coordinate
  // back so a multi-hour pull cannot walk the int32 step position off the end.
  if (planner.position.e != e_before)
    planner.position.e = e_before;
  current_position.e = 0;

  if (!accepted) return false;
  if (planner.movesplanned() == queued_before) return false;

  motor_released = false;
  return true;
}

void PetPuller::service_motion() {
  const celsius_t now = thermalManager.wholeDegHotend(0);
  if (now < stop_c() || thermalManager.tooColdToExtrude(0)) {
    state = PetState::STOPPING;
    return;
  }

  if (direction_latched != direction_reversed) {
    if (planner.has_blocks_queued()) return;
    direction_latched = direction_reversed;
    current_position.e = 0;
    planner.set_e_position_mm(0);
  }

  uint8_t guard = PET_QUEUE_DEPTH;
  while (guard--) {
    if (planner.movesplanned() >= PET_QUEUE_DEPTH) break;
    if (!enqueue_segment()) break;
  }
}

void PetPuller::update_state() {
  if (state == PetState::STOPPING) {
    if (planner.has_blocks_queued() || planner.cleaning_buffer_counter) return;
    stepper.disable_extruder();
    motor_released = true;
    current_position.e = 0;
    planner.set_e_position_mm(0);
  }

  const celsius_t now = thermalManager.wholeDegHotend(0);
  const bool hot = now >= start_c() && !thermalManager.tooColdToExtrude(0);

  if (run_requested) {
    if (!setpoint_valid()) {
      run_requested = false;
    }
    else if (!hot || planner.cleaning_buffer_counter) {
      state = PetState::HEATING;
      release_motor();
      return;
    }
    else {
      if (state != PetState::RUNNING)
        direction_latched = direction_reversed;
      state = PetState::RUNNING;
      return;
    }
  }

  // A cancelled pull still has to finish the blocks already accepted.
  if (planner.has_blocks_queued() || planner.cleaning_buffer_counter) {
    state = PetState::STOPPING;
    return;
  }

  release_motor();
  if (target_c <= 0 || !hardware_confirmed)
    state = PetState::IDLE;
  else
    state = hot ? PetState::READY : PetState::HEATING;
}

void PetPuller::tick() {
  if (marlin_state == MF_INITIALIZING) return;

  if (save_at && ELAPSED(millis(), save_at)) {
    save_at = 0;
    save_record();
  }

  if (!IsRunning()) {
    enter_fault();
    return;
  }
  if (state == PetState::FAULT) {
    if (thermalManager.degTargetHotend(0) != 0)
      thermalManager.setTargetHotend(0, 0);
    return;
  }

  apply_heater();
  update_state();
  if (state == PetState::RUNNING)
    service_motion();
}

void PetPuller::init() {
  load();
  // Never arm the heater or the motor from saved data.
  target_c = 0;
  run_requested = false;
  state = PetState::IDLE;
  direction_latched = direction_reversed;
  sanitize();
  apply_motion_settings();
  thermalManager.setTargetHotend(0, 0);
  stepper.disable_all_steppers();
  motor_released = true;

  SERIAL_ECHOLNPGM("PET Puller");
  SERIAL_ECHOLNPGM("  TEMP_SENSOR_0: ", int(TEMP_SENSOR_0));
  SERIAL_ECHOLNPGM("  E0 driver: ", PET_E0_DRIVER_LABEL);
  SERIAL_ECHOLNPGM("  Steps/mm placeholder: ", steps_per_mm);
  SERIAL_ECHOLNPGM("  Marlin EEPROM bytes: ", MarlinSettings::datasize());
  SERIAL_ECHOLNPGM("  Heater is off until Set Temperature or Start.");
  SERIAL_ECHOLNPGM("  Confirm thermistor, driver, and PSU before heating.");

  if (!hardware_confirmed)
    ui.goto_screen(menu_pet_confirm);
}

void PetPuller::request_start() {
  if (state == PetState::FAULT || !IsRunning()) {
    ui.set_status(F("Clear the fault first"));
    return;
  }
  if (!hardware_confirmed) {
    ui.set_status(F("Confirm hardware first"));
    ui.goto_screen(menu_pet_confirm);
    return;
  }
  if (speed_mm_min < PET_MIN_RUN_MM_MIN) {
    ui.set_status(F("Set pull speed"));
    return;
  }

  const int16_t want = target_c > 0 ? target_c : preset_c;
  if (want <= 0) {
    ui.set_status(F("Set temperature"));
    return;
  }

  target_c = want;
  sanitize();
  if (!setpoint_valid()) {
    target_c = 0;
    apply_heater();
    ui.set_status(F("Temp below safe minimum"));
    return;
  }

  apply_heater();
  run_requested = true;
  ui.set_status(F("Pull requested"));
  ui.return_to_status();
}

void PetPuller::request_stop() {
  run_requested = false;
  if (planner.has_blocks_queued() || state == PetState::RUNNING)
    state = PetState::STOPPING;
  ui.set_status(F("Stopping"));
  ui.return_to_status();
}

void PetPuller::halt_motion() {
  run_requested = false;
  if (planner.has_blocks_queued() || state == PetState::RUNNING || state == PetState::STOPPING) {
    planner.quick_stop();
    state = PetState::STOPPING;
  }
}

void PetPuller::commit_speed() {
  sanitize();
  if (run_requested && speed_mm_min < PET_MIN_RUN_MM_MIN)
    speed_mm_min = PET_MIN_RUN_MM_MIN;
  save_soon();
}

void PetPuller::heater_off() {
  run_requested = false;
  target_c = 0;
  if (planner.has_blocks_queued() || state == PetState::RUNNING)
    state = PetState::STOPPING;
  apply_heater();
  ui.set_status(F("Heater off"));
  ui.return_to_status();
}

void PetPuller::clear_fault() {
  if (marlin_state == MF_KILLED) return;
  marlin_state = MF_RUNNING;
  run_requested = false;
  target_c = 0;
  state = PetState::IDLE;
  if (!planner.has_blocks_queued() && !planner.cleaning_buffer_counter) {
    current_position.e = 0;
    planner.set_e_position_mm(0);
  }
  ui.set_status(F("Fault cleared"));
  ui.return_to_status();
}

void PetPuller::confirm_hardware() {
  hardware_confirmed = true;
  save_record();
  ui.set_status(F("Hardware confirmed"));
  ui.return_to_status();
}

void PetPuller::revoke_hardware() {
  hardware_confirmed = false;
  run_requested = false;
  target_c = 0;
  if (planner.has_blocks_queued() || state == PetState::RUNNING)
    state = PetState::STOPPING;
  else if (state != PetState::FAULT)
    state = PetState::IDLE;
  apply_heater();
  save_record();
  ui.set_status(F("Heater locked"));
  ui.goto_screen(menu_pet_confirm);
}

void PetPuller::set_direction(const bool reversed) {
  direction_reversed = reversed;
  if (state != PetState::RUNNING && state != PetState::STOPPING)
    direction_latched = reversed;
  save_soon();
  ui.set_status(reversed ? F("Direction reverse") : F("Direction forward"));
}

void PetPuller::adjust_speed(const int16_t delta) {
  if (!delta) return;
  const int16_t floor = run_requested ? PET_MIN_RUN_MM_MIN : 0;
  int32_t next = int32_t(speed_mm_min) + delta;
  if (next < floor) next = floor;
  if (next > max_speed_mm_min) next = max_speed_mm_min;
  if (int16_t(next) == speed_mm_min) return;
  speed_mm_min = int16_t(next);
  save_soon();
}

void PetPuller::commit_target() {
  sanitize();
  if (!hardware_confirmed) {
    ui.goto_screen(menu_pet_confirm);
    return;
  }
  target_c = preset_c;
  apply_heater();
  save_soon();
}

void PetPuller::commit_limits() {
  sanitize();
  apply_limits();
  save_soon();
}

void PetPuller::commit_motion() {
  if (!motion_idle()) {
    ui.set_status(F("Stop motor first"));
    return;
  }
  sanitize();
  apply_motion_settings();
  save_soon();
}

#endif // PET_PULLER
