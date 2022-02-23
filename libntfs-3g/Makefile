ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
all: deps
else
ifeq (, $(shell which makepkg))
MAKEPKG	:=	dkp-makepkg
else
MAKEPKG	:=	makepkg
endif
endif

all:
	@$(MAKEPKG) -c -C -f -i -s --noconfirm > /dev/null

deps:
	@pacman -S --needed --noconfirm patch autotools > /dev/null
