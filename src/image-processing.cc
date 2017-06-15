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

void BitmapImage::ToPBM(FILE *file) const {
    fprintf(file, "P4\n%d %d\n", width_, height_);
    fwrite(bits_->buffer(), 1, width_ * height_ / 8, file);
    fclose(file);
}

bool BitmapImage::CopyFrom(const BitmapImage &other) {
    if (other.width_ != width_ || other.height_ != height_) return false;
    memcpy(bits_->buffer(), other.bits_->buffer(), bits_->size_bits() / 8);
    return true;
}

BitmapImage *LoadPNGImage(const char *filename, bool invert, double *dpi) {
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
    // So, for anyone who knows the libpng: is there no way to get a bitmap
    // out of it directly ?
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

    // Let's make sure our image is already multiple to 8x8 pixels and round
    // up the values is needed. That way, we can rotate without reading from
    // invalid locations. Width is already internally rounded to the next byte,
    // we only need to make sure height properly rounded.
    BitmapImage *result = new BitmapImage(width, (height + 7) & ~0x7);

    const int bytes_per_pixel = png_get_rowbytes(png, info) / width;

    // Our BitmapImage rounds up to the next full byte, so make sure that
    // we allocate potential free space beyond what png would write.
    png_byte *const row_data = new png_byte[bytes_per_pixel * result->width()]();

    //fprintf(stderr, "Reading %dx%d image (res=%.f).\n", width, height, *dpi);
    for (int y = 0; y < height; ++y) {
        png_read_row(png, row_data, NULL);
        png_byte *from_pixel = row_data;
        uint8_t *to_byte = result->GetMutableRow(y);
        for (int x = 0; x < result->width(); x+= 8) {
            *to_byte |= (*from_pixel > 128) << 7; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 6; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 5; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 4; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 3; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 2; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 1; from_pixel += bytes_per_pixel;
            *to_byte |= (*from_pixel > 128) << 0; from_pixel += bytes_per_pixel;
            if (invert) *to_byte = ~*to_byte;
            to_byte += 1;
        }
    }

    delete [] row_data;

    fclose(fp);
    return result;
}

static void ThinOneDimension(int radius, int max,
                             std::function<bool (int pos)> get_at,
                             std::function<void (int pos, bool)> set_at) {
    // For debugging: choose a value != 0, so that we can see the effect
    // of the adjustment in a ToPGM() output, but low enough that it gets cut
    // off when converting to bitmap.
    for (int pos = 0; pos < max; /**/) {
        while (pos < max && get_at(pos) == 0)
            ++pos;  // skip 'blackspace'.
        int start_on = pos;
        while (pos < max && get_at(pos) != 0)
            ++pos;
        int end_on = pos;
        if (end_on - start_on <= 2*radius) {
            // Less than radius; erase all and keep single middle pixel.
            for (int i = start_on; i < end_on; ++i)
                set_at(i, false);
            if (start_on < max)
                set_at((start_on + end_on) / 2, true);
        } else {
            for (int i = start_on; i < start_on + radius && i < max; ++i)
                set_at(i, false);
            for (int i = end_on - radius; i < end_on; ++i)
                set_at(i, false);
        }
    }
}

// (very simplistic for now, adjusting each direction separately.). Essentially
// erosion, but keep the last bit.
// TODO: this can use some optimization with a kernel; also: speed.
void ThinImageStructures(BitmapImage *img, int x_radius, int y_radius) {
    BitmapImage res(*img);
    //fprintf(stderr, "Thin pixel structure by %dx%d\n", x_radius, y_radius);
    if (x_radius) {
        for (int y = 0; y < img->height(); ++y) {
            ThinOneDimension(x_radius, img->width(),
                             [y, img](int p) -> bool { return img->Get(p, y); },
                             [y, &res](int p, bool v) { res.Set(p, y, v); });
        }
    }
    if (y_radius) {
        for (int x = 0; x < img->width(); ++x) {
            ThinOneDimension(y_radius, img->height(),
                             [x, img](int p) -> bool { return img->Get(x, p); },
                             [x, &res](int p, bool v) { res.Set(x, p, v); });
        }
    }
    img->CopyFrom(res);
}

