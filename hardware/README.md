Casing
------

The [laser-cut-case.ps](./laser-cut-case.ps) PostScript file is a somewhat
parametrized source for making an acrylic case for LDGraphy.
Use black or dark-red acrylic to prevent the 405nm laser to escape in
eye-damaging levels.
You might see some pictures here that are transparent for illustrative purposes,
and which are good engineering samples while developing. **Do not** use
glass-clear transparent acrylic for the real machine, a 500mW laser can
blind you!

### How to cut
The file covers several parts, and depending on which you can fit
on your acrylic sheet and/or which you have to re-do, you can choose the ones
to be output in the first section that defines the
[`/print-...`](./laser-cut-case.ps#L4) boolean variables.

The Makefile can create a dxf file that is then usable in any laser cutter, just
call `make`.

There are two layers, the 'black layer' and the 'green layer'. The green layer
should be used in 'engraving' mode on the laser cutter, the black layer for
cutting.

### Note
This is pretty much work in progress, the drive and angle of laser mount etc.
are still beeing tweaked, there is no cover yet, there is no bottom yet or
a place to mount the BeagleBone.

![](../img/sample-case.jpg)