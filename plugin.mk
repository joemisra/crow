# Rack SDK
RACK_DIR ?= ../..

# Plugin sources
SOURCES += $(wildcard VCVCrow.cpp)
SOURCES += $(wildcard Crow.cpp)
SOURCES += $(wildcard src/asl/asl.cpp)

# Lua sources
LUA_DIR = dep/lua
SOURCES += $(wildcard $(LUA_DIR)/*.c)
# Remove lua.c and luac.c as they contain main() functions
SOURCES := $(filter-out $(LUA_DIR)/lua.c, $(SOURCES))
SOURCES := $(filter-out $(LUA_DIR)/luac.c, $(SOURCES))


# Include paths
INC += $(LUA_DIR)
INC += src/asl # Add ASL include path

# Compiler flags
CXXFLAGS += -I$(LUA_DIR)
CXXFLAGS += -Isrc/asl # Add ASL include path

# Plugin settings
SLUG = VCVCrow
VERSION = 2.0.0
BRAND = VCVCrow

include $(RACK_DIR)/plugin.mk
