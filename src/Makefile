# (c) 2017 h.zeller@acm.org
# This is free software. License: GNU 3.0

# In case you cross compile this on a different architecture, uncomment this
# and set the prefix. Or simply set the environment variable.
#CROSS_COMPILE?=arm-arago-linux-gnueabi-

# Tuning options for ARM CPU. Unset this in an environment variable if compiled
# on a different system.
ARM_COMPILE_FLAGS?=-mtune=cortex-a8 -march=armv7-a

# Location of am335x package https://github.com/beagleboard/am335x_pru_package
# We check this out in a local git submodule.
AM335_BASE=../am335x_pru_package
PASM=$(AM335_BASE)/pru_sw/utils/pasm
LIBDIR_APP_LOADER?=$(AM335_BASE)/pru_sw/app_loader/lib
INCDIR_APP_LOADER?=$(AM335_BASE)/pru_sw/app_loader/include

CAPE_INCLUDE=../hardware/$(LDGRAPHY_HARDWARE_TARGET)

# Optmization flags. Typically, that should be -O3 for regluar use, but for
# debugging and development "-g -O0" can be more helpful. Overridable by
# environment variable.
LDGRAPHY_OPT_CFLAGS?=-O3

CFLAGS+=-Wall -I$(INCDIR_APP_LOADER) -I$(CAPE_INCLUDE) -D_XOPEN_SOURCE=500 $(ARM_COMPILE_FLAGS) $(LDGRAPHY_OPT_CFLAGS) -DCAPE_NAME='"$(LDGRAPHY_HARDWARE_TARGET)"'

# We use c++11, but it looks like that even the latest
# bone-debian-7.11-lxde-4gb-armhf-2016-06-16-4gb image has an ancient 4.6.3
# compiler that still referred to that standard as c++0x
export CXXFLAGS+=-std=c++0x $(CFLAGS)
export CXX?=g++

LDFLAGS+=-lpthread -lm
PRUSS_LIBS=$(LIBDIR_APP_LOADER)/libprussdrv.a

# Assembled binary from *.p file.
PRU_BIN=laser-scribe-pru_bin.h

OBJECTS=uio-pruss-interface.o
MAIN_OBJECTS=main.o
TARGETS=ldgraphy

DEPENDENCY_RULES=$(OBJECTS:=.d)

all : $(TARGETS)

ldgraphy: main.o $(OBJECTS)
	$(CROSS_COMPILE)$(CXX) -o $@ $^ $(LDFLAGS) $(PRUSS_LIBS)

%.o: %.cc .compiler-flags
	$(CROSS_COMPILE)$(CXX) $(CXXFLAGS)  -c  $< -o $@
	@$(CROSS_COMPILE)$(CXX) $(CXXFLAGS) -MM $< > $@.d

%_bin.h : %.p $(PASM)
	$(PASM) -I$(CAPE_INCLUDE) -V3 -c $<

$(PASM):
	make -C $(AM335_BASE)

uio-pruss-interface.cc : laser-scribe-pru_bin.h

# Auto generated dependencies
-include $(DEPENDENCY_RULES)

clean:
	rm -rf $(TARGETS) $(MAIN_OBJECTS) $(OBJECTS) $(PRU_BIN) $(DEPENDENCY_RULES)

.compiler-flags: FORCE
	@echo '$(CXX) $(CXXFLAGS) $(GTEST_INCLUDE)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS) $(GTEST_INCLUDE)' > $@

.PHONY: FORCE
