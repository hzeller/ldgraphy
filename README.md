LDgraphy - Laser Direct Lithography
===================================

(work in progress, nothing to see here yet)

Simple implementation of direct photo resist exposure using a 405nm laser.

Uses
  * 500mW 405nm laser
  * Commonly available polygon mirror scanner (from laser printers)
  * stepper motor
  * Beaglebone Black to control it all.

Build
-----
git clone --recursive https://github.com/hzeller/ldgraphy.git

----
Polygon scanner mirror
NBC3111
24V (but seems to work as well with 12v)

https://www.youtube.com/watch?v=Tg4NdJ-Y3nw
  1: input frequency. 50-500Hz ?
  2: NC
  3: ~motor_enable
  4: GND
  5: 12-24V
