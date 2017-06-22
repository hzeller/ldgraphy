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

#ifndef LDGRAPHY_DEBUG_OUTPUTS
#  define LDGRAPHY_DEBUG_OUTPUTS 0
#endif

// Output images to TMP to observe the image processing progress.
constexpr bool debug_images = false;

/*
 * Most of the following parameters are dependent on the particular
 * machine built. We should probably have a common header or configuration
 * where all these go.
 */

// Actual power that arrives as light at the output of the laser and after
// all optical losses along the path.
//
// This is a guess at this point (taking a fraction of the probably hugly
// overstated 500mW figure printed on these lasers).
// It would be really sweet if we could measure the actual value.
//
// All the datasheets of photosensitive material give mJ/cm^2 for sensitivity,
// so having something that we directly can relate to would be nice.
//
// Even without accurate power value here, we can have comparable energy
// units/area that take out the influence of lead-screw pitch or scan angle.
constexpr float kLaserOpticalPowerMilliwatt = 100;

// How fine can we get the focus ? Needs to be tuned per machine. The
// focus often is slightly oval.
constexpr float kFocus_Sled_Dia = 0.07; // mm sled direction X
constexpr float kFocus_Scan_Dia = 0.1;  // mm scan direction Y

constexpr int SCAN_PIXELS = SCANLINE_DATA_SIZE * 8;
constexpr float deg2rad = 2*M_PI/360;

/*
 * These are constants that depend on the set-up of the LDGraphy hardware.
 */
constexpr int kHSyncShoulder = 200;   // Distance between sensor and start

// The kMirrorFrequency is a multiple of the laser pixel clock frequency.
constexpr float kMirrorTicks = TICKS_PER_MIRROR_SEGMENT;
constexpr float kLaserPixelFrequency = 200e6 / TICK_DELAY;
constexpr float kMirrorLineFrequency = kLaserPixelFrequency / kMirrorTicks;

// The SCAN_PIXELS only cover part of the full segment, to better utilize
// the bits in the usable area.
constexpr float kDataFraction = SCAN_PIXELS / kMirrorTicks;

constexpr int kMirrorFaces = 6;
// Reflection is 2*angle.
constexpr float kMirrorThrowAngleRad = 2 * (360 / kMirrorFaces) * deg2rad;
constexpr float kSegmentAngleRad = kMirrorThrowAngleRad * kDataFraction;

// TODO(hzeller): read these numbers from the same source in the PostScript
// file and here.
constexpr float bed_length = 162.0;  // Sled length.

// Width of the laser to throw. Comes from the case calculation.
// Fudge value from real life :)
constexpr float bed_width_fudge_value = -1.88;  // Measured :)
constexpr float bed_width  = 102.0 + bed_width_fudge_value;
constexpr float kScanAngleRad = 40.0 * deg2rad;
constexpr float kRadiusMM = (bed_width/2) / tan(kScanAngleRad / 2);

LDGraphyScanner::LDGraphyScanner(float exposure_factor)
    : exposure_factor_(roundf(exposure_factor)),  // We only do integer for now.
      laser_sled_dot_size_(kFocus_Sled_Dia),
      laser_scan_dot_size_(kFocus_Scan_Dia)
{
    assert(exposure_factor >= 1);
#if LDGRAPHY_DEBUG_OUTPUTS
    constexpr float laser_dots_per_mm =
        SCAN_PIXELS * (kScanAngleRad / kSegmentAngleRad) / bed_width;
    fprintf(stderr, "Mirror freq: %.1fHz; Pixel clock: %.3fMHz; "
            "Avg. laser resolution: %.4fmm (%.0fdpi)\n"
            "Data covering %.2f°/%.0f°, "
            "mechanically used: %.2f°, using %.1f%% data bits.\n",
            kMirrorLineFrequency, kLaserPixelFrequency / 1e6,
            1 / laser_dots_per_mm, laser_dots_per_mm * 25.4,
            kSegmentAngleRad / deg2rad, kMirrorThrowAngleRad / deg2rad,
            kScanAngleRad/deg2rad, 100 * kScanAngleRad / kSegmentAngleRad);
#endif
}

LDGraphyScanner::~LDGraphyScanner() {
    if (backend_) backend_->Shutdown();
}

void LDGraphyScanner::SetLaserDotSize(float sled_dot_size_mm,
                                      float scan_dot_size_mm) {
    // Negative values reset.
    laser_sled_dot_size_ =
        (sled_dot_size_mm < -1e-6) ? kFocus_Sled_Dia : sled_dot_size_mm;
    laser_scan_dot_size_ =
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
    const float angle_step = kSegmentAngleRad / num;  // Overall arc mapped to full
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
    fprintf(stderr, "Last Y coordinate full width = %d; "
            "mapped to %d + %d shoulder = %d\n",
            result.back(), (int)result.size(),
            kHSyncShoulder, (int)result.size() + kHSyncShoulder);
#endif
    return result;
}

uint8_t flip_bits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}
// Copy and mirror line. Essentially memcpy() but backwards and bits reversed
static void mirror_copy(uint8_t *to, const uint8_t *const src, size_t n) {
    const uint8_t *from = src + n - 1;
    while (from >= src)
        *to++ = flip_bits(*from--);
}

