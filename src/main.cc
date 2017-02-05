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
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <vector>
#include <Magick++.h>

#include "laser-scribe-constants.h"
#include "containers.h"
#include "scanline-sender.h"

// These need to be modified depending on the machine.
constexpr float kSledMMperStep = (25.4 / 24) / 200 / 8;
constexpr float deg2rad = 2*M_PI/360;

// reflection: angle * 2
constexpr int kMirrorFaces = 6;
constexpr float segment_angle = 2 * (360 / kMirrorFaces) * deg2rad;

constexpr float kRadiusMM = 127.0;
constexpr float bed_len = 160.0;
constexpr float bed_width = 100.0;

constexpr float line_frequency = 223.0;  // Hz

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
            "\t-s <spinup>: Warmup seconds of mirror spin (default 10)\n"
            "\t-i         : Inverse image: black becomes laser on\n"
            "\t-M         : Inhibit move in x direction\n"
            "\t-F         : Run a focus round until the first Ctrl-C\n"
            "\t-n         : Dryrun. Do not do any scanning.\n"
            "\t-h         : This help\n");
    return errmsg ? 1 : 0;
}

// Radius is given in pixels; output is a vector given a data position shows
// where to look up the pixel in the original image. Maximum of 'num' values, but
// will return less as we only cover a smaller segment.
static std::vector<int> PrepareTangensLookup(float radius_pixels,
                                             float scan_range_pixels,
                                             size_t num) {
    std::vector<int> result;
    const float scan_angle_range = 2 * atan(scan_range_pixels/2 / radius_pixels);
    const float scan_angle_start = -scan_angle_range/2;
    const float angle_step = segment_angle / num;  // Overall arc mapped to full
    const float scan_center = scan_range_pixels / 2;
    // only the values between -angle_range/2 .. angle_range/2
    for (size_t i = 0; i < num; ++i) {
        int y_pixel = (int)roundf(tan(scan_angle_start + i * angle_step)
                                  * radius_pixels + scan_center);
        if (y_pixel > scan_range_pixels)
            break;
        result.push_back(y_pixel);
    }
    fprintf(stderr, "Last pixel was %d, using %d out of %d. scan angle: %.2f\n", result.back(),
            result.size(), num, scan_angle_range / deg2rad);
    return result;
}

