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


;; TODO(hzeller): needs some cleanup now that it is more clear what to do.

#include "laser-scribe-constants.h"

.origin 0
.entrypoint INIT

#define PRU0_ARM_INTERRUPT 19
#define CONST_PRUDRAM	   C24

#define PRUSS_PRU_CTL      0x22000
#define CYCLE_COUNTER_OFFSET  0x0C

#define GPIO_0_BASE       0x44e07000
#define GPIO_1_BASE       0x4804c000

#define GPIO_OE           0x134
#define GPIO_DATAOUT      0x13c
#define GPIO_DATAIN       0x138

#define GPIO_LASER_DATA   5   // GPIO_0, PIN_P9_17
#define GPIO_MIRROR_CLOCK 4   // GPIO_0, PIN_P9_18

#define GPIO_HSYNC_IN     17  // GPIO_1, PIN_P9_23

#define GPIO_MOTORS_ENABLE 19 // GPIO_1, PIN_P9_16
#define GPIO_SLED_DIR 18      // GPIO_1, PIN_P9_14
#define GPIO_SLED_STEP 16     // GPIO_1, PIN_P9_15

// Each mirror segment, we divide int number of ticks.
#define TICK_PER_SEGMENT (2*8*SCANLINE_DATA_SIZE)
#define JITTER_ALLOW (TICK_PER_SEGMENT/150)

// Each line, hence mirror segment, is divided in 8192 cycles.
// This is the rough number of CPU cycles between each of the steps.
#define TICK_DELAY 60

// Significant bit in the global time that toggles the mirror. A full mirror
// clock loop is 2 * 2^(9 + 3) bits long (2^9 = 512; 2^3= 8 bits/byte, two
// times, as we only get data for the first half). We want to toggle every
// half cycle so if we are looking at that bit, which is the clock bit for
// the mirror.
#define MIRROR_COUNT_BIT (9 + 3)

// Cycles to spin up mirror. roughly 1 second per 1 million.
#define SPINUP_TICKS 2500000	     ; Spinup, laser off
#define MAX_WAIT_STABLE_TIME 3000000 ; laser on, while waiting for sync.
#define END_OF_DATA_WAIT 2000000     ; No data for this time - finish.

// Mapping some fixed registers to named variables.
// We have enough registers to keep things readable.
.struct Variables
	;; Some convenient constants. 32 bit values cannot be given as
	;; immediate, so we have to store them in registers.
	.u32 gpio_0_read
	.u32 gpio_1_read
	.u32 gpio_0_write
	.u32 gpio_1_write
	.u32 start_sync_after	; constant: time after which we should start sync

	.u32 global_time	; our cycle time.

	.u32 ringbuffer_size
	.u32 item_size

	.u32 wait_countdown	; countdown used in states  for certain states to finish
	.u32 hsync_time		; time when we have seen the hsync
	.u32 last_hsync_time
	.u32 sync_laser_on_time

	.u32 gpio_out0	   ; Stuff we write out to GPIO. Bits for polygon + laser
	.u32 gpio_out1	   ; Stuff we write out to GPIO. Bits step/dir/enable

	.u32 item_start	   ; Start position of current item in ringbuffer
	.u32 item_pos		; position within item.

	.u16 state		; Current state machine state.
	.u8  bit_loop		; bit loop
	.u8  last_hsync_bit	; so that we can trigger on an edge
.ends
.assign Variables, r10, r26, v

;; Registers
;; r1 ... r9 : common use
;; r10 ... named variables

// 5 CPU cycles
.macro set_laser_mirror_bit
.mparam which_bit, to_value
	MOV r5, to_value
	AND r5, r5, 1
	LSL r5, r5, which_bit
	CLR v.gpio_out0, which_bit
	OR v.gpio_out0, v.gpio_out0, r5
.endm

.macro branch_if_not_between
.mparam to_label, value, min_cmp, max_cmp
	MOV r5, min_cmp
	MOV r6, max_cmp
	QBLT to_label, r5, value
	QBGT to_label, r6, value
.endm

