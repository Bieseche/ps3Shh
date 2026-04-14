#---------------------------------------------------------------------------------
# PS3 SafeHDD Health — v1.1
# Makefile para ps3dev / PSL1GHT (toolchain bucanero)
#
# PS3DEV=/usr/local/ps3dev
# PSL1GHT=/usr/local/ps3dev/psl1ght  ← subpasta dentro do ps3dev
#---------------------------------------------------------------------------------

TARGET  := PS3SHH
TITLE   := PS3 SafeHDD Health
APPID   := SHDD00001
VERSION := 01.01

SRCS := \
    main.c      \
    scanner.c   \
    mapper.c    \
    renderer.c  \
    sound.c     \
    report.c    \
    saferead.c

INCDIRS := .

# ppu_rules já adiciona $(PSL1GHT)/ppu/lib automaticamente
# Só precisamos das libs — sem LIBDIRS manual
LIBS := -lrsx -lgcm_sys -laudio -lsysutil -lio -lm -lpthread -llv2

CFLAGS := \
    -O2            \
    -Wall          \
    -Wextra        \
    -std=c99       \
    -mhard-float   \
    -fno-strict-aliasing

#-- font8x8.h é header-only, não precisa de .c

include $(PSL1GHT)/ppu_rules

#-- Targets PKG / SELF / ELF
pkg: $(TARGET).pkg

$(TARGET).pkg: $(TARGET).self
	@echo "  PKG    $@"
	$(PKG_FINALIZE) $@ $(TARGET).self ICON0.PNG

$(TARGET).self: $(TARGET).elf
	@echo "  SELF   $@"
	$(MAKE_FSELF) $< $@

.PHONY: clean
clean:
	rm -f $(TARGET).elf $(TARGET).self $(TARGET).pkg $(OBJS)
