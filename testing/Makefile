# Requires GNU Make, Ruby, and a host C compiler (not arm-none-eabi-gcc).
# Run here or from the repository root: make test [data/config ...]

.DEFAULT_GOAL := test
.DELETE_ON_ERROR:

RUBY ?= ruby
ifneq ($(filter default undefined,$(origin CC)),)
CC := $(RUBY) support/host_cc.rb
endif
CFLAGS ?= -std=c11 -Wall -Wextra -g

BUILD_DIR := build
UNITY_DIR := cmock/vendor/unity
RUNNER_GENERATOR := $(UNITY_DIR)/auto/generate_test_runner.rb
MOCK_DIR := $(BUILD_DIR)/mocks
CMOCK_FILES := $(wildcard cmock/lib/*.rb cmock/lib/plugins/*.rb)
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
endif

# Discover tests at any depth; tests/data/test_config.c becomes data/config.
rwildcard = $(foreach entry,$(wildcard $1*),$(call rwildcard,$(entry)/,$2) $(if $(filter $2,$(notdir $(entry))),$(entry)))
TEST_FILES := $(sort $(call rwildcard,tests/,test_%.c))
test_name = $(patsubst tests/%,%,$(dir $1))$(patsubst test_%.c,%,$(notdir $1))
TESTS := $(foreach file,$(TEST_FILES),$(call test_name,$(file)))

# Optional companion files configure MOCKS_<name>, SOURCES_<name>,
# EXTRA_SOURCES_<name>, and CPPFLAGS_<name>. Paths are relative to testing/.
TEST_CONFIGS := $(wildcard $(TEST_FILES:.c=.mk))
include $(TEST_CONFIGS)

SELECTORS := $(filter-out all test compile runner clean list,$(MAKECMDGOALS))
UNKNOWN_TESTS := $(filter-out $(TESTS),$(SELECTORS))
ifneq ($(strip $(UNKNOWN_TESTS)),)
$(error Unknown test(s): $(UNKNOWN_TESTS). Available: $(TESTS))
endif
SELECTED_TESTS := $(if $(SELECTORS),$(SELECTORS),$(TESTS))

CORE_HEADERS := $(call rwildcard,../Core/Inc/,%.h)
MOCK_HEADERS := $(call rwildcard,mocks/,%.h)
MOCKS := $(sort $(foreach name,$(TESTS),$(MOCKS_$(name))))
MOCK_HEADER_cmsis_os := support/cmsis_os.h
MOCK_HEADER_stm32f4xx_hal := support/stm32f4xx_hal.h
$(foreach name,$(MOCKS),$(eval MOCK_HEADER_$(name) ?= $(firstword $(filter %/$(name).h,$(CORE_HEADERS)))))
MISSING_MOCK_HEADERS := $(foreach name,$(MOCKS),$(if $(MOCK_HEADER_$(name)),,$(name)))
ifneq ($(strip $(MISSING_MOCK_HEADERS)),)
$(error No header for mock(s): $(MISSING_MOCK_HEADERS). Set MOCK_HEADER_<name> in the test .mk file)
endif
INCLUDES := -I$(MOCK_DIR) -Isupport \
            $(addprefix -I,$(sort ../Core/Inc/ $(dir $(CORE_HEADERS)) mocks/ $(dir $(MOCK_HEADERS)))) \
            -Icmock/src -I$(UNITY_DIR)/src
COMMON_SOURCES := support/hardware.c cmock/src/cmock.c $(UNITY_DIR)/src/unity.c
HEADERS := $(CORE_HEADERS) $(MOCK_HEADERS) $(wildcard support/*.h cmock/src/*.h $(UNITY_DIR)/src/*.h)

# A multi-output pattern rule generates each mock once, even under make -j.
.SECONDEXPANSION:
$(MOCK_DIR)/Mock%.c $(MOCK_DIR)/Mock%.h: $$(MOCK_HEADER_$$*) cmock.yml $(CMOCK_FILES)
	$(RUBY) cmock/lib/cmock.rb -ocmock.yml $<

.PHONY: all test compile runner clean list $(TESTS)
all: test
test: $(SELECTED_TESTS)
compile: $(addprefix $(BUILD_DIR)/,$(addsuffix $(EXEEXT),$(SELECTED_TESTS)))
runner: $(addprefix $(BUILD_DIR)/,$(addsuffix _runner.c,$(SELECTED_TESTS)))
list:
	@$(RUBY) -e "puts ARGV" $(TESTS)

# Each suite gets its own executable and runner, including its directory name.
define TEST_template
SOURCES_$(1) ?= ../Core/Src/$(1).c

$(1): $(BUILD_DIR)/$(1)$(EXEEXT)
	$$(RUBY) -e "exit(system(File.expand_path(ARGV.fetch(0))) ? 0 : 1)" "$$<"

$(BUILD_DIR)/$(1)_runner.c: $(2) $(addprefix $(MOCK_DIR)/Mock,$(addsuffix .h,$(MOCKS_$(1)))) $(wildcard $(UNITY_DIR)/auto/*.rb) Makefile
	$$(RUBY) -rfileutils -e "FileUtils.mkdir_p(File.dirname(ARGV.fetch(0)))" "$$@"
	$$(RUBY) $$(RUNNER_GENERATOR) $$< $$@

$(BUILD_DIR)/$(1)$(EXEEXT): $(2) $(BUILD_DIR)/$(1)_runner.c $$(SOURCES_$(1)) $$(EXTRA_SOURCES_$(1)) $(addprefix $(MOCK_DIR)/Mock,$(addsuffix .c,$(MOCKS_$(1)))) $(COMMON_SOURCES) $(HEADERS) $(TEST_CONFIGS) support/host_cc.rb Makefile
	$$(CC) $$(CPPFLAGS) $$(CPPFLAGS_$(1)) $$(INCLUDES) $$(CFLAGS) $(2) $(BUILD_DIR)/$(1)_runner.c $$(SOURCES_$(1)) $$(EXTRA_SOURCES_$(1)) $(addprefix $(MOCK_DIR)/Mock,$(addsuffix .c,$(MOCKS_$(1)))) $$(COMMON_SOURCES) $$(LDFLAGS) $$(LDLIBS) -o $$@
endef
$(foreach file,$(TEST_FILES),$(eval $(call TEST_template,$(call test_name,$(file)),$(file))))

# Remove only generated test artifacts; preserve other build/ contents.
clean:
	$(RUBY) -rfileutils -e "FileUtils.rm_f(ARGV)" $(foreach name,$(TESTS),"$(BUILD_DIR)/$(name)_runner.c" "$(BUILD_DIR)/$(name)" "$(BUILD_DIR)/$(name).exe") $(foreach name,$(MOCKS),"$(MOCK_DIR)/Mock$(name).c" "$(MOCK_DIR)/Mock$(name).h")
