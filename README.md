LDgraphy - Laser Direct Lithography
===================================

Or Laser Direct Imaging, whatever you prefer.

_([work in progress][LDGraphy-posts], don't expect a polished result yet)_

Simple implementation of photo resist exposure directly using a 405nm laser.

![Case](./img/sample-case.jpg)

Uses
  * 500mW 405nm laser
  * Commonly available polygon mirror scanner (from laser printers)
  * Stepper motor for linear axis (plus end-stop switches)
  * Photo diode to determine start-of-line (as the polygon mirrors have
    slightly different long faces and also phase-drift over time).
  * Beaglebone Black/Green to control it all (using the PRU to generate precise
    timings for motors and laser).

Work in Progress
----------------
Currently, the design is work-in-progress while testing various different
types of commonly available Polygon Mirrors and Lasers. Also the
[PCBs](./pcb) for the [Cape] and the [Laser current driver] are in their
first iteration with focus on easy measurements and removable parts
(e.g. stepper driver and DC/DC converter are external modules) than compact
design. And of course, the software would need a little more bells and whistles.

Having said that, the device is fully operational with a reliable
resolution of 0.2mm (goal: reliable 0.1mm (4mil) traces with 0.1mm gaps).

Build
-----
```
git clone --recursive https://github.com/hzeller/ldgraphy.git
```

Install relevant packages. We are reading png-images as input, so we need the
library that helps us with that (probably already installed on your system):
```
sudo apt-get update
sudo apt-get install libpng-dev -y
```

Then compile:
```
cd ldgraphy/src
make
```

At this point, the input is very simply a PNG image (as that has
embedded DPI information). For converting Gerber files to PNG, see the
`gerber2png` tool in the [scripts/](./scripts) directory.

Usage:
```
./ldgraphy [options] <png-image-file>
Options:
        -d <val>   : Override DPI of input image. Default -1
        -i         : Inverse image: black becomes laser on
        -x<val>    : Exposure factor. Default 1.
        -o<val>    : Offset in sled direction in mm
        -R         : Quarter image turn left; can be given multiple times.
Mostly for testing:
        -S         : Skip sled loading; assume board already loaded.
        -E         : Skip eject at end.
        -F         : Run a focus round until Ctrl-C
        -M         : Testing: Inhibit sled move.
        -n         : Dryrun. Do not do any scanning; laser off.
        -j<exp>    : Mirror jitter test with given exposure repeat
        -h         : This help
```

Case
----
The first test-device was put together with extruded aluminum
(see below). The current version is made out of [laser-cut acrylic](./hardware)
parts to have it simple and cheaply build for everyone who has
access to a laser cutter (hint: your local Hackerspace might have one).

Above is current status of the case, which went through some refinements (here
a previous [intermediate case](./img/intermediate-case.jpg) which shows better
the internal arrangement).

This was the first experiment
-------------------------------

#### First Light

   Setup               | Result
-----------------------|---------------------------------
![](./img/setup.jpg)   | ![](./img/firstexposure.jpg)

Somewhat crappy first result, but has potential. Exposure time for this 30 mm
long patch was around 90 seconds (for reference: on the right is how the
geometry _should_ look like).

As you can see, the early stages had some issues, e.g. you can't trust the "PLL"
of the mirror to properly lock - it has a phase drift over
time (hence the 'curve'). Overall the [progress] after that improved various
issues seen up to the point where it starts to be usable for PCB work.

[progress]: https://plus.google.com/u/0/+HennerZeller/posts/FeqdPoEZ3AT
[Laser current driver]: ./pcb/laser-drive
[Cape]: ./pcb/cape
[LDGraphy-posts]: https://plus.google.com/u/0/s/%23ldgraphy/top