/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * (c) 2017 Henner Zeller <h.zeller@acm.org>
 *
 * This file is part of LDGraphyhttp://github.com/hzeller/ldgraphy
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

#include "uio-pruss-interface.h"

#include <stdio.h>
#include <stdint.h>


int main(int argc, char *argv[]) {
    UioPrussInterface pru;
    if (!pru.Init())
        return 1;

    volatile uint8_t *loop_status;
    pru.AllocateSharedMem((void**)&loop_status, sizeof(*loop_status));

    *loop_status = 0x55;

    // Very simple: run until return.
    pru.StartExecution();

    fprintf(stderr, "Press RETURN to finish\n");
    getchar();

    *loop_status = 0x00;  // shutdown.
    pru.WaitEvent();   // Wait until finished.

    fprintf(stderr, "Successful shutdown (0x%x)\n", *loop_status);

    pru.Shutdown();
}
