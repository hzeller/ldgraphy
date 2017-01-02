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

#include "uio-pruss-interface.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "laser-scribe-constants.h"

struct QueueElement {
    volatile uint8_t state;
    volatile uint8_t data[DATA_SIZE];
};

// Stop gap for compiler attempting to be overly clever when copying between
// host and PRU memory.
static void unaligned_memcpy(volatile void *dest, const void *src, size_t size) {
  volatile char *d = (volatile char*) dest;
  const char *s = (char*) src;
  const volatile char *end = d + size;
  while (d < end) {
    *d++ = *s++;
  }
}

// Sends scan lines to the ring-buffer in the PRU.
class ScanLineSender {
public:
    ScanLineSender() : running_(false), queue_pos_(0) {
        // Make sure that things are packed the way we think it is.
        assert(sizeof(struct QueueElement) == ITEM_SIZE);
    }
    ~ScanLineSender() {
        if (running_) pru_.Shutdown();
    }

    bool Init() {
        if (running_) {
            fprintf(stderr, "Already running. Init() has no effect\n");
            return false;
        }
        if (!pru_.Init()) {
            return false;
        }

        if (!pru_.AllocateSharedMem((void**) &ring_buffer_,
                                    QUEUE_LEN * sizeof(QueueElement))) {
            return false;
        }
        for (int i = 0; i < QUEUE_LEN; ++i) {
            ring_buffer_[i].state = STATE_EMPTY;
        }
        running_ = pru_.StartExecution();
        return running_;
    }

    void SetNextData(uint8_t *data, size_t size) {
        assert(size == DATA_SIZE);  // We only accept full lines :)
        WaitUntil(queue_pos_, STATE_EMPTY);
        unaligned_memcpy(ring_buffer_[queue_pos_].data, data, size);
        ring_buffer_[queue_pos_].state = STATE_FILLED;

        queue_pos_++;
        queue_pos_ %= QUEUE_LEN;
    }

    bool Shutdown() {
        WaitUntil(queue_pos_, STATE_EMPTY);
        ring_buffer_[queue_pos_].state = STATE_EXIT;
        // PRU will set it to empty again when actually halted.
        WaitUntil(queue_pos_, STATE_DONE);
        pru_.Shutdown();
        running_ = false;
        return true;
    }

private:
    void WaitUntil(int pos, int state) {
        while (ring_buffer_[pos].state != state) {
            pru_.WaitEvent();
        }
    }

    volatile QueueElement *ring_buffer_;
    bool running_;
    int queue_pos_;
    UioPrussInterface pru_;
};

int main(int argc, char *argv[]) {
    ScanLineSender line_sender;

    if (!line_sender.Init())
        return 1;

    // Create two buffers
    uint8_t buffer1[DATA_SIZE];
    uint8_t buffer2[DATA_SIZE];
    for (int i = 0; i < DATA_SIZE; ++i) {
        if (i > 80 && i < DATA_SIZE - 60) {
            buffer1[i] = (i % 2 == 0) ? 0xff : 0x00;
            buffer2[i] = ((i + 1) % 2 == 0) ? 0xff : 0x00;
        } else {
            buffer1[i] = 0x00;
            buffer2[i] = 0x00;
        }
    }

    fprintf(stderr, "wait until done.\n");
    sleep(1);  // Let motor spin up and synchronize

    //getchar();
    for (int i = 0; i < 1000; ++i) {
        if ((i / 50) % 2 == 0) {
            line_sender.SetNextData(buffer1, DATA_SIZE);
        } else  {
            line_sender.SetNextData(buffer2, DATA_SIZE);
        }
    }

    line_sender.Shutdown();

    fprintf(stderr, "Successful shutdown.\n");
    return 0;
}
