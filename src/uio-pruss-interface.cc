/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * (c) 2016 Henner Zeller <h.zeller@acm.org>,
 *          Leonardo Romor <leonardo.romor@gmail.com>,
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

// Implementation of the hardware interface of the PRU using the uio_pruss
// driver from the bone line kernel.

#include "uio-pruss-interface.h"

#include <assert.h>
#include <errno.h>
#include <pruss_intc_mapping.h>
#include <prussdrv.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

// Generated PRU code from laser-scribe-pru.p
#include "laser-scribe-pru_bin.h"

// Target PRU
#define PRU_NUM 0

#if PRU_NUM == 0
#  define PRU_DATARAM PRUSS0_PRU0_DATARAM
#  define PRU_INSTRUCTIONRAM PRUSS0_PRU0_IRAM
#  define PRU_ARM_INTERRUPT PRU0_ARM_INTERRUPT
#elif PRU_NUM == 1
#  define PRU_DATARAM PRUSS0_PRU1_DATARAM
#  define PRU_INSTRUCTIONRAM PRUSS0_PRU1_IRAM
#  define PRU_ARM_INTERRUPT PRU1_ARM_INTERRUPT
#endif

bool UioPrussInterface::Init() {
  tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;
  prussdrv_init();

  /* Get the interrupt initialized */
  int ret = prussdrv_open(PRU_EVTOUT_0);  // allow access.
  if (ret) {
    fprintf(stderr, "prussdrv_open() failed (%d) %s\n", ret, strerror(errno));
    return false;
  }
  prussdrv_pruintc_init(&pruss_intc_initdata);
  return true;
}

bool UioPrussInterface::AllocateSharedMem(void **pru_mmap, const size_t size) {
  prussdrv_map_prumem(PRU_DATARAM, pru_mmap);
  if (*pru_mmap == NULL) {
    fprintf(stderr, "Couldn't map PRU memory.\n");
    return false;
  }
  bzero(*pru_mmap, size);
  return true;
}

bool UioPrussInterface::StartExecution() {
  prussdrv_pru_write_memory(PRU_INSTRUCTIONRAM, 0, PRUcode, sizeof(PRUcode));
  prussdrv_pru_enable(PRU_NUM);
  return true;
}

unsigned UioPrussInterface::WaitEvent() {
  const unsigned num_events = prussdrv_pru_wait_event(PRU_EVTOUT_0);
  prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU_ARM_INTERRUPT);
  return num_events;
}

bool UioPrussInterface::Shutdown() {
  prussdrv_pru_disable(PRU_NUM);
  prussdrv_exit();
  return true;
}