// Load image and pre-process to be in a SimpleImage structure. Reading pixels
// from a generic image is incredibly slow in Magick::Image in particular on
// a Beaglebone, so this will save some time later in the scan loop.
static SimpleImage *LoadImage(const char *filename, bool invert) {
    std::vector<Magick::Image> bundle;
    try {
        fprintf(stderr, "Decode image %s\n", filename);
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
    fprintf(stderr, "Convert %dx%d image to bitmap. ",
            (int)img.columns(), (int)img.rows());
    fflush(stderr);
    SimpleImage *result = new SimpleImage(img.columns(), img.rows());
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const double gray = img.pixelColor(x, y).intensity();
            result->at(x, y) = ((gray >= 0.5) ^ invert) ? 1 : 0;
        }
    }
    fprintf(stderr, "Done.\n");
    return result;
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);
    int input_dpi = 600;
    bool dryrun = false;
    bool invert = false;
    bool do_focus = false;
    bool do_move = true;
    int spin_warmup_seconds = 10;

    int opt;
    while ((opt = getopt(argc, argv, "MFhnid:s:")) != -1) {
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
        case 'F':
            do_focus = true;
            break;
        case 's':
            spin_warmup_seconds = atoi(optarg);
            break;
        case 'M':
            do_move = false;
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
    const float sled_step_per_pixel = image_resolution_mm_per_pixel / kSledMMperStep;
    const int scanlines = img->width() * sled_step_per_pixel;

    constexpr int SCAN_PIXELS = SCANLINE_DATA_SIZE * 8;
    std::vector<int> y_lookup = PrepareTangensLookup(kRadiusMM / image_resolution_mm_per_pixel,
                                                     bed_width / image_resolution_mm_per_pixel,
                                                     SCAN_PIXELS);
    // Due to the tangens, we have worse resolution at the endges. So look at the
    // beginning to report a worst-case
    const int beginning_pixel = y_lookup.size() / 20;  // First 5%
    const float laser_dots_per_pixel
        = 1.0 * beginning_pixel / (y_lookup[beginning_pixel] - y_lookup[0]);
    const float laser_dots_per_mm = laser_dots_per_pixel / image_resolution_mm_per_pixel;
    // Let's see what the range is we need to scan.
    fprintf(stderr, "Exposure size: (%.1fmm long; %.1fmm wide).\n"
            "For resolution of %d dpi: "
            "%.1f sled steps per width-pixel; %s%.2f laser dots per height pixel. "
            "%.3fmm dot resolution; %.1fkHz laser modulation.\n",
            img->width() * image_resolution_mm_per_pixel,
            img->height() * image_resolution_mm_per_pixel,
            input_dpi,
            sled_step_per_pixel, laser_dots_per_pixel < 1.0 ? "only ":"",
            laser_dots_per_pixel,
            1 / laser_dots_per_mm,
            line_frequency * SCAN_PIXELS / 1000.0);
    fprintf(stderr, "Estimated time: %.0f seconds\n", scanlines / line_frequency);

    if (dryrun)
        return 0;

    ScanLineSender line_sender;
    if (!line_sender.Init())
        return 1;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    fprintf(stderr, "Spinning up mirror and wait for it to be stable:\n");
    const time_t finish_spinup = time(NULL) + spin_warmup_seconds;
    while (time(NULL) < finish_spinup && !interrupt_received) {
        fprintf(stderr, "\b\b\b\b%4d", (int) (finish_spinup - time(NULL)));
        usleep(500 * 1000);
    }
    fprintf(stderr, "\b\b\b\bDone.\n");

    BitArray<SCAN_PIXELS> scan_bits;

    if (do_focus) {
        fprintf(stderr, "FOCUS run. Actual exposure starts after Ctrl-C.\n");
        scan_bits.Clear();
        constexpr int ft = 4;  // focus thickness.
        // One dot every centimeter
        for (int j = 0; j < 10; ++j) {
            for (int i = 0; i < ft; ++i) scan_bits.Set(j*y_lookup.size()/10+i, true);
        }
        // mark center.
        for (int i = 0; i < 12; ++i) scan_bits.Set(y_lookup.size()/2+i, true);

        while (!interrupt_received) {
            line_sender.EnqueueNextData(scan_bits.buffer(), SCANLINE_DATA_SIZE, false);
        }
        interrupt_received = false;  // Re-arm signal handler.
        fprintf(stderr, "Focus run done.\n");
    }

    fprintf(stderr, "Exposure. Cancel with Ctrl-C\n");
    const time_t start_t = time(NULL);
    int prev_percent = -1;
    int x_last_img_pos = -1;
    scan_bits.Clear();
    for (int step_x = 0; step_x < scanlines && !interrupt_received; ++step_x) {
        const int percent = roundf(100.0 * step_x / scanlines);
        if (percent != prev_percent) {
            fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b%d%% (%d)",
                    percent, step_x);
            fflush(stderr);
            prev_percent = percent;
        }

        const int x_pixel = roundf(step_x / sled_step_per_pixel);
        if (x_pixel != x_last_img_pos) {
            x_last_img_pos = x_pixel;

            for (size_t i = 0; i < y_lookup.size(); ++i) {
                const int y_pixel = img->height() - y_lookup[i];
                if (y_pixel < 0)
                    break;
                scan_bits.Set(i, img->at(x_pixel, y_pixel));
            }
        }
        line_sender.EnqueueNextData(scan_bits.buffer(), SCANLINE_DATA_SIZE,
                                    do_move);
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
