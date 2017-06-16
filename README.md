LDgraphy - Laser Direct Lithography
===================================

http://ldgraphy.org/

A process also known as Laser Direct Imaging.

[![First Test][run-vid]](https://youtu.be/G9-JK2Nc7w0)

Simple implementation of photo resist exposure using a 405nm laser.
Goal is to have this Open Source/Open Hardware system easy to reproduce with
commonly available parts. The BOM is in the order of $100 including the
Beaglebone Green.

  * 500mW 405nm laser ($30ish)
  * Commonly available polygon mirror scanner (from laser printers) ($20ish)
  * Beaglebone Black/Green to control it all (using the PRU to generate precise
    timings for motors and laser) ($40ish)
  * Stepper motor for linear axis (plus end-stop switches) (scrap box)
  * Photo diode to determine start-of-line (as the polygon mirrors have
    slightly different long faces and also phase-drift over time) (SFH203P)
  * Local electronics: fast Laser diode driver and stepmotor driver (few $$)

Stay tuned for a putting-it-together video once the design is settled.

Here is some [rough design outline][design] in case you are interested in more
details.

Work in Progress
----------------
Currently, the design is [work in progress][LDGraphy-posts] while testing
various different types of commonly available polygon mirrors and lasers. The
[PCBs](./pcb) for the [cape] and the [laser current driver] are in their
first iteration with more focus on easy measurements and removable parts
(e.g. stepper driver and DC/DC converter are external modules) than compact
design. This part of the hardware is likely to change in the
short term; it turns out, for instance, that some polygon mirrors have not
perfectly perpendicular faces. So an additional sensor is needed to correct
for that.

And of course, the software would need a little more bells and whistles
(e.g. a web-server to easily upload Gerber designs).

Having said that, the device is fully operational with a reliable
resolution of ~0.15mm/6mil, which is right up there with basic fab-house
offerings (goal: reliable 0.1mm (4mil) traces and clearance).

[![Resolution test][resolution-thumb]][resolution]

Compile
-------
This compiles on a BeagleBone Green/Black; it requires the PRU on these
computers for hard realtime-switching of the Laser.

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

To properly prepare the GPIOs and the PRU to be used, you have to install
the device tree overlay on your Beaglebone:
```
cd ldgraphy/device-tree
sudo ./start-devicetree-overlay.sh LDGraphy.dts
```

The input is a PNG image. For converting Gerber files to PNG, see the
`gerber2png` tool in the [scripts/](./scripts) directory.

Usage:
```
Usage:
./ldgraphy [options] <png-image-file>
Options:
        -d <val>   : Override DPI of input image. Default -1
        -i         : Inverse image: black becomes laser on
        -x<val>    : Exposure factor. Default 1.
        -o<val>    : Offset in sled direction in mm
        -R         : Quarter image turn left; can be given multiple times.
        -h         : This help
Mostly for testing or calibration:
        -S         : Skip sled loading; assume board already loaded.
        -E         : Skip eject at end.
        -F         : Run a focus round until Ctrl-C
        -M         : Testing: Inhibit sled move.
        -n         : Dryrun. Do not do any scanning; laser off.
        -j<exp>    : Mirror jitter test with given exposure repeat
        -D<line-width:start,step> : Laser Dot Diameter test chart. Creates a test-strip 10cm x 2cm with 10 samples.
```

Case
----
The current version is made out of [laser-cut acrylic](./hardware)
parts to have it easily and cheaply build for everyone who has
access to a laser cutter (hint: your local Hackerspace might have one).

Here, all electronics is mounted on the top for easier measurements and
such. The final version will fit everything inside.

![Case](./img/sample-case.jpg)

Above is current status of the case, which went through some refinements (here
an [earlier case](./img/intermediate-case.jpg) which better shows
the internal arrangement).

This was the first experiment
-------------------------------

#### First Light

|   Setup                | Result
|------------------------|---------------------------------
| ![](./img/setup.jpg)   | ![](./img/firstexposure.jpg)

The first test-device was put together with extruded aluminum.
Somewhat crappy first result, but had potential. Exposure time for this 30 mm
long patch was around 90 seconds (for reference: on the right is how the
geometry _should_ look like).

As you can see, the early stages had some issues, e.g. you can't trust the "PLL"
of the mirror to properly lock - it has a phase drift over
time (hence the warped square). The current design has a HSync line sensor.
Overall the [progress] after that improved various
issues seen up to the point where it starts to be usable for PCB work.

[progress]: https://plus.google.com/u/0/+HennerZeller/posts/FeqdPoEZ3AT
[Laser current driver]: ./pcb/laser-drive
[Cape]: ./pcb/cape
[LDGraphy-posts]: https://plus.google.com/u/0/s/%23ldgraphy/top
[run-vid]: ./img/ldgraphy-yt.jpg
[resolution-thumb]: ./img/line-resolution-small.jpg
[resolution]: https://plus.google.com/u/0/+HennerZeller/posts/a8taHWeL5CC
[design]: ./design.md
