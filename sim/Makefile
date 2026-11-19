# Desktop simulator. Needs libsdl2-dev and LVGL v9.2.2 in deps/lvgl
# (./fetch-deps.sh). Objects go to $(BUILD), default build/.
BUILD ?= build
FW := ../firmware/lib
LVGL := deps/lvgl
CC ?= cc
CXX ?= c++
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
INC := -I. -I$(LVGL) -I$(FW)/jamcore -I$(FW)/crgclient -I$(FW)/jtctl -I$(FW)/jtui -I$(FW)/boxcore -I$(FW)/boxctl \
       -I$(FW)/boxui -I$(FW)/devmenu -I$(FW)/devmenuui -I$(FW)/wsposix -I$(FW)/device \
       -I$(FW)/settings -I$(FW)/httpmini -I$(FW)/update -I$(FW)/buttons -I$(FW)/jtfonts466 $(SDL_CFLAGS)
DEFS := -DLV_CONF_INCLUDE_SIMPLE -DSIM_FONT_DIR='"$(abspath deps/montserrat)"'
CFLAGS := -O2 -g $(INC) $(DEFS)
CXXFLAGS := -std=c++17 -O1 -g -Wall $(INC) $(DEFS)

LV_SRC := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(LV_SRC))
APP_SRC := main.cpp $(FW)/wsposix/wsposix.cpp $(FW)/device/device.cpp $(FW)/jamcore/jamcore.cpp $(FW)/crgclient/crgclient.cpp $(FW)/crgclient/crgjson.cpp \
           $(FW)/jtctl/jtctl.cpp $(FW)/jtui/jtui.cpp $(FW)/boxcore/boxcore.cpp $(FW)/boxctl/boxctl.cpp \
           $(FW)/boxui/boxui.cpp $(FW)/devmenu/devmenu.cpp $(FW)/devmenuui/devmenuui.cpp \
           $(FW)/settings/settings.cpp $(FW)/httpmini/httpmini.cpp \
           $(FW)/update/update.cpp $(FW)/update/sha256.cpp $(FW)/update/httpget.cpp
APP_OBJ := $(patsubst %.cpp,$(BUILD)/app/%.o,$(notdir $(APP_SRC)))
FONT_SRC := $(wildcard $(FW)/jtui/fonts/*.c) $(wildcard $(FW)/jtfonts466/*.c)
FONT_OBJ := $(patsubst %.c,$(BUILD)/fonts/%.o,$(notdir $(FONT_SRC)))
vpath %.cpp . $(FW)/jamcore $(FW)/crgclient $(FW)/jtctl $(FW)/jtui $(FW)/boxcore $(FW)/boxctl $(FW)/boxui $(FW)/devmenu $(FW)/devmenuui $(FW)/wsposix $(FW)/device $(FW)/settings $(FW)/httpmini $(FW)/update

$(BUILD)/sim: $(APP_OBJ) $(FONT_OBJ) $(BUILD)/liblvgl.a
	$(CXX) -o $@ $(APP_OBJ) $(FONT_OBJ) $(BUILD)/liblvgl.a $(SDL_LIBS) -lm

$(BUILD)/fonts/%.o: $(FW)/jtui/fonts/%.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DLV_LVGL_H_INCLUDE_SIMPLE -c $< -o $@

$(BUILD)/fonts/%.o: $(FW)/jtfonts466/%.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DLV_LVGL_H_INCLUDE_SIMPLE -c $< -o $@

$(BUILD)/liblvgl.a: $(LV_OBJ)
	ar rcs $@ $^

$(BUILD)/%.o: %.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/app/%.o: %.cpp lv_conf.h $(wildcard $(FW)/*/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: clean
