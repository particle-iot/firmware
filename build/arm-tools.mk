#
# Define ARM tools
#


# Define the compiler/tools prefix
GCC_PREFIX ?= arm-none-eabi-

include $(COMMON_BUILD)/common-tools.mk

AR = $(GCC_ARM_PATH)$(GCC_PREFIX)gcc-ar

#
# default flags for targeting ARM Cortex-M3
#
ifeq ("$(ARM_CPU)","cortex-m3")
# C compiler flags
CFLAGS +=  -g3 -gdwarf-2 -Os -mcpu=cortex-m3 -mthumb

# C++ specific flags
CPPFLAGS += -fno-exceptions -fno-rtti -fcheck-new

CONLYFLAGS +=

ASFLAGS +=  -g3 -gdwarf-2 -mcpu=cortex-m3 -mthumb

LDFLAGS += -nostartfiles -Xlinker --gc-sections
endif

#
# default flags for targeting ARM Cortex-M4
#
ifeq ("$(ARM_CPU)","cortex-m4")
CFLAGS += -g3 -gdwarf-2 -Os -mcpu=cortex-m4 -mthumb -mabi=aapcs
CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16

# C++ specific flags
CPPFLAGS += -fno-exceptions -fno-rtti -fcheck-new

CONLYFLAGS +=

ASFLAGS +=  -g3 -gdwarf-2 -mcpu=cortex-m4 -mthumb -mabi=aapcs
ASFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16

LDFLAGS += -nostartfiles -Xlinker --gc-sections
endif

CFLAGS += -flto -ffat-lto-objects -DPARTICLE_COMPILE_LTO_FAT

ifeq ($(COMPILE_LTO),y)
LDFLAGS += -flto -Os -fuse-linker-plugin
endif

# We are using newlib-nano for all the platforms
CFLAGS += --specs=nano.specs

# Check if the compiler version is the minimum required
version_to_number=$(shell v=$1; v=($${v//./ }); echo $$((v[0] * 10000 + v[1] * 100 + v[2])))
get_major_version=$(shell v=$1; v=($${v//./ }); echo $${v[0]})
arm_gcc_version_str:=$(shell $(CC) -dumpversion)
arm_gcc_version:=$(call version_to_number,$(arm_gcc_version_str))
expected_version_str:=9.2.1
ifeq ($(shell test $(arm_gcc_version) -lt $(call version_to_number,$(expected_version_str)); echo $$?),0)
     $(error "ARM gcc version $(expected_version_str) or later required, but found $(arm_gcc_version_str)")
endif

ifeq ($(shell test $(arm_gcc_version) -lt $(call version_to_number,4.9.0); echo $$?),0)
     NANO_SUFFIX=_s
endif

# GCC 8 linker is broken and doesn't support LENGTH(region) when defining another memory region within
# MEMORY block
ifeq ($(call get_major_version,$(arm_gcc_version_str)),8)
	LDFLAGS += -Wl,--defsym=GCC_LD_BROKEN=1
endif
