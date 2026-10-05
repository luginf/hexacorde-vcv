# Module VCV Rack 2 : make RACK_DIR=/chemin/vers/Rack-SDK   (par défaut ~/src/rack/Rack-SDK)
RACK_DIR ?= $(HOME)/src/rack/Rack-SDK

# le cœur C++ (../core) est inclus tel quel
FLAGS += -Icore
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
# après plugin.mk : compile.mk impose -std=c++11, le cœur demande C++17 (le dernier -std gagne)
CXXFLAGS += -std=c++17
