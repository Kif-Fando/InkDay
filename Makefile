# InkDay build for PocketBook (SDK_481, ARM EABI) with Clang + LLD.
#
#   make            build build/InkDay.app
#   make deploy     build, stop the running copy, upload and start it over SSH
#   make clean      remove build/
#
# Remote debugging (gdbserver on the reader, gdb-multiarch on the PC):
#   make gdbserver-install  copy the SDK's static gdbserver to the reader (once)
#   make debug              debug build, upload, run under gdbserver (terminal 1)
#   make gdb                connect gdb-multiarch to it (terminal 2)
#
# Override paths if needed: make SDK=/path/to/SDK_481 PB_HOST=pb

SDK     ?= $(abspath $(CURDIR)/../pocketbook-sdk/SDK_481)
SYSROOT := $(SDK)/arm-obreey-linux-gnueabi/sysroot
GCCLIB  := $(SDK)/lib/gcc/arm-obreey-linux-gnueabi/4.8.1

CC      := clang
LD      := ld.lld
TARGET  := arm-linux-gnueabi
CFLAGS  ?= -Wall -Wextra

PB_HOST ?= pb
PB_DIR  ?= /mnt/ext1/applications

GDB            ?= gdb-multiarch
GDB_PORT       ?= 2345
GDBSERVER      := $(SDK)/arm-obreey-linux-gnueabi/debug-root/usr/bin/gdbserver
PB_GDBSERVER   ?= /mnt/ext1/system/bin/gdbserver
PB_IP          ?= $(shell ssh -G $(PB_HOST) 2>/dev/null | awk '$$1 == "hostname" { print $$2 }')

DEBUG   ?= 0
ifeq ($(DEBUG),1)
BUILD   := build/debug
CFLAGS  += -g -O0 -fno-omit-frame-pointer
else
BUILD   := build
endif
APP     := $(BUILD)/InkDay.app
SRCS    := main.c weather.c settings.c geocoding.c settings_store.c i18n.c
OBJS    := $(SRCS:%.c=$(BUILD)/%.o)

.PHONY: all deploy clean stop gdbserver-install debug gdb

all: $(APP)

$(BUILD):
	mkdir -p $@

# -MMD -MP: rebuild objects when included headers change.
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) --target=$(TARGET) --sysroot="$(SYSROOT)" $(CFLAGS) -MMD -MP -c $< -o $@

$(APP): $(OBJS)
	$(LD) \
		--sysroot="$(SYSROOT)" \
		-m armelf_linux_eabi \
		--dynamic-linker=/lib/ld-linux.so.3 \
		--allow-shlib-undefined \
		"$(SYSROOT)/usr/lib/crt1.o" \
		"$(SYSROOT)/usr/lib/crti.o" \
		"$(GCCLIB)/crtbegin.o" \
		$(OBJS) \
		-L"$(SYSROOT)/usr/lib" \
		-L"$(SYSROOT)/lib" \
		-L"$(GCCLIB)" \
		-linkview \
		--start-group \
		"$(SYSROOT)/usr/lib/libjson-c.a" \
		-lm -lc -lgcc \
		--end-group \
		"$(GCCLIB)/crtend.o" \
		"$(SYSROOT)/usr/lib/crtn.o" \
		-o $@
	file $@

stop:
	ssh $(PB_HOST) ' \
		if pids=$$(pidof InkDay.app); then \
			for pid in $$pids; do kill -TERM "$$pid" || exit 1; done; \
		else \
			status=$$?; \
			[ "$$status" -eq 1 ] || exit "$$status"; \
			printf "InkDay is not running; continuing deployment.\n"; \
		fi'

deploy: $(APP) stop
	scp -O $(APP) $(PB_HOST):$(PB_DIR)/
	ssh -t $(PB_HOST) 'cd $(PB_DIR) && chmod +x InkDay.app && exec ./InkDay.app'

gdbserver-install:
	ssh $(PB_HOST) 'mkdir -p $(dir $(PB_GDBSERVER))'
	scp -O "$(GDBSERVER)" $(PB_HOST):$(PB_GDBSERVER)
	ssh $(PB_HOST) 'chmod +x $(PB_GDBSERVER) && $(PB_GDBSERVER) --version | head -1'

# The reader's Dropbear forbids port forwarding and gdbserver 7.4 has no stdio
# mode, so gdbserver listens on the LAN only for the duration of the session.
ifeq ($(DEBUG),1)
debug: $(APP) stop
	scp -O $(APP) $(PB_HOST):$(PB_DIR)/
	ssh -t $(PB_HOST) \
		'cd $(PB_DIR) && chmod +x InkDay.app && exec $(PB_GDBSERVER) :$(GDB_PORT) ./InkDay.app'

gdb: $(APP)
	$(GDB) -q \
		-ex 'set pagination off' \
		-ex 'target remote $(PB_IP):$(GDB_PORT)' \
		$(APP)
else
debug gdb:
	@$(MAKE) --no-print-directory DEBUG=1 $@
endif

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
