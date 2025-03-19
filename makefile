# drm-test/makefile

# behaviour

MAKEFLAGS += --no-print-directory

# files and directories

PROGRAM            := drm_test
SOURCE_DIR         := src
BUILD_DIR          := bin
MAIN_C             := $(SOURCE_DIR)/main.c
RELEASE_EXECUTABLE := $(BUILD_DIR)/$(PROGRAM)
DEV_EXECUTABLE     := $(RELEASE_EXECUTABLE)_dev
ARGS               := ../img/fuji.ppm

# compilation

GCC_RELEASE_FLAGS := -std=c99 -O3 -DNDEBUG -march=native -fwhole-program -flto
GCC_DEV_FLAGS     := -std=c99 -pedantic -O1 -g3 -Wall -Wextra -Wpedantic -Wconversion -Werror
HEADERS           := -I/nix/store/xjvnswjz32dw12ld8nb29lhwp92275aj-libdrm-2.4.120-dev/include/libdrm
LIBRARIES         := -ldrm

# tools

CPPCHECK_FLAGS := --cppcheck-build-dir=$(BUILD_DIR) --check-level=exhaustive --enable=all --inconclusive --inline-suppr --fsigned-char -j 1 --language=c --max-ctu-depth=0 --platform=unix64 --std=c99 --verbose --suppress=unmatchedSuppression --suppress=missingIncludeSystem --suppress=checkersReport --error-exitcode=1
VALGRIND_FLAGS := --tool=memcheck --trace-children=yes --track-fds=all --error-limit=no --show-error-list=yes --keep-debuginfo=yes --show-below-main=yes --default-suppressions=no --smc-check=all --read-inline-info=yes --read-var-info=yes --show-emwarns=yes --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --track-origins=yes --expensive-definedness-checks=yes

# helpers

PREPARE         := mkdir -p $(BUILD_DIR)
SILENCE_STDOUT  := 1> /dev/null
SILENCE_STDERR  := 2> /dev/null
IGNORE_MAKE_ERR := || true

# commands

TARGET:
	@$(PREPARE)
	@gcc $(GCC_RELEASE_FLAGS) $(HEADERS) $(MAIN_C) -o $(RELEASE_EXECUTABLE) $(LIBRARIES)

run:
	@sudo $(RELEASE_EXECUTABLE) $(ARGS) $(IGNORE_MAKE_ERR)

clean:
	@rm -r $(BUILD_DIR) $(SILENCE_STDERR) $(IGNORE_MAKE_ERR)
	@rm vgcore.*        $(SILENCE_STDERR) $(IGNORE_MAKE_ERR)

# debugging

static_analysis:
	@$(PREPARE)
	@cppcheck $(CPPCHECK_FLAGS) $(MAIN_C) $(SILENCE_STDOUT)

dev:
	@$(PREPARE)
	@gcc $(GCC_DEV_FLAGS) $(HEADERS) $(MAIN_C) -o $(DEV_EXECUTABLE) $(LIBRARIES)

memory_check:
	@sudo valgrind $(VALGRIND_FLAGS) $(DEV_EXECUTABLE) $(ARGS) $(IGNORE_MAKE_ERR)

