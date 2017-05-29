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
#ifndef LDGRAPHY_GENERIC_GPIO_H
#define LDGRAPHY_GENERIC_GPIO_H

#include <stdint.h>

// Memory space mapped to the GPIO registers
#define GPIO_0_BASE       0x44e07000
#define GPIO_1_BASE       0x4804c000
#define GPIO_2_BASE       0x481ac000
#define GPIO_3_BASE       0x481ae000

int get_gpio(uint32_t gpio_def);

void set_gpio(uint32_t gpio_def);
void clr_gpio(uint32_t gpio_def);

bool map_gpio();
void unmap_gpio();

#endif  // LDGRAPHY_GENERIC_GPIO_H
