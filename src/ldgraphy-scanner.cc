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
#include <assert.h>
#include <string.h>

#include "scanline-sender.h"
#include "image-processing.h"
#include "laser-scribe-constants.h"
#include "sled-control.h"

#define LDGRAPHY_DEBUG_OUTPUTS 0

// Output images to TMP to observe the image processing progress.
constexpr bool debug_images = false;

// How fine can we get the focus ? Needs to be tuned per machine. The
// focus often is slightly oval.
constexpr float kFocus_Sled_Dia = 0.06;   // mm sled direction X
constexpr float kFocus_Scan_Dia = 0.116;  // mm scan direction Y

constexpr int SCAN_PIXELS = SCANLINE_DATA_SIZE * 8;
constexpr float deg2rad = 2*M_PI/360;

/*
 * These are constants that depend on the set-up of the LDGraphy hardware.
 */
constexpr int kHSyncShoulder = 200;   // Distance between sensor and start

// Right now, the frequencies are just factor 8192 apart, but we might
// choose in the future to create the mirror frequency with a different
// fraction to utilize more of the data bits.
constexpr float kLaserPixelFrequency = 200e6 / TICK_DELAY;
constexpr float kMirrorLineFrequency =
    kLaserPixelFrequency / TICKS_PER_MIRROR_SEGMENT;

// Mirror ticks as multiple of laser modulation time. This is a long way
// to write 8192, which is currently baked in, see above.
constexpr float kMirrorTicks = kLaserPixelFrequency / kMirrorLineFrequency;

// The SCAN_PIXELS only cover part of the full segment, to better utilize
// the bits in the usable area.
constexpr float kDataFraction = SCAN_PIXELS / kMirrorTicks;

constexpr int kMirrorFaces = 6;
// Reflection is 2*angle.
constexpr float kMirrorThrowAngle = 2 * (360 / kMirrorFaces) * deg2rad;
constexpr float segment_data_angle = kMirrorThrowAngle * kDataFraction;

// TODO(hzeller): read these numbers from the same source in the PostScript
// file and here.
constexpr float bed_length = 162.0;  // Sled length.
constexpr float bed_width  = 102.0;  // Width of the laser to throw.
constexpr float kScanAngle = 40.0;   // Degrees
constexpr float kRadiusMM = (bed_width/2) / tan(kScanAngle * deg2rad / 2);

LDGraphyScanner::LDGraphyScanner(float exposure_factor)
    : exposure_factor_(roundf(exposure_factor)),  // We only do integer for now.
      laser_sled_dot_size_(kFocus_Sled_Dia),
      laser_scan_dot_size_(kFocus_Scan_Dia)
{
    assert(exposure_factor >= 1);
#if LDGRAPHY_DEBUG_OUTPUTS
    fprintf(stderr, "Mirror freq: %.1fHz; Pixel clock: %.3fMHz.\n"
            "Data covering %.2f°/%.0f°, "
            "mechanically used: %.2f°, using %.1f%% data bits.\n",
            kMirrorLineFrequency, kLaserPixelFrequency / 1e6,
            segment_data_angle / deg2rad, kMirrorThrowAngle / deg2rad,
            kScanAngle, 100 * kScanAngle / (segment_data_angle / deg2rad));
#endif
}

LDGraphyScanner::~LDGraphyScanner() {
    if (backend_) backend_->Shutdown();
}

void LDGraphyScanner::SetLaserDotSize(float sled_dot_size_mm,
                                      float scan_dot_size_mm) {
    laser_sled_dot_size_ =
        (sled_dot_size_mm < -1e-6) ? kFocus_Sled_Dia : sled_dot_size_mm;
    laser_sled_dot_size_ =
        (scan_dot_size_mm < -1e-6) ? kFocus_Scan_Dia : scan_dot_size_mm;
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
#if LDGRAPHY_DEBUG_OUTPUTS
    fprintf(stderr, "Last Y coordinate full width = %d "
            "(Nerd-info: mapped to %d + %d shoulder = %d; scan angle: %.3f)\n",
            result.back(), (int)result.size(),
            kHSyncShoulder, (int)result.size() + kHSyncShoulder,
            scan_angle_range / deg2rad);
#endif
    return result;
}