BitmapImage *CreateThinningTestChart(float mm_per_pixel, float line_width_mm,
                                     int count,
                                     float start_diameter, float step) {
    const float pixel_per_mm = 1 / mm_per_pixel;
    const float period = 2 * line_width_mm;
    BitmapImage *const result = new BitmapImage(20 * pixel_per_mm,
                                                10 * pixel_per_mm * count);
    BitmapImage chart_template(20 * pixel_per_mm, 10 * pixel_per_mm);
    const int chart_square_pixels = chart_template.height();
    const int chart_cutoff = 0.95 * chart_square_pixels;
    for (int i = 0; i < chart_cutoff; ++i) {
        const bool in_strip = fmodf(i / pixel_per_mm, period) < line_width_mm;
        for (int y = 0; y < chart_cutoff; ++y)
            chart_template.Set(i, y, in_strip);
        for (int x = 0; x < chart_cutoff; ++x)
            chart_template.Set(x + chart_square_pixels, i, in_strip);
    }
    fprintf(stderr, "\nChart squares:");
    float dia_mm = start_diameter;
    for (int i = 0; i < count; ++i) {
        BitmapImage chart(chart_template);
        int thin_radius = dia_mm * pixel_per_mm / 2;
        fprintf(stderr, "[%.3fmm] ", dia_mm);
        ThinImageStructures(&chart, thin_radius, thin_radius);
        for (int y = 0; y < chart.height(); ++y)
            for (int x = 0; x < chart.width(); ++x)
                result->Set(x, result->height() - 1 - y - i * chart_square_pixels,
                            chart.Get(x, y));
        dia_mm += step;
    }
    fprintf(stderr, "\n\n");
    return result;
}

// "Transposing a 8x8 bit matrix", Hacker's Delight. It is delightful.
static void transpose8(const uint8_t *in, int m,
                       uint8_t *out, int n) {
    uint32_t x, y, t;

    // Load the array and pack it into x and y.

    x = (in[0*m]<<24) | (in[1*m]<<16) | (in[2*m]<<8) | in[3*m];
    y = (in[4*m]<<24) | (in[5*m]<<16) | (in[6*m]<<8) | in[7*m];

    t = (x ^ (x >> 7)) & 0x00AA00AA;  x = x ^ t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AA;  y = y ^ t ^ (t << 7);

    t = (x ^ (x >>14)) & 0x0000CCCC;  x = x ^ t ^ (t <<14);
    t = (y ^ (y >>14)) & 0x0000CCCC;  y = y ^ t ^ (t <<14);

    t = (x & 0xF0F0F0F0) | ((y >> 4) & 0x0F0F0F0F);
    y = ((x << 4) & 0xF0F0F0F0) | (y & 0x0F0F0F0F);
    x = t;

    out[7*n]=x>>24;  out[6*n]=x>>16;  out[5*n]=x>>8;  out[4*n]=x;
    out[3*n]=y>>24;  out[2*n]=y>>16;  out[1*n]=y>>8;  out[0*n]=y;
}

BitmapImage *CreateRotatedImage(const BitmapImage &img) {
    BitmapImage *const result = new BitmapImage(img.height(), img.width());
    const int in_stride = img.width() / 8;
    const int out_stride = result->width() / 8;
    for (int y = 0; y < img.height(); y+=8) {
        const uint8_t *in_row = img.GetRow(y);
        for (int x = 0; x < img.width(); x+=8) {
            uint8_t *out_row = result->GetMutableRow(result->height()-x-8)+y/8;
            transpose8(in_row, in_stride, out_row, out_stride);
            in_row++;
        }
    }
    return result;
}
