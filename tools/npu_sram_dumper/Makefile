# Program paths
ADB := adb

# Local environment
NDK_PATH := /opt/android-ndk
NDK_PREBUILTS := $(NDK_PATH)/toolchains/llvm/prebuilt/darwin-x86_64/bin
CC := $(NDK_PREBUILTS)/aarch64-linux-android29-clang

# Local directories
DUMPER_SRC := npu_sram_dumper.c
DUMPER := npu_sram_dumper

# Device directories
DEVICE_DIR := /data/local/tmp
SRAM_DUMP := $(DEVICE_DIR)/npu_sram_dump

.DEFAULT_GOAL := build

build:
	$(CC) -o $(DUMPER) $(DUMPER_SRC)
	$(ADB) wait-for-device push $(DUMPER) $(DEVICE_DIR)

run: build
	$(ADB) wait-for-device shell su root sh -c "$(DEVICE_DIR)/$(DUMPER) $(SRAM_DUMP)"
	$(ADB) wait-for-device pull $(SRAM_DUMP)
