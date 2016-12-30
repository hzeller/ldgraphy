ALL_DXF=laser-rot-mount.dxf

all : $(ALL_DXF)

laser-rot-mount.dxf:

%.dxf: %.ps
	pstoedit -psarg "-r600x600" -dt -f "dxf_s:-mm -ctl" $< $@

clean:
	rm -f $(ALL_DXF)
