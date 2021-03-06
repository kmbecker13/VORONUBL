/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2016 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * configuration_store.cpp
 *
 * Configuration and EEPROM storage
 *
 * IMPORTANT:  Whenever there are changes made to the variables stored in EEPROM
 * in the functions below, also increment the version number. This makes sure that
 * the default values are used whenever there is a change to the data, to prevent
 * wrong data being written to the variables.
 *
 * ALSO: Variables in the Store and Retrieve sections must be in the same order.
 *       If a feature is disabled, some data must still be written that, when read,
 *       either sets a Sane Default, or results in No Change to the existing value.
 *
 */

#define EEPROM_VERSION "V30"

// Change EEPROM version if these are changed:
#define EEPROM_OFFSET 8
#define MAX_EXTRUDERS 4


#include "Marlin.h"
#include "language.h"
#include "planner.h"
#include "temperature.h"
#include "ultralcd.h"
#include "configuration_store.h"

#if ENABLED(UNIFIED_BED_LEVELING_FEATURE)
  #include "Bed_Leveling.h"
#endif

uint16_t eeprom_16_bit_CRC;
const char version[4] = EEPROM_VERSION;


// This is a CCITT approved 16-Bit CRC.  It will catch most errors
// that a Checksum will miss.

uint16_t crc16mp( void *data_p, uint16_t count) {
    uint16_t  xx;
    uint8_t   *ptr = (uint8_t *) data_p;

    while (count-- > 0) {
        eeprom_16_bit_CRC=(uint16_t)(eeprom_16_bit_CRC^(uint16_t)(((uint16_t)*ptr++)<<8));
        for (xx=0; xx<8; xx++) {
            if(eeprom_16_bit_CRC & 0x8000) { 
		    eeprom_16_bit_CRC=(uint16_t)((uint16_t)(eeprom_16_bit_CRC<<1)^0x1021); 
	    }
            else  { 
		    eeprom_16_bit_CRC=(uint16_t)(eeprom_16_bit_CRC<<1);                }
            }
        }
    return(eeprom_16_bit_CRC);
}

void _EEPROM_writeData(int &pos, uint8_t* value, uint8_t size) {
  uint8_t c;
  while (size--) {
    eeprom_write_byte((unsigned char*)pos, *value);
    c = eeprom_read_byte((unsigned char*)pos);
    if (c != *value) {
      SERIAL_ECHO_START;
      SERIAL_ECHOLNPGM(MSG_ERR_EEPROM_WRITE);
    }
//  eeprom_16_bit_CRC += c;
    crc16mp( &c, 1);
    pos++;
    value++;
  };
}
void _EEPROM_readData(int &pos, uint8_t* value, uint8_t size) {
  do {
    uint8_t c = eeprom_read_byte((unsigned char*)pos);
    *value = c;
//  eeprom_16_bit_CRC += c;
    crc16mp( &c, 1);
    pos++;
    value++;
  } while (--size);
}

/**
 * Post-process after Retrieve or Reset
 */
void Config_Postprocess() {
  // steps per s2 needs to be updated to agree with units per s2
  planner.reset_acceleration_rates();

  #if ENABLED(DELTA)
    recalc_delta_settings(delta_radius, delta_diagonal_rod);
  #endif

  #if ENABLED(PIDTEMP)
    thermalManager.updatePID();
  #endif

  calculate_volumetric_multipliers();
}

#if ENABLED(EEPROM_SETTINGS)

  #define DUMMY_PID_VALUE 3000.0f
  #define EEPROM_WRITE_VAR(pos, value) _EEPROM_writeData(pos, (uint8_t*)&value, sizeof(value))
  #define EEPROM_READ_VAR(pos, value) _EEPROM_readData(pos, (uint8_t*)&value, sizeof(value))

/**
 * M500 - Store Configuration
 */
