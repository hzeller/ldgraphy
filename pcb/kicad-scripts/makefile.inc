# -*- Makefile -*-

%-fab.zip : %-fab.kicad_pcb
	python ../kicad-scripts/kicad-fab.py $< plot/
	zip -r $@ plot/

%-fab.kicad_pcb : %.kicad_pcb
	sed "s/%%gitversion%%/`git log --date=short --pretty=format:'%h@%cd' -n 1 $^`/" < $^ > $@

# Generating a 1200 dpi image
%-top.png : %-fab.zip
	../../scripts/gerber2png 1200 plot/$*-fab-4-CuTop.gtl plot/$*-fab-0.drl > $@
