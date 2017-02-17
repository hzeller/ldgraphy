all : test-pattern-1500dpi.png resolution-snake-1500dpi.png

%-1500dpi.png: %.ps
	../scripts/ps2bwpng 1500 $< > $@
