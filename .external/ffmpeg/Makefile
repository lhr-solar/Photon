MAIN_MAKEFILE=1
include ffbuild/config.mak

vpath %.c     $(SRC_PATH)
vpath %.cpp   $(SRC_PATH)
vpath %.h     $(SRC_PATH)
vpath %.inc   $(SRC_PATH)
vpath %.m     $(SRC_PATH)
vpath %.S     $(SRC_PATH)
vpath %.asm   $(SRC_PATH)
vpath %.rc    $(SRC_PATH)
vpath %.v     $(SRC_PATH)
vpath %.glsl  $(SRC_PATH)
vpath %.spv   $(SRC_PATH)
vpath %.cu    $(SRC_PATH)
vpath %.ptx   $(SRC_PATH)
vpath %.metal $(SRC_PATH)

ALLFFLIBS = avcodec avdevice avfilter avformat avutil swscale swresample

FFLIBS-$(CONFIG_AVDEVICE)   += avdevice
FFLIBS-$(CONFIG_AVFILTER)   += avfilter
FFLIBS-$(CONFIG_AVFORMAT)   += avformat
FFLIBS-$(CONFIG_AVCODEC)    += avcodec
FFLIBS-$(CONFIG_SWRESAMPLE) += swresample
FFLIBS-$(CONFIG_SWSCALE)    += swscale
FFLIBS := avutil

all: all-yes

include $(SRC_PATH)/ffbuild/common.mak

FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS := $(DEP_LIBS)
FF_STATIC_DEP_LIBS := $(STATIC_DEP_LIBS)

SUBDIR_VARS := CLEANFILES FFLIBS HOSTPROGS TESTPROGS TOOLS               \
               HEADERS ARCH_HEADERS BUILT_HEADERS SKIPHEADERS            \
               ARMV5TE-OBJS ARMV6-OBJS ARMV8-OBJS VFP-OBJS NEON-OBJS     \
               ALTIVEC-OBJS VSX-OBJS X86ASM-OBJS                         \
               MIPSFPU-OBJS MIPSDSPR2-OBJS MIPSDSP-OBJS MSA-OBJS         \
               MMI-OBJS LSX-OBJS LASX-OBJS RV-OBJS RVV-OBJS RVVB-OBJS    \
               OBJS SHLIBOBJS STLIBOBJS HOSTOBJS TESTOBJS SIMD128-OBJS   \
               SVE-OBJS SVE2-OBJS SME-OBJS SME2-OBJS

define RESET
$(1) :=
$(1)-yes :=
endef

define DOSUBDIR
$(foreach V,$(SUBDIR_VARS),$(eval $(call RESET,$(V))))
SUBDIR := $(1)/
include $(SRC_PATH)/$(1)/Makefile
-include $(SRC_PATH)/$(1)/$(ARCH)/Makefile
-include $(SRC_PATH)/$(1)/$(INTRINSICS)/Makefile
include $(SRC_PATH)/ffbuild/library.mak
endef

$(foreach D,$(FFLIBS),$(eval $(call DOSUBDIR,lib$(D))))

$(ALLFFLIBS:%=lib%/version.o): libavutil/ffversion.h

VERSION_SH = $(SRC_PATH)/ffbuild/version.sh
.version: $(VERSION_SH) ffbuild/config.mak
.version: M=@
libavutil/ffversion.h .version: REVISION=unknown
libavutil/ffversion.h .version:
	$(M)revision=$(REVISION) $(VERSION_SH) $(SRC_PATH) libavutil/ffversion.h $(EXTRA_VERSION)
	$(Q)touch .version
-include .version

$(sort $(OUTDIRS)):
	$(Q)mkdir -p $@

%.h:
	@:

.SUFFIXES:
.PHONY: all all-yes