void Config_StoreSettings()  {
  float dummy = 0.0f;
  char ver[4] = "000";
  int i = EEPROM_OFFSET;

  EEPROM_WRITE_VAR(i, ver);     // invalidate data first
  				// But as a "To Do" item, we really should include the version 
				// number in the CRC that we store.   For now it is OK, if it is
				// wrong (or corrupted) we should check it with the implicit comparison
				// we do.
				
  i += sizeof(eeprom_16_bit_CRC); // Skip the checksum slot

  eeprom_16_bit_CRC = 0xffff; // CCITT prefers a starting value of all 1 bits

  EEPROM_WRITE_VAR(i, planner.axis_steps_per_mm);
  EEPROM_WRITE_VAR(i, planner.max_feedrate);
  EEPROM_WRITE_VAR(i, planner.max_acceleration_mm_per_s2);
  EEPROM_WRITE_VAR(i, planner.acceleration);
  EEPROM_WRITE_VAR(i, planner.retract_acceleration);
  EEPROM_WRITE_VAR(i, planner.travel_acceleration);
  EEPROM_WRITE_VAR(i, planner.min_feedrate);
  EEPROM_WRITE_VAR(i, planner.min_travel_feedrate);
  EEPROM_WRITE_VAR(i, planner.min_segment_time);
  EEPROM_WRITE_VAR(i, planner.max_xy_jerk);
  EEPROM_WRITE_VAR(i, planner.max_z_jerk);
  EEPROM_WRITE_VAR(i, planner.max_e_jerk);
  EEPROM_WRITE_VAR(i, home_offset);


  #if !HAS_BED_PROBE
    float zprobe_zoffset = 0;
  #endif
  EEPROM_WRITE_VAR(i, zprobe_zoffset);

  // 9 floats for DELTA / Z_DUAL_ENDSTOPS
  #if ENABLED(DELTA)
    EEPROM_WRITE_VAR(i, endstop_adj);               // 3 floats
    EEPROM_WRITE_VAR(i, delta_radius);              // 1 float
    EEPROM_WRITE_VAR(i, delta_diagonal_rod);        // 1 float
    EEPROM_WRITE_VAR(i, delta_segments_per_second); // 1 float
    EEPROM_WRITE_VAR(i, delta_diagonal_rod_trim_tower_1);  // 1 float
    EEPROM_WRITE_VAR(i, delta_diagonal_rod_trim_tower_2);  // 1 float
    EEPROM_WRITE_VAR(i, delta_diagonal_rod_trim_tower_3);  // 1 float
    EEPROM_WRITE_VAR(i, delta_radius_trim_tower_1); // 1 float
    EEPROM_WRITE_VAR(i, delta_radius_trim_tower_2); // 1 float
    EEPROM_WRITE_VAR(i, delta_radius_trim_tower_3); // 1 float
  #elif ENABLED(Z_DUAL_ENDSTOPS)
    EEPROM_WRITE_VAR(i, z_endstop_adj);            // 1 float
    dummy = 0.0f;
    for (uint8_t q = 8; q--;) EEPROM_WRITE_VAR(i, dummy);
  #else
    dummy = 0.0f;
    for (uint8_t q = 9; q--;) EEPROM_WRITE_VAR(i, dummy);
  #endif

  #if DISABLED(ULTIPANEL)
    int plaPreheatHotendTemp = PLA_PREHEAT_HOTEND_TEMP, plaPreheatHPBTemp = PLA_PREHEAT_HPB_TEMP, plaPreheatFanSpeed = PLA_PREHEAT_FAN_SPEED,
        absPreheatHotendTemp = ABS_PREHEAT_HOTEND_TEMP, absPreheatHPBTemp = ABS_PREHEAT_HPB_TEMP, absPreheatFanSpeed = ABS_PREHEAT_FAN_SPEED;
  #endif // !ULTIPANEL

  EEPROM_WRITE_VAR(i, plaPreheatHotendTemp);
  EEPROM_WRITE_VAR(i, plaPreheatHPBTemp);
  EEPROM_WRITE_VAR(i, plaPreheatFanSpeed);
  EEPROM_WRITE_VAR(i, absPreheatHotendTemp);
  EEPROM_WRITE_VAR(i, absPreheatHPBTemp);
  EEPROM_WRITE_VAR(i, absPreheatFanSpeed);

  for (uint8_t e = 0; e < MAX_EXTRUDERS; e++) {

    #if ENABLED(PIDTEMP)
      if (e < HOTENDS) {
        EEPROM_WRITE_VAR(i, PID_PARAM(Kp, e));
        EEPROM_WRITE_VAR(i, PID_PARAM(Ki, e));
        EEPROM_WRITE_VAR(i, PID_PARAM(Kd, e));
        #if ENABLED(PID_ADD_EXTRUSION_RATE)
          EEPROM_WRITE_VAR(i, PID_PARAM(Kc, e));
        #else
          dummy = 1.0f; // 1.0 = default kc
          EEPROM_WRITE_VAR(i, dummy);
        #endif
      }
      else
    #endif // !PIDTEMP
      {
        dummy = DUMMY_PID_VALUE; // When read, will not change the existing value
        EEPROM_WRITE_VAR(i, dummy); // Kp
        dummy = 0.0f;
        for (uint8_t q = 3; q--;) EEPROM_WRITE_VAR(i, dummy); // Ki, Kd, Kc
      }

  } // Hotends Loop

  #if DISABLED(PID_ADD_EXTRUSION_RATE)
    int lpq_len = 20;
  #endif
  EEPROM_WRITE_VAR(i, lpq_len);

  #if DISABLED(PIDTEMPBED)
    dummy = DUMMY_PID_VALUE;
    for (uint8_t q = 3; q--;) EEPROM_WRITE_VAR(i, dummy);
  #else
    EEPROM_WRITE_VAR(i, thermalManager.bedKp);
    EEPROM_WRITE_VAR(i, thermalManager.bedKi);
    EEPROM_WRITE_VAR(i, thermalManager.bedKd);
  #endif

  #if !HAS_LCD_CONTRAST
    const int lcd_contrast = 32;
  #endif
  EEPROM_WRITE_VAR(i, lcd_contrast);

  #if ENABLED(SCARA)
    EEPROM_WRITE_VAR(i, axis_scaling); // 3 floats
  #else
    dummy = 1.0f;
    EEPROM_WRITE_VAR(i, dummy);
  #endif

  #if ENABLED(FWRETRACT)
    EEPROM_WRITE_VAR(i, autoretract_enabled);
    EEPROM_WRITE_VAR(i, retract_length);
    #if EXTRUDERS > 1
      EEPROM_WRITE_VAR(i, retract_length_swap);
    #else
      dummy = 0.0f;
      EEPROM_WRITE_VAR(i, dummy);
    #endif
    EEPROM_WRITE_VAR(i, retract_feedrate_mm_s);
    EEPROM_WRITE_VAR(i, retract_zlift);
    EEPROM_WRITE_VAR(i, retract_recover_length);
    #if EXTRUDERS > 1
      EEPROM_WRITE_VAR(i, retract_recover_length_swap);
    #else
      dummy = 0.0f;
      EEPROM_WRITE_VAR(i, dummy);
    #endif
    EEPROM_WRITE_VAR(i, retract_recover_feedrate);
  #endif // FWRETRACT

  EEPROM_WRITE_VAR(i, volumetric_enabled);

  // Save filament sizes
  for (uint8_t q = 0; q < MAX_EXTRUDERS; q++) {
    if (q < EXTRUDERS) dummy = filament_size[q];
    EEPROM_WRITE_VAR(i, dummy);
  }

  uint16_t final_checksum = eeprom_16_bit_CRC;

  int j = EEPROM_OFFSET;
  EEPROM_WRITE_VAR(j, version);
  EEPROM_WRITE_VAR(j, final_checksum);

  // Report storage size
  SERIAL_ECHO_START;
  SERIAL_ECHOPAIR("Settings Stored (", i);
  SERIAL_ECHOLNPGM(" bytes)");

// It can be argued that the M500 should only save the state of the Unified Bed Leveling System and
// not the active mesh.  Especially since the Unified Bed Leveling System has its own Load and Store
// Mesh commands.   But this approach will cause a lot of user pain because they will assume everything
// modified in RAM has been saved to EEPROM after a M500 command.   So for now, we will also save the
// active mesh if we have an EEPROM Storage Slot identified.
//
// If a user does a M502 that will invalidate the EEPROM Storage Slot until the user does either a Load
// or Store Mesh command within the Unified Bed Leveling System.  In that case, no mesh will be stored
// but the user can easily load what ever mesh is appropriate from EEPROM because they should be untouched.
// 
#ifdef UNIFIED_BED_LEVELING_FEATURE
  blm.store_state();
  if (blm.state.EEPROM_storage_slot >= 0 )
	blm.store_mesh( blm.state.EEPROM_storage_slot );
#endif
}

