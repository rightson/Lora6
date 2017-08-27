MODULE_NAME := lora6
MODEM_DIR := $(PWD)/modem
obj-m += $(MODULE_NAME).o
ccflags-y = -I$(PWD)

$(MODULE_NAME)-objs += lora_cmd.o lora_core.o

include $(MODEM_DIR)/*.mk

MODULE_DIR := $(PWD)/../rootfs
KVERSION ?= $(shell ls $(MODULE_DIR)/lib/modules)


all:
	@echo $($(MODULE_NAME)-objs)
	$(MAKE) -C $(MODULE_DIR)/lib/modules/$(KVERSION)/build M=$(PWD) modules


clean:
	$(MAKE) -C $(MODULE_DIR)/lib/modules/$(KVERSION)/build M=$(PWD) clean
