include USB/tinyusb/tools/top.mk
include tools/make.mk

CFLAGS_OPTIMIZED = -O0

INC += \
  Application/usb \
  Application/drivers \
  hw \

# Example source
PROJECT_SOURCE = $(wildcard Application/usb/*.c)
PROJECT_SOURCE += $(wildcard Application/drivers/*.c)
PROJECT_SOURCE += $(wildcard Application/app/*.c)
SRC_C += $(PROJECT_SOURCE)

include USB/tinyusb/examples/rules.mk
