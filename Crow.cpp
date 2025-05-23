#include "Crow.hpp"
#include "rack.hpp"
#include <iostream> // For fprintf and stderr
#include <fstream>  // For file reading
#include <sstream>  // For file reading
#include <cmath>    // For fabs
#include "asl.hpp" // For asl::ASL
#include <algorithm> // for std::min_element if needed for scale

using namespace rack;

// CrowOutputState Methods
Crow::CrowOutputState::~CrowOutputState() {
    stopASL(nullptr); 
    stopClock(nullptr);
}

void Crow::CrowOutputState::stopASL(lua_State* L_for_unref) {
    delete active_asl;
    active_asl = nullptr;
    if (asl_lua_ref_done_callback != LUA_NOREF && L_for_unref) {
        luaL_unref(L_for_unref, LUA_REGISTRYINDEX, asl_lua_ref_done_callback);
    }
    asl_lua_ref_done_callback = LUA_NOREF;
}

void Crow::CrowOutputState::stopClock(lua_State* L_for_unref) {
    is_clock_output = false; 
    if (clock_lua_pulse_action_ref != LUA_NOREF && L_for_unref) {
        luaL_unref(L_for_unref, LUA_REGISTRYINDEX, clock_lua_pulse_action_ref);
    }
    clock_lua_pulse_action_ref = LUA_NOREF;
    clock_pulse_timer = 0.0f; 
}

// CrowInputState Methods
void Crow::CrowInputState::reset_mode(lua_State* L) {
    mode = "none";
    
    // Unreference stream callback
    if (lua_ref_stream_callback != LUA_NOREF && L) {
        luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_stream_callback);
    }
    lua_ref_stream_callback = LUA_NOREF;
    stream_time_seconds = 0.1f; // Default stream time
    stream_timer = 0.0f;

    // Unreference change callback
    if (lua_ref_change_callback != LUA_NOREF && L) {
        luaL_unref(L, LUA_REGISTRYINDEX, lua_ref_change_callback);
    }
    lua_ref_change_callback = LUA_NOREF;
    change_threshold = 1.0f;    // Default threshold
    change_hysteresis = 0.1f;   // Default hysteresis
    change_direction = "both";  // Default direction
    // change_last_state_above_threshold is initialized when mode is set.
}


// Helper to get Crow module instance from Lua state
static Crow* getCrowModule(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "crow_module_instance");
    Crow* module = (Crow*)lua_touserdata(L, -1);
    lua_pop(L, 1); // Remove the userdata from stack
    if (!module) {
        luaL_error(L, "Could not retrieve Crow module instance from Lua registry.");
    }
    return module;
}

// output[idx].volts = value
static int lua_output_set_volts(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    float voltage = luaL_checknumber(L, 1); 

    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    os.stopASL(L); 
    os.stopClock(L); // Setting volts stops clock mode too
    os.target_voltage = voltage;
    if (os.slew_seconds == 0.0f) { 
        os.current_voltage = voltage;
    }
    return 0;
}

// val = output[idx].volts
static int lua_output_get_volts(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    lua_pushnumber(L, module->cv_outputs[output_idx - 1].current_voltage);
    return 1;
}

// output[idx].slew = value
static int lua_output_set_slew(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    float slew_time = luaL_checknumber(L, 1);
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    os.stopClock(L); // Setting slew stops clock mode
    if (slew_time < 0.0f) slew_time = 0.0f;
    os.slew_seconds = slew_time;
    return 0;
}

// val = output[idx].slew
static int lua_output_get_slew(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
     if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    lua_pushnumber(L, module->cv_outputs[output_idx - 1].slew_seconds);
    return 1;
}

// output[idx].shape = "string"
static int lua_output_set_shape(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); 
    const char* shape_str = luaL_checkstring(L, 1); 
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    os.stopClock(L); // Setting shape stops clock mode
    os.current_shape = shape_str;
    return 0;
}

