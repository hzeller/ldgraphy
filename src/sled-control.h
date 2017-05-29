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
#ifndef LDGRAPHY_SLEDCONTROL_H
#define LDGRAPHY_SLEDCONTROL_H

#include <stdint.h>

// User-space control of the sled. This does not generate realtime exact
// timings of the steps, but is merely used to move the sled to the end-stops.
class SledControl {
public:
    // Stepper motor + lead settings. Here: 1/4 stepping and 24 threads/inch
    static constexpr float kSledMMperStep = (25.4 / 24) / 200 / 4;

    // Control sled with given stepper frequency (which is very approximate
    // right now).
    // If do_move is false, no hardware is controlled and Move() returns
    // immediately (can be used for dryrun).
    SledControl(int step_frequency, bool do_move = true);
    ~SledControl();

    // Move the number of millimeter or until end-stop was hit.
    // Returns actual amount moved. Positive numbers: forward,
    // negative numbers: backward.
    float Move(float millimeter);

private:
    const bool do_move_;
    int64_t sleep_nanos_;
};

#endif // LDGRAPHY_SLEDCONTROL_H
