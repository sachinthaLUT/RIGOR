#pragma once
#include <stdint.h>
#include <stddef.h>

// ===========================================================================
//  DATALOG  --  records force-vs-time samples to a microSD card.
//
//  ---------------------------------------------------------------------
//  CARD REQUIREMENTS  --  READ THIS IF THE CARD WILL NOT MOUNT
//  ---------------------------------------------------------------------
//  Cards of 32 GB or larger ship formatted as exFAT, which the Arduino SD
//  library does NOT read. They must be reformatted as FAT32 first (use a
//  third-party FAT32 tool on Windows; macOS/Linux mkfs.vfat is fine).
//  Cards of 16 GB or less are usually FAT32 already and just work.
//
//  A sample line is now ~30 bytes (time, force, current, temperature), so
//  logging at 10 Hz is about 1 MB per hour.
//
//  ---------------------------------------------------------------------
//  CSV FORMAT
//  ---------------------------------------------------------------------
//    timestamp_ms,force_kg,current_A,driver_temp_C
//  force_* follows the unit setting (kg or N). current_A is signed
//  (+ push, - retract). driver_temp_C is left EMPTY when the rig reports a
//  thermistor fault or is running firmware older than rev 5.
//
//  ---------------------------------------------------------------------
//  WIRING (SPI)
//  ---------------------------------------------------------------------
//  GPIO 10, 11, 12, 13 are free on this board and are the standard Sunton
//  microSD pinout.
//
//  ---------------------------------------------------------------------
//  NO CARD?  IT STILL WORKS
//  ---------------------------------------------------------------------
//  If no card is detected, logging falls back to a RAM ring buffer holding
//  the most recent RAM_FALLBACK_SAMPLES.
// ===========================================================================

// Samples kept in RAM when no SD card is present. 3000 x 16 bytes = 48 KB.
#define RAM_FALLBACK_SAMPLES 3000

// Longest user-supplied test name (becomes the filename).
#define TESTNAME_MAX 32

// ---------------------------------------------------------------------------
//  LIFECYCLE
// ---------------------------------------------------------------------------
void datalog_begin();
bool datalog_has_card();
uint32_t datalog_total_mb();
uint32_t datalog_free_mb();

// ---------------------------------------------------------------------------
//  RECORDING
//
//  A session is: start -> many add() calls -> stop.
//  `name` becomes "<name>.csv" on the card, sanitised for FAT.
//
//  temp_valid = false writes an empty temperature field.
// ---------------------------------------------------------------------------
bool datalog_start(const char* name);
void datalog_add(uint32_t t_ms, float force_kg, float current_a,
                 float drv_temp_c, bool temp_valid);
void datalog_stop();

bool     datalog_is_recording();
uint32_t datalog_sample_count();

const char* datalog_current_name();

// ---------------------------------------------------------------------------
//  MAINTENANCE
// ---------------------------------------------------------------------------
int datalog_erase_all();
int datalog_file_count();
