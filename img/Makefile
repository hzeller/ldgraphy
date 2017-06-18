# Make the resolution beyond 4x the device resolution to prevent aliasing
# issues.
all : test-pattern-3000dpi.png resolution-snake-3000dpi.png line-resolution-test-3000dpi.png scan-line-resolution-3000dpi.png

%-3000dpi.png: %.ps
	../scripts/ps2bwpng 3000 $< > $@