// val = output[idx].shape
static int lua_output_get_shape(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); 
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    lua_pushstring(L, module->cv_outputs[output_idx - 1].current_shape.c_str());
    return 1;
}

// output[idx].action = { {voltage, time, shape}, ... }
static int lua_output_set_action(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); 
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "action must be a table");
    }

    asl::ASLDefinition definition;
    lua_len(L, 1); 
    int num_segments = lua_tointeger(L, -1);
    lua_pop(L, 1);

    for (int i = 1; i <= num_segments; ++i) {
        lua_rawgeti(L, 1, i); 
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1); 
            return luaL_error(L, "ASL segment #%d for output %d is not a table", i, output_idx);
        }
        float voltage, time;
        std::string shape = "linear"; 
        lua_rawgeti(L, -1, 1); 
        if (!lua_isnumber(L, -1)) { lua_pop(L, 2); return luaL_error(L, "ASL segment #%d voltage (output %d) not a number", i, output_idx); }
        voltage = lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 2); 
        if (!lua_isnumber(L, -1)) { lua_pop(L, 2); return luaL_error(L, "ASL segment #%d time (output %d) not a number", i, output_idx); }
        time = lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 3); 
        if (lua_isstring(L, -1)) {
            shape = lua_tostring(L, -1);
        }
        lua_pop(L, 1); 
        definition.emplace_back(voltage, time, shape);
        lua_pop(L, 1); 
    }

    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    os.stopASL(L); 
    os.stopClock(L); // Setting action stops clock mode
    os.active_asl = new asl::ASL(definition, &os.asl_env_params, os.current_voltage);
    return 0;
}

// output[idx].done = function() ... end
static int lua_output_set_done(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); 
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    if (!lua_isfunction(L, 1) && !lua_isnil(L, 1)) {
        return luaL_error(L, "done callback for output %d must be a function or nil", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    if (os.asl_lua_ref_done_callback != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, os.asl_lua_ref_done_callback);
    }
    if (lua_isfunction(L, 1)) {
        lua_pushvalue(L, 1); 
        os.asl_lua_ref_done_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    } else { 
        os.asl_lua_ref_done_callback = LUA_NOREF;
    }
    return 0;
}

// output[idx]() -- starts the action
static int lua_output_call(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); 
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8) for call", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    os.stopClock(L); // Calling output() to start ASL stops clock mode
    if (os.active_asl) {
        os.active_asl->set_initial_voltage(os.current_voltage); 
        os.active_asl->start(); 
    }
    return 0; 
}

// output[n].clock(division_or_string)
static int lua_output_set_clock_mode(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); // Upvalue for the output[n] table itself
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range for clock()", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];

    os.stopASL(L); // Stop ASL
    os.stopClock(L); // Stop previous clock state and unref old pulse action

    if (lua_isnumber(L, 1)) {
        os.is_clock_output = true;
        os.clock_division = lua_tonumber(L, 1);
        if (os.clock_division <= 0) { // Prevent non-positive divisions
             os.clock_division = 1.0f; // Default to 1 pulse per beat
        }
        // Check for optional ASL table as pulse action
        if (lua_istable(L, 2)) {
            lua_pushvalue(L, 2); // Copy table to top
            os.clock_lua_pulse_action_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        os.target_voltage = 0.f; // Clock output usually pulses from 0
        os.current_voltage = 0.f;
        os.slew_seconds = 0.f;
    } else if (lua_isstring(L, 1) && strcmp(lua_tostring(L, 1), "none") == 0) {
        os.is_clock_output = false; // Handled by stopClock already
        // Voltage state is left as is, or could be reset.
    } else {
        return luaL_error(L, "output[%d].clock expects a number (division) or 'none'", output_idx);
    }
    return 0;
}

// output[idx].clock_div = value
static int lua_output_set_clock_div(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    float division = luaL_checknumber(L, 1);
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    if (division <= 0) return luaL_error(L, "clock_div must be positive");
    module->cv_outputs[output_idx - 1].clock_division = division;
    return 0;
}

