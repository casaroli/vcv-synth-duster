RACK_DIR ?= $(HOME)/Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=

LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
