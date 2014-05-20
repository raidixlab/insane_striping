obj-m := insane_striping.o insane_raid6.o insane_raid6e.o insane_raid7.o insane_elegant.o insane_elegant_rebuilt.o insane_elegant_degraded.o

KDIR := /lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS += -O0

PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
