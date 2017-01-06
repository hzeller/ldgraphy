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
#ifndef LDGRAPHY_CONTAINERS_H
#define LDGRAPHY_CONTAINERS_H

#include <strings.h>

template <int N>
class BitArray {
public:
    BitArray() { bzero(buffer_, (N+7)/8); }

    void Set(int b, bool value) {
        assert(b >= 0 && b < N);
        if (value)
            buffer_[b / 8] |= 1 << (7 - b % 8);
        else
            buffer_[b / 8] &= ~(1 << (7 - b % 8));
    }

    const uint8_t *buffer() const { return buffer_; }

private:
    uint8_t buffer_[N+7/8];
};

// An Image .. 2D container essentially.
class SimpleImage {
public:
    SimpleImage(int width, int height)
        : width_(width), height_(height), buffer_(new char [width * height]) {}
    ~SimpleImage() { delete [] buffer_; }

    int width() const { return width_; }
    int height() const { return height_; }

    char &at(int x, int y) { return buffer_[width_ * y + x]; }

private:
    int width_, height_;
    char *buffer_;
};

#endif // LDGRAPHY_CONTAINERS_H
