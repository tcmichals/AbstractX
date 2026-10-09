################################################################################
#
# cpputest
#
################################################################################

CPPUTEST_VERSION = v4.0
CPPUTEST_SITE = $(call github,cpputest,cpputest,$(CPPUTEST_VERSION))
CPPUTEST_LICENSE = BSD-3-Clause
CPPUTEST_LICENSE_FILES = COPYING
CPPUTEST_INSTALL_STAGING = YES
CPPUTEST_CONF_OPTS = \
	-DTESTS=OFF \
	-DTESTS_DETAILED=OFF \
	-DTESTS_BUILD_DISCOVER=OFF \
	-DEXTENSIONS=ON \
	-DEXAMPLES=OFF \
	-DMEMORY_LEAK_DETECTION=ON \
	-DCPPUTEST_FLAGS=OFF \
	-DC++11=ON

$(eval $(cmake-package))
