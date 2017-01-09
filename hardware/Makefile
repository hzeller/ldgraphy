ALL_TARGETS=laser-rot-mount.dxf motor-mount.dxf side-mount.dxf side-mount2.dxf rot-mirror-mount.dxf sled.dxf spacers.dxf acme-square-nut-mold.stl

all : $(ALL_TARGETS)

%.dxf: %.ps
	pstoedit -psarg "-r600x600" -dt -f "dxf_s:-mm -ctl" $< $@

%.stl: %.scad
	openscad -o $@ -d $@.deps $<

clean:
	rm -f $(ALL_TARGETS)