// val = output[idx].clock_div
static int lua_output_get_clock_div(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range (1-8)", output_idx);
    }
    lua_pushnumber(L, module->cv_outputs[output_idx - 1].clock_division);
    return 1;
}

// output[n].scale({notes_table} [, temperament=12.0 [, volts_per_octave=1.0]])
// output[n].scale("none")
static int lua_output_set_scale(lua_State* L) {
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); // Upvalue for the output[n] table itself
    if (output_idx < 1 || output_idx > Crow::NUM_OUTPUTS) {
        return luaL_error(L, "output index %d out of range for scale()", output_idx);
    }
    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];

    if (lua_isstring(L, 1) && strcmp(lua_tostring(L, 1), "none") == 0) {
        os.scale_notes.clear();
        // Optionally reset temperament and V/oct to defaults, or leave them.
        // os.scale_temperament = 12.0f;
        // os.scale_volts_per_octave = 1.0f;
        return 0;
    }

    if (!lua_istable(L, 1)) {
        return luaL_error(L, "output[%d].scale expects a table of notes or 'none'", output_idx);
    }

    os.scale_notes.clear();
    lua_len(L, 1);
    int num_notes = lua_tointeger(L, -1);
    lua_pop(L, 1);

    for (int i = 1; i <= num_notes; ++i) {
        lua_rawgeti(L, 1, i);
        if (!lua_isnumber(L, -1)) {
            os.scale_notes.clear(); // Clear partial scale on error
            return luaL_error(L, "output[%d].scale: note #%d is not a number", output_idx, i);
        }
        os.scale_notes.push_back(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    // Sort notes for easier processing if needed (e.g. for finding closest)
    // std::sort(os.scale_notes.begin(), os.scale_notes.end());

    os.scale_temperament = luaL_optnumber(L, 2, 12.0);
    os.scale_volts_per_octave = luaL_optnumber(L, 3, 1.0);

    if (os.scale_temperament <= 0) os.scale_temperament = 12.0f; // Prevent invalid temperament
    if (os.scale_volts_per_octave <= 0) os.scale_volts_per_octave = 1.0f; // Prevent invalid V/oct

    return 0;
}

// output[idx].dyn[key]
static int lua_dyn_param_index(lua_State* L) {
    // Stack: 1=dyn_table, 2=key
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); // Output index from closure
    const char* dyn_key = luaL_checkstring(L, 2);

    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    auto it = os.asl_env_params.dynamics.find(dyn_key);
    if (it != os.asl_env_params.dynamics.end()) {
        lua_pushnumber(L, it->second);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// output[idx].dyn[key] = value
static int lua_dyn_param_newindex(lua_State* L) {
    // Stack: 1=dyn_table, 2=key, 3=value
    Crow* module = getCrowModule(L);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1)); // Output index from closure
    const char* dyn_key = luaL_checkstring(L, 2);

    Crow::CrowOutputState& os = module->cv_outputs[output_idx - 1];
    if (lua_isnil(L, 3)) {
        os.asl_env_params.dynamics.erase(dyn_key);
    } else {
        float value = luaL_checknumber(L, 3);
        os.asl_env_params.dynamics[dyn_key] = value;
    }
    return 0;
}

// Called when output[idx].dyn is accessed
static int lua_output_get_dyn_table(lua_State* L) {
    // Output index is an upvalue for this function when returned by lua_output_generic_index
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));

    lua_newtable(L); // This is the dyn table to be returned
    int dyn_table_idx = lua_gettop(L);

    lua_createtable(L, 0, 2); // Metatable for dyn table
    int metatable_idx = lua_gettop(L);

    // Set __index for dyn metatable
    lua_pushinteger(L, output_idx); // Pass output_idx as upvalue to lua_dyn_param_index
    lua_pushcclosure(L, lua_dyn_param_index, 1);
    lua_setfield(L, metatable_idx, "__index");

    // Set __newindex for dyn metatable
    lua_pushinteger(L, output_idx); // Pass output_idx as upvalue to lua_dyn_param_newindex
    lua_pushcclosure(L, lua_dyn_param_newindex, 1);
    lua_setfield(L, metatable_idx, "__newindex");

    lua_setmetatable(L, dyn_table_idx); // Set metatable on the dyn table

    return 1; // Return the dyn table
}


