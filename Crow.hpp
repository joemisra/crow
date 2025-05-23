#pragma once

#include "rack.hpp"

// Lua headers
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace rack;

// Forward-declare the Plugin, Panel and Model objects
extern Plugin *pluginInstance;

// Forward declarations for ASL
namespace asl {
	class ASL; // Defined in asl.hpp
	struct Env; // Defined in asltypes.hpp
}

struct Crow : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		CV_INPUT_1,
		CV_INPUT_2,
		CV_INPUT_3,
		CV_INPUT_4,
		CV_INPUT_5,
		CV_INPUT_6,
		CV_INPUT_7,
		CV_INPUT_8,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUT_1,
		CV_OUTPUT_2,
		CV_OUTPUT_3,
		CV_OUTPUT_4,
		CV_OUTPUT_5,
		CV_OUTPUT_6,
		CV_OUTPUT_7,
		CV_OUTPUT_8,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	lua_State *L;
	// Array to hold the state for each of the 8 CV outputs
	struct CrowOutputState {
		float current_voltage = 0.0f;
		float target_voltage = 0.0f; // Target for direct .volts command
		float slew_seconds = 0.0f;   // Slew time for direct .volts command
		std::string current_shape = "linear"; // Shape for direct .volts command

		// ASL related members
		asl::ASL* active_asl = nullptr;
		int asl_lua_ref_done_callback = LUA_NOREF;
		asl::Env asl_env_params; // Each output has its own ASL environment parameters

		// Clock related members
		bool is_clock_output = false;
		float clock_division = 1.0f; // Stores the value passed to output[n]:clock(division)
		                               // e.g., if clock(1/4), this is 0.25. A pulse every 0.25 beats.
		int clock_lua_pulse_action_ref = LUA_NOREF; // Optional: for custom pulse ASL
		float clock_last_beat_query = 0.0f; // For tracking clock pulses
		float clock_pulse_timer = 0.0f; // For default pulse duration

		// Scale related members
		std::vector<float> scale_notes; // In semitones from root (0, 2, 4, 5, 7, 9, 11 for major)
		float scale_temperament = 12.0f;
		float scale_volts_per_octave = 1.0f;


		CrowOutputState() : current_shape("linear"), active_asl(nullptr), asl_lua_ref_done_callback(LUA_NOREF),
		                    is_clock_output(false), clock_division(1.0f), clock_lua_pulse_action_ref(LUA_NOREF),
		                    clock_last_beat_query(0.0f), clock_pulse_timer(0.0f),
		                    scale_temperament(12.0f), scale_volts_per_octave(1.0f)
		{
			// Initialize asl_env_params if it has a non-trivial constructor or needs default map values
		}

		// Destructor to clean up ASL and Lua references
		~CrowOutputState();

		// Method to stop and clean up ASL (also clears Lua callback)
		void stopASL(lua_State* L_for_unref_if_needed); // L can be nullptr if not in Lua context
		void stopClock(lua_State* L_for_unref_if_needed); // To unref clock_lua_pulse_action_ref
	};
	CrowOutputState cv_outputs[8];

	// Input state
	struct CrowInputState {
		float current_voltage = 0.0f;
		std::string mode = "none";
		// Stream mode params
		float stream_time_seconds = 0.1f;
		float stream_timer = 0.0f;
		int lua_ref_stream_callback = LUA_NOREF;
		// Change mode params
		float change_threshold = 1.0f;
		float change_hysteresis = 0.1f; // Value below threshold to trigger falling: threshold - hysteresis
		std::string change_direction = "both"; // "rising", "falling", "both"
		bool change_last_state_above_threshold = false; // Tracks if voltage was last above (threshold - hysteresis) for rising, or above threshold for falling
		int lua_ref_change_callback = LUA_NOREF;
		
		CrowInputState() = default; // Keep default constructor

		void reset_mode(lua_State* L); // Forward declaration
	};
	CrowInputState cv_inputs[8];


	Crow();
	~Crow();

    // Global clock simulation (temporary for this subtask)
    float global_simulated_beat_counter = 0.0f;
    float global_simulated_bpm = 120.0f;


	void process(const ProcessArgs &args) override;
	void runLuaScript(const std::string& script);
};

struct CrowWidget : ModuleWidget {
	CrowWidget(Crow *module);
};