// Sets hsync_time and jumps to label if hsync seen. HSync is defined as
// the laser just finishing the fluorescencing hsync block. Electrically
// the rising edge after we have been low for a bit (while laser is over block).
.macro branch_if_hsync
.mparam to_label
	LBBO r5, v.gpio_1_read, 0, 4
	QBBC bit_is_clear, r5, GPIO_HSYNC_IN
	QBEQ no_hsync, v.last_hsync_bit, 1 ; we are only interested in 0->1 edge
	MOV v.last_hsync_bit, 1
	MOV v.hsync_time, v.global_time
	JMP to_label
bit_is_clear:
	MOV v.last_hsync_bit, 0
no_hsync:
.endm

// Using cpu cycle counter instead of IEP, so that we have
// an easier time to transfer that to a simpler processor later.
.macro start_cpu_cycle_counter
	MOV r5, PRUSS_PRU_CTL
	LBBO r6, r5, 0, 4

	CLR r6, 3		; bit 3: disable
	SBBO r6, r5, 0, 4

	MOV r7, 0
	SBBO r7, r5, CYCLE_COUNTER_OFFSET, 4 ; reset

	SET r6, 3		; bit 3: enable
	SBBO r6, r5, 0, 4
.endm

.macro wait_until_cpu_cycle_counter_reaches
.mparam value
	MOV r5, PRUSS_PRU_CTL
	MOV r6, value
wait_loop:
	LBBO r7, r5, CYCLE_COUNTER_OFFSET, 4
	QBLT wait_loop, r6, r7

	LBBO r6, r5, 0, 4
	CLR r6, 3		; bit 3: disable
	SBBO r6, r5, 0, 4
.endm

INIT:
	;; Clear STANDBY_INIT in SYSCFG register.
	LBCO r0, C4, 4, 4
	CLR r0, r0, 4
	SBCO r0, C4, 4, 4

	;; Populate some constants
	MOV v.gpio_0_read, GPIO_0_BASE | GPIO_DATAIN
	MOV v.gpio_1_read, GPIO_1_BASE | GPIO_DATAIN
	MOV v.gpio_0_write, GPIO_0_BASE | GPIO_DATAOUT
	MOV v.gpio_1_write, GPIO_1_BASE | GPIO_DATAOUT
	MOV v.item_size, SCANLINE_ITEM_SIZE
	MOV v.ringbuffer_size, SCANLINE_ITEM_SIZE * QUEUE_LEN

	;; switch the laser full on at this period so that we reliably hit the
	;; hsync sensor.
	MOV v.start_sync_after, TICK_PER_SEGMENT - 2*JITTER_ALLOW

	;; Set GPIO bits to writable. Output bits need to be set to 0.
	;; GPIO-0
	MOV r1, (0xffffffff ^ ((1<<GPIO_LASER_DATA)|(1<<GPIO_MIRROR_CLOCK)))
	MOV r2, GPIO_0_BASE | GPIO_OE
	SBBO r1, r2, 0, 4

	;; GPIO-1
	MOV r1, (0xffffffff ^ ((1<<GPIO_MOTORS_ENABLE)|(1<<GPIO_SLED_DIR)|(1<<GPIO_SLED_STEP)))
	MOV r2, GPIO_1_BASE | GPIO_OE
	SBBO r1, r2, 0, 4

	MOV v.gpio_out0, 0
	MOV v.gpio_out1, 0

	SET v.gpio_out1, GPIO_MOTORS_ENABLE ; negative logic, so motors off.
	CLR v.gpio_out1, GPIO_SLED_DIR ; direction needs changing later.
	CLR v.gpio_out1, GPIO_SLED_STEP

	MOV v.item_start, 0		    ; Byte position in DRAM
	MOV v.state, STATE_IDLE

MAIN_LOOP:
	start_cpu_cycle_counter

	/* for now, as we don't send data */
	LBCO r1.b0, CONST_PRUDRAM, v.item_start, 1 ; read header
	QBEQ FINISH, r1.b0, CMD_EXIT
	/* end debug */

	JMP v.state		; switch/case with direct jump :)

	;; Waiting for Data to arrive
