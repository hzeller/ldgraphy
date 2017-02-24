LDgraphy - Laser Direct Lithography
===================================

Or Laser Direct Imaging, whatever you prefer.

_(work in progress, nothing to see here yet)_

Simple implementation of photo resist exposure directly using a 405nm laser.

Uses
  * 500mW 405nm laser
  * Commonly available polygon mirror scanner (from laser printers)
  * stepper motor for linear axis.
  * Beaglebone Black to control it all (using the PRU to generate precise
    timings for mirror and laser).
  * Possibly later some simpler set-up with Cortex M4 or so.

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

Usage:
```
Usage:
./ldgraphy [options] <image-file>
Options:
        -d <val>   : DPI of input image. Default 600
        -i         : Inverse image: black becomes laser on
        -M         : Inhibit move in x direction
        -F         : Run a focus round until the first Ctrl-C
        -n         : Dryrun. Do not do any scanning.
        -h         : This help
```

Case
----
The first test-device was put together with extruded aluminum
(see below). There is [work-in-progress](./hardware) to make a case out of
laser-cut acrylic to have it cheap and simple to build for everyone who has
access to a laser cutter (hint: your local Hackerspace might have one).

![Case](./img/sample-case.jpg)

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