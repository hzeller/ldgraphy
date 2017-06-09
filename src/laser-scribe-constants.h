/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * (c) 2017 Henner Zeller <h.zeller@acm.org>
 *
 * This file is part of LDGraphy http://github.com/hzeller/ldgraphy
 *
 * LDGraphy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LDGraphy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LDGraphy.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LASER_SCRIBE_CONSTANTS_H
#define LASER_SCRIBE_CONSTANTS_H

#define CMD_EMPTY   0
#define CMD_SCAN_DATA  1
#define CMD_SCAN_DATA_NO_SLED  2
#define CMD_EXIT    3
#define CMD_DONE    4

// The data per segment is sent in a bit-array. The laser covers about half
// the range of the 120 degrees it can do, wo we only send bits for the
// first half of global time ticks.
#define SCANLINE_HEADER_SIZE 1   // A single byte containing the command.
#define SCANLINE_DATA_SIZE 512   // Bytes that follow, containing the bit-set.

#define SCANLINE_ITEM_SIZE (SCANLINE_HEADER_SIZE + SCANLINE_DATA_SIZE)

#define QUEUE_LEN 8

// This is the CPU cycles (on the 200Mhz CPU) between each laser dot,
// determining the pixel clock.
// Other values are derived from this.
#define TICK_DELAY 100

// Each mirror segment is this number of pixel ticks long (only the first
// 8*SCANLINE_DATA_SIZE are filled with pixels, the rest is dead part of the
// segment).
// (NOTE, currently this is fixed to this number, need to replace
// MIRROR_COUNT_BIT logic)
#define TICKS_PER_MIRROR_SEGMENT (2*8*SCANLINE_DATA_SIZE)

#endif // LASER_SCRIBE_CONSTANTS_H
