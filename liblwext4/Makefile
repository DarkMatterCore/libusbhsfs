ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
PACMAN	:=	pacman
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

BINS	:=	$(MAKEPKG) $(PACMAN)
EVAL	:=	$(foreach bin,$(BINS),$(if $(shell which $(bin)),placeholder,$(error "No $(bin) in PATH, consider reinstalling devkitPro")))

all: deps
	@$(MAKEPKG) -c -C -f -i -s --noconfirm > /dev/null

deps:
	-@$(PACMAN) -R pkg-config > /dev/null
	@$(PACMAN) -S --needed --noconfirm patch dkp-toolchain-vars cmake tar bzip2 diffutils pkgconf > /dev/null
