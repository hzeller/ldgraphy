;; -*- asm -*-
;;
;; (c) 2017 Henner Zeller <h.zeller@acm.org>
;;
;; This file is part of LDGraphy. http://github.com/hzeller/ldgraphy
;;
;; LDGraphy is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.
;;
;; LDGraphy is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with LDGraphy.  If not, see <http://www.gnu.org/licenses/>.


#include "laser-scribe-constants.h"

.origin 0
.entrypoint INIT

#define PRU0_ARM_INTERRUPT 19
#define CONST_PRUDRAM	   C24

#define GPIO_0_BASE       0x44e07000
#define GPIO_1_BASE       0x4804c000

#define GPIO_OE           0x134
#define GPIO_DATAOUT      0x13c

#define LASER_DATA   5    // GPIO_0, PIN_P9_17
#define MIRROR_CLOCK 4    // GPIO_0, PIN_P9_18

#define SLED_DIR 16    // GPIO_1, PIN_P9_15
#define SLED_ENABLE 17 // GPIO_1, PIN_P9_23
#define SLED_STEP 18   // GPIO_1, PIN_P9_14

#define MIRROR_SLEEP 20000

;; Each loop takes 2 CPU cycles, so freq = 200Mhz / (2*1000)
;; So here: 50kHz
#define DATA_SLEEP 200

// Mapping some fixed registers to names variables.
// We have enough registers to keep things readable.
.struct Variables
	;; Some convenient constants. 32 bit values cannot be given as
	;; immediate, so we have to store them in registers.
	.u32 gpio_0_write
	.u32 gpio_1_write
	.u32 ringbuffer_size
	.u32 item_size

	.u32 laser_mask		; mask to write to GPIO0. Zero if laser disabled.

	.u32 item_start		; Start position of current item in ringbuffer
	.u32 item_pos		; position within item.
	.u8  needs_advancing	; flag: if this was not an empty run, advance
	.u8  bit_loop		; bit loop
.ends
.assign Variables, r10, r17.w0, v

;; Registers
;; r1 ... r9 : common use
;; r10 ... named variables

;; uses r5
.macro WaitLoop
.mparam loop_count
	MOV r5, loop_count
SLEEP_LOOP:
	SUB r5, r5, 1                   ; two cycles per loop.
	QBNE SLEEP_LOOP, r5, 0
.endm

INIT:
	;; Clear STANDBY_INIT in SYSCFG register.
	LBCO r0, C4, 4, 4
	CLR r0, r0, 4
	SBCO r0, C4, 4, 4

	;; Populate some constants
	MOV v.gpio_0_write, GPIO_0_BASE | GPIO_DATAOUT
	MOV v.gpio_1_write, GPIO_1_BASE | GPIO_DATAOUT
	MOV v.item_size, ITEM_SIZE
	MOV v.ringbuffer_size, ITEM_SIZE*QUEUE_LEN

	;; Set GPIO bits to writable. Output bits need to be set to 0.
	MOV r1, (0xffffffff ^ ((1<<LASER_DATA)|(1<<MIRROR_CLOCK)))
	MOV r2, GPIO_0_BASE | GPIO_OE
	SBBO r1, r2, 0, 4

	MOV r1, (0xffffffff ^ ((1<<SLED_DIR)|(1<<SLED_STEP)|(1<<SLED_ENABLE)))
	MOV r2, GPIO_1_BASE | GPIO_OE
	SBBO r1, r2, 0, 4

	MOV v.item_start, 0		    ; Byte position in DRAM
	MOV v.laser_mask, 0		    ; Laser off by default.

NEXT_LINE:
	LBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	QBEQ FINISH, r1.b0, STATE_EXIT

	;; Unless this slot is filled, we switch off the laser.
	;; We have to keep going clocking data to not interrupt the
	;; mirror clock.
	QBEQ DATA_RUN, r1.b0, STATE_FILLED

EMPTY_RUN:
	MOV v.laser_mask, 0
	MOV v.needs_advancing, 0
	JMP SCAN_LINE

DATA_RUN:
	MOV v.laser_mask, (1<<LASER_DATA)
	MOV v.needs_advancing, 1

SCAN_LINE:
	;; Set pulse for the mirror
	MOV r1, (1<<MIRROR_CLOCK)
	SBBO r1, v.gpio_0_write, 0, 4
	WaitLoop MIRROR_SLEEP
	MOV r1, 0
	SBBO r1, v.gpio_0_write, 0, 4

	MOV v.item_pos, HEADER_SIZE 		; Start after header
DATA_LOOP:
	MOV r2, v.item_start
	ADD r2, r2, v.item_pos
	LBCO r1.b0, CONST_PRUDRAM, r2, 1

	MOV v.bit_loop, 7		; Bit loop
LASER_BITS_LOOP:
	QBBS LASER_SET_ON, r1.b0, v.bit_loop

	MOV r4, 0
	JMP LASER_DATA_SET
LASER_SET_ON:
	MOV r4, v.laser_mask	; Set laser, but only if we have it enabled
LASER_DATA_SET:
	SBBO r4, v.gpio_0_write, 0, 4	; Switch laser via GPIO
	WaitLoop DATA_SLEEP

	QBEQ LASER_BITS_LOOP_DONE, v.bit_loop, 0 ; that was the last bit
	SUB v.bit_loop, v.bit_loop, 1
	JMP LASER_BITS_LOOP

LASER_BITS_LOOP_DONE:
	ADD v.item_pos, v.item_pos, 1		; next data byte
	QBLT DATA_LOOP, v.item_size, v.item_pos ; item_pos < item_size

	QBEQ NEXT_LINE, v.needs_advancing, 0	; Empty run: check if data now

	;; We are done with this element. Tell the host.
	MOV r1.b0, STATE_EMPTY
	SBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; tell that status changed.

	ADD v.item_start, v.item_start, v.item_size

	QBLT NEXT_LINE, v.ringbuffer_size, v.item_start ; item_start < rb_size

	MOV v.item_start, 0		; Wrap around
	JMP NEXT_LINE

FINISH:
	MOV r1, 0		; Switch off all GPIO bits.
	SBBO r1, v.gpio_0_write, 0, 4

	;; Tell host that we've seen the STATE_EXIT and acknowledge with DONE
	MOV r1.b0, STATE_DONE
	SBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; Tell that we're done.

	HALT