static bool WouldFitRotated(const BitmapImage &img, float mm_per_pixel) {
    return img.width() * mm_per_pixel <= bed_width
        && img.height() * mm_per_pixel <= bed_length;
}

static bool TestImageFitsOnBed(const BitmapImage &img, float mm_per_pixel) {
    if (mm_per_pixel * img.width() > bed_length) {
        fprintf(stderr, "Board too long (%.1fmm), does not fit in %.0fmm "
                "bed along sled.", mm_per_pixel * img.width(), bed_length);
        fprintf(stderr, WouldFitRotated(img, mm_per_pixel)
                ? "; it would fit rotated; use -R.\n"
                : "; it would not even fit rotated.\n");
        return false;
    }

    if (mm_per_pixel * img.height() > bed_width) {
        fprintf(stderr, "Board too high (%.1fmm), does not fit in %.0fmm bed "
                "along laser scan", mm_per_pixel * img.height(), bed_width);
        fprintf(stderr, WouldFitRotated(img, mm_per_pixel)
                ? "; it would fit rotated, use -R.\n"
                : "; it would not even fit rotated.\n");
        return false;
    }
    return true;
}

bool LDGraphyScanner::SetImage(BitmapImage *img,
                               float image_resolution_mm_per_pixel) {
    if (!TestImageFitsOnBed(*img, image_resolution_mm_per_pixel))
        return false;

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
            "Input image resolution %.0fdpi (%.3fmm/pixel)\n"
            "  %5.2f Sled steps per X-pixel.\n  %5.2f Laser dots per Y-pixel "
            "(Worst res: %.3fmm dots @ %.0fkHz pixel frequency (=%.0fdpi)).\n",
            img->width() * image_resolution_mm_per_pixel,
            img->height() * image_resolution_mm_per_pixel,
            25.4f / image_resolution_mm_per_pixel, image_resolution_mm_per_pixel,
            sled_step_per_image_pixel_, laser_dots_per_image_pixel,
            1 / laser_dots_per_mm,
            kMirrorLineFrequency * kMirrorTicks / 1000.0,
            laser_dots_per_mm * 25.4);
    if (img->width() > img->height() + 5.0/image_resolution_mm_per_pixel
        && WouldFitRotated(*img, image_resolution_mm_per_pixel)) {
        fprintf(stderr, "\n[ TIP: Currently the long side is along the sled. It "
                "would be faster in portrait orientation; give -R option ]\n\n");
    }
#endif

    if (debug_images) img->ToPBM(fopen("/tmp/ld_0_input.pbm", "w"));

    // Convert this into the image, tangens-corrected and rotated by
    // 90 degrees, so that we can send it line-by-line.
    fprintf(stderr, " Geometry preprocess...\n");
    scan_image_.reset(new BitmapImage(img->width(), SCAN_PIXELS));
    for (size_t i = 0; i < y_lookup.size(); ++i) {
        const int from_y_pixel = img->height() - 1 - y_lookup[i];
        if (from_y_pixel < 0) break;  // done.
        const int to_y_pixel = i + kHSyncShoulder;
        mirror_copy(scan_image_->GetMutableRow(to_y_pixel),
                    img->GetRow(from_y_pixel), img->width() / 8);
    }
    delete img;
    scan_image_.reset(CreateRotatedImage(*scan_image_));

    if (debug_images) scan_image_->ToPBM(fopen("/tmp/ld_1_tangens.pbm", "w"));
    const float laser_resolution_in_mm_per_pixel = bed_width / y_lookup.size();
    fprintf(stderr, " Thinning structures for (%.2f, %.2f) laser dot size...\n",
            laser_sled_dot_size_, laser_scan_dot_size_);
    ThinImageStructures(
        scan_image_.get(),
        laser_scan_dot_size_ / laser_resolution_in_mm_per_pixel / 2,
        laser_sled_dot_size_ / image_resolution_mm_per_pixel / 2);
    if (debug_images) scan_image_->ToPBM(fopen("/tmp/ld_2_thinned.pbm", "w"));

    return true;
}

float LDGraphyScanner::exposure_speed_mm_per_sec() const {
    return (SledControl::kSledMMperStep * kMirrorLineFrequency) / exposure_factor_;
}

float LDGraphyScanner::exposure_joule_per_cm2() const {
    constexpr float kLaserUseFraction = kScanAngleRad / kMirrorThrowAngleRad;
    return kLaserOpticalPowerMilliwatt * kLaserUseFraction   // -> mJ/s
        / exposure_speed_mm_per_sec()      // -> mJ/mm travelled
        / bed_width                        // -> mJ/mm^2 spread over this
        / 10;                              // mm^2 = 100*cm^2; mJ/1000 = J
    return 1;
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
        if (!backend_->EnqueueNextData(row_data, SCANLINE_DATA_SIZE, do_move))
            break;
	for (int i = 1; i < exposure_factor_; ++i) {
            backend_->EnqueueNextData(row_data, SCANLINE_DATA_SIZE, false);
	}
    }
    if (backend_->status() != ScanLineSender::STATUS_RUNNING) {
        fprintf(stderr, "Issue: %s\nShutting down.\n",
                ScanLineSender::StatusToString(backend_->status()));
        return false;
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
