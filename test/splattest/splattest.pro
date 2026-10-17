include(../../splat.pri)

TARGET = splattest$$SPLAT_SUFFIX

DESTDIR = $$SPLAT_BIN

TEMPLATE = app

QT += gui gui-private

CONFIG += console

LIBS += -L$$SPLAT_LIB -lSplat$$SPLAT_SUFFIX

# NOTE: QVulkanInstance needs the Vulkan headers, the Vulkan backend is skipped without them.
#       They come from the Vulkan SDK, or from the Khronos Vulkan-Headers in build/3rdparty.
VULKAN_SDK_DIR = $$(VULKAN_SDK)

exists($$VULKAN_SDK_DIR/Include/vulkan/vulkan.h) {
    INCLUDEPATH += $$VULKAN_SDK_DIR/Include
} else:exists($$SPLAT/build/3rdparty/Vulkan-Headers/include/vulkan/vulkan.h) {
    INCLUDEPATH += $$SPLAT/build/3rdparty/Vulkan-Headers/include
}

INCLUDEPATH += $$SPLAT/test/common

HEADERS += splattest.h \
           ../common/WSplatBench.h

SOURCES += main.cpp \
           rhi.cpp \
           scene.cpp \
           compare.cpp \
           bench.cpp
