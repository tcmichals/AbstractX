################################################################################
#
# LIBMETAL
#
################################################################################

LIBMETAL_VERSION = master
LIBMETAL_SITE = $(call github,OpenAMP,libmetal,$(LIBMETAL_VERSION))
LIBMETAL_LICENSE = Apache-2.0 or BSD-3-Clause
LIBMETAL_LICENSE_FILES = LICENSE LICENSE-BSD3
LIBMETAL_INSTALL_STAGING = YES

LIBMETAL_CXXFLAGS = $(TARGET_CXXFLAGS)

ifeq ($(BR2_PACKAGE_LIBMETAL_CONTRIB),y)
LIBMETAL_CONF_OPTS += -DHWY_ENABLE_CONTRIB=ON
else
LIBMETAL_CONF_OPTS += -DHWY_ENABLE_CONTRIB=OFF
endif

ifeq ($(BR2_PACKAGE_LIBMETAL_EXAMPLES),y)
LIBMETAL_CONF_OPTS += -DHWY_ENABLE_EXAMPLES=ON
# Examples are not installed by cmake. This binary can be useful for
# quick testing and debug.
define LIBMETAL_INSTALL_EXAMPLES
	$(INSTALL) -m 0755 \
		$(@D)/examples/hwy_benchmark \
		$(TARGET_DIR)/usr/bin/hwy_benchmark
endef
LIBMETAL_POST_INSTALL_TARGET_HOOKS += LIBMETAL_INSTALL_EXAMPLES
else
LIBMETAL_CONF_OPTS += -DHWY_ENABLE_EXAMPLES=OFF
endif

ifeq ($(BR2_ARM_FPU_VFPV4),y)
LIBMETAL_CONF_OPTS += -DHWY_CMAKE_ARM7=ON
else
# LIBMETAL Armv7 Neon support requires in fact vfpv4 / neon v2. When we
# are in a vfpv3 case (e.g. Cortex-A8, Cortex-A9) this flag need to be
# set to off.
LIBMETAL_CONF_OPTS += -DHWY_CMAKE_ARM7=OFF
endif

ifeq ($(BR2_RISCV_32),y)
LIBMETAL_CONF_OPTS += -DHWY_CMAKE_RVV=OFF
endif



LIBMETAL_CONF_OPTS += \
	-DCMAKE_CXX_FLAGS="$(LIBMETAL_CXXFLAGS)"

$(eval $(cmake-package))