/**
 * M501 - Retrieve Configuration
 */
void Config_RetrieveSettings() {
  int i = EEPROM_OFFSET;
  char stored_ver[4];
  uint16_t Stored_CRC;
  EEPROM_READ_VAR(i, stored_ver);
  EEPROM_READ_VAR(i, Stored_CRC);

  if (strncmp(version, stored_ver, 3) != 0) {
    Config_ResetDefault();
  }
  else {
    float dummy = 0;

    eeprom_16_bit_CRC = 0xffff; // CCITT prefers a starting value of all 1 bits

    // version number match
    EEPROM_READ_VAR(i, planner.axis_steps_per_mm);
    EEPROM_READ_VAR(i, planner.max_feedrate);
    EEPROM_READ_VAR(i, planner.max_acceleration_mm_per_s2);

    EEPROM_READ_VAR(i, planner.acceleration);
    EEPROM_READ_VAR(i, planner.retract_acceleration);
    EEPROM_READ_VAR(i, planner.travel_acceleration);
    EEPROM_READ_VAR(i, planner.min_feedrate);
    EEPROM_READ_VAR(i, planner.min_travel_feedrate);
    EEPROM_READ_VAR(i, planner.min_segment_time);
    EEPROM_READ_VAR(i, planner.max_xy_jerk);
    EEPROM_READ_VAR(i, planner.max_z_jerk);
    EEPROM_READ_VAR(i, planner.max_e_jerk);
    EEPROM_READ_VAR(i, home_offset);

    #if !HAS_BED_PROBE
      float zprobe_zoffset = 0;
    #endif
    EEPROM_READ_VAR(i, zprobe_zoffset);

    #if ENABLED(DELTA)
      EEPROM_READ_VAR(i, endstop_adj);                // 3 floats
      EEPROM_READ_VAR(i, delta_radius);               // 1 float
      EEPROM_READ_VAR(i, delta_diagonal_rod);         // 1 float
      EEPROM_READ_VAR(i, delta_segments_per_second);  // 1 float
      EEPROM_READ_VAR(i, delta_diagonal_rod_trim_tower_1);  // 1 float
      EEPROM_READ_VAR(i, delta_diagonal_rod_trim_tower_2);  // 1 float
      EEPROM_READ_VAR(i, delta_diagonal_rod_trim_tower_3);  // 1 float
      EEPROM_READ_VAR(i, delta_radius_trim_tower_1); // 1 float
      EEPROM_READ_VAR(i, delta_radius_trim_tower_2); // 1 float
      EEPROM_READ_VAR(i, delta_radius_trim_tower_3); // 1 floa
      recalc_delta_settings(delta_radius, delta_diagonal_rod);
    #elif ENABLED(Z_DUAL_ENDSTOPS)
      EEPROM_READ_VAR(i, z_endstop_adj);
      dummy = 0.0f;
      for (uint8_t q=8; q--;) EEPROM_READ_VAR(i, dummy);
    #else
      dummy = 0.0f;
      for (uint8_t q=9; q--;) EEPROM_READ_VAR(i, dummy);
    #endif

    #if DISABLED(ULTIPANEL)
      int plaPreheatHotendTemp, plaPreheatHPBTemp, plaPreheatFanSpeed,
          absPreheatHotendTemp, absPreheatHPBTemp, absPreheatFanSpeed;
    #endif

    EEPROM_READ_VAR(i, plaPreheatHotendTemp);
    EEPROM_READ_VAR(i, plaPreheatHPBTemp);
    EEPROM_READ_VAR(i, plaPreheatFanSpeed);
    EEPROM_READ_VAR(i, absPreheatHotendTemp);
    EEPROM_READ_VAR(i, absPreheatHPBTemp);
    EEPROM_READ_VAR(i, absPreheatFanSpeed);

    #if ENABLED(PIDTEMP)
      for (uint8_t e = 0; e < MAX_EXTRUDERS; e++) {
        EEPROM_READ_VAR(i, dummy); // Kp
        if (e < HOTENDS && dummy != DUMMY_PID_VALUE) {
          // do not need to scale PID values as the values in EEPROM are already scaled
          PID_PARAM(Kp, e) = dummy;
          EEPROM_READ_VAR(i, PID_PARAM(Ki, e));
          EEPROM_READ_VAR(i, PID_PARAM(Kd, e));
          #if ENABLED(PID_ADD_EXTRUSION_RATE)
            EEPROM_READ_VAR(i, PID_PARAM(Kc, e));
          #else
            EEPROM_READ_VAR(i, dummy);
          #endif
        }
        else {
          for (uint8_t q=3; q--;) EEPROM_READ_VAR(i, dummy); // Ki, Kd, Kc
        }
      }
    #else // !PIDTEMP
      // 4 x 4 = 16 slots for PID parameters
      for (uint8_t q = MAX_EXTRUDERS * 4; q--;) EEPROM_READ_VAR(i, dummy);  // Kp, Ki, Kd, Kc
    #endif // !PIDTEMP

    #if DISABLED(PID_ADD_EXTRUSION_RATE)
      int lpq_len;
    #endif
    EEPROM_READ_VAR(i, lpq_len);

    #if ENABLED(PIDTEMPBED)
      EEPROM_READ_VAR(i, dummy); // bedKp
      if (dummy != DUMMY_PID_VALUE) {
        thermalManager.bedKp = dummy;
        EEPROM_READ_VAR(i, thermalManager.bedKi);
        EEPROM_READ_VAR(i, thermalManager.bedKd);
      }
    #else
      for (uint8_t q=3; q--;) EEPROM_READ_VAR(i, dummy); // bedKp, bedKi, bedKd
    #endif

    #if !HAS_LCD_CONTRAST
      int lcd_contrast;
    #endif
    EEPROM_READ_VAR(i, lcd_contrast);

    #if ENABLED(SCARA)
      EEPROM_READ_VAR(i, axis_scaling);  // 3 floats
    #else
      EEPROM_READ_VAR(i, dummy);
    #endif

    #if ENABLED(FWRETRACT)
      EEPROM_READ_VAR(i, autoretract_enabled);
      EEPROM_READ_VAR(i, retract_length);
      #if EXTRUDERS > 1
        EEPROM_READ_VAR(i, retract_length_swap);
      #else
        EEPROM_READ_VAR(i, dummy);
      #endif
      EEPROM_READ_VAR(i, retract_feedrate_mm_s);
      EEPROM_READ_VAR(i, retract_zlift);
      EEPROM_READ_VAR(i, retract_recover_length);
      #if EXTRUDERS > 1
        EEPROM_READ_VAR(i, retract_recover_length_swap);
      #else
        EEPROM_READ_VAR(i, dummy);
      #endif
      EEPROM_READ_VAR(i, retract_recover_feedrate);
    #endif // FWRETRACT

    EEPROM_READ_VAR(i, volumetric_enabled);

    for (uint8_t q = 0; q < MAX_EXTRUDERS; q++) {
      EEPROM_READ_VAR(i, dummy);
      if (q < EXTRUDERS) filament_size[q] = dummy;
    }

    if (eeprom_16_bit_CRC == Stored_CRC) {
      SERIAL_ECHO_START;
      SERIAL_ECHO(version);
      SERIAL_ECHOPAIR(" stored settings retrieved (", i);
      SERIAL_ECHOLNPGM(" bytes)");
      Config_Postprocess(); 
	}
    else {
      SERIAL_ERROR_START;
/*
SERIAL_ECHO(" eeprom_16_bit_CRC:");
prt_hex_word(eeprom_16_bit_CRC);
SERIAL_ECHO(" Stored_CRC:");
prt_hex_word( Stored_CRC);
SERIAL_ERRORLNPGM("\n");
*/
      SERIAL_ERRORLNPGM("EEPROM checksum mismatch");
      Config_ResetDefault();
    }

#ifdef UNIFIED_BED_LEVELING_FEATURE
    Unified_Bed_Leveling_EEPROM_start = (i + 32) & 0xfff8;  	// Pad the end of configuration data so it
   								// can float up or down a little bit without
								// disrupting the Unified Bed Leveling data 
    blm.load_state();

if ( blm.state.active )
SERIAL_ECHO(" UBL Active!\n");
else
SERIAL_ECHO(" UBL Not active!\n");

    if ( blm.sanity_check() == 0 ) {
      int tmp_mesh; 		// We want to preserve whether the UBL System is Active
      bool tmp_active;		// If it is, we want to preserve the Mesh that is being used.
      tmp_mesh = blm.state.EEPROM_storage_slot;
      tmp_active = blm.state.active; 
      SERIAL_ECHOLNPGM("\nInitializing Bed Leveling State to current firmware settings.\n");
      blm.state = blm.pre_initialized;
      blm.state.EEPROM_storage_slot = tmp_mesh;
      blm.state.active              = tmp_active;
    }
    else {
      SERIAL_PROTOCOLPGM("?Unable to enable Unified Bed Leveling.\n");
      blm.state = blm.pre_initialized;
      blm.reset();
      blm.store_state();
    }

    if (blm.state.EEPROM_storage_slot >= 0 )  {
	blm.load_mesh( blm.state.EEPROM_storage_slot );
      	SERIAL_ECHOPAIR("Mesh ", blm.state.EEPROM_storage_slot );
        SERIAL_ECHOLNPGM(" loaded from storage.");
    }
    else  {
        blm.reset();
SERIAL_ECHO("UBL System reset() \n");
    }
#endif
  }

  #if ENABLED(EEPROM_CHITCHAT)
    Config_PrintSettings();
  #endif
}

