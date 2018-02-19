LDGraphy PCBs
=============

Some boot-strapping required ... we need these PCBs first, before we can make
PCBs :)

  * [Laser driver](./laser-drive): fast current source to drive Lasers. Often,
    the Lasers you can buy come with their own driver, but they are very slow.
    This one is meant to switch >= 1Mhz while behaving as a current source.
  * [Beaglebone cape](./cape): a cape for the
    Beaglebone Black or Green providing all the connections to various
    parts of the set-up. This was the first version mostly for experimenting
    and successfully used.
  * [PocketCape](./pocket-cape): The new cape, ready for the cheaper
    and smaller PocketBeagle.
    Unlike the experimental board that used a DC/DC converter and stepmotor
    driver as separate modules, this has everything on-board.
    It is ready for double-sided boards with two outputs for lasers, polygon
    mirrors and two inputs for horizontal sync detection.

In the current set-up, they are mounted externally to provide easy access for
measuring various aspects. After the dust settles, they will all fit in the
LDGraphy case.

![](../img/pcbs.jpg)
