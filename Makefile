#---------------------------------------------------------------------------------
# PS3 SafeHDD Health — v1.1
# 
#--------------------------------------------------------------------------------

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

# Bibliotecas para o sistema de Health Check
LIBS := -lrsx -lgcm_sys -laudio -lsysutil -lio -lrt -lm

LDFLAGS := -L$(LIBDIRS) -Wl,--gc-sections

.PHONY: all pkg clean

all: pkg

# Regra de compilação dos objetos
%.o: %.c
	@echo "  CC     $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Linkagem do ELF
$(TARGET).elf: $(OBJS)
	@echo "  LD     $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	@$(STRIP) -s $@

# Construção do PKG 
pkg: $(TARGET).elf
	@echo "  PKG    Construindo estrutura..."
	@mkdir -p pkg/USRDIR
	@# Aqui o ELF vira o EBOOT principal
	$(FSELF) $(TARGET).elf pkg/USRDIR/EBOOT.BIN
	@# Se você tiver um ICON0.PNG no repo, descomente a linha abaixo
	@# cp ICON0.PNG pkg/ICON0.PNG
	@echo "  SFO    Gerando PARAM.SFO..."
	python $(SFO) --title "PS3 SafeHDD Health" --appid "$(APPID)" -f pkg/sfo.xml pkg/PARAM.SFO
	@echo "  PACK   Gerando $(TARGET).pkg..."
	python $(PKG) --contentid $(CONTENTID) pkg/ $(TARGET).pkg
	@echo "PKG pronto para o combate."

clean:
	@echo "  CLEAN  Limpando a bagunça..."
	rm -f *.o *.elf *.map pkg/USRDIR/EBOOT.BIN pkg/PARAM.SFO *.pkg
	rm -rf pkg/
