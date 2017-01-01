ALL_DXF=laser-rot-mount.dxf motor-mount.dxf side-mount.dxf side-mount2.dxf rot-mirror-mount.dxf sled.dxf

all : $(ALL_DXF)

%.dxf: %.ps
	pstoedit -psarg "-r600x600" -dt -f "dxf_s:-mm -ctl" $< $@

clean:
	rm -f $(ALL_DXF)
