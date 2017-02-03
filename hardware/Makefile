ALL_TARGETS=laser-cut-case.dxf

all : $(ALL_TARGETS)

%.dxf: %.ps
	pstoedit -psarg "-r600x600" -mergetext -dt -f "dxf_s:-mm -ctl" $< $@

%.png : %.ps
	../scripts/ps2pnm 60 $< | pnmtopng > $@

%.stl: %.scad
	openscad -o $@ -d $@.deps $<

clean:
	rm -f $(ALL_TARGETS)
