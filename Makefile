include USB/tinyusb/tools/top.mk
include USB/tinyusb/examples/make.mk

INC += \
  Application/usb \
  $(TOP)/hw \

# Example source
EXAMPLE_SOURCE += $(wildcard Application/usb/*.c)
SRC_C += $(addprefix $(CURRENT_PATH)/, $(EXAMPLE_SOURCE))

include USB/tinyusb/examples/rules.mk
