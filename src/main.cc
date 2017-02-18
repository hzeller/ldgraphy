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
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <png.h>

#include <vector>

#include "laser-scribe-constants.h"
#include "containers.h"
#include "scanline-sender.h"

// These need to be modified depending on the machine.
constexpr float kSledMMperStep = (25.4 / 24) / 200 / 8;
constexpr float deg2rad = 2*M_PI/360;

constexpr int kMirrorFaces = 6;
// Reflection would cover 2*angle, but we only send data for the first
// half.
constexpr float segment_data_angle = (360 / kMirrorFaces) * deg2rad;

constexpr float kRadiusMM = 130.0;
constexpr float bed_len = 160.0;
constexpr float bed_width = 100.0;

constexpr float line_frequency = 257.0;  // Hz. Measured with scope.

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname, const char *errmsg = NULL) {
    if (errmsg) {
        fprintf(stderr, "\n%s\n\n", errmsg);
    }
    fprintf(stderr, "Usage:\n%s [options] <png-image-file>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-d <val>   : DPI of input image. Default -1\n"
            "\t-i         : Inverse image: black becomes laser on\n"
            "\t-M         : Inhibit move in x direction\n"
            "\t-F         : Run a focus round until the first Ctrl-C\n"
            "\t-n         : Dryrun. Do not do any scanning.\n"
            "\t-h         : This help\n");
    return errmsg ? 1 : 0;
}

SimpleImage *LoadPNGImage(const char *filename, bool invert, double *dpi) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Opening image");
        return NULL;
    }
    const png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                   NULL, NULL, NULL);
    if (!png) return NULL;

    const png_infop info = png_create_info_struct(png);
    if (!info) return NULL;

    png_init_io(png, fp);
    png_read_info(png, info);

    const png_byte bit_depth = png_get_bit_depth(png, info);
    if (bit_depth == 16)
        png_set_strip_16(png);

    const png_byte color_type = png_get_color_type(png, info);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_rgb_to_gray(png, PNG_ERROR_ACTION_NONE,
                            PNG_RGB_TO_GRAY_DEFAULT, PNG_RGB_TO_GRAY_DEFAULT);
    }

    const int width = png_get_image_width(png, info);
    const int height = png_get_image_height(png, info);

    png_uint_32 res_x, res_y;
    int unit;
    png_get_pHYs(png, info, &res_x, &res_y, &unit);
    *dpi = (unit == PNG_RESOLUTION_METER) ? res_x / (1000 / 25.4) : res_x;

    png_read_update_info(png, info);
    png_bytep *const row_pointers = new png_bytep[height];
    const int rowbytes = png_get_rowbytes(png, info);
    png_byte *const data = new png_byte[height * rowbytes];
    for (int y = 0; y < height; y++) {
        row_pointers[y] = data + rowbytes * y;
    }

    png_read_image(png, row_pointers);

    fprintf(stderr, "Reading %dx%d image.\n", width, height);
    fflush(stderr);
    const int bytes_per_pixel = rowbytes / width;
    png_byte *pixel = data;
    SimpleImage *result = new SimpleImage(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result->at(x, y) = ((*pixel > 128) ^ invert) ? 255 : 0;
            pixel += bytes_per_pixel;
        }
    }

    delete [] data;
    delete [] row_pointers;

    fclose(fp);
    return result;
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
    const float angle_step = segment_data_angle / num;  // Overall arc mapped to full
    const float scan_center = scan_range_pixels / 2;
    // only the values between -angle_range/2 .. angle_range/2
    for (size_t i = 0; i < num; ++i) {
        int y_pixel = (int)roundf(tan(scan_angle_start + i * angle_step)
                                  * radius_pixels + scan_center);
        if (y_pixel > scan_range_pixels)
            break;
        result.push_back(y_pixel);
    }
    fprintf(stderr, "%d pixels in Y direction "
            "(Nerd-info: using %d out of %d. scan angle: %.2f)\n",
            result.back(), (int)result.size(),
            (int)num, scan_angle_range / deg2rad);
    return result;
}

int main(int argc, char *argv[]) {
    double input_dpi = -1;
    bool dryrun = false;
    bool invert = false;
    bool do_focus = false;
    bool do_move = true;

    int opt;
    while ((opt = getopt(argc, argv, "MFhnid:")) != -1) {
        switch (opt) {
        case 'h': return usage(argv[0]);
        case 'd':
            input_dpi = atof(optarg);
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
        case 'M':
            do_move = false;
            break;
        }
    }

    if (argc <= optind)
        return usage(argv[0], "Image file parameter expected.");
    if (argc > optind+1)
        return usage(argv[0], "Exactly one image file expected");

    const char *filename = argv[optind];
    SimpleImage *img = LoadPNGImage(filename, invert, &input_dpi);
    if (img == NULL)
        return 1;

    if (input_dpi < 100 || input_dpi > 20000)
        return usage(argv[0], "Couldn't extract usable DPI from image. Please provide -d <dpi>");

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
            "Resolution %.0fdpi: "
            "%.1f sled steps per width-pixel; %.2f laser dots per height pixel. "
            "%.3fmm dot resolution; %.1fkHz laser modulation.\n",
            img->width() * image_resolution_mm_per_pixel,
            img->height() * image_resolution_mm_per_pixel,
            input_dpi, sled_step_per_pixel, laser_dots_per_pixel,
            1 / laser_dots_per_mm, line_frequency * SCAN_PIXELS / 1000.0);
    fprintf(stderr, "Estimated time: %.0f seconds\n", scanlines / line_frequency);

    if (dryrun)
        return 0;

    ScanLineSender line_sender;
    if (!line_sender.Init())
        return 1;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    BitArray<SCAN_PIXELS> scan_bits;
    scan_bits.SetOffset(100);

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
