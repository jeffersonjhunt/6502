SRC_FILES := $(wildcard src/*.s)
ROM_FILES := $(basename $(SRC_FILES))
CC := $(HOME)/.local/bin/vbcc/bin/vasm6502_oldstyle -Fbin -dotdir -wdc02
ROM_PROG := minipro -p 28C256 -uP -w


all: $(ROM_FILES)

clean:
	rm $(ROM_FILES)


#
# Utils
#
ifeq (write,$(firstword $(MAKECMDGOALS)))
  TOOL_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(TOOL_ARGS):;@:)
else ifeq (hex,$(firstword $(MAKECMDGOALS)))
  TOOL_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(TOOL_ARGS):;@:)
endif

.PHONY: write
write:
	$(ROM_PROG) $(TOOL_ARGS)

.PHONY: hex
hex:
	hexdump -v -e '16/1 "0x%02X, "' -e '"\n"' $(TOOL_ARGS)

# Fin