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

#include "ldgraphy-scanner.h"

#include <math.h>

#include "scanline-sender.h"
#include "image-processing.h"
#include "laser-scribe-constants.h"

constexpr int SCAN_PIXELS = SCANLINE_DATA_SIZE * 8;
constexpr float deg2rad = 2*M_PI/360;

/*
 * These are constants that depend on the set-up of the LDGraphy hardware.
 */
constexpr int kHSyncShoulder = 100;   // Distance between sensor and start

// Stepper motor + lead settings. Here: 1/4 stepping and 24 threads/inch
constexpr float kSledMMperStep = (25.4 / 24) / 200 / 4;

constexpr int kMirrorFaces = 6;
// Reflection would cover 2*angle, but we only send data for the first
// half.
constexpr float segment_data_angle = (360 / kMirrorFaces) * deg2rad;

constexpr float kRadiusMM = 133.0;   // distance between rot-mirror -> PCB
//constexpr float bed_len = 160.0;
constexpr float bed_width = 100.0;   // Width of the laser to throw.

// How fine can we get the focus ? Needs to be tuned per machine.
constexpr float kFocus_X_Dia = 0.04;  // mm
constexpr float kFocus_Y_Dia = 0.04;  // mm

constexpr float line_frequency = 257.0;  // Hz. Measured with scope.

constexpr bool output_debug_images = false;

LDGraphyScanner::LDGraphyScanner(ScanLineSender *sink) : backend_(sink) {}
LDGraphyScanner::~LDGraphyScanner() {
    backend_->Shutdown();
}

// Create lookup-table for a particular image resolution: what pixel to extract
// from the image at a particular data position.
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
        const float y_pixel = roundf(tan(scan_angle_start + i * angle_step)
                                     * radius_pixels + scan_center);
        if (y_pixel > scan_range_pixels)
            break;
        result.push_back(roundf(y_pixel));
    }
#if 0
    fprintf(stderr, "Last Y coordinate full width = %d "
            "(Nerd-info: mapped to %d. scan angle: %.2f)\n",
            result.back(), (int)result.size(), scan_angle_range / deg2rad);
#endif
    return result;
}

void LDGraphyScanner::SetImage(SimpleImage *img, float dpi) {
    const float image_resolution_mm_per_pixel = 25.4f / dpi;
    sled_step_per_image_pixel_ = image_resolution_mm_per_pixel / kSledMMperStep;
    scanlines_ = img->width() * sled_step_per_image_pixel_;

    // Create lookup-table: data pixel position to actual position in image.
    std::vector<int> y_lookup
        = PrepareTangensLookup(kRadiusMM / image_resolution_mm_per_pixel,
                               bed_width / image_resolution_mm_per_pixel,
                               SCAN_PIXELS);
#if 1
    // Due to the tangens, we have worse resolution at the endges. So look at the
    // beginning to report a worst-case resolution.
    const int beginning_pixel = y_lookup.size() / 20;  // First 5%
    const float laser_dots_per_image_pixel
        = 1.0 * beginning_pixel / (y_lookup[beginning_pixel] - y_lookup[0]);
    const float laser_dots_per_mm = (laser_dots_per_image_pixel
                                     / image_resolution_mm_per_pixel);
    // Let's see what the range is we need to scan.
    fprintf(stderr, "Exposure size: (%.1fmm along sled; %.1fmm wide).\n"
            "Resolution %.0fdpi: "
            "%.1f sled steps per width-pixel; %.2f laser dots per height pixel. "
            "(%.3fmm dots @ %.0fkHz laser modulation.)\n",
            img->width() * image_resolution_mm_per_pixel,
            img->height() * image_resolution_mm_per_pixel,
            dpi, sled_step_per_image_pixel_, laser_dots_per_image_pixel,
            1 / laser_dots_per_mm, line_frequency * SCAN_PIXELS / 1000.0);
#endif
    // Convert this into the image, tangens-corrected and rotated by
    // 90 degrees, so that we can send it line-by-line.
    if (output_debug_images) fprintf(stderr, " Tangens correct...\n");
    SimpleImage *exposure_image = new SimpleImage(y_lookup.size(), img->width());
    for (int x_pixel = 0; x_pixel < img->width(); ++x_pixel) {
        for (size_t i = 0; i < y_lookup.size(); ++i) {
            const int y_pixel = img->height() - 1 - y_lookup[i];
            if (y_pixel < 0)
                break;
            exposure_image->at(i, x_pixel) = img->at(x_pixel, y_pixel);
        }
    }
    delete img;

    // Correct for physical laser dot thickness - taking off pixels off edges.
    if (output_debug_images) exposure_image->ToPGM(fopen("/tmp/ld_1.pgm", "w"));
    const float laser_resolution_in_mm_per_pixel = bed_width / y_lookup.size();
    if (output_debug_images) fprintf(stderr, " Thinning structures...\n");
    ThinImageStructures(exposure_image,
                        kFocus_X_Dia / laser_resolution_in_mm_per_pixel / 2,
                        kFocus_Y_Dia / image_resolution_mm_per_pixel / 2);
    if (output_debug_images) exposure_image->ToPGM(fopen("/tmp/ld_2.pgm", "w"));

    // Now, convert it to the final bitmap to be sent to the output, padded
    // to the full data width.
    if (output_debug_images) fprintf(stderr, " Convert to Bitmap...\n");
    scan_image_.reset(new BitmapImage(SCAN_PIXELS, exposure_image->height()));
    for (int y = 0; y < exposure_image->height(); ++y) {
        for (int x = 0; x < exposure_image->width(); ++x) {
            scan_image_->Set(x + kHSyncShoulder, y,
                             exposure_image->at(x, y) > 127);
        }
    }
    if (output_debug_images) fprintf(stderr, "[image preparation done].\n");
    if (output_debug_images) scan_image_->ToPBM(fopen("/tmp/ld_3.pbm", "w"));
    delete exposure_image;
}

float LDGraphyScanner::estimated_time_seconds() const {
    return scanlines_ / line_frequency;
}

void LDGraphyScanner::ScanExpose(bool do_move,
                                 std::function<bool(int d, int t)> progress_cont)
{
    if (!scan_image_) return;
    const int max = scan_image_->height();
    for (int scan = 0; scan < scanlines_ && progress_cont(scan, scanlines_); ++scan) {
        const int scan_pixel = roundf(scan / sled_step_per_image_pixel_);
        if (scan_pixel >= max) break;   // could be due to rounding.
        const uint8_t *row_data = scan_image_->GetRow(scan_pixel);
        if (!backend_->EnqueueNextData(row_data, SCANLINE_DATA_SIZE, do_move)) {
            // For now, the only error condition is getting a sync. Later we
            // might need to make this message more diverse.
            fprintf(stderr, "\nIssue synchronizing. Shutting down.\n");
            break;
        }
    }
}
