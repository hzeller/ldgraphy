LDgraphy - Laser Direct Lithography
===================================

(work in progress, nothing to see here yet)

Simple implementation of direct photo resist exposure using a 405nm laser.

Uses
  * 500mW 405nm laser
  * Commonly available polygon mirror scanner (from laser printers)
  * stepper motor for linear axis.
  * Beaglebone Black to control it all (using the PRU to generate precise
    timings for mirror and laser).

Build
-----
git clone --recursive https://github.com/hzeller/ldgraphy.git

Setup
-----
![Initial setup](./img/setup.jpg)
