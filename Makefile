include config.mk

# rebuild when makefile changes
-include dummy.rebuild

EXE := bench

.PHONY: all clean

CXX ?= g++
CC ?= gcc
ASM ?= nasm
ASM_FLAGS ?= -DNASM_ENABLE_DEBUG=$(NASM_DEBUG) -w+all

ARCH_FLAGS := -march=$(CPU_ARCH)

# make submakes use the specified compiler also
export CXX
export CC

# any file that is only conditionally compiled goes here,
# we filter it out from the wildcard below and then add
# it back in using COND_SRC, which gets built up based
# on various conditions
CONDSRC_MASTER := tsc-support.cpp
CONDSRC :=

ifneq ($(USE_RDTSC),0)
CONDSRC += tsc-support.cpp
endif

DEFINES = -DUSE_RDTSC=$(USE_RDTSC)

INCLUDES += -Ifmt/include

COMMON_FLAGS := -MMD -Wall -Wextra $(DEFINES) $(ARCH_FLAGS) -g -funroll-loops $(O_LEVEL) $(INCLUDES) $(NDEBUG)

CPPFLAGS +=
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -Wno-unused-variable 

SRC_FILES := $(wildcard *.cpp) $(wildcard *.c) fmt/src/format.cc
SRC_FILES := $(filter-out $(CONDSRC_MASTER), $(SRC_FILES)) $(CONDSRC)

JE_LIB := jevents/libjevents.a
JE_SRC := $(wildcard jevents/*.c jevents/*.h)

EXTRA_DEPS :=

OBJECTS := $(SRC_FILES:.cpp=.o) $(ASM_FILES:.asm=.o)
OBJECTS := $(OBJECTS:.cc=.o)
OBJECTS := $(OBJECTS:.c=.o)
DEPFILES = $(OBJECTS:.o=.d)
# $(info OBJECTS=$(OBJECTS))

# VPATH = test:$(PSNIP_DIR)/cpu

###########
# Targets #
###########

all: $(EXE)

-include $(DEPFILES)

clean:
	rm -f *.d *.o $(EXE)

$(EXE): $(OBJECTS) $(EXTRA_DEPS) $(JE_LIB) pmu-events
	$(CXX) $(OBJECTS) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(LDLIBS) $(JE_LIB) -o $@

util/seqtest: util/seqtest.o

%.o : %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o : %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

%.o: %.asm
	$(ASM) $(ASM_FLAGS) -f elf64 $<

# extract events to pmu-events dir
pmu-events:
	tar xzf pmu-events.tar.gz

$(JE_LIB): $(JE_SRC)
	cd jevents && $(MAKE) MAKEFLAGS=

LOCAL_MK = $(wildcard local.mk)

# https://stackoverflow.com/a/3892826/149138
dummy.rebuild: Makefile config.mk $(LOCAL_MK)
	touch $@
	$(MAKE) -s clean
