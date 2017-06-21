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
#include "scanline-sender.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "laser-scribe-constants.h"

const char *ScanLineSender::StatusToString(Status s) {
    switch (s) {
    case STATUS_NOT_RUNNING: return "Not running";
    case STATUS_RUNNING:     return "Running";
    case STATUS_DEBUG_BREAK: return "Debug break point";
    case STATUS_ERR_MIRROR_SYNC: return "Error while synchronizing mirror";
    case STATUS_ERR_TIME_OVERRUN: return "State machine time overrun";
    default: return "Unknown status";
    }
}

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

namespace {
struct QueueElement {
    volatile uint8_t state;
    volatile uint8_t data[SCANLINE_DATA_SIZE];
} __attribute__((packed));
}

struct PRUScanLineSender::PRUCommunication {
    volatile uint8_t error_status;
    volatile QueueElement ring_buffer[QUEUE_LEN];
} __attribute__((packed));

PRUScanLineSender::PRUScanLineSender() : status_(STATUS_NOT_RUNNING),
                                         queue_pos_(0) {
    // Make sure that things are packed the way we think it is.
    assert(sizeof(QueueElement) == SCANLINE_ITEM_SIZE);
}
PRUScanLineSender::~PRUScanLineSender() {
    if (status_ == STATUS_RUNNING) pru_.Shutdown();
}

ScanLineSender *PRUScanLineSender::Create() {
    PRUScanLineSender *result = new PRUScanLineSender();
    if (!result->Init()) {
        delete result;
        return nullptr;
    }
    return result;
}

bool PRUScanLineSender::Init() {
    if (status_ == STATUS_RUNNING) {
        fprintf(stderr, "Already running. Init() has no effect\n");
        return false;
    }

    if (!pru_.Init()) {
        return false;
    }

    if (!pru_.AllocateSharedMem((void**) &pru_data_, sizeof(*pru_data_))) {
        fprintf(stderr, "Cannot allocate shared memory\n");
        return false;
    }
    pru_data_->error_status = ERROR_NONE;
    for (int i = 0; i < QUEUE_LEN; ++i) {
        pru_data_->ring_buffer[i].state = CMD_EMPTY;
    }
    status_ = pru_.StartExecution() ? STATUS_RUNNING : STATUS_NOT_RUNNING;
    return status_ == STATUS_RUNNING;
}

bool PRUScanLineSender::EnqueueNextData(const uint8_t *data, size_t size,
                                        bool sled_on) {
    if (status_ != STATUS_RUNNING) return false;
    assert(size == SCANLINE_DATA_SIZE);  // We only accept full lines :)
    WaitUntil(queue_pos_, CMD_EMPTY);
    unaligned_memcpy(pru_data_->ring_buffer[queue_pos_].data, data, size);
    // TODO: maybe later transmit a byte telling how many steps the sled-stepper
    // should do. Including zero.
    pru_data_->ring_buffer[queue_pos_].state =
        sled_on ? CMD_SCAN_DATA : CMD_SCAN_DATA_NO_SLED;

    queue_pos_++;
    queue_pos_ %= QUEUE_LEN;
    return status_ == STATUS_RUNNING;
}

bool PRUScanLineSender::Shutdown() {
    if (status_ != STATUS_RUNNING) return false;
    WaitUntil(queue_pos_, CMD_EMPTY);
    pru_data_->ring_buffer[queue_pos_].state = CMD_EXIT;
    // PRU will set it to empty again when actually halted.
    WaitUntil(queue_pos_, CMD_DONE);
    pru_.Shutdown();
    status_ = STATUS_NOT_RUNNING;
    fprintf(stderr, "Finished scanning.\n");
    return true;
}

void PRUScanLineSender::WaitUntil(int pos, int state) {
    for (;;) {
        const int buffer_state = pru_data_->ring_buffer[pos].state;
        if (buffer_state == state)
            return;

        if (buffer_state == CMD_DONE) {  // Error cond. Should be separate state
            status_ = (enum Status) pru_data_->error_status;
            return;
        }
        pru_.WaitEvent();
    }
}

DummyScanLineSender::DummyScanLineSender() : lines_enqueued_(0) {
    fprintf(stderr, "Dry-run, including rough timing simulation.\n");
}

bool DummyScanLineSender::EnqueueNextData(const uint8_t *, size_t, bool) {
    lines_enqueued_++;
    usleep(1000000 / 244);  // rough simulation of scan; see line_frequency
    return true;
}
bool DummyScanLineSender::Shutdown() {
    fprintf(stderr, "Dry-run: total %d lines sent\n", lines_enqueued_);
    return true;
}
