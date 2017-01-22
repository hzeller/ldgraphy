test-pattern-1200dpi.png: test-pattern.ps
	cat $^ | gs -q -sDEVICE=pnm -sOutputFile=- -r1200x1200 -g1600x1600 - | pnmcrop | pnmtopng > $@
