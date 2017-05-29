/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * (c) 2013, 2014 Henner Zeller <h.zeller@acm.org>
 *
 * This file is part of BeagleG. http://github.com/hzeller/beagleg
 *
 * BeagleG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * BeagleG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with BeagleG.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "generic-gpio.h"

// GPIO Registers
#define GPIO_OE           0x134
#define GPIO_DATAIN       0x138
#define GPIO_DATAOUT      0x13c
#define GPIO_CLEARDATAOUT 0x190
#define GPIO_SETDATAOUT   0x194

// Memory space mapped to the Clock Module registers
#define CM_BASE                 0x44e00000
#define CM_SIZE                 0x4000

// Clock Module Peripheral and Wakeup registers
#define CM_WKUP_GPIO0_CLKCTRL   (0x400 + 0x008)
#define CM_PER_GPIO1_CLKCTRL    (0x000 + 0x0ac)
#define CM_PER_GPIO2_CLKCTRL    (0x000 + 0x0b0)
#define CM_PER_GPIO3_CLKCTRL    (0x000 + 0x0b4)

#define IDLEST_MASK             (0x03 << 16)
#define MODULEMODE_ENABLE       (0x02 << 0)

#define GPIO_MMAP_SIZE 0x2000

// GPIO registers.
static volatile uint32_t *gpio_0 = NULL;
static volatile uint32_t *gpio_1 = NULL;
static volatile uint32_t *gpio_2 = NULL;
static volatile uint32_t *gpio_3 = NULL;

static volatile uint32_t *get_gpio_base(uint32_t gpio_def) {
  switch (gpio_def & 0xfffff000) {
  case GPIO_0_BASE: return gpio_0;
  case GPIO_1_BASE: return gpio_1;
  case GPIO_2_BASE: return gpio_2;
  case GPIO_3_BASE: return gpio_3;
  default: return NULL;
  }
}

int get_gpio(uint32_t gpio_def) {
  volatile uint32_t *gpio_port = get_gpio_base(gpio_def);
  uint32_t bitmask = 1 << (gpio_def & 0x1f);
  uint32_t status;

  if (gpio_port) {
    status = gpio_port[GPIO_DATAIN/4];
    return (status & bitmask) ? 1 : 0;
  }
  return -1;
}

void set_gpio(uint32_t gpio_def) {
  volatile uint32_t *gpio_port = get_gpio_base(gpio_def);
  uint32_t bitmask = 1 << (gpio_def & 0x1f);
  if (gpio_port)
    gpio_port[GPIO_SETDATAOUT/4] = bitmask;
}

void clr_gpio(uint32_t gpio_def) {
  volatile uint32_t *gpio_port = get_gpio_base(gpio_def);
  uint32_t bitmask = 1 << (gpio_def & 0x1f);
  if (gpio_port)
    gpio_port[GPIO_CLEARDATAOUT/4] = bitmask;
}

static void set_gpio_mask(uint32_t *mask, uint32_t gpio_def) {
  const uint32_t bitmask = 1 << (gpio_def & 0x1f);
  switch (gpio_def & 0xfffff000) {
  case GPIO_0_BASE: mask[0] |= bitmask; break;
  case GPIO_1_BASE: mask[1] |= bitmask; break;
  case GPIO_2_BASE: mask[2] |= bitmask; break;
  case GPIO_3_BASE: mask[3] |= bitmask; break;
  }
}

static volatile uint32_t *map_port(int fd, size_t length, off_t offset) {
  return (volatile uint32_t*) mmap(0, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

static void ena_gpio_clk(volatile uint32_t *cm, uint32_t reg, int) {
  uint32_t val;

  val = cm[reg/4];
  if (val & IDLEST_MASK) {
    val |= MODULEMODE_ENABLE;
    cm[reg/4] = val;
    do {
      val = cm[reg/4];
    } while (val & IDLEST_MASK);
  }
}

static int enable_gpio_clocks(int fd) {
  volatile uint32_t *cm;

  cm = map_port(fd, CM_SIZE, CM_BASE);
  if (cm == MAP_FAILED) { perror("mmap() CM"); return 0; }

  ena_gpio_clk(cm, CM_WKUP_GPIO0_CLKCTRL, 0);
  ena_gpio_clk(cm, CM_PER_GPIO1_CLKCTRL, 1);
  ena_gpio_clk(cm, CM_PER_GPIO2_CLKCTRL, 2);
  ena_gpio_clk(cm, CM_PER_GPIO3_CLKCTRL, 3);

  munmap((void*)cm, CM_SIZE);
  return 1;
}

bool map_gpio() {
  bool ret = false;
  int fd;

  fd = open("/dev/mem", O_RDWR);
  if (fd == -1) { perror("open()"); return ret; }

  if (!enable_gpio_clocks(fd))  goto exit;

  gpio_0 = map_port(fd, GPIO_MMAP_SIZE, GPIO_0_BASE);
  if (gpio_0 == MAP_FAILED) { perror("mmap() GPIO-0"); goto exit; }
  gpio_1 = map_port(fd, GPIO_MMAP_SIZE, GPIO_1_BASE);
  if (gpio_1 == MAP_FAILED) { perror("mmap() GPIO-1"); goto exit; }
  gpio_2 = map_port(fd, GPIO_MMAP_SIZE, GPIO_2_BASE);
  if (gpio_2 == MAP_FAILED) { perror("mmap() GPIO-2"); goto exit; }
  gpio_3 = map_port(fd, GPIO_MMAP_SIZE, GPIO_3_BASE);
  if (gpio_3 == MAP_FAILED) { perror("mmap() GPIO-3"); goto exit; }

  ret = true;

exit:
  close(fd);
  if (!ret)
    unmap_gpio();
  return ret;
}

void unmap_gpio() {
  if (gpio_0) { munmap((void*)gpio_0, GPIO_MMAP_SIZE); gpio_0 = NULL; }
  if (gpio_1) { munmap((void*)gpio_1, GPIO_MMAP_SIZE); gpio_1 = NULL; }
  if (gpio_2) { munmap((void*)gpio_2, GPIO_MMAP_SIZE); gpio_2 = NULL; }
  if (gpio_3) { munmap((void*)gpio_3, GPIO_MMAP_SIZE); gpio_3 = NULL; }
}
