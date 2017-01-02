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

#define GPIO_0_BASE  0x44e07000
#define GPIO_OE           0x134
#define GPIO_DATAOUT      0x13c

#define LASER_DATA   5    // PIN_P9_17
#define MIRROR_CLOCK 4    // PIN_P9_18

#define MIRROR_SLEEP 20000

;; Each loop takes 2 CPU cycles, so freq = 200Mhz / (2*1000)
;; So here: 50kHz
#define DATA_SLEEP 200

	;; Registers
	;; r1 ... r8 : common use
	;; r9  - queue memory position
	;; r10 - GPIO write position
	;; r11 - Laser mask
	;; r12 - needs advancing

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

	;; Set bits to writable. Output bits need to be set to 0.
	MOV r2, (0xffffffff ^ ((1<<LASER_DATA)|(1<<MIRROR_CLOCK)))
	MOV r3, GPIO_0_BASE | GPIO_OE
	SBBO r2, r3, 0, 4

	MOV r9, 0			    ; Byte position in DRAM
	MOV r10, GPIO_0_BASE | GPIO_DATAOUT ; remember where we can write
	MOV r11, 0			    ; Laser off by default.

NEXT_LINE:
	LBCO r1.b0, CONST_PRUDRAM, r9, 1
	QBEQ FINISH, r1.b0, STATE_EXIT

	;; Unless this slot is filled, we switch off the laser.
	;; We have to keep going clocking data to not interrupt the
	;; mirror clock.
	QBEQ DATA_RUN, r1.b0, STATE_FILLED

EMPTY_RUN:
	MOV r11, 0
	MOV r12, 0
	JMP SCAN_LINE

DATA_RUN:
	MOV r11, (1<<LASER_DATA)
	MOV r12, 1

SCAN_LINE:
	;; Set pulse for the mirror
	MOV r1, (1<<MIRROR_CLOCK)
	SBBO r1, r10, 0, 4
	WaitLoop MIRROR_SLEEP
	MOV r1, 0
	SBBO r1, r10, 0, 4

	MOV r2, HEADER_SIZE 		; Start after header
DATA_LOOP:
	MOV r3, r9
	ADD r3, r3, r2
	LBCO r1.b0, CONST_PRUDRAM, r3, 1

	MOV r3, 7		; Bit loop
LASER_BITS_LOOP:
	QBBS LASER_SET_ON, r1.b0, r3

	MOV r4, 0
	JMP LASER_DATA_SET
LASER_SET_ON:
	MOV r4, r11		; Set laser, but only if we have it enabled
LASER_DATA_SET:
	SBBO r4, r10, 0, 4	; Switch laser via GPIO
	WaitLoop DATA_SLEEP

	QBEQ LASER_BITS_LOOP_DONE, r3, 0 ; that was the last bit
	SUB r3, r3, 1
	JMP LASER_BITS_LOOP

LASER_BITS_LOOP_DONE:
	ADD r2, r2, 1		; next data byte
	MOV r3, ITEM_SIZE
	QBLT DATA_LOOP, r3, r2

	QBEQ NEXT_LINE, r12, 0	; Empty run: don't increase, just recheck.

	;; We are done with this element. Tell the host.
	MOV r1.b0, STATE_EMPTY
	SBCO r1.b0, CONST_PRUDRAM, r9, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; tell that status changed.

	MOV r3, ITEM_SIZE
	ADD r9, r9, r3

	MOV r3, QUEUE_LEN*ITEM_SIZE
	QBLT NEXT_LINE, r3, r9

	MOV r9, 0		; Start at beginning of ring-buffer again
	JMP NEXT_LINE

FINISH:
	MOV r1, 0		; Switch off all GPIO bits.
	SBBO r1, r10, 0, 4

	;; Tell host that we've read the current queue status and exited.
	MOV r1.b0, STATE_DONE
	SBCO r1.b0, CONST_PRUDRAM, r9, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; Tell that we're done.

	HALT
