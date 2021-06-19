ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
all: deps
else
MAKEPKG	:=	dkp-makepkg
endif

all:
	@$(MAKEPKG) -c -C -f -i -s --noconfirm > /dev/null

deps:
	@pacman -S --needed --noconfirm patch > /dev/null
