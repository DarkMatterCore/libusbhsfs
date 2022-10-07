ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
PACMAN	:=	pacman
all: deps
else
ifeq (,$(shell which makepkg))
MAKEPKG	:=	dkp-makepkg
else
MAKEPKG	:=	makepkg
endif
ifeq (,$(shell which pacman))
PACMAN	:=	dkp-pacman
else
PACMAN	:=	pacman
endif
endif

all:
	@$(MAKEPKG) -c -C -f -i -s --noconfirm > /dev/null

deps:
	-@$(PACMAN) -R pkg-config > /dev/null
	@$(PACMAN) -S --needed --noconfirm patch dkp-toolchain-vars cmake tar bzip2 diffutils pkgconf > /dev/null
