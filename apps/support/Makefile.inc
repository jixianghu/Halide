HALIDE_BIN_PATH ?= ../..
HALIDE_SRC_PATH ?= ../..
LDFLAGS ?=
BIN ?= ./bin
IMAGES ?= ../images
HL_TARGET ?= host
UNAME ?= $(shell uname)

ifeq ($(OS), Windows_NT)
# Flags for mingw environment
LDFLAGS += -lz -lole32 -lkernel32 -lpsapi -luuid -Wl,--stack,8388608
else
LDFLAGS += -ldl -lpthread -lz
endif

CXX ?= g++
GXX ?= g++

CFLAGS += -I $(HALIDE_BIN_PATH)/include/ -I $(HALIDE_SRC_PATH)/tools/ -I $(HALIDE_SRC_PATH)/apps/support/
CXXFLAGS += -std=c++11 -I $(HALIDE_BIN_PATH)/include/ -I $(HALIDE_SRC_PATH)/tools/ -I $(HALIDE_SRC_PATH)/apps/support/

ifeq ($(UNAME), Darwin)
CXXFLAGS += -fvisibility=hidden
endif

# To run apps on android that support this, build a separate toolchain from the Android NDK
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?= $(CXXFLAGS)
CXXFLAGS-arm-64-android ?= $(CXXFLAGS)
CXXFLAGS-arm-32-android ?= $(CXXFLAGS)

LDFLAGS-host ?= $(LDFLAGS)
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie

LIB_HALIDE = $(HALIDE_BIN_PATH)/lib/libHalide.a

GENERATOR_DEPS ?= $(HALIDE_BIN_PATH)/lib/libHalide.a $(HALIDE_BIN_PATH)/include/Halide.h $(HALIDE_SRC_PATH)/tools/GenGen.cpp

LLVM_CONFIG ?= llvm-config
LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
HALIDE_SYSTEM_LDFLAGS = $(shell $(LLVM_CONFIG) --system-libs)

ifeq ($(UNAME), Linux)
# llvm-config doesn't always report -ltinfo in the system-libs. Detect it by seeing if llvm-config links to it.
HALIDE_SYSTEM_LDFLAGS += $(shell ldd `which $(LLVM_CONFIG)` | grep libtinfo > /dev/null && echo -ltinfo)
endif

LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags) $(HALIDE_SYSTEM_LDFLAGS)

LLVM_CONFIG ?= llvm-config
LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)

ifeq ($(USE_LLVM_SHARED_LIB), )
LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(LLVM_OLD_JIT_COMPONENT) $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB))
LLVM_SHARED_LIBS =
else
LLVM_STATIC_LIBS =
LLVM_SHARED_LIBS = -L $(LLVM_LIBDIR) -lLLVM-$(LLVM_FULL_VERSION)
endif


LIBPNG_LIBS_DEFAULT = $(shell libpng-config --ldflags)
LIBPNG_CXX_FLAGS ?= $(shell libpng-config --cflags)
# Workaround for libpng-config pointing to 64-bit versions on linux even when we're building for 32-bit
ifneq (,$(findstring -m32,$(CXX)))
ifneq (,$(findstring x86_64,$(LIBPNG_LIBS_DEFAULT)))
LIBPNG_LIBS ?= -lpng
endif
endif
LIBPNG_LIBS ?= $(LIBPNG_LIBS_DEFAULT)

# Workaround brew Cellar path for libpng-config output.
LIBJPEG_LINKER_PATH ?= $(shell echo $(LIBPNG_LIBS_DEFAULT) | sed -e'/-L.*[/][Cc]ellar[/]libpng/!d;s=\(.*\)/[Cc]ellar/libpng/.*=\1/lib=')
LIBJPEG_LIBS ?= $(LIBJPEG_LINKER_PATH) -ljpeg

# There's no libjpeg-config, unfortunately. We should look for
# jpeglib.h one directory level up from png.h . Also handle
# Mac OS brew installs where libpng-config returns paths
# into the PNG cellar.
LIBPNG_INCLUDE_DIRS = $(filter -I%,$(LIBPNG_CXX_FLAGS))
LIBJPEG_CXX_FLAGS ?= $(shell echo $(LIBPNG_INCLUDE_DIRS) | sed -e'/[Cc]ellar[/]libpng/!s=\(.*\)=\1/..=;s=\(.*\)/[Cc]ellar/libpng/.*=\1/include=')

IMAGE_IO_LIBS = $(LIBPNG_LIBS) $(LIBJPEG_LIBS)
IMAGE_IO_CXX_FLAGS = $(LIBPNG_CXX_FLAGS) $(LIBJPEG_CXX_FLAGS)

IMAGE_IO_FLAGS = $(IMAGE_IO_LIBS) $(IMAGE_IO_CXX_FLAGS)

PLATFORM_OPENGL_LDFLAGS=-lGL -lX11
ifeq ($(UNAME), Darwin)
PLATFORM_OPENGL_LDFLAGS=-framework OpenGL
endif

ifneq (, $(findstring opengl,$(HL_TARGET)))
  OPENGL_LDFLAGS=$(PLATFORM_OPENGL_LDFLAGS)
endif

ifneq (, $(findstring metal,$(HL_TARGET)))
  LDFLAGS += -framework Metal -framework Foundation
endif

$(BIN)/RunGen.o: $(HALIDE_SRC_PATH)/tools/RunGen.cpp
	@mkdir -p $(@D)
	@$(CXX) -c $< $(CXXFLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(BIN) -o $@

# Really, .SECONDARY is what we want, but it won't accept wildcards
.PRECIOUS: $(BIN)/%.rungen
$(BIN)/%.rungen: $(BIN)/%.a $(BIN)/RunGen.o $(HALIDE_SRC_PATH)/tools/RunGenStubs.cpp
	$(CXX) $(CXXFLAGS) -I$(BIN) -DHL_RUNGEN_FILTER_HEADER=\"$*.h\" $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

RUNARGS ?=

# Pseudo target that allows us to build-and-run in one step, e.g.
#
#     make foo.run RUNARGS='input=a output=baz'
#
$(BIN)/%.run: $(BIN)/%.rungen
	@$(CURDIR)/$< $(RUNARGS)
