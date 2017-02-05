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

#define STATE_EMPTY   0
#define STATE_SCAN_DATA  1
#define STATE_SCAN_DATA_NO_SLED  2
#define STATE_EXIT    3
#define STATE_DONE    4

#define SCANLINE_HEADER_SIZE 1   // A single byte containting the state.
#define SCANLINE_DATA_SIZE 512   // Bytes that follow. We have 8x the pixels.

#define SCANLINE_ITEM_SIZE (SCANLINE_HEADER_SIZE + SCANLINE_DATA_SIZE)

#define QUEUE_LEN 8

#endif // LASER_SCRIBE_CONSTANTS_H
