ALL_TARGETS=laser-cut-case.dxf

all : $(ALL_TARGETS)

%.dxf: %.ps
	pstoedit -psarg "-r600x600" -nb -mergetext -dt -f "dxf_s:-mm -ctl" $< $@

%.png : %.ps
	../scripts/ps2pnm 240 $< | pnmscale 0.25 | pnmtopng > $@

%.stl: %.scad
	openscad -o $@ -d $@.deps $<

clean:
	rm -f $(ALL_TARGETS)
