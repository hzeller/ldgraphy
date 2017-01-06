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
#ifndef LDGRAPHY_SCANLINESENDER_H
#define LDGRAPHY_SCANLINESENDER_H

#include "uio-pruss-interface.h"
#include <stdint.h>

// Sends scan lines to the ring-buffer in the PRU.
class ScanLineSender {
public:
    ScanLineSender();
    ~ScanLineSender();

    // Initialize and return if successful.
    bool Init();

    // Enqueue next scanline. Blocks until there is space in the ring buffer.
    void EnqueueNextData(const uint8_t *data, size_t size, bool sled_on);

    // Shutdown the system.
    bool Shutdown();

private:
    class QueueElement;

    void WaitUntil(int pos, int state);

    volatile QueueElement *ring_buffer_;
    bool running_;
    int queue_pos_;
    UioPrussInterface pru_;
};

#endif  // LDGRAPHY_SCANLINESENDER_H
