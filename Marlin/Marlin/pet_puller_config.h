/**
 * PET bottle filament puller — machine configuration.
 *
 * Included twice:
 *   1. At the end of Configuration.h
 *   2. At the end of Configuration_adv.h
 *
 * Edit this file. Do not scatter puller settings through the rest of Marlin.
 * Nothing in here has been validated on the assembled machine.
 *
 * Confirm before flashing or powering the heater cartridge:
 *   - PET_THERMISTOR_TYPE matches the sensor on T0
 *   - PET_E0_DRIVER_TYPE matches the driver on the E0 socket
 *   - The supply voltage matches the heater cartridge
 *   - The LCD option matches the panel plugged into EXP1/EXP2
 * The heater stays off until someone confirms that on the LCD.
 *
 * Do not use #pragma once. This header is included twice on purpose.
 */

#ifndef PET_PULLER_PHASE1
#define PET_PULLER_PHASE1

#define PET_PULLER 1

// ---------------------------------------------------------------------------
// Hardware that must be confirmed. These are compile-time placeholders.
// ---------------------------------------------------------------------------

// Marlin thermistor table number. 1 is the 100k EPCOS table commonly shipped
// with RAMPS kits. It is NOT a measurement of the sensor on this machine.
// Wrong table + confirmation = wrong temperature and a possible heater fire.
#define PET_THERMISTOR_TYPE 1

// Step/dir driver on the RAMPS E0 socket. A4988 is the historical RAMPS
// default. DRV8825 needs a longer step pulse; TMC UART/SPI needs a different
// driver type and wiring. Current on A4988/DRV8825 is the module potentiometer,
// not a firmware value.
#define PET_E0_DRIVER_TYPE A4988
#define PET_E0_DRIVER_LABEL "A4988"

// Select the panel that is actually plugged in. Exactly one.
#define PET_LCD_FULL_GRAPHIC
//#define PET_LCD_SMART_20X4
//#define PET_LCD_CR10_STOCK

#if defined(PET_LCD_FULL_GRAPHIC) + defined(PET_LCD_SMART_20X4) + defined(PET_LCD_CR10_STOCK) != 1
  #error "Select exactly one PET LCD option in pet_puller_config.h."
#endif

// ---------------------------------------------------------------------------
// Motion and temperature placeholders. Not validated process setpoints.
// ---------------------------------------------------------------------------

// Uncalibrated steps/mm. Calibrate before trusting mm/min. See the README.
#define PET_DEFAULT_STEPS_PER_MM 100

// Gentle E-only acceleration (mm/s^2). E-only moves use retract acceleration.
#define PET_DEFAULT_ACCEL_MM_S2 100

// Speed cap until the mechanics are proven. mm/min. Runtime menu can raise
// this up to PET_ABSOLUTE_MAX_MM_MIN.
#define PET_DEFAULT_MAX_MM_MIN 300
#define PET_ABSOLUTE_MAX_MM_MIN 1200

// Slowest non-zero pull command. Kept above Marlin's minimum planner speed.
#define PET_MIN_RUN_MM_MIN 5

// Cold-extrusion floor, in °C. This is Marlin's safety floor, not a PET
// processing temperature. Pulling is also refused below the runtime minimum.
#define PET_EXTRUDE_MINTEMP 170

// Runtime defaults. Target 0 means the heater is off.
// Minimum safe temperature defaults to the cold-extrusion floor. Raise it
// on the machine to the lowest temperature at which pulling should continue.
#define PET_DEFAULT_PRESET_C 0
#define PET_DEFAULT_MIN_SAFE_C 170
#define PET_DEFAULT_WINDOW_C 5
#define PET_DEFAULT_HYSTERESIS_C 3
#define PET_DEFAULT_SPEED_MM_MIN 0

// Menu floor for M303. Not a suggested operating temperature.
#define PET_DEFAULT_AUTOTUNE_C 150

// Planner queue. Short segments keep speed changes responsive without
// letting the block buffer grow without bound.
#define PET_QUEUE_DEPTH 3
#define PET_SEGMENT_MS 200

// EEPROM slot above Marlin's settings block. Checked again at runtime.
#define PET_EEPROM_BASE 3584

#if MOTHERBOARD != BOARD_RAMPS_14_EFB
  #error "This puller firmware expects BOARD_RAMPS_14_EFB (hotend MOSFET on D10)."
#endif

#undef STRING_CONFIG_H_AUTHOR
#define STRING_CONFIG_H_AUTHOR "(PET Puller, uncalibrated)"