bool LDGraphyScanner::SetImage(SimpleImage *img, float dpi) {
    const float image_resolution_mm_per_pixel = 25.4f / dpi;

    if (image_resolution_mm_per_pixel * img->width() > bed_length) {
        fprintf(stderr, "Board too long, does not fit in %.0fmm "
                "bed along sled.\n", bed_length);
        return false;
    }
    if (image_resolution_mm_per_pixel * img->height() > bed_width) {
        fprintf(stderr, "Board too high, does not fit in %.0fmm bed "
                "along laser scan.\n", bed_width);
        return false;
    }

    sled_step_per_image_pixel_ = (image_resolution_mm_per_pixel
                                  / SledControl::kSledMMperStep);
    scanlines_ = img->width() * sled_step_per_image_pixel_;

    // Create lookup-table: data pixel position to actual position in image.
    std::vector<int> y_lookup
        = PrepareTangensLookup(kRadiusMM / image_resolution_mm_per_pixel,
                               bed_width / image_resolution_mm_per_pixel,
                               SCAN_PIXELS);
#if 1
    // Due to the tangens, we have worse resolution at the edges. So look at the
    // beginning to report a worst-case resolution.
    const int beginning_pixel = y_lookup.size() / 20;  // First 5%
    const float laser_dots_per_image_pixel
        = 1.0 * beginning_pixel / (y_lookup[beginning_pixel] - y_lookup[0]);
    const float laser_dots_per_mm = (laser_dots_per_image_pixel
                                     / image_resolution_mm_per_pixel);
    // Let's see what the range is we need to scan.
    fprintf(stderr, "Exposure size: "
            "(X=%.1fmm along sled; Y=%.1fmm wide laser scan).\n"
            "Resolution %.0fdpi (%.3fmm/pixel)\n"
            "  %5.2f Sled steps per X-pixel.\n  %5.2f Laser dots per Y-pixel "
            "(%.3fmm dots @ %.0fkHz laser pixel frequency (=%.0fdpi)).\n",
            img->width() * image_resolution_mm_per_pixel,
            img->height() * image_resolution_mm_per_pixel,
            dpi, image_resolution_mm_per_pixel,
            sled_step_per_image_pixel_, laser_dots_per_image_pixel,
            1 / laser_dots_per_mm,
            kMirrorLineFrequency * kMirrorTicks / 1000.0,
            laser_dots_per_mm * 25.4);
    if (img->width() > img->height() + 5.0 / image_resolution_mm_per_pixel
        && img->width() * image_resolution_mm_per_pixel <= bed_width) {
        fprintf(stderr, "\n[ TIP: Currently the long side is along the sled. It "
                "would be faster in portrait orientation; give -R option ]\n\n");
    }
#endif

    if (debug_images) img->ToPGM(fopen("/tmp/ld_0_input.pgm", "w"));

    // Convert this into the image, tangens-corrected and rotated by
    // 90 degrees, so that we can send it line-by-line.
    if (debug_images) fprintf(stderr, " Tangens correct...\n");
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

    const float laser_resolution_in_mm_per_pixel = bed_width / y_lookup.size();
    if (debug_images) fprintf(stderr, " Thinning structures...\n");
    ThinImageStructures(
        exposure_image,
        laser_scan_dot_size_ / laser_resolution_in_mm_per_pixel / 2,
        laser_sled_dot_size_ / image_resolution_mm_per_pixel / 2);
    if (debug_images) exposure_image->ToPGM(fopen("/tmp/ld_1_tangens_and_thinned.pgm", "w"));

    // Now, convert it to the final bitmap to be sent to the output, padded
    // to the full data width.
    if (debug_images) fprintf(stderr, " Convert to Bitmap...\n");
    scan_image_.reset(new BitmapImage(SCAN_PIXELS, exposure_image->height()));
    for (int y = 0; y < exposure_image->height(); ++y) {
        for (int x = 0; x < exposure_image->width(); ++x) {
            scan_image_->Set(x + kHSyncShoulder, y,
                             exposure_image->at(x, y) > 127);
        }
    }
    if (debug_images) fprintf(stderr, "[image preparation done].\n");
    if (debug_images) scan_image_->ToPBM(fopen("/tmp/ld_3_bitmap.pbm", "w"));
    delete exposure_image;

    return true;
}

float LDGraphyScanner::estimated_time_seconds() const {
    return exposure_factor_ * (scanlines_ / kMirrorLineFrequency);
}

bool LDGraphyScanner::ScanExpose(bool do_move,
                                 std::function<bool(int d, int t)> progress_cont)
{
    if (!scan_image_) return true;
    if (!backend_) {
        fprintf(stderr, "No ScanLine backend provided\n");
        return false;
    }
    const int max = scan_image_->height();
    for (int scan = 0; scan < scanlines_ && progress_cont(scan, scanlines_); ++scan) {
        const int scan_pixel = roundf(scan / sled_step_per_image_pixel_);
        if (scan_pixel >= max) break;   // could be due to rounding.
        const uint8_t *row_data = scan_image_->GetRow(scan_pixel);
        if (!backend_->EnqueueNextData(row_data, SCANLINE_DATA_SIZE, do_move)) {
            // TODO: For now, the only error condition is getting a sync. Later
            // we might need to make this message more diverse.
            fprintf(stderr, "\nIssue synchronizing. Shutting down.\n");
            return false;
        }
	for (int i = 1; i < exposure_factor_; ++i) {
            backend_->EnqueueNextData(row_data, SCANLINE_DATA_SIZE, false);
	}
    }
    return true;
}

void LDGraphyScanner::ExposeJitterTest(int mirrors, int repeats) {
    assert(backend_);
    uint8_t *buffer = new uint8_t[mirrors * SCANLINE_DATA_SIZE]();
    // Only use part of our scanline for the test.
    const int mirror_line_len = (0.5 * SCANLINE_DATA_SIZE) / mirrors;
    uint8_t *write_pos = buffer;
    for (int m = 0; m < mirrors; ++m) {
        memset(write_pos, 0xff, mirror_line_len);
        write_pos += mirror_line_len + SCANLINE_DATA_SIZE;
    }
    for (int i = 0; i < repeats; ++i) {
        // We send six lines, one for each mirror. We don't know which mirror
        // is first currently, so it starts with whatever mirror was first.
        for (int m = 0; m < mirrors; ++m) {
            backend_->EnqueueNextData(buffer + m*SCANLINE_DATA_SIZE,
                                      SCANLINE_DATA_SIZE, false);
        }
    }
}
