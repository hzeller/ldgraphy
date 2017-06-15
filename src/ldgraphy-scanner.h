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
#ifndef LDGRAPHY_IMAGE_SCANNER_H
#define LDGRAPHY_IMAGE_SCANNER_H

class ScanLineSender;
class BitmapImage;

#include <memory>
#include <functional>

// Laser Lithography scanner facade taking an image and operating the machinery
// to expose it by scanning.
class LDGraphyScanner {
public:
    ~LDGraphyScanner();

    // Create an image scanner.
    // Exposure factor above 1 indicates multiples of exposure time to
    // baseline (NB: right now, this only rounds to full integers).
    LDGraphyScanner(float exposure_factor);

    // Set the laser dot size in X and Y direction. This affects image
    // correction in subsequent SetImage() calls. So this has to be called first.
    // Negative values will just set the default value.
    void SetLaserDotSize(float sled_dot_size_mm,
                         float scan_dot_size_mm);

    // Set new image with the given "mm_per_pixel" resolution.
    // Image is a grayscale image that needs to already be quantized to
    // black/white.
    // Takes ownership of the image.
    // Returns boolean indicating if successful (e.g. it would not be successful
    // if it doesn't fit on the bed).
    bool SetImage(BitmapImage *img, float mm_per_pixel);

    // Give an estimation how long a ScanExpose() would take with current image.
    float estimated_time_seconds() const;

    // Set the ScanLineSender Backend to be used. Required before calling any
    // of the Expose() methods.
    void SetScanLineSender(ScanLineSender *sink) { backend_.reset(sink); }

    // Scan expose the image until done or progress_out() returns false.
    // If do_move is false, then do not move sled. Requires that
    // SetScanLineSender() has been called with a valid backend before.
    //
    // The "progress_out" callback is called with the current state of how
    // much is done from all. This progress_out() callback should return a
    // boolean indicating if we should continue.
    // Returns "true" on success.
    bool ScanExpose(bool do_move,
                    std::function<bool(int done, int total)> progress_out);

    // Create an exposure of the jitter that compares the projection of
    // each mirror face. Unfortunately, some mirrors do have some up/down
    // jitter in this regard - this creates an exposure to later correct for
    // that.
    // "mirrors" is number of mirror faces the polygon mirror has, "repeats" is
    // number of exposure lines over the same place.
    void ExposeJitterTest(int mirrors, int repeats);

private:
    const int exposure_factor_;
    float laser_sled_dot_size_, laser_scan_dot_size_;
    std::unique_ptr<ScanLineSender> backend_;
    std::unique_ptr<BitmapImage> scan_image_;  // preprocessed.
    int scanlines_;
    float sled_step_per_image_pixel_;
};

#endif  // LDGRAPHY_IMAGE_SCANNER_H