#define CUSTOM_MACHINE_NAME "PET Puller"

#undef TEMP_SENSOR_0
#define TEMP_SENSOR_0 PET_THERMISTOR_TYPE

#undef TEMP_SENSOR_BED
#define TEMP_SENSOR_BED 0

#undef THERMAL_PROTECTION_BED
#undef THERMAL_PROTECTION_CHAMBER
#undef THERMAL_PROTECTION_COOLER

#undef E0_DRIVER_TYPE
#define E0_DRIVER_TYPE PET_E0_DRIVER_TYPE

#undef EXTRUDE_MINTEMP
#define EXTRUDE_MINTEMP PET_EXTRUDE_MINTEMP

#undef DEFAULT_AXIS_STEPS_PER_UNIT
#define DEFAULT_AXIS_STEPS_PER_UNIT { 80, 80, 400, PET_DEFAULT_STEPS_PER_MM }

#undef DEFAULT_MAX_FEEDRATE
#define DEFAULT_MAX_FEEDRATE { 5, 5, 1, 5 }

#undef DEFAULT_MAX_ACCELERATION
#define DEFAULT_MAX_ACCELERATION { 100, 100, 100, PET_DEFAULT_ACCEL_MM_S2 }

#undef DEFAULT_ACCELERATION
#define DEFAULT_ACCELERATION PET_DEFAULT_ACCEL_MM_S2

#undef DEFAULT_RETRACT_ACCELERATION
#define DEFAULT_RETRACT_ACCELERATION PET_DEFAULT_ACCEL_MM_S2

#undef DEFAULT_TRAVEL_ACCELERATION
#define DEFAULT_TRAVEL_ACCELERATION PET_DEFAULT_ACCEL_MM_S2

#undef PREHEAT_1_LABEL
#define PREHEAT_1_LABEL "Tune"
#undef PREHEAT_1_TEMP_HOTEND
#define PREHEAT_1_TEMP_HOTEND 150
#undef PREHEAT_1_TEMP_BED
#define PREHEAT_1_TEMP_BED 0

#undef PREHEAT_2_LABEL
#define PREHEAT_2_LABEL "Tune2"
#undef PREHEAT_2_TEMP_HOTEND
#define PREHEAT_2_TEMP_HOTEND 150
#undef PREHEAT_2_TEMP_BED
#define PREHEAT_2_TEMP_BED 0

// Unused axes never stay energized.
#define DISABLE_X
#define DISABLE_Y
#define DISABLE_Z
#define DISABLE_REDUCED_ACCURACY_WARNING

#define EEPROM_SETTINGS
#define EEPROM_AUTO_INIT

#define PID_EDIT_MENU
#define PID_AUTOTUNE_MENU

#define SPEAKER
// RAMPS fan MOSFET D9 is unused. Soft PWM frees Timer 2 for the LCD beeper.
#define FAN_SOFT_PWM

#undef SHOW_BOOTSCREEN

// Short E segments should keep their programmed feedrate.
#undef JD_HANDLE_SMALL_SEGMENTS

#if defined(PET_LCD_FULL_GRAPHIC)
  #define REPRAP_DISCOUNT_FULL_GRAPHIC_SMART_CONTROLLER
#elif defined(PET_LCD_SMART_20X4)
  #define REPRAP_DISCOUNT_SMART_CONTROLLER
#elif defined(PET_LCD_CR10_STOCK)
  #define CR10_STOCKDISPLAY
#endif

// Uncomment if the installed encoder turns the wrong way.
//#define REVERSE_ENCODER_DIRECTION
//#define REVERSE_MENU_DIRECTION

#else  // second include, from Configuration_adv.h
#ifndef PET_PULLER_PHASE2
#define PET_PULLER_PHASE2

// Arcs and the print-buffer slowdown fight a deliberately shallow queue.
#undef ARC_SUPPORT
#undef SLOWDOWN

// Do not drop the pull motor because the planner paused between segments.
#undef DISABLE_IDLE_E
#undef DEFAULT_STEPPER_TIMEOUT_SEC
#define DEFAULT_STEPPER_TIMEOUT_SEC 0

// The operating screen owns the encoder. Feedrate-percent mode would steal it.
#undef ULTIPANEL_FEEDMULTIPLY

#undef LCD_TIMEOUT_TO_STATUS
#define LCD_TIMEOUT_TO_STATUS 20000

#define STATUS_MESSAGE_TIMEOUT_SEC 8

#define EMERGENCY_PARSER
#define NO_VOLUMETRICS

#endif
#endif