STATE_IDLE:
	LBCO r1.b0, CONST_PRUDRAM, v.item_start, 1 ; read header
	QBEQ FINISH, r1.b0, CMD_EXIT
	QBEQ MAIN_LOOP_NEXT, r1.b0, CMD_EMPTY
	MOV v.global_time, 0	; have monotone increasing time for 1h or so
	MOV v.wait_countdown, SPINUP_TICKS
	MOV v.state, STATE_SPINUP
	CLR v.gpio_out1, GPIO_MOTORS_ENABLE ; negative logic

	;; prepare data
	MOV v.item_pos, SCANLINE_HEADER_SIZE 		; Start after header
	MOV v.bit_loop, 7

	JMP MAIN_LOOP_NEXT

	;; Spinup. The mirror takes a second or so until it is ready,
	;; don't switch on the laser quite yet.
STATE_SPINUP:
	SUB v.wait_countdown, v.wait_countdown, 1
	QBEQ spinup_done, v.wait_countdown, 0
	JMP MAIN_LOOP_NEXT
spinup_done:
	set_laser_mirror_bit GPIO_LASER_DATA, 1 ; Now: laser on
	MOV v.wait_countdown, MAX_WAIT_STABLE_TIME
	MOV v.state, STATE_WAIT_STABLE
	JMP MAIN_LOOP_NEXT

	;; Switch on the laser the full time and wait until we get it within
	;; some acceptable margin. Sometimes, mirrors have a harder time
	;; synchronizing in the beginning. We wait until we are stable.
STATE_WAIT_STABLE:
	;; If we are too long waiting for a sync, assume there is an issue
	;; with the laser not properly rotating or no feedback.
	SUB v.wait_countdown, v.wait_countdown, 1
	QBEQ ERROR_FINISH, v.wait_countdown, 0

	branch_if_hsync wait_stable_hsync_seen
	JMP MAIN_LOOP_NEXT	; todo: account for cpu-cycles
wait_stable_hsync_seen:
	SUB r1, v.hsync_time, v.last_hsync_time
	MOV v.last_hsync_time, v.hsync_time
	branch_if_not_between wait_stable_not_synced_yet, r1, TICK_PER_SEGMENT-JITTER_ALLOW, TICK_PER_SEGMENT+JITTER_ALLOW
	CLR v.gpio_out0, GPIO_LASER_DATA   ; laser off for now
	ADD v.sync_laser_on_time, v.hsync_time, v.start_sync_after ; laser on then
	MOV v.state, STATE_CONFIRM_STABLE
	JMP MAIN_LOOP_NEXT

wait_stable_not_synced_yet:
	JMP MAIN_LOOP_NEXT

	;; We got synchronization and know when it is time to switch on
	;; the laser to get the next synchronization. Let's see if we can repeat
	;; this.
STATE_CONFIRM_STABLE:
	QBLT MAIN_LOOP_NEXT, v.sync_laser_on_time, v.global_time
	SET v.gpio_out0, GPIO_LASER_DATA
confirm_stable_test_for_hsync:
	branch_if_hsync confirm_stable_hsync_seen
	JMP MAIN_LOOP_NEXT
confirm_stable_hsync_seen:
	CLR v.gpio_out0, GPIO_LASER_DATA ; hsync finished.
	ADD v.sync_laser_on_time, v.hsync_time, v.start_sync_after
	/* todo: test if in between expected range, otherwise state wait stable */
	MOV v.state, STATE_DATA_RUN
	JMP MAIN_LOOP_NEXT

	;; Sync step between data lines.
STATE_DATA_WAIT_FOR_SYNC:
	QBLT MAIN_LOOP_NEXT, v.sync_laser_on_time, v.global_time ; not yet
	;; Now we are close enough to the hsync-block, switch on the laser.
	SET v.gpio_out0, GPIO_LASER_DATA
wait_for_sync:
	branch_if_hsync wait_for_sync_hsync_seen
	JMP MAIN_LOOP_NEXT
wait_for_sync_hsync_seen:
	CLR v.gpio_out0, GPIO_LASER_DATA ; hsync finished.
	ADD v.sync_laser_on_time, v.hsync_time, v.start_sync_after

	;; we step at the end of a data line, so here we should reset.
	CLR v.gpio_out1, GPIO_SLED_STEP

	MOV v.state, STATE_DATA_RUN
	JMP MAIN_LOOP_NEXT

	;; Loop to send all the data. We go through each byte, and within that
	;; through each bit, once per state.
STATE_DATA_RUN:
	MOV r1, v.item_size
	QBLT data_run_data_output, r1, v.item_pos
	MOV v.state, STATE_ADVANCE_RINGBUFFER
	JMP MAIN_LOOP_NEXT