// Generic __index metamethod for output[n] tables
static int lua_output_generic_index(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* key = luaL_checkstring(L, 2);
    int output_idx_for_closure = lua_tointeger(L, lua_upvalueindex(1)); 

    if (strcmp(key, "volts") == 0) {
        lua_pushinteger(L, output_idx_for_closure); 
        lua_pushcclosure(L, lua_output_get_volts, 1);
        return 1;
    }
    if (strcmp(key, "slew") == 0) {
        lua_pushinteger(L, output_idx_for_closure); 
        lua_pushcclosure(L, lua_output_get_slew, 1);
        return 1;
    }
    if (strcmp(key, "shape") == 0) {
        lua_pushinteger(L, output_idx_for_closure);
        lua_pushcclosure(L, lua_output_get_shape, 1);
        return 1;
    }
    if (strcmp(key, "clock_div") == 0) {
        lua_pushinteger(L, output_idx_for_closure);
        lua_pushcclosure(L, lua_output_get_clock_div, 1);
        return 1;
    }
    if (strcmp(key, "clock") == 0) { // output[n].clock is a function
        lua_pushinteger(L, output_idx_for_closure);
        lua_pushcclosure(L, lua_output_set_clock_mode, 1);
        return 1;
    }
    if (strcmp(key, "scale") == 0) { // output[n].scale is a function
        lua_pushinteger(L, output_idx_for_closure);
        lua_pushcclosure(L, lua_output_set_scale, 1);
        return 1;
    }
    if (strcmp(key, "dyn") == 0) { // output[n].dyn returns a table
        lua_pushinteger(L, output_idx_for_closure); // Pass output_idx as upvalue to lua_output_get_dyn_table
        lua_pushcclosure(L, lua_output_get_dyn_table, 1);
        return 1;
    }
    if (strcmp(key, "action") == 0 || strcmp(key, "done") == 0) {
        lua_pushnil(L); 
        return 1;
    }
    lua_getfield(L, 1, key); 
    return 1;
}

// Generic __newindex metamethod for output[n] tables
static int lua_output_generic_newindex(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* key = luaL_checkstring(L, 2);
    int output_idx = lua_tointeger(L, lua_upvalueindex(1));
    lua_pushvalue(L, 3); 
    
    if (strcmp(key, "volts") == 0) {
        lua_pushinteger(L, output_idx); 
        lua_pushcclosure(L, lua_output_set_volts, 1); 
        lua_insert(L, -2); 
        lua_call(L, 1, 0); 
        return 0;
    }
    if (strcmp(key, "slew") == 0) {
        lua_pushinteger(L, output_idx);
        lua_pushcclosure(L, lua_output_set_slew, 1);
        lua_insert(L, -2);
        lua_call(L, 1, 0);
        return 0;
    }
    if (strcmp(key, "shape") == 0) {
        lua_pushinteger(L, output_idx);
        lua_pushcclosure(L, lua_output_set_shape, 1);
        lua_insert(L, -2);
        lua_call(L, 1, 0);
        return 0;
    }
    if (strcmp(key, "clock_div") == 0) {
        lua_pushinteger(L, output_idx);
        lua_pushcclosure(L, lua_output_set_clock_div, 1);
        lua_insert(L, -2);
        lua_call(L, 1, 0);
        return 0;
    }
    if (strcmp(key, "action") == 0) {
        lua_pushinteger(L, output_idx);
        lua_pushcclosure(L, lua_output_set_action, 1);
        lua_insert(L, -2);
        lua_call(L, 1, 0);
        return 0;
    }
     if (strcmp(key, "done") == 0) {
        lua_pushinteger(L, output_idx);
        lua_pushcclosure(L, lua_output_set_done, 1);
        lua_insert(L, -2);
        lua_call(L, 1, 0);
        return 0;
    }
    lua_pop(L, 1); 
    return luaL_error(L, "cannot set unknown property '%s' on output table for output %d", key, output_idx);
}


