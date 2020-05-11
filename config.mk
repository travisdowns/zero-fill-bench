-include local.mk

# set DEBUG to 1 to enable various debugging checks
DEBUG ?= 0
CPP_STD ?= c++11
C_STD ?= c11
CPU_ARCH ?= native

# $(info DEBUG=$(DEBUG))

ifeq ($(DEBUG),1)
O_LEVEL ?= -O0
NASM_DEBUG ?= 1
NDEBUG=
else
O_LEVEL ?= -O3
NASM_DEBUG ?= 0
NDEBUG=-DNDEBUG
endif

## detect the platform and use rdtsc only on x86
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),x86_64)
USE_RDTSC ?= 1
else
USE_RDTSC ?= 0
endif

$(info ARCH=$(UNAME_M) USE_RDTSC=$(USE_RDTSC))
