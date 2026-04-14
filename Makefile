#---------------------------------------------------------------------------------
# PS3 SafeHDD Health — v1.1 - MurilooPrDev Edition
#---------------------------------------------------------------------------------

TARGET    := PS3SHH
APPID     := SHDD00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000

PS3DEV  ?= /usr/local/ps3dev
PSL1GHT ?= $(PS3DEV)

CC         := $(PS3DEV)/ppu/bin/ppu-gcc
LD         := $(PS3DEV)/ppu/bin/ppu-gcc
STRIP      := $(PS3DEV)/ppu/bin/ppu-strip
FSELF      := $(PS3DEV)/bin/fself
SFO        := $(PS3DEV)/bin/sfo.py
PKG        := $(PS3DEV)/bin/pkg.py

SRCS := main.c scanner.c mapper.c renderer.c sound.c report.c saferead.c
OBJS := $(SRCS:.c=.o)

# Headers e Flags
INCDIRS := . $(PSL1GHT)/ppu/include $(PSL1GHT)/ppu/include/ppu-lv2
CFLAGS  := -O2 -Wall -Wextra -fno-builtin -mhard-float -fshort-wchar --sysroot=$(PS3DEV)/ppu $(addprefix -I,$(INCDIRS))
LIBDIRS := $(PSL1GHT)/ppu/lib

# Bibliotecas
LIBS := -lrsx -lgcm_sys -laudio -lsysutil -lio -lrt -lm
LDFLAGS := -L$(LIBDIRS) -Wl,--gc-sections

.PHONY: all pkg clean

all: pkg

# Compilação
%.o: %.c
	@echo "  CC     $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Linkagem
$(TARGET).elf: $(OBJS)
	@echo "  LD     $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	@$(STRIP) -s $@

# Construção do PKG
pkg: $(TARGET).elf
	@echo "  PKG    Preparando pastas..."
	@mkdir -p pkg/USRDIR
	
	@echo "  FSELF  Assinando EBOOT..."
	@$(FSELF) $(TARGET).elf pkg/USRDIR/EBOOT.BIN
	
	@echo "  ICON   Copiando Icone..."
	@if [ -f ICON0.png ]; then cp ICON0.png pkg/ICON0.png; fi
	
	@echo "  XML    Gerando template SFO..."
	@echo '<?xml version="1.0" encoding="utf-8"?><paramsfo><param key="ATTRIBUTE">0</param><param key="CATEGORY">HG</param><param key="TITLE">PS3 SafeHDD Health</param><param key="TITLE_ID">$(APPID)</param><param key="VERSION">01.01</param></paramsfo>' > pkg/sfo.xml
	
	@echo "  SFO    Gerando PARAM.SFO (usando Python2)..."
	@python2 $(SFO) --title "PS3 SafeHDD Health" --appid "$(APPID)" -f pkg/sfo.xml pkg/PARAM.SFO
	
	@echo "  PACK   Gerando $(TARGET).pkg..."
	@python2 $(PKG) --contentid $(CONTENTID) pkg/ $(TARGET).pkg
	@echo "--------------------------------------"
	@echo "PKG pronto para o combate: $(TARGET).pkg"
	@echo "--------------------------------------"

clean:
	@echo "  CLEAN  Limpando a bagunça..."
	rm -f *.o *.elf *.map pkg/USRDIR/EBOOT.BIN pkg/PARAM.SFO *.pkg pkg/sfo.xml
	rm -rf pkg/
