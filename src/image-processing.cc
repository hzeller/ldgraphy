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

#include "image-processing.h"
#include <png.h>
#include <string.h>
#include <math.h>

#include <functional>

SimpleImage::SimpleImage(const SimpleImage &other)
    : width_(other.width()), height_(other.height()),
      buffer_(new uint8_t [width_ * height_]) {
    memcpy(buffer_, other.buffer_, width_ * height_);
}

void SimpleImage::ToPGM(FILE *file) const {
    fprintf(file, "P5\n%d %d\n255\n", width_, height_);
    fwrite(buffer_, 1, width_ * height_, file);
    fclose(file);
}

void BitmapImage::ToPBM(FILE *file) const {
    fprintf(file, "P4\n%d %d\n", width_, height_);
    fwrite(bits_->buffer(), 1, width_ * height_ / 8, file);
    fclose(file);
}

SimpleImage *LoadPNGImage(const char *filename, double *dpi) {
    //  More or less textbook libpng tutorial.
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Opening image");
        return NULL;
    }
    const png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                   NULL, NULL, NULL);
    if (!png) return NULL;

    const png_infop info = png_create_info_struct(png);
    if (!info) return NULL;

    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "Issue reading %s\n", filename);
        return NULL;
    }

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
        png_set_rgb_to_gray(png, 1 /*PNG_ERROR_ACTION_NONE*/,
                            -1 /*PNG_RGB_TO_GRAY_DEFAULT*/,
                            -1 /*PNG_RGB_TO_GRAY_DEFAULT*/);
    }

    const int width = png_get_image_width(png, info);
    const int height = png_get_image_height(png, info);

    png_uint_32 res_x = 0, res_y = 0;
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

    //fprintf(stderr, "Reading %dx%d image (res=%.f).\n", width, height, *dpi);
    const int bytes_per_pixel = rowbytes / width;
    png_byte *pixel = data;
    SimpleImage *result = new SimpleImage(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result->at(x, y) = *pixel;
            pixel += bytes_per_pixel;
        }
    }

    delete [] data;
    delete [] row_pointers;

    fclose(fp);
    return result;
}

// Convert all values to 0 or 255 depending on the threshold. If "invert", then
// flip the values.
void ConvertBlackWhite(SimpleImage *img, uint8_t threshold, bool invert) {
    for (int y = 0; y < img->height(); ++y) {
        for (int x = 0; x < img->width(); ++x) {
            img->at(x, y) = ((img->at(x, y) > threshold) ^ invert) ? 255 : 0;
        }
    }
}

static void ThinOneDimension(int radius, int dither,
                             int max,
                             std::function<uint8_t& (int pos)> access_at) {
    // For debugging: choose a value != 0, so that we can see the effect
    // of the adjustment in a ToPGM() output, but low enough that it gets cut
    // off when converting to bitmap.
    constexpr char kVisualRemovedPixelLevel = 64;
    assert(dither >= 0 && dither <= 1);
    for (int pos = 0; pos < max; /**/) {
        while (pos < max && access_at(pos) == 0)
            ++pos;  // skip 'blackspace'.
        int start_on = pos;
        while (pos < max && access_at(pos) != 0)
            ++pos;
        int end_on = pos;
        if (end_on - start_on <= 2*radius) {
            // Less than radius; erase all and keep single middle pixel.
            for (int i = start_on; i < end_on; ++i)
                access_at(i) = kVisualRemovedPixelLevel;
            if (start_on < max)
                access_at((start_on + end_on) / 2
                          - (pos <= 1 ? 0 : dither)) = 255;
        } else {
            for (int i = start_on; i < start_on + radius && i < max; ++i)
                access_at(i) = kVisualRemovedPixelLevel;
            for (int i = end_on - radius; i < end_on; ++i)
                access_at(i) = kVisualRemovedPixelLevel;
        }
    }
}

// (very simplistic for now, adjusting each direction separately.).
void ThinImageStructures(SimpleImage *img, int x_radius, int y_radius) {
    //fprintf(stderr, "Thin pixel structure by %dx%d\n", x_radius, y_radius);
    for (int y = 0; y < img->height(); ++y) {
        ThinOneDimension(x_radius, 0, img->width(),
                         [y, img](int p) -> uint8_t& { return img->at(p, y); });
    }
    for (int x = 0; x < img->width(); ++x) {
        ThinOneDimension(y_radius, 0, img->height(),
                         [x, img](int p) -> uint8_t& { return img->at(x, p); });
    }
}

SimpleImage *CreateThinningTestChart(float dpi, float line_width_mm,
                                     int count,
                                     float start_diameter, float step) {
    const float pixel_per_mm = dpi / 25.4f;
    const float period = 2 * line_width_mm;
    SimpleImage *const result = new SimpleImage(20 * pixel_per_mm,
                                                10 * pixel_per_mm * count);
    SimpleImage chart_template(20 * pixel_per_mm, 10 * pixel_per_mm);
    const int chart_square_pixels = chart_template.height();
    const int chart_cutoff = 0.95 * chart_square_pixels;
    for (int i = 0; i < chart_cutoff; ++i) {
        const bool in_strip = fmodf(i / pixel_per_mm, period) < line_width_mm;
        for (int y = 0; y < chart_cutoff; ++y)
            chart_template.at(i, y) = in_strip ? 255 : 0;
        for (int x = 0; x < chart_cutoff; ++x)
            chart_template.at(x + chart_square_pixels, i) = in_strip ? 255 : 0;
    }
    fprintf(stderr, "\nChart squares:");
    float dia_mm = start_diameter;
    for (int i = 0; i < count; ++i) {
        SimpleImage chart(chart_template);
        int thin_radius = dia_mm * pixel_per_mm / 2;
        fprintf(stderr, "[%.3fmm] ", dia_mm);
        ThinImageStructures(&chart, thin_radius, thin_radius);
        for (int y = 0; y < chart.height(); ++y)
            for (int x = 0; x < chart.width(); ++x)
                result->at(x, result->height() - 1 - y - i * chart_square_pixels) = chart.at(x, y);
        dia_mm += step;
    }
    fprintf(stderr, "\n\n");
    return result;
}

SimpleImage *CreateRotatedImage(const SimpleImage &img) {
    SimpleImage *const result = new SimpleImage(img.height(), img.width());
    for (int x = 0; x < img.width(); ++x) {
        for (int y = 0; y < img.height(); ++y) {
            result->at(y, result->height() - x - 1) = img.at(x, y);
        }
    }
    return result;
}
