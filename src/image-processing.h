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
#ifndef LDGRAPHY_IMAGE_PROCESSING_H
#define LDGRAPHY_IMAGE_PROCESSING_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include <vector>

#include "containers.h"

// A bitmap image with packed bits and direct access.
// Image width is aligned to the next full byte.
class BitmapImage {
public:
    BitmapImage(int width, int height)
        : width_((width + 7) & ~0x7), height_(height),
          bits_(new BitArray(width_ * height)) {}
    BitmapImage(const BitmapImage &o)
        : width_(o.width_), height_(o.height_), bits_(new BitArray(*o.bits_)) {
    }

    ~BitmapImage() { delete bits_; }

    int width() const { return width_; }
    int height() const { return height_; }

    inline bool Get(int x, int y) const {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        return bits_->Get(width_ * y + x);
    }
    inline void Set(int x, int y, bool value) {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        bits_->Set(width_ * y + x, value);
    }

    // Raw read access to a full row.
    const uint8_t *GetRow(int r) const {
        return bits_->buffer() + r * width_ / 8;
    }
    uint8_t *GetMutableRow(int r) { return bits_->buffer() + r * width_ / 8; }

    bool CopyFrom(const BitmapImage &other);
    void ToPBM(FILE *file) const;

private:
    const int width_, height_;
    BitArray *const bits_;
};

// Load PNG file, convert to grayscale and return result as allocated
// SimpleImage. NULL on failure.
// Returns the image dpi if it was stored in the meta data.
BitmapImage *LoadPNGImage(const char *filename, bool invert, double *dpi);

// Thin out contiguous regions in x and y direction by x_radius, y_radius,
// but never in a way that pixels are eliminated entirely.
void ThinImageStructures(BitmapImage *img, int x_radius, int y_radius);

// Create a test-chart with pre-thinned lines of "line_width_mm" size. Creates
// "count" sample charts, starting with "start_diameter" and steps.
// Each sample will be 1 cm long and 2 cm wide.
BitmapImage *CreateThinningTestChart(float mm_per_pixel, float line_width_mm,
                                     int count,
                                     float start_diameter, float step);

// Create a new bitmap, that is rotated by 90 degrees.
BitmapImage *CreateRotatedImage(const BitmapImage &img);

#endif  // LDGRAPHY_IMAGE_PROCESSING_H
