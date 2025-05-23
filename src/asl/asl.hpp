#pragma once

#include "asltypes.hpp"
#include <string> // For std::string
#include <vector> // For std::vector (used by ASLDefinition)

namespace asl {

class ASL {
public:
    ASL(const ASLDefinition& def, asl::Env* env_params_ptr, float initial_voltage_val);
    ~ASL();

    void start(bool new_gate_value = true); // new_gate_value for ADSR later
    bool process(float sampleTime, float& out_voltage); // Returns true if still running
    void stop();
    bool is_running() const { return running; }
    void set_initial_voltage(float voltage) { initial_voltage = voltage; }


private:
    ASLDefinition definition;
    asl::Env* env_params; // Pointer to owner's Env params for this ASL instance

    float current_segment_voltage_start = 0.0f;
    float current_segment_voltage_target = 0.0f;
    float current_segment_duration_total = 0.0f;
    float current_segment_time_elapsed = 0.0f;
    std::string current_segment_shape = "linear";
    size_t current_segment_index = 0;
    bool running = false;
    float initial_voltage = 0.0f; // Voltage at the moment ASL is started or re-triggered
};

} // namespace asl