data_run_data_output:
	;; super lazy, we read the full byte every time, but there is
	;; enough time.
	MOV r2, v.item_start
	ADD r2, r2, v.item_pos
	LBCO r1.b0, CONST_PRUDRAM, r2, 1

	LSR r1.b0, r1.b0, v.bit_loop
	set_laser_mirror_bit GPIO_LASER_DATA, r1.b0

	QBEQ data_run_next_byte, v.bit_loop, 0
	SUB v.bit_loop, v.bit_loop, 1
	JMP MAIN_LOOP_NEXT
data_run_next_byte:
	ADD v.item_pos, v.item_pos, 1
	MOV v.bit_loop, 7
	JMP MAIN_LOOP_NEXT

	;;  not really necessary to be its own state.
STATE_ADVANCE_RINGBUFFER:
	CLR v.gpio_out0, GPIO_LASER_DATA ; not needed now.

	;; check if we need to advance stepper
	LBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	QBEQ advance_sled_done, r1.b0, CMD_SCAN_DATA_NO_SLED
	SET v.gpio_out1, GPIO_SLED_STEP
advance_sled_done:
	;; signal host that we're done with this item.
	MOV r1.b0, CMD_EMPTY
	SBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; tell that status changed.

	ADD v.item_start, v.item_start, v.item_size ; advance in ringbuffer
	QBLT rb_advanced, v.ringbuffer_size, v.item_start ; item_start < rb_sizes
	MOV v.item_start, 0		; Wrap around
rb_advanced:
	MOV v.wait_countdown, END_OF_DATA_WAIT
	MOV v.state, STATE_AWAIT_MORE_DATA
	JMP MAIN_LOOP_NEXT

STATE_AWAIT_MORE_DATA:
	SUB v.wait_countdown, v.wait_countdown, 1
	QBNE active_data_wait, v.wait_countdown, 0
	;; ok, we waited too long, let's switch off motors and go back
	;; to idle.
	SET v.gpio_out1, GPIO_MOTORS_ENABLE ; negative logic
	MOV v.state, STATE_IDLE
	JMP MAIN_LOOP_NEXT

active_data_wait:
	LBCO r1.b0, CONST_PRUDRAM, v.item_start, 1 ; read header
	QBEQ FINISH, r1.b0, CMD_EXIT
	QBEQ MAIN_LOOP_NEXT, r1.b0, CMD_EMPTY

	MOV v.item_pos, SCANLINE_HEADER_SIZE 		; Start after header
	MOV v.bit_loop, 7

	MOV v.state, STATE_DATA_WAIT_FOR_SYNC
	JMP MAIN_LOOP_NEXT

MAIN_LOOP_NEXT:
	;; The current state set whatever state it needed, now wait for the
	;; end of our period to execute the actions: set GPIO bits.
	wait_until_cpu_cycle_counter_reaches TICK_DELAY

	;; Global time update. The global time wraps around after 1h or so
	;; but it is sufficient for the typical exposure times of a few minutes.
	ADD v.global_time, v.global_time, 1

	;; Extract the counting bit and send to gpio.
	LSR r1, v.global_time, MIRROR_COUNT_BIT
	set_laser_mirror_bit GPIO_MIRROR_CLOCK, r1

	;; GPIO out, once per loop.
	SBBO v.gpio_out0, v.gpio_0_write, 0, 4
	SBBO v.gpio_out1, v.gpio_1_write, 0, 4

	JMP MAIN_LOOP

ERROR_FINISH:
	;; maybe do something here reporting back state.
FINISH:
	MOV r1, 0		; Switch off all GPIO bits.
	SBBO r1, v.gpio_0_write, 0, 4

	SET r1, r1, GPIO_MOTORS_ENABLE ; Well, and the motor ~enable
	SBBO r1, v.gpio_1_write, 0, 4

	;; Tell host that we've seen the CMD_EXIT and acknowledge with CMD_DONE
	MOV r1.b0, CMD_DONE
	SBCO r1.b0, CONST_PRUDRAM, v.item_start, 1
	MOV R31.b0, PRU0_ARM_INTERRUPT+16 ; Tell that we're done.

	HALT
