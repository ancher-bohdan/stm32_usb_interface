# ---------------------------------------
# Common make definition for all examples
# ---------------------------------------

# Build directory
BUILD := _build/$(BOARD)

PROJECT := $(notdir $(CURDIR))
BIN := $(TOP)/_bin/$(BOARD)/$(notdir $(CURDIR))

# Handy check parameter function
check_defined = \
    $(strip $(foreach 1,$1, \
    $(call __check_defined,$1,$(strip $(value 2)))))
__check_defined = \
    $(if $(value $1),, \
    $(error Undefined make flag: $1$(if $2, ($2))))

#-------------- Select the board to build for. ------------

# NOTE: as this project will support only one target board (STM32F407 discovery),
#       all path to config .mk will be hardcoded.
#       If in the future, project will be ported to another platform,
#       copy the part of the script from USB/tinyusb/example/make.mk:22
BOARD_PATH := hw/bsp/stm32f4/boards/stm32f407disco
FAMILY := stm32f4
FAMILY_PATH := hw/bsp/stm32f4

# Include Family and Board specific defs
include $(FAMILY_PATH)/family.mk

SRC_C += $(wildcard $(FAMILY_PATH)/*.c)

#-------------- Cross Compiler  ------------
# Can be set by board, default to ARM GCC
CROSS_COMPILE ?= arm-none-eabi-
# Allow for -Os to be changed by board makefiles in case -Os is not allowed
CFLAGS_OPTIMIZED ?= -Os

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
GDB = $(CROSS_COMPILE)gdb
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size
MKDIR = mkdir

ifeq ($(CMDEXE),1)
  CP = copy
  RM = del
  PYTHON = python
else
  SED = sed
  CP = cp
  RM = rm
  PYTHON = python3
endif

#-------------- Source files and compiler flags --------------

# Include all source C in family & board folder
SRC_C += hw/bsp/board.c
SRC_C += $(wildcard $(BOARD_PATH)/*.c)

INC   += $(FAMILY_PATH)

# Compiler Flags
CFLAGS += \
  -ggdb \
  -fdata-sections \
  -ffunction-sections \
  -fsingle-precision-constant \
  -fno-strict-aliasing \
  -Wall \
  -Wextra \
  -Wfatal-errors \
  -Wdouble-promotion \
  -Wstrict-prototypes \
  -Wstrict-overflow \
  -Werror-implicit-function-declaration \
  -Wfloat-equal \
  -Wundef \
  -Wshadow \
  -Wwrite-strings \
  -Wsign-compare \
  -Wmissing-format-attribute \
  -Wunreachable-code \
  -Wcast-function-type \
  -Wcast-qual \
  -Wnull-dereference \
  -Wuninitialized \
  -Wunused

# conversion is too strict for most mcu driver, may be disable sign/int/arith-conversion
#  -Wconversion
  
# Debugging/Optimization
ifeq ($(DEBUG), 1)
  CFLAGS += -Og
else
  CFLAGS += $(CFLAGS_OPTIMIZED)
endif

# Log level is mapped to TUSB DEBUG option
ifneq ($(LOG),)
  CMAKE_DEFSYM +=	-DLOG=$(LOG)
  CFLAGS += -DCFG_TUSB_DEBUG=$(LOG)
endif

# Logger: default is uart, can be set to rtt or swo
ifneq ($(LOGGER),)
	CMAKE_DEFSYM +=	-DLOGGER=$(LOGGER)
endif

ifeq ($(LOGGER),rtt)
  CFLAGS += -DLOGGER_RTT -DSEGGER_RTT_MODE_DEFAULT=SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL
  RTT_SRC = lib/SEGGER_RTT
  INC   += $(TOP)/$(RTT_SRC)/RTT
  SRC_C += $(RTT_SRC)/RTT/SEGGER_RTT.c
else ifeq ($(LOGGER),swo)
  CFLAGS += -DLOGGER_SWO
endif