#endif // EEPROM_SETTINGS

/**
 * M502 - Reset Configuration
 */
void Config_ResetDefault() {
  float tmp1[] = DEFAULT_AXIS_STEPS_PER_UNIT;
  float tmp2[] = DEFAULT_MAX_FEEDRATE;
  long tmp3[] = DEFAULT_MAX_ACCELERATION;
  for (uint8_t i = 0; i < NUM_AXIS; i++) {
    planner.axis_steps_per_mm[i] = tmp1[i];
    planner.max_feedrate[i] = tmp2[i];
    planner.max_acceleration_mm_per_s2[i] = tmp3[i];
    #if ENABLED(SCARA)
      if (i < COUNT(axis_scaling))
        axis_scaling[i] = 1;
    #endif
  }

  planner.acceleration = DEFAULT_ACCELERATION;
  planner.retract_acceleration = DEFAULT_RETRACT_ACCELERATION;
  planner.travel_acceleration = DEFAULT_TRAVEL_ACCELERATION;
  planner.min_feedrate = DEFAULT_MINIMUMFEEDRATE;
  planner.min_segment_time = DEFAULT_MINSEGMENTTIME;
  planner.min_travel_feedrate = DEFAULT_MINTRAVELFEEDRATE;
  planner.max_xy_jerk = DEFAULT_XYJERK;
  planner.max_z_jerk = DEFAULT_ZJERK;
  planner.max_e_jerk = DEFAULT_EJERK;
  home_offset[X_AXIS] = home_offset[Y_AXIS] = home_offset[Z_AXIS] = 0;

  #if ENABLED(UNIFIED_BED_LEVELING_FEATURE)
    blm.reset();
  #endif

  #if HAS_BED_PROBE
    zprobe_zoffset = Z_PROBE_OFFSET_FROM_EXTRUDER;
  #endif

  #if ENABLED(DELTA)
    endstop_adj[X_AXIS] = endstop_adj[Y_AXIS] = endstop_adj[Z_AXIS] = 0;
    delta_radius =  DELTA_RADIUS;
    delta_diagonal_rod =  DELTA_DIAGONAL_ROD;
    delta_segments_per_second =  DELTA_SEGMENTS_PER_SECOND;
    delta_diagonal_rod_trim_tower_1 = DELTA_DIAGONAL_ROD_TRIM_TOWER_1;
    delta_diagonal_rod_trim_tower_2 = DELTA_DIAGONAL_ROD_TRIM_TOWER_2;
    delta_diagonal_rod_trim_tower_3 = DELTA_DIAGONAL_ROD_TRIM_TOWER_3;
    delta_radius_trim_tower_1 = DELTA_RADIUS_TRIM_TOWER_1;
    delta_radius_trim_tower_2 = DELTA_RADIUS_TRIM_TOWER_2;
    delta_radius_trim_tower_3 = DELTA_RADIUS_TRIM_TOWER_3;

    recalc_delta_settings(delta_radius, delta_diagonal_rod);
  #elif ENABLED(Z_DUAL_ENDSTOPS)
    z_endstop_adj = 0;
  #endif

  #if ENABLED(ULTIPANEL)
    plaPreheatHotendTemp = PLA_PREHEAT_HOTEND_TEMP;
    plaPreheatHPBTemp = PLA_PREHEAT_HPB_TEMP;
    plaPreheatFanSpeed = PLA_PREHEAT_FAN_SPEED;
    absPreheatHotendTemp = ABS_PREHEAT_HOTEND_TEMP;
    absPreheatHPBTemp = ABS_PREHEAT_HPB_TEMP;
    absPreheatFanSpeed = ABS_PREHEAT_FAN_SPEED;
  #endif

  #if HAS_LCD_CONTRAST
    lcd_contrast = DEFAULT_LCD_CONTRAST;
  #endif

  #if ENABLED(PIDTEMP)
    #if ENABLED(PID_PARAMS_PER_HOTEND)
      for (uint8_t e = 0; e < HOTENDS; e++)
    #else
      int e = 0; UNUSED(e); // only need to write once
    #endif
    {
      PID_PARAM(Kp, e) = DEFAULT_Kp;
      PID_PARAM(Ki, e) = scalePID_i(DEFAULT_Ki);
      PID_PARAM(Kd, e) = scalePID_d(DEFAULT_Kd);
      #if ENABLED(PID_ADD_EXTRUSION_RATE)
        PID_PARAM(Kc, e) = DEFAULT_Kc;
      #endif
    }
    #if ENABLED(PID_ADD_EXTRUSION_RATE)
      lpq_len = 20; // default last-position-queue size
    #endif
  #endif // PIDTEMP

  #if ENABLED(PIDTEMPBED)
    thermalManager.bedKp = DEFAULT_bedKp;
    thermalManager.bedKi = scalePID_i(DEFAULT_bedKi);
    thermalManager.bedKd = scalePID_d(DEFAULT_bedKd);
  #endif

  #if ENABLED(FWRETRACT)
    autoretract_enabled = false;
    retract_length = RETRACT_LENGTH;
    #if EXTRUDERS > 1
      retract_length_swap = RETRACT_LENGTH_SWAP;
    #endif
    retract_feedrate_mm_s = RETRACT_FEEDRATE;
    retract_zlift = RETRACT_ZLIFT;
    retract_recover_length = RETRACT_RECOVER_LENGTH;
    #if EXTRUDERS > 1
      retract_recover_length_swap = RETRACT_RECOVER_LENGTH_SWAP;
    #endif
    retract_recover_feedrate = RETRACT_RECOVER_FEEDRATE;
  #endif

  volumetric_enabled = false;
  for (uint8_t q = 0; q < COUNT(filament_size); q++)
    filament_size[q] = DEFAULT_NOMINAL_FILAMENT_DIA;

  Config_Postprocess();
// If the user is trying to restore the printer to its hard coded default settings, probably
// it makes sense to reset the mesh.  It is cleaner for the Unified Bed Leveling System to
// save this state but to be consistent with how the other parameters are handled, we will 
// wait until the user does an M500 before that happens.
//
#ifdef UNIFIED_BED_LEVELING_FEATURE
      int tmp_mesh; 		// We want to preserve whether the UBL System is Active
      bool tmp_active;		// If it is, we want to preserve the Mesh that is being used.
      tmp_mesh = blm.state.EEPROM_storage_slot;
      tmp_active = blm.state.active; 
      blm.state = blm.pre_initialized;
      blm.state.EEPROM_storage_slot = tmp_mesh;
      blm.state.active              = tmp_active;
//    blm.reset();
//    blm.store_state();
#endif

  SERIAL_ECHO_START;
  SERIAL_ECHOLNPGM("Hardcoded Default Settings Loaded");
}