// Helper function to load and execute a Lua file
// Returns true on success, false on failure
bool loadLuaFile(lua_State* L, const std::string& filename) {
    std::string path = asset::plugin(pluginInstance, "res/lua/" + filename);
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "[VCVCrow] Error: Could not open Lua file: %s\n", path.c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    std::string scriptContent = buffer.str();

    if (luaL_dostring(L, scriptContent.c_str()) != LUA_OK) {
        fprintf(stderr, "[VCVCrow] Lua error loading %s: %s\n", filename.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message from the stack
        return false;
    }
    fprintf(stdout, "[VCVCrow] Successfully loaded Lua file: %s\n", filename.c_str());
    return true;
}


Crow::Crow() {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	L = luaL_newstate();
	if (L == NULL) {
		fprintf(stderr, "[VCVCrow] Error initializing Lua state\n");
		return;
	}
	fprintf(stdout, "[VCVCrow] Lua state initialized.\n");
	luaL_openlibs(L);
	fprintf(stdout, "[VCVCrow] Lua standard libraries opened.\n");

    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "crow_module_instance");
    fprintf(stdout, "[VCVCrow] Crow module instance stored in Lua registry.\n");

	runLuaScript("print('[LUA] Hello from VCVCrow!')");

	const char* luaFiles[] = {
		"core.lua", "asllib.lua", "sequins.lua", "metro.lua", "clock.lua", "timeline.lua"
	};
	for (const char* filename : luaFiles) {
		if (!loadLuaFile(L, filename)) { /* error printed by loadLuaFile */ }
	}

    lua_newtable(L); // Global 'output' table
    int output_global_table_idx = lua_gettop(L);

    for (int i = 0; i < NUM_OUTPUTS; ++i) {
        lua_createtable(L, 0, 1); // output_obj_table (for output[i+1]), 0 array, 1 hash part for _idx initially
        int output_obj_table_idx = lua_gettop(L);

        lua_pushinteger(L, i + 1);
        lua_setfield(L, output_obj_table_idx, "_idx"); // output[n]._idx = n

        lua_createtable(L, 0, 3); // Metatable: __index, __newindex, __call
        int metatable_idx = lua_gettop(L);

        lua_pushinteger(L, i + 1); // Upvalue for __index closure
        lua_pushcclosure(L, lua_output_generic_index, 1);
        lua_setfield(L, metatable_idx, "__index");

        lua_pushinteger(L, i + 1); // Upvalue for __newindex closure
        lua_pushcclosure(L, lua_output_generic_newindex, 1);
        lua_setfield(L, metatable_idx, "__newindex");

        lua_pushinteger(L, i + 1); // Upvalue for __call closure
        lua_pushcclosure(L, lua_output_call, 1);
        lua_setfield(L, metatable_idx, "__call");

        lua_setmetatable(L, output_obj_table_idx);
        lua_rawseti(L, output_global_table_idx, i + 1); // output[i+1] = output_obj_table
    }
    lua_setglobal(L, "output");
    fprintf(stdout, "[VCVCrow] 'output' table (with call metatable) created and populated in Lua.\n");

	runLuaScript("if crow and crow.version then print('[LUA] crow.version from core.lua: ' .. crow.version) else print('[LUA] crow.version not found.') end");
    runLuaScript("if lfo then my_lfo = lfo(1, 5, 'sine'); print('[LUA] Created my_lfo: ' .. type(my_lfo)) else print('[LUA] lfo function not found.') end");

    // Previous output tests are good but let's comment them for new ASL test
    /* runLuaScript(R"(
        print('[LUA] Testing outputs...') ... (old tests)
    )"); */

    // Previous output tests are good but let's comment them for new ASL test
    /* runLuaScript(R"(
        print('[LUA] Testing outputs...') ... (old tests)
    )"); */

    // New ASL Test (will be added in next step, once process loop is updated)
    // For now, add clock test, then scale test
    runLuaScript(R"(
        print('[LUA] Testing output clock...')
        output[1].clock(1/4) 
        print('[LUA] Output 1 clock_div (should be 0.25): ' .. output[1].clock_div)
        output[1].clock_div = 1/8 
        print('[LUA] Output 1 new clock_div (should be 0.125): ' .. output[1].clock_div)

        output[2].clock(1) 
        output[2].clock{ {5.0, 0.005}, {0.0, 0.05} } 
        print('[LUA] Output 2 set to clock with custom pulse ASL.')

        output[3].clock('none')
        print('[LUA] Output 3 clock mode disabled.')

        print('[LUA] Testing output scale...')
        output[4].volts = 0.2 -- approx C# (0.166 * 1.2 = 0.1992 for 1.2V/oct)
                              -- For 1V/oct, C is 0, C# is 1/12 = 0.0833, D is 2/12 = 0.1666
                              -- So 0.2 is between D and D#
        output[4].scale({0, 2, 4, 5, 7, 9, 11}) -- C Major scale (semitones)
        -- After process, output[4].volts should be quantized to D (0.1666V) or C (0V) or E (0.3333V)
        -- depending on closest. 0.2 is closer to D (0.1666) than E (0.3333).
        -- (0.2 - 0.1666) = 0.0334. (0.3333 - 0.2) = 0.1333. So D.
        -- The Lua print will happen before process(), so it will show 0.2.
        print('[LUA] Output 4 volts set to 0.2, scale C Major. Current (pre-process) volts: ' .. output[4].volts)

        output[1].scale("none") -- Turn off scale for output 1
        print('[LUA] Output 1 scale disabled.')
    )");

    runLuaScript(R"(
        print("[LUA] Starting output.dyn verification tests...");

        -- Test 1: dyn affecting target voltage of the first segment
        print("[LUA] Dyn Test 1: Modifying _asl_target_V before start for output[1]");
        output[1].action = { {1.0, 0.5, "linear"}, {3.0, 0.5, "linear"} };
        output[1].dyn._asl_target_V = 2.0;
        output[1](); 
        -- Expected C++ asl.cpp print: ASL using dynamic target_v: 2.000000 (for segment 0)
        -- Expected C++ asl.cpp print: ASL using definition target_v: 3.000000 (for segment 1, if dyn cleared or not re-applied)
        
        -- Clear dyn var for output[1] to avoid interference
        print("[LUA] Clearing output[1].dyn._asl_target_V");
        output[1].dyn._asl_target_V = nil;

        -- Test 2: dyn affecting duration of the first segment for output[2]
        print("[LUA] Dyn Test 2: Modifying _asl_duration_S before start for output[2]");
        output[2].action = { {4.0, 1.0, "linear"} };
        output[2].dyn._asl_duration_S = 0.25;
        output[2]();
        -- Expected C++ asl.cpp print: ASL using dynamic duration_s: 0.250000 (for segment 0 of output 2's ASL)

        -- Clear dyn var for output[2]
        print("[LUA] Clearing output[2].dyn._asl_duration_S");
        output[2].dyn._asl_duration_S = nil;

        -- Test 3: Sticky dyn affecting subsequent segments for output[3]
        print("[LUA] Dyn Test 3: Sticky dyn affecting later segments for output[3]");
        output[3].action = { {1.0, 0.1, "linear"}, {2.0, 0.1, "linear"}, {3.0, 0.1, "linear"} };
        output[3].dyn._asl_target_V = 1.5;   
        output[3].dyn._asl_duration_S = 0.05; 
        output[3]();
        -- Expected C++ asl.cpp prints for output 3's ASL:
        -- Segment 0: dyn target_v: 1.5, dyn duration_s: 0.05
        -- Segment 1: dyn target_v: 1.5, dyn duration_s: 0.05 (due to stickiness)
        -- Segment 2: dyn target_v: 1.5, dyn duration_s: 0.05 (due to stickiness)
        
        print("[LUA] output.dyn verification tests complete.");
    )");
}

Crow::~Crow() {
	if (L) {
        for (int i = 0; i < NUM_OUTPUTS; ++i) {
            cv_outputs[i].stopASL(L); 
            cv_outputs[i].stopClock(L); 
        }
		lua_close(L);
		fprintf(stdout, "[VCVCrow] Lua state closed.\n");
	}
}

void Crow::process(const ProcessArgs &args) {
    // Simulate global clock
    float beats_per_second = global_simulated_bpm / 60.0f;
    global_simulated_beat_counter += args.sampleTime * beats_per_second;

    const float DEFAULT_PULSE_DURATION_SECONDS = 0.001f; // 1ms pulse

    for (int i = 0; i < NUM_OUTPUTS; ++i) {
        CrowOutputState& os = cv_outputs[i];

        // Handle clock pulse timer (to bring default pulse down after duration)
        if (os.clock_pulse_timer > 0.0f) {
            os.clock_pulse_timer -= args.sampleTime;
            if (os.clock_pulse_timer <= 0.0f) {
                os.clock_pulse_timer = 0.0f;
                // Only bring pulse down if no custom ASL has taken over for the pulse.
                // If a custom ASL is running, it controls the voltage.
                if (os.clock_lua_pulse_action_ref == LUA_NOREF || (os.active_asl && !os.active_asl->is_running())) {
                    os.target_voltage = 0.0f; 
                    os.current_voltage = 0.0f; 
                    os.slew_seconds = 0.0f;
                }
            }
        }

        if (os.is_clock_output) {
            float beats_per_pulse = os.clock_division; 
            if (beats_per_pulse <= 0.00001f) beats_per_pulse = 1.0f; // safety

            // Check if a new pulse needs to be triggered
            if ( floor(global_simulated_beat_counter / beats_per_pulse) > 
                 floor(os.clock_last_beat_query / beats_per_pulse) ) {
                
                os.stopASL(L); // Stop any previous ASL on this output.

                if (os.clock_lua_pulse_action_ref != LUA_NOREF && L) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, os.clock_lua_pulse_action_ref);
                    if (lua_istable(L, -1)) {
                        asl::ASLDefinition pulse_asl_def;
                        lua_len(L, -1);
                        int num_segments = lua_tointeger(L, -1);
                        lua_pop(L,1); 

                        for (int k=1; k <= num_segments; ++k) {
                            lua_rawgeti(L, -2, k); 
                            if (lua_istable(L, -1)) {
                                float v; std::string s = "linear"; float t;
                                lua_rawgeti(L, -1, 1); v = luaL_optnumber(L, -1, 0.0); lua_pop(L,1);
                                lua_rawgeti(L, -1, 2); t = luaL_optnumber(L, -1, 0.01); lua_pop(L,1); 
                                lua_rawgeti(L, -1, 3); if(lua_isstring(L,-1)) s=lua_tostring(L,-1); lua_pop(L,1);
                                pulse_asl_def.emplace_back(v,t,s);
                            }
                            lua_pop(L,1); 
                        }
                        // Ensure old ASL is cleaned up before assigning new
                        delete os.active_asl; 
                        os.active_asl = new asl::ASL(pulse_asl_def, &os.asl_env_params, os.current_voltage);
                        os.active_asl->start();
                         // Custom ASL pulse started, clock_pulse_timer not used for this pulse.
                        os.clock_pulse_timer = 0.0f; 
                    }
                    lua_pop(L,1); 
                } else {
                    // Default pulse: 5V for 1ms
                    os.target_voltage = 5.0f;
                    os.current_voltage = 5.0f; 
                    os.slew_seconds = 0.0f;
                    os.clock_pulse_timer = DEFAULT_PULSE_DURATION_SECONDS;
                }
            }
            os.clock_last_beat_query = global_simulated_beat_counter;
        }


        // ASL processing (could be from clock's custom pulse or direct .action)
        if (os.active_asl && os.active_asl->is_running()) {
            float voltage_from_asl;
            if (os.active_asl->process(args.sampleTime, voltage_from_asl)) {
                os.current_voltage = voltage_from_asl;
            } else { 
                os.current_voltage = voltage_from_asl; 
                os.target_voltage = os.current_voltage; 
                os.slew_seconds = 0.f; 
                if (os.asl_lua_ref_done_callback != LUA_NOREF && L) { 
                    lua_rawgeti(L, LUA_REGISTRYINDEX, os.asl_lua_ref_done_callback);
                    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[VCVCrow] Lua error in ASL done callback for output %d: %s\n", i+1, lua_tostring(L, -1));
                        lua_pop(L, 1); 
                    }
                }
            }
        } else if (!os.is_clock_output || os.clock_pulse_timer > 0) { 
            // Standard slew logic if not a clock output, or if it's a clock output currently in its default pulse down-phase.
            // If clock_pulse_timer > 0, current_voltage is already set by pulse logic.
            // This mainly applies if NOT a clock output.
            if (!os.is_clock_output) {
                 if (os.slew_seconds > 0.00001f) { 
                    float diff_to_target = os.target_voltage - os.current_voltage;
                    if (fabs(diff_to_target) < 0.0001f) { 
                        os.current_voltage = os.target_voltage;
                        os.slew_seconds = 0.0f; 
                    } else {
                        float full_range_for_rate_calc = 15.0f; 
                        float rate = full_range_for_rate_calc / os.slew_seconds;
                        float max_delta_this_frame = rate * args.sampleTime;
                        float movement = std::max(-max_delta_this_frame, std::min(max_delta_this_frame, diff_to_target));
                        os.current_voltage += movement;
                    }
                } else { 
                    os.current_voltage = os.target_voltage;
                }
            }
        }
        // If it IS a clock output and not in pulse timer, and no ASL, voltage should be 0 (or last custom ASL value)
        // This is handled: if clock_pulse_timer just finished, current_voltage becomes 0.
        // If an ASL pulse finished, current_voltage is its last value.

        outputs[CV_OUTPUT_1 + i].setVoltage(os.current_voltage);
    }
}

