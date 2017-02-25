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

struct PRUScanLineSender::QueueElement {
    volatile uint8_t state;
    volatile uint8_t data[SCANLINE_DATA_SIZE];
};

PRUScanLineSender::PRUScanLineSender() : running_(false), queue_pos_(0) {
    // Make sure that things are packed the way we think it is.
    assert(sizeof(QueueElement) == SCANLINE_ITEM_SIZE);
}
PRUScanLineSender::~PRUScanLineSender() {
    if (running_) pru_.Shutdown();
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
    if (running_) {
        fprintf(stderr, "Already running. Init() has no effect\n");
        return false;
    }

    if (!pru_.Init()) {
        return false;
    }

    if (!pru_.AllocateSharedMem((void**) &ring_buffer_,
                                QUEUE_LEN * sizeof(QueueElement))) {
        fprintf(stderr, "Cannot allocate shared memory\n");
        return false;
    }
    for (int i = 0; i < QUEUE_LEN; ++i) {
        ring_buffer_[i].state = CMD_EMPTY;
    }
    running_ = pru_.StartExecution();
    return running_;
}

void PRUScanLineSender::EnqueueNextData(const uint8_t *data, size_t size,
                                        bool sled_on) {
    assert(size == SCANLINE_DATA_SIZE);  // We only accept full lines :)
    WaitUntil(queue_pos_, CMD_EMPTY);
    unaligned_memcpy(ring_buffer_[queue_pos_].data, data, size);
    // TODO: maybe later transmit a byte telling how many steps the sled-stepper
    // should do. Including zero.
    ring_buffer_[queue_pos_].state =
        sled_on ? CMD_SCAN_DATA : CMD_SCAN_DATA_NO_SLED;

    queue_pos_++;
    queue_pos_ %= QUEUE_LEN;
}

bool PRUScanLineSender::Shutdown() {
    WaitUntil(queue_pos_, CMD_EMPTY);
    ring_buffer_[queue_pos_].state = CMD_EXIT;
    // PRU will set it to empty again when actually halted.
    WaitUntil(queue_pos_, CMD_DONE);
    pru_.Shutdown();
    running_ = false;
    fprintf(stderr, "Finished scanning.\n");
    return true;
}

void PRUScanLineSender::WaitUntil(int pos, int state) {
    while (ring_buffer_[pos].state != state) {
        pru_.WaitEvent();
    }
}

DummyScanLineSender::DummyScanLineSender() : lines_enqueued_(0) {
    fprintf(stderr, "Dry-run, including rough timing simulation.\n");
}

void DummyScanLineSender::EnqueueNextData(const uint8_t *, size_t, bool) {
    lines_enqueued_++;
    usleep(1000000 / 257);  // rough simulation of scan; see line_frequency
}
bool DummyScanLineSender::Shutdown() {
    fprintf(stderr, "Dry-run: total %d lines sent\n", lines_enqueued_);
    return true;
}