#if DISABLED(DISABLE_M503)

#define CONFIG_ECHO_START do{ if (!forReplay) SERIAL_ECHO_START; }while(0)

/**
 * M503 - Print Configuration
 */
void Config_PrintSettings(bool forReplay) {
  // Always have this function, even with EEPROM_SETTINGS disabled, the current values will be shown

  CONFIG_ECHO_START;

  if (!forReplay) {
    SERIAL_ECHOLNPGM("Steps per unit:");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M92 X", planner.axis_steps_per_mm[X_AXIS]);
  SERIAL_ECHOPAIR(" Y", planner.axis_steps_per_mm[Y_AXIS]);
  SERIAL_ECHOPAIR(" Z", planner.axis_steps_per_mm[Z_AXIS]);
  SERIAL_ECHOPAIR(" E", planner.axis_steps_per_mm[E_AXIS]);
  SERIAL_EOL;

  CONFIG_ECHO_START;

  #if ENABLED(SCARA)
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Scaling factors:");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M365 X", axis_scaling[X_AXIS]);
    SERIAL_ECHOPAIR(" Y", axis_scaling[Y_AXIS]);
    SERIAL_ECHOPAIR(" Z", axis_scaling[Z_AXIS]);
    SERIAL_EOL;
    CONFIG_ECHO_START;
  #endif // SCARA

  if (!forReplay) {
    SERIAL_ECHOLNPGM("Maximum feedrates (mm/s):");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M203 X", planner.max_feedrate[X_AXIS]);
  SERIAL_ECHOPAIR(" Y", planner.max_feedrate[Y_AXIS]);
  SERIAL_ECHOPAIR(" Z", planner.max_feedrate[Z_AXIS]);
  SERIAL_ECHOPAIR(" E", planner.max_feedrate[E_AXIS]);
  SERIAL_EOL;

  CONFIG_ECHO_START;
  if (!forReplay) {
    SERIAL_ECHOLNPGM("Maximum Acceleration (mm/s2):");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M201 X", planner.max_acceleration_mm_per_s2[X_AXIS]);
  SERIAL_ECHOPAIR(" Y", planner.max_acceleration_mm_per_s2[Y_AXIS]);
  SERIAL_ECHOPAIR(" Z", planner.max_acceleration_mm_per_s2[Z_AXIS]);
  SERIAL_ECHOPAIR(" E", planner.max_acceleration_mm_per_s2[E_AXIS]);
  SERIAL_EOL;
  CONFIG_ECHO_START;
  if (!forReplay) {
    SERIAL_ECHOLNPGM("Accelerations: P=printing, R=retract and T=travel");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M204 P", planner.acceleration);
  SERIAL_ECHOPAIR(" R", planner.retract_acceleration);
  SERIAL_ECHOPAIR(" T", planner.travel_acceleration);
  SERIAL_EOL;

  CONFIG_ECHO_START;
  if (!forReplay) {
    SERIAL_ECHOLNPGM("Advanced variables: S=Min feedrate (mm/s), T=Min travel feedrate (mm/s), B=minimum segment time (ms), X=maximum XY jerk (mm/s),  Z=maximum Z jerk (mm/s),  E=maximum E jerk (mm/s)");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M205 S", planner.min_feedrate);
  SERIAL_ECHOPAIR(" T", planner.min_travel_feedrate);
  SERIAL_ECHOPAIR(" B", planner.min_segment_time);
  SERIAL_ECHOPAIR(" X", planner.max_xy_jerk);
  SERIAL_ECHOPAIR(" Z", planner.max_z_jerk);
  SERIAL_ECHOPAIR(" E", planner.max_e_jerk);
  SERIAL_EOL;

  CONFIG_ECHO_START;
  if (!forReplay) {
    SERIAL_ECHOLNPGM("Home offset (mm)");
    CONFIG_ECHO_START;
  }
  SERIAL_ECHOPAIR("  M206 X", home_offset[X_AXIS]);
  SERIAL_ECHOPAIR(" Y", home_offset[Y_AXIS]);
  SERIAL_ECHOPAIR(" Z", home_offset[Z_AXIS]);
  SERIAL_EOL;

  #if ENABLED(UNIFIED_BED_LEVELING_FEATURE)
    SERIAL_ECHOLNPGM("Unified Bed Leveling:");
    CONFIG_ECHO_START;

    SERIAL_ECHOPGM("System is: ");
    if ( blm.state.active )
       SERIAL_ECHOLNPGM("Active\n");
    else
       SERIAL_ECHOLNPGM("Deactive\n");
    SERIAL_ECHOPAIR("Active Mesh Slot: ", blm.state.EEPROM_storage_slot );
    SERIAL_ECHOLNPGM("\n");

    SERIAL_ECHO("z_offset: ");
    SERIAL_ECHO_F( blm.state.z_offset, 6 );
    SERIAL_PROTOCOLPGM("\n");

/*    
    SERIAL_ECHOPAIR("EEPROM can hold ", (int) ((E2END-sizeof(blm.state )
				    		-Unified_Bed_Leveling_EEPROM_start)/sizeof(blm.z_values)));
    SERIAL_ECHOLNPGM(" meshes. \n");
*/

    SERIAL_ECHOPAIR("\nMESH_NUM_X_POINTS  ", MESH_NUM_X_POINTS );
    SERIAL_ECHOPAIR("\nMESH_NUM_Y_POINTS  ", MESH_NUM_Y_POINTS );
    
    SERIAL_ECHOPAIR("\nMESH_MIN_X         ", MESH_MIN_X );
    SERIAL_ECHOPAIR("\nMESH_MIN_Y         ", MESH_MIN_Y );
   
    SERIAL_ECHOPAIR("\nMESH_MAX_X         ", MESH_MAX_X );
    SERIAL_ECHOPAIR("\nMESH_MAX_Y         ", MESH_MAX_Y );
  
    SERIAL_ECHO("\nMESH_X_DIST        ");
    SERIAL_ECHO_F( MESH_X_DIST, 6 );
    SERIAL_ECHO("\nMESH_Y_DIST        ");
    SERIAL_ECHO_F( MESH_Y_DIST, 6 );
    SERIAL_PROTOCOLPGM("\n");
    SERIAL_EOL;
  #endif

  #if ENABLED(DELTA)
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Endstop adjustment (mm):");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M666 X", endstop_adj[X_AXIS]);
    SERIAL_ECHOPAIR(" Y", endstop_adj[Y_AXIS]);
    SERIAL_ECHOPAIR(" Z", endstop_adj[Z_AXIS]);
    SERIAL_EOL;
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Delta settings: L=diagonal_rod, R=radius, S=segments_per_second, ABC=diagonal_rod_trim_tower_[123]");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M665 L", delta_diagonal_rod);
    SERIAL_ECHOPAIR(" R", delta_radius);
    SERIAL_ECHOPAIR(" S", delta_segments_per_second);
    SERIAL_ECHOPAIR(" A", delta_diagonal_rod_trim_tower_1);
    SERIAL_ECHOPAIR(" B", delta_diagonal_rod_trim_tower_2);
    SERIAL_ECHOPAIR(" C", delta_diagonal_rod_trim_tower_3);
    SERIAL_ECHOPAIR(" I", delta_radius_trim_tower_1 );
    SERIAL_ECHOPAIR(" J", delta_radius_trim_tower_2 );
    SERIAL_ECHOPAIR(" K", delta_radius_trim_tower_3 );
    SERIAL_EOL;
  #elif ENABLED(Z_DUAL_ENDSTOPS)
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Z2 Endstop adjustment (mm):");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M666 Z", z_endstop_adj);
    SERIAL_EOL;
  #endif // DELTA

  #if ENABLED(ULTIPANEL)
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Material heatup parameters:");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M145 S0 H", plaPreheatHotendTemp);
    SERIAL_ECHOPAIR(" B", plaPreheatHPBTemp);
    SERIAL_ECHOPAIR(" F", plaPreheatFanSpeed);
    SERIAL_EOL;
    CONFIG_ECHO_START;
    SERIAL_ECHOPAIR("  M145 S1 H", absPreheatHotendTemp);
    SERIAL_ECHOPAIR(" B", absPreheatHPBTemp);
    SERIAL_ECHOPAIR(" F", absPreheatFanSpeed);
    SERIAL_EOL;
  #endif // ULTIPANEL

  #if HAS_PID_HEATING

    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("PID settings:");
    }
    #if ENABLED(PIDTEMP)
      #if HOTENDS > 1
        if (forReplay) {
          for (uint8_t i = 0; i < HOTENDS; i++) {
            CONFIG_ECHO_START;
            SERIAL_ECHOPAIR("  M301 E", i);
            SERIAL_ECHOPAIR(" P", PID_PARAM(Kp, i));
            SERIAL_ECHOPAIR(" I", unscalePID_i(PID_PARAM(Ki, i)));
            SERIAL_ECHOPAIR(" D", unscalePID_d(PID_PARAM(Kd, i)));
            #if ENABLED(PID_ADD_EXTRUSION_RATE)
              SERIAL_ECHOPAIR(" C", PID_PARAM(Kc, i));
              if (i == 0) SERIAL_ECHOPAIR(" L", lpq_len);
            #endif
            SERIAL_EOL;
          }
        }
        else
      #endif // HOTENDS > 1
      // !forReplay || HOTENDS == 1
      {
        CONFIG_ECHO_START;
        SERIAL_ECHOPAIR("  M301 P", PID_PARAM(Kp, 0)); // for compatibility with hosts, only echo values for E0
        SERIAL_ECHOPAIR(" I", unscalePID_i(PID_PARAM(Ki, 0)));
        SERIAL_ECHOPAIR(" D", unscalePID_d(PID_PARAM(Kd, 0)));
        #if ENABLED(PID_ADD_EXTRUSION_RATE)
          SERIAL_ECHOPAIR(" C", PID_PARAM(Kc, 0));
          SERIAL_ECHOPAIR(" L", lpq_len);
        #endif
        SERIAL_EOL;
      }
    #endif // PIDTEMP

    #if ENABLED(PIDTEMPBED)
      CONFIG_ECHO_START;
      SERIAL_ECHOPAIR("  M304 P", thermalManager.bedKp);
      SERIAL_ECHOPAIR(" I", unscalePID_i(thermalManager.bedKi));
      SERIAL_ECHOPAIR(" D", unscalePID_d(thermalManager.bedKd));
      SERIAL_EOL;
    #endif

  #endif // PIDTEMP || PIDTEMPBED

  #if HAS_LCD_CONTRAST
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("LCD Contrast:");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M250 C", lcd_contrast);
    SERIAL_EOL;
  #endif

  #if ENABLED(FWRETRACT)

    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Retract: S=Length (mm) F:Speed (mm/m) Z: ZLift (mm)");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M207 S", retract_length);
    #if EXTRUDERS > 1
      SERIAL_ECHOPAIR(" W", retract_length_swap);
    #endif
    SERIAL_ECHOPAIR(" F", retract_feedrate_mm_s * 60);
    SERIAL_ECHOPAIR(" Z", retract_zlift);
    SERIAL_EOL;
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Recover: S=Extra length (mm) F:Speed (mm/m)");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M208 S", retract_recover_length);
    #if EXTRUDERS > 1
      SERIAL_ECHOPAIR(" W", retract_recover_length_swap);
    #endif
    SERIAL_ECHOPAIR(" F", retract_recover_feedrate * 60);
    SERIAL_EOL;
    CONFIG_ECHO_START;
    if (!forReplay) {
      SERIAL_ECHOLNPGM("Auto-Retract: S=0 to disable, 1 to interpret extrude-only moves as retracts or recoveries");
      CONFIG_ECHO_START;
    }
    SERIAL_ECHOPAIR("  M209 S", autoretract_enabled ? 1 : 0);
    SERIAL_EOL;

  #endif // FWRETRACT

  /**
   * Volumetric extrusion M200
   */
  if (!forReplay) {
    CONFIG_ECHO_START;
    SERIAL_ECHOPGM("Filament settings:");
    if (volumetric_enabled)
      SERIAL_EOL;
    else
      SERIAL_ECHOLNPGM(" Disabled");
  }

  CONFIG_ECHO_START;
  SERIAL_ECHOPAIR("  M200 D", filament_size[0]);
  SERIAL_EOL;
  #if EXTRUDERS > 1
    CONFIG_ECHO_START;
    SERIAL_ECHOPAIR("  M200 T1 D", filament_size[1]);
    SERIAL_EOL;
    #if EXTRUDERS > 2
      CONFIG_ECHO_START;
      SERIAL_ECHOPAIR("  M200 T2 D", filament_size[2]);
      SERIAL_EOL;
      #if EXTRUDERS > 3
        CONFIG_ECHO_START;
        SERIAL_ECHOPAIR("  M200 T3 D", filament_size[3]);
        SERIAL_EOL;
      #endif
    #endif
  #endif

  if (!volumetric_enabled) {
    CONFIG_ECHO_START;
    SERIAL_ECHOLNPGM("  M200 D0");
  }

  /**
   * Auto Bed Leveling
   */
  #if HAS_BED_PROBE
    if (!forReplay) {
      CONFIG_ECHO_START;
      SERIAL_ECHOLNPGM("Z-Probe Offset (mm):");
    }
    CONFIG_ECHO_START;
    SERIAL_ECHOPAIR("  M851 Z", zprobe_zoffset);
    SERIAL_EOL;
  #endif
}

#endif // !DISABLE_M503
