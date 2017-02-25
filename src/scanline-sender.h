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

#if __GNUC__ == 4 && __GNUC_MINOR__ < 7
// Default ompiler on beaglebone black does not understand this one yet.
#  define override
#endif

class ScanLineSender {
public:
    virtual ~ScanLineSender() {}

    // Enqueue next scanline. Blocks until there is space in the ring buffer.
    // If "sled_on" == true, then advances the sled after this line.
    virtual void EnqueueNextData(const uint8_t *data, size_t size,
                                 bool sled_on) = 0;

    // Shutdown the system.
    virtual bool Shutdown() = 0;
};

// Lower-level interface with the hardware: send scan lines to the ring-buffer
// in the PRU.
class PRUScanLineSender : public ScanLineSender {
public:
    virtual ~PRUScanLineSender();

    // Create and initialize hardware (which might fail). Return non-null
    // object if successful.
    static ScanLineSender *Create();

    // -- ScanLineSender interface
    void EnqueueNextData(const uint8_t *data, size_t size, bool sled_on) override;
    bool Shutdown() override;

private:
    PRUScanLineSender();
    bool Init();

    struct QueueElement;

    void WaitUntil(int pos, int state);

    volatile QueueElement *ring_buffer_;
    bool running_;
    int queue_pos_;
    UioPrussInterface pru_;
};

class DummyScanLineSender : public ScanLineSender {
public:
    DummyScanLineSender();

    void EnqueueNextData(const uint8_t *data, size_t size, bool sled_on) override;
    bool Shutdown() override;

private:
    int lines_enqueued_;
};

#endif  // LDGRAPHY_SCANLINESENDER_H
