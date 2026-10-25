# InkDay build for PocketBook (SDK_481, ARM EABI) with Clang + LLD.
#
#   make            build build/InkDay.app
#   make deploy     build, stop the running copy, upload and start it over SSH
#   make clean      remove build/
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

BUILD   := build
APP     := $(BUILD)/InkDay.app
SRCS    := main.c weather.c settings.c geocoding.c settings_store.c i18n.c
OBJS    := $(SRCS:%.c=$(BUILD)/%.o)

.PHONY: all deploy clean

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

deploy: $(APP)
	ssh $(PB_HOST) ' \
		if pids=$$(pidof InkDay.app); then \
			for pid in $$pids; do kill -TERM "$$pid" || exit 1; done; \
		else \
			status=$$?; \
			[ "$$status" -eq 1 ] || exit "$$status"; \
			printf "InkDay is not running; continuing deployment.\n"; \
		fi'
	scp -O $(APP) $(PB_HOST):$(PB_DIR)/
	ssh -t $(PB_HOST) 'cd $(PB_DIR) && chmod +x InkDay.app && exec ./InkDay.app'

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