void Crow::runLuaScript(const std::string& script) {
	if (!L) {
		fprintf(stderr, "[VCVCrow] Lua state not initialized, cannot run script.\n");
		return;
	}

	if (luaL_dostring(L, script.c_str()) != LUA_OK) {
		fprintf(stderr, "[VCVCrow] Lua error running script: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1); // Pop the error message from the stack
	}
}


CrowWidget::CrowWidget(Crow *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Crow.svg")));

	// Add screws
	addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	// Add input and output ports
	float portStartY = 50.f;
	float portSpacingY = 40.f;
	float inputX = 15.f;
	float outputX = 60.f; // Adjust as needed for standard width panel

	for (int i = 0; i < Crow::NUM_INPUTS; ++i) {
		addInput(createInputCentered<PJ301MPort>(Vec(inputX, portStartY + i * portSpacingY), module, Crow::InputIds(CV_INPUT_1 + i)));
	}

	for (int i = 0; i < Crow::NUM_OUTPUTS; ++i) {
		addOutput(createOutputCentered<PJ301MPort>(Vec(outputX, portStartY + i * portSpacingY), module, Crow::OutputIds(CV_OUTPUT_1 + i)));
	}
}

// Register the module model
Model *modelCrow = createModel<Crow, CrowWidget>("Crow");
