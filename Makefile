# Host-side unit tests for the pure-C core (no Flipper SDK needed).
# The app itself is built with ufbt: `ufbt` / `ufbt launch`.

CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g
BUILD := build_host

CORE := core/chord.c core/keycodes.c core/motion_math.c

.PHONY: test clean

test: $(BUILD)/tests
	./$(BUILD)/tests

$(BUILD)/tests: tests/test_main.c $(CORE) core/*.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_main.c $(CORE) -lm

clean:
	rm -rf $(BUILD)
