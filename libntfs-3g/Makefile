ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
PACMAN	:=	pacman
PACKAGES:=  patch dkp-toolchain-vars autotools tar bzip2 diffutils pkgconf
else
PACKAGES:=  patch dkp-toolchain-vars autoconf automake tar bzip2 diffutils pkgconf
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
	@su -s /bin/bash nobody -c "$(MAKEPKG) -c -C -f"
	@$(PACMAN) -U --needed --noconfirm *.pkg.tar.*

deps:
ifeq ($(OS),Windows_NT)
	@$(PACMAN) -R pkg-config > /dev/null
endif
	-@for pkg in $(PACKAGES); do \
		$(PACMAN) -Q $$pkg > /dev/null || $(PACMAN) -S --needed --noconfirm $$pkg > /dev/null; \
	done
