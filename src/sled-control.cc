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
#include "sled-control.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#include "generic-gpio.h"

#define SLED_MOTOR_STEP (GPIO_1_BASE | 16)
#define SLED_MOTOR_DIR (GPIO_1_BASE | 18)
#define SLED_MOTOR_ENABLE (GPIO_1_BASE | 19)

#define SLED_ENDSWITCH_FRONT (GPIO_0_BASE | 31)
#define SLED_ENDSWITCH_BACK (GPIO_1_BASE | 28)

SledControl::SledControl(int step_frequency)
    : sleep_nanos_(1e9 / step_frequency) {
    sleep_nanos_ -= 50000;  // some fudging.
    if (sleep_nanos_ < 0) sleep_nanos_ = 0;
    map_gpio();  // We the only non-PRU code that needs access to gpio.
}

SledControl::~SledControl() {
    unmap_gpio();
}

float SledControl::Move(float millimeter) {
    uint32_t switch_to_watch;
    if (millimeter < 0) {
        switch_to_watch = SLED_ENDSWITCH_BACK;
        set_gpio(SLED_MOTOR_DIR);
    } else {
        switch_to_watch = SLED_ENDSWITCH_FRONT;
        clr_gpio(SLED_MOTOR_DIR);
    }

    const int steps = ::abs((int)(millimeter / kSledMMperStep));
    int remaining = steps;

    struct timespec wait_time = {0, 0};

    // We start slower with larger gaps that reduce simulating a crude
    // acceleration profile.
    uint64_t acceleration_extra = sleep_nanos_ * 3;

    // Very crude stepmotor move. This is of course not realtime steps as
    // we just use nanosleep() so might not be very smooth depending on how
    // busy Linux is.
    // Let's put that into the PRU later.
    clr_gpio(SLED_MOTOR_ENABLE);
    while (remaining) {
        if (get_gpio(switch_to_watch) == 0)
            break;
        wait_time.tv_nsec = sleep_nanos_ + acceleration_extra;
        set_gpio(SLED_MOTOR_STEP);
        for (int i = 0; i < 1000; ++i) asm("nop");
        clr_gpio(SLED_MOTOR_STEP);
        nanosleep(&wait_time, NULL);
        --remaining;
        acceleration_extra /= 1.01f;
    }
    set_gpio(SLED_MOTOR_ENABLE);
    return (steps - remaining) * kSledMMperStep * (millimeter < 0 ? -1 : 1);
}
