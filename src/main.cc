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

#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <vector>
#include <Magick++.h>

#include "laser-scribe-constants.h"
#include "containers.h"
#include "scanline-sender.h"

// These need to be modified depending on the machine.
constexpr float kSledMMperStep = 2.0 / 200 / 8;
constexpr float kRadiusMM = 70.0;
constexpr int kMirrorFaces = 6;

constexpr float kScanFrequency = 245*2;  // Hz. For rough time calculation

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

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
};

// Returns where in the original image we have to sample for the given scan
// position. Return in an (preallocated) array of size num.
// This is vertically centered around the height of the image, thus half
// the y-coordinates will be negative.
static void PrepareSampleLookupArc(float radius, size_t num,
                                   ScanLookup *lookups) {
    const float range_radiants = 2 * M_PI / kMirrorFaces;
    const float step_radiants = range_radiants / num;
    const float start = -range_radiants / 2;
    for (size_t i = 0; i < num; ++i) {
        // X is on the left, so negative.
        lookups[i].x = (int)roundf(-cosf(start + i * step_radiants) * radius);
        // Y is scanning from the bottom up, hence negative.
        lookups[i].y = (int)roundf(-sinf(start + i * step_radiants) * radius);
    }
}

// Load image and pre-process to be in a SimpleImage structure. Reading pixels
// from a generic image is incredibly slow in Magick::Image in particular on
// a Beaglebone, so this will save some time later in the scan loop.
static SimpleImage *LoadImage(const char *filename, bool invert) {
    std::vector<Magick::Image> bundle;
    try {
        readImages(&bundle, filename);
    } catch (std::exception& e) {
        fprintf(stderr, "%s: %s\n", filename, e.what());
        return NULL;
    }
    if (bundle.size() == 0) {
        fprintf(stderr, "%s: Couldn't load image", filename);
        return NULL;
    }
    if (bundle.size() > 1) {
        fprintf(stderr, "%s contains more than one image. Using first.\n",
                filename);
    }
    const Magick::Image &img = bundle[0];
    SimpleImage *result = new SimpleImage(img.columns(), img.rows());
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const double gray = img.pixelColor(x, y).intensity();
            result->at(x, y) = ((gray >= 0.5) ^ invert) ? 1 : 0;
        }
    }
    return result;
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
    if (input_dpi < 100 || input_dpi > 20000)
        return usage(argv[0], "DPI is somewhat out of usable range.");

    const char *filename = argv[optind];
    SimpleImage *img = LoadImage(filename, invert);
    if (img == NULL)
        return 1;

    const float image_resolution_mm_per_pixel = 25.4f / input_dpi;
    const float image_scale = image_resolution_mm_per_pixel / kSledMMperStep;
    const int width = img->width() * image_scale;
    const int height = img->height() * image_scale;

    constexpr int SCAN_PIXELS = SCANLINE_DATA_SIZE * 8;
    ScanLookup scan_pos[SCAN_PIXELS];
    PrepareSampleLookupArc(kRadiusMM / kSledMMperStep, SCAN_PIXELS, scan_pos);

    // Let's see what the range is we need to scan.
    int start_x_offset = scan_pos[SCAN_PIXELS/2].x;
    const int image_half = height / 2;
    fprintf(stderr, "Exposure size: (%.1fmm, %.1fmm)\n",
            width * kSledMMperStep, height * kSledMMperStep);
    if (image_half > scan_pos[0].y) {
        // The outer edge of the scanline's Y determines how wide we can go
        fprintf(stderr, "Image too wide for this device (%.1fmm).\n",
                scan_pos[0].y * 2 * kSledMMperStep);
        return 1;
    }
    for (int i = 0; i < SCAN_PIXELS/2; ++i) {
        if (scan_pos[i].y < image_half) {
            start_x_offset = -scan_pos[i].x + 1;
            break;
        }
    }

    // Because we scan a bow, we need to scan more until the center is
    // covered.
    const int overshoot_scanning = -start_x_offset - scan_pos[SCAN_PIXELS/2].x;
    const int scanlines = width + overshoot_scanning;
    fprintf(stderr, "Need to scan %.1fmm further due to arc. "
            "Total %d scan lines. Total %.0f seconds.\n",
            overshoot_scanning * kSledMMperStep,
            scanlines, ceil(scanlines / kScanFrequency));
    if (dryrun)
        return 0;

    ScanLineSender line_sender;
    if (!line_sender.Init())
        return 1;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    sleep(1);  // Let motor spin up and synchronize

    fprintf(stderr, "\n");
    const time_t start_t = time(NULL);
    int prev_percent = -1;
    BitArray<SCAN_PIXELS> scan_bits;
    for (int x = 0; x < scanlines && !interrupt_received; ++x) {
        // Some status
        int percent = 100 * x / scanlines;
        if (percent != prev_percent) {
            fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b%d%% (%d)", percent, x);
            fflush(stderr);
            prev_percent = percent;
        }

        // Assemble one scan line and send it over.
        for (int i = 0; i < SCAN_PIXELS; ++i) {
            const int pic_x = scan_pos[i].x + x + start_x_offset;
            const int pic_y = scan_pos[i].y + image_half;
            if (pic_x < 0 || pic_x >= (int)height
                || pic_y < 0 || pic_y >= (int)width) {
                scan_bits.Set(i, false);
                continue;
            }
            scan_bits.Set(i, img->at(pic_x / image_scale,
                                     pic_y / image_scale));
        }
        line_sender.EnqueueNextData(scan_bits.buffer(), SCANLINE_DATA_SIZE);
    }

    line_sender.Shutdown();

    if (interrupt_received)
        fprintf(stderr, "Received interrupt. Exposure might be incomplete.\n");
    else
        fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
                "Done (%ld sec)\n", time(NULL) - start_t);

    delete img;
    return 0;
}
