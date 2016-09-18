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
 * Arduino Mega with RAMPS v1.4 adjusted pin assignments
 *
 *  MKS v1.3  (Extruder, Fan, Bed)
 *  MKS v1.3  (Extruder, Extruder, Fan, Bed)
 *  MKS v1.4  (Extruder, Fan, Bed)
 *  MKS v1.4  (Extruder, Extruder, Fan, Bed)
 */

#define BOARD_NAME "MKS > v1.3"

#include "pins_RAMPS_14_EFB.h"

#undef HEATER_1_PIN
#define HEATER_1_PIN        7 // EXTRUDER 2 (-1 on RAMPS 1.4)

#if ENABLED(VIKI2) || ENABLED(miniVIKI)
  //#undef SD_DETECT_PIN
  //#define SD_DETECT_PIN 49  // For easy adapter board
#endif
