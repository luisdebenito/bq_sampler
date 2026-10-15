UNAME := $(shell uname -s)
CXX ?= clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -MMD -MP -Ithird_party -Isrc -Itests
BUILD := build

ifeq ($(UNAME),Darwin)
  LDLIBS := -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework CoreMIDI -lpthread -lm
  RTMIDI_API := __MACOSX_CORE__
else
  LDLIBS := -lpthread -lm -ldl -lasound
  RTMIDI_API := __LINUX_ALSA__
endif

DOMAIN_SRCS := src/domain/transport.cpp
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp src/adapters/ansi_display.cpp src/adapters/input_mux.cpp src/adapters/rtmidi_input.cpp src/adapters/midi_scanner.cpp
APP_SRCS := src/config.cpp src/midi_binding.cpp src/setup_learner.cpp src/setup_wizard.cpp
MAIN_SRC := src/main.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp tests/test_input_mux.cpp tests/test_rtmidi_input.cpp tests/test_midi_scanner.cpp tests/test_setup_learner.cpp
VENDOR_SRCS := third_party/RtMidi.cpp

APP_OBJS := $(addprefix $(BUILD)/,$(VENDOR_SRCS:.cpp=.o) $(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(MAIN_SRC:.cpp=.o))
TEST_OBJS := $(addprefix $(BUILD)/,$(VENDOR_SRCS:.cpp=.o) $(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(TEST_SRCS:.cpp=.o))
DEPS := $(sort $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d))

all: $(BUILD)/sampler

$(BUILD)/sampler: $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/run_tests: $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# miniaudio genera warnings propios que no son cosa nuestra
$(BUILD)/src/adapters/miniaudio_impl.o: src/adapters/miniaudio_impl.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -MMD -MP -Ithird_party -c -o $@ $<

# RtMidi tampoco es cosa nuestra; parche local: getCoreMidiClientSingleton sin throw() para que su error se pueda capturar
$(BUILD)/third_party/RtMidi.o: third_party/RtMidi.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -MMD -MP -D$(RTMIDI_API) -Ithird_party -c -o $@ $<

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests

clean:
	rm -rf $(BUILD)

-include $(DEPS)

.PHONY: all test clean
