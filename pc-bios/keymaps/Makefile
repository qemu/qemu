
KEYMAP	:= $(shell which qemu-keymap 2>/dev/null)

MAPS	:= ar bepo cz da de de-ch en-us en-gb es et fi fo \
	   fr fr-be fr-ca fr-ch \
	   hr hu is it ja lt lv mk nl no pl pt pt-br ru th tr

ar	: MAP_FLAGS :=	-l ar
bepo	: MAP_FLAGS :=	-l fr -v dvorak
cz	: MAP_FLAGS :=	-l cz
da	: MAP_FLAGS :=	-l dk
de	: MAP_FLAGS :=	-l de
de-ch	: MAP_FLAGS :=	-l ch
en-us	: MAP_FLAGS :=	-l us
en-gb	: MAP_FLAGS :=	-l gb
es	: MAP_FLAGS :=	-l es
et	: MAP_FLAGS :=	-l et
fi	: MAP_FLAGS :=	-l fi
fo	: MAP_FLAGS :=	-l fo
fr	: MAP_FLAGS :=	-l fr
fr-be	: MAP_FLAGS :=	-l be
fr-ca	: MAP_FLAGS :=	-l ca -v fr
fr-ch	: MAP_FLAGS :=	-l ch -v fr
hr	: MAP_FLAGS :=	-l hr
hu	: MAP_FLAGS :=	-l hu
is	: MAP_FLAGS :=	-l is
it	: MAP_FLAGS :=	-l it
ja	: MAP_FLAGS :=	-l jp -m jp106
lt	: MAP_FLAGS :=	-l lt
lv	: MAP_FLAGS :=	-l lv
mk	: MAP_FLAGS :=	-l mk
nl	: MAP_FLAGS :=	-l nl
no	: MAP_FLAGS :=	-l no
pl	: MAP_FLAGS :=	-l pl
pt	: MAP_FLAGS :=	-l pt
pt-br	: MAP_FLAGS :=	-l br
ru	: MAP_FLAGS :=	-l ru
th	: MAP_FLAGS :=	-l th
tr	: MAP_FLAGS :=	-l tr

ifeq ($(KEYMAP),)

all:
	@echo "nothing to do (qemu-keymap not found)"

else

all: $(MAPS)

clean:
	rm -f $(MAPS)

$(MAPS): $(KEYMAP) Makefile
	$(KEYMAP) -f $@ $(MAP_FLAGS)

endif
