
all : LDGraphy-00A0.dtbo

# Making a binary devicetree
%-00A0.dtbo: %.dts
	dtc -I dts -O dtb -o $@ -b 0 -@ $^
