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
#include <signal.h>
#include <math.h>
#include <strings.h>

#include <vector>
#include <Magick++.h>
#include "laser-scribe-constants.h"

// These need to be modified depending on the machine.
constexpr float kSledMMperStep = 2.0 / 200 / 8;
constexpr float kRadiusMM = 70.0;
constexpr int kMirrorFaces = 6;

constexpr float angle_fudging = 2.0;   // need to figure out that.

constexpr float kScanFrequency = 250;  // Hz. For rough time calculation

//constexpr int kBytePhaseShift = 0;  // corrective

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

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

    void SetNextData(const uint8_t *data, size_t size) {
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

template <int N>
class BitArray {
public:
    BitArray() { bzero(buffer_, (N+7)/8); }

    void Set(int b, bool value) {
        assert(b >= 0 && b < N);
        if (value)
            buffer_[b / 8] |= 1 << (7 - b % 8);
        else
            buffer_[b / 8] &= ~(1 << (7 - b % 8));
    }

    const uint8_t *buffer() const { return buffer_; }

private:
    uint8_t buffer_[N+7/8];
};

static int usage(const char *progname, const char *errmsg = NULL) {
    if (errmsg) {
        fprintf(stderr, "\n%s\n\n", errmsg);
    }
    fprintf(stderr, "Usage:\n%s [options] <image-file>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-d <val>   : DPI of input image. Default 600\n"
            "\t-i         : Inverse image: black becomes laser on\n"
            "\t-n         : Dryrun. Do not do any scanning.\n"
            "\t-h         : This help\n");
    return errmsg ? 1 : 0;
}

// A position in the image to sample from.
struct ScanLookup {
    int x, y;
    // TODO: define a kernel of some kind to average from.
};

// Returns where in the original image we have to sample for the given scan
// position. Return in an (preallocated) array of size num.
// This is vertically centered around the height of the image, thus half
// the y-coordinates will be negative.
static void CreateSampleLookup(float radius, size_t num,
                               ScanLookup *lookups) {
    const float range_radiants = 2 * M_PI / kMirrorFaces * angle_fudging;
    const float step_radiants = range_radiants / num;
    const float start = -range_radiants / 2;
    for (size_t i = 0; i < num; ++i) {
        // X is on the left, so negative.
        lookups[i].x = (int)roundf(-cosf(start + i * step_radiants) * radius);
        lookups[i].y = (int)roundf(-sinf(start + i * step_radiants) * radius);
    }
}

static bool LoadImage(const char *filename, Magick::Image *result) {
    std::vector<Magick::Image> bundle;
    try {
        readImages(&bundle, filename);
    } catch (std::exception& e) {
        fprintf(stderr, "%s: %s\n", filename, e.what());
        return false;
    }
    if (bundle.size() == 0) {
        fprintf(stderr, "%s: Couldn't load image", filename);
        return false;
    }
    if (bundle.size() > 1) {
        fprintf(stderr, "%s contains more than one image. Using first.\n",
                filename);
    }
    *result = bundle[0];
    return true;
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);
    int input_dpi = 600;
    bool dryrun = false;
    bool invert = false;

    int opt;
    while ((opt = getopt(argc, argv, "hnid:")) != -1) {
        switch (opt) {
        case 'h': return usage(argv[0]);
        case 'd':
            input_dpi = atoi(optarg);
            break;
        case 'n':
            dryrun = true;
            break;
        case 'i':
            invert = true;
            break;
        }
    }
    if (argc <= optind)
        return usage(argv[0], "Image file parameter expected.");
    if (argc > optind+1)
        return usage(argv[0], "Exactly one image file expected");
    if (input_dpi < 100 || input_dpi > 4800)
        return usage(argv[0], "DPI is somewhat out of usable range.");

    const char *filename = argv[optind];
    Magick::Image img;
    if (!LoadImage(filename, &img)) {
        return 1;
    }

    const float image_resolution_mm_per_pixel = 25.4f / input_dpi;
    const float image_scale = image_resolution_mm_per_pixel / kSledMMperStep;
    const int width = img.columns() * image_scale;
    const int height = img.rows() * image_scale;

    constexpr int SCAN_PIXELS = DATA_SIZE * 8;
    ScanLookup lookups[SCAN_PIXELS];
    CreateSampleLookup(kRadiusMM / kSledMMperStep, SCAN_PIXELS, lookups);

    // Let's see what the range is we need to scan.
    int start_x_offset = lookups[SCAN_PIXELS/2].x;
    const int image_half = height / 2;
    fprintf(stderr, "Exposure size: (%.1fmm, %.1fmm) = (%dpx, %dpx)\n",
            width * kSledMMperStep, height * kSledMMperStep,
            width, height);
    if (image_half > lookups[0].y)
        return usage(argv[0], "Image too wide for this device.\n");

    for (int i = 0; i < SCAN_PIXELS/2; ++i) {
        if (lookups[i].y < image_half) {
            start_x_offset = -lookups[i].x + 1;
            break;
        }
    }
    // Because we scan a bow, we need to scan more until the center is
    // covered.
    const int overshoot_scanning = start_x_offset + lookups[0].x;
    const int scanlines = width + overshoot_scanning;
    fprintf(stderr, "Need to scan %.1fmm further due to arc. "
            "Total %d scan lines. Total %.1f seconds.\n",
            overshoot_scanning * kSledMMperStep,
            scanlines, scanlines / kScanFrequency);
    if (dryrun)
        return 0;

    ScanLineSender line_sender;
    if (!line_sender.Init())
        return 1;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    sleep(1);  // Let motor spin up and synchronize

    fprintf(stderr, "\n");
    int prev_percent = -1;
    BitArray<SCAN_PIXELS> scan_bits;
    for (int x = 0; x < scanlines && !interrupt_received; ++x) {
        int percent = 100 * x / scanlines;
        if (percent != prev_percent) {
            fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b%d%% (%d)", percent, x);
            fflush(stderr);
            prev_percent = percent;
        }
        for (int i = 0; i < SCAN_PIXELS; ++i) {
            const int pic_x = lookups[i].x + x + start_x_offset;
            const int pic_y = lookups[i].y + image_half;
            if (pic_x < 0 || pic_x >= (int)height
                || pic_y < 0 || pic_y >= (int)width) {
                scan_bits.Set(i, false);
                continue;
            }
            const double gray = img.pixelColor(pic_x / image_scale,
                                               pic_y / image_scale).intensity();
            scan_bits.Set(i, (gray >= 0.5) ^ invert);
        }
        line_sender.SetNextData(scan_bits.buffer(), DATA_SIZE);
    }

    line_sender.Shutdown();

    if (interrupt_received)
        fprintf(stderr, "Received interrupt. Exposure might be incomplete.\n");
    else
        fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\bDone.\n");
    return 0;
}
