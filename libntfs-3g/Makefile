ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
all: deps
else
MAKEPKG	:=	dkp-makepkg
endif

all:
	@$(MAKEPKG) -i -s --needed --noconfirm > /dev/null

deps:
	@pacman -S --needed --noconfirm patch > /dev/null
