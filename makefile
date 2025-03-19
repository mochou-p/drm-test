# drm-test/makefile

MAKEFLAGS     += --no-print-directory

PROGRAM       := drm_test
SOURCE_DIR    := src
BUILD_DIR     := bin
MAIN          := $(SOURCE_DIR)/main.c
EXECUTABLE    := $(BUILD_DIR)/$(PROGRAM)
PREPARE       := mkdir -p $(BUILD_DIR)
ARGS          := ../img/fuji.ppm

COMPILER      := gcc
COMPILE_FLAGS := -std=c99 -pedantic -O3 -g3 -Wall -Wextra -Wpedantic -Wconversion -Werror
INCLUDES      := -I/nix/store/xjvnswjz32dw12ld8nb29lhwp92275aj-libdrm-2.4.120-dev/include/libdrm
LINKS         := -ldrm

CHECKER       := cppcheck
CHECK_FLAGS   := --cppcheck-build-dir=$(BUILD_DIR) --check-level=exhaustive --enable=all --inconclusive --inline-suppr --fsigned-char -j 1 --language=c --max-ctu-depth=0 --platform=unix64 --std=c99 --verbose --suppress=unmatchedSuppression --suppress=missingIncludeSystem --suppress=checkersReport --error-exitcode=1 $(MAIN)

DEBUGGER      := valgrind
DEBUG_FLAGS   := --tool=memcheck --trace-children=yes --track-fds=all --error-limit=no --show-error-list=yes --keep-debuginfo=yes --show-below-main=yes --default-suppressions=no --smc-check=all --read-inline-info=yes --read-var-info=yes --show-emwarns=yes --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --track-origins=yes --expensive-definedness-checks=yes

IGNORE_STDOUT := > /dev/null

TARGET:
	@$(PREPARE)
	@$(COMPILER) $(COMPILE_FLAGS) $(INCLUDES) $(MAIN) -o $(EXECUTABLE) $(LINKS)

check:
	@$(PREPARE)
	@$(CHECKER) $(CHECK_FLAGS) $(IGNORE_STDOUT)

debug:
	@sudo $(DEBUGGER) $(DEBUG_FLAGS) $(EXECUTABLE) $(ARGS)

all:
	@$(MAKE) && $(MAKE) check && $(MAKE) debug

