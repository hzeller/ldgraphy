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
#define DATA_SLEEP 1000

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

	MOV r4, GPIO_0_BASE | GPIO_DATAOUT ; remember where we can write

NEXT_LINE:
	;; first four bytes: status
	LBCO r1.b0, CONST_PRUDRAM, 0, 1
	QBNE FINISH, r1.b0, 0x55 ; this value means: keep going.

	MOV r1, (1<<MIRROR_CLOCK)
	SBBO r1, r4, 0, 4
	WaitLoop MIRROR_SLEEP

	;; Simple test: just toggle a while
	MOV r2, 512		; this number of 'pixels'

LASER_LOOP:
	QBBS SET_LASER_ONE, r2, 3 ; div by 8

	MOV r1, 0
	JMP LASER_DATA_SET

SET_LASER_ONE:
	MOV r1, (1<<LASER_DATA)

LASER_DATA_SET:
	SBBO r1, r4, 0, 4
	WaitLoop DATA_SLEEP

	SUB r2, r2, 1
	QBNE LASER_LOOP, r2, 0

	JMP NEXT_LINE

FINISH:
	MOV r1, 0
	SBBO r1, r4, 0, 4


	MOV r1.b0, 0x22
	SBCO r1.b0, CONST_PRUDRAM, 0, 1	  ; Indicate finish status.
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; Tell that we're done.
	HALT
