#include "asl.hpp"
#include <cmath> // For pow, log, fabs
#include <algorithm> // For std::min, std::max
// #include <iostream> // For debug prints if needed

namespace asl {

ASL::ASL(const ASLDefinition& def, asl::Env* env_params_ptr, float initial_voltage_val) :
    definition(def),
    env_params(env_params_ptr),
    initial_voltage(initial_voltage_val),
    running(false),
    current_segment_index(0),
    current_segment_time_elapsed(0.0f) {
    // env_params might be used later for shape dynamics if not handled in Lua
}

ASL::~ASL() {
    // Nothing to do here for now, no dynamic allocations owned by ASL itself
}

void ASL::start(bool new_gate_value) { // new_gate_value for ADSR later
    if (definition.empty()) {
        running = false;
        return;
    }
    running = true;
    current_segment_index = 0;
    current_segment_time_elapsed = 0.0f;

    current_segment_voltage_start = initial_voltage; // Captured when ASL is started
    
    float target_override = definition[0].target_voltage;
    float duration_override = definition[0].duration_secs;

    if (env_params) {
        std::string target_key = "_asl_target_V"; 
        auto target_it = env_params->dynamics.find(target_key);
        if (target_it != env_params->dynamics.end()) {
            target_override = target_it->second;
            fprintf(stdout, "[ASL DEBUG] ASL::start() - Seg 0: Found dyn._asl_target_V = %.2f\n", target_override);
        } else {
            // fprintf(stdout, "[ASL DEBUG] ASL::start() - Seg 0: No dyn._asl_target_V, using def: %.2f\n", target_override);
        }

        std::string duration_key = "_asl_duration_S"; 
        auto duration_it = env_params->dynamics.find(duration_key);
        if (duration_it != env_params->dynamics.end()) {
            duration_override = duration_it->second;
            fprintf(stdout, "[ASL DEBUG] ASL::start() - Seg 0: Found dyn._asl_duration_S = %.2f\n", duration_override);
        } else {
            // fprintf(stdout, "[ASL DEBUG] ASL::start() - Seg 0: No dyn._asl_duration_S, using def: %.2f\n", duration_override);
        }
    }
    current_segment_voltage_target = target_override;
    current_segment_duration_total = duration_override;
    current_segment_shape = definition[0].shape;

    if (current_segment_duration_total <= 0.00001f) {
        initial_voltage = current_segment_voltage_target; 
        current_segment_time_elapsed = current_segment_duration_total; 
    }
    // std::cout << "[ASL] Started. Seg 0. InitV: " << initial_voltage << " Target: " << current_segment_voltage_target << " Dur: " << current_segment_duration_total << " Shape: " << current_segment_shape << std::endl;
}

bool ASL::process(float sampleTime, float& out_voltage) {
    if (!running || definition.empty()) {
        if (running) { // Was running but definition became empty somehow (should not happen)
            stop();
        }
        // If it was never properly started or definition is empty, output initial_voltage
        // or the last known target if it was running.
        // For safety, let's assume if it's not running it shouldn't change out_voltage.
        // The caller (Crow::process) will handle what to do if ASL is not running.
        return false;
    }

    current_segment_time_elapsed += sampleTime;

    float t = 0.0f;
    if (current_segment_duration_total > 0.00001f) { // Avoid division by zero
        t = current_segment_time_elapsed / current_segment_duration_total;
    } else {
        t = 1.0f; // Effectively instant if duration is zero/negative
    }
    t = std::max(0.0f, std::min(1.0f, t)); // Clamp t

    if (current_segment_shape == "linear") {
        out_voltage = current_segment_voltage_start + (current_segment_voltage_target - current_segment_voltage_start) * t;
    } else if (current_segment_shape == "exponential" || current_segment_shape == "exp") {
        // Simple exponential: Vstart + (Vtarget - Vstart) * t^power
        // A common 'exponential' in audio often means Vstart * (Vtarget/Vstart)^t (if Vstart != 0)
        // or based on log/exp curves. For now, simple power.
        // Crow's own asllib.lua uses a specific formula for exp which might be different.
        // This C++ version is a placeholder.
        // float power = (env_params && env_params->dynamics.count("exponential")) ? env_params->dynamics.at("exponential") : 2.0f;
        // if (power <= 0) power = 2.0f; // Ensure positive power
        // out_voltage = current_segment_voltage_start + (current_segment_voltage_target - current_segment_voltage_start) * std::pow(t, power);
        // Let's use the behavior of VCV's LFO for "exponential":
        // if target > start, it's (target-start) * t^2 + start
        // if target < start, it's (start-target) * (1-(1-t)^2) + target ... no, that's for log when going down.
        // A common way: map t to an exponential curve.
        // For now, defer to linear if shape is not "linear", and print warning once.
        // This part should ideally match lua behavior or be very well defined.
        // For this task, only linear is strictly required for ASL::process.
        out_voltage = current_segment_voltage_start + (current_segment_voltage_target - current_segment_voltage_start) * t;
        // static bool shape_warning_printed = false;
        // if (!shape_warning_printed) {
        //    fprintf(stderr, "[VCVCrow ASL] Warning: Non-linear shapes in C++ ASL::process are placeholders and behave as linear.\n");
        //    shape_warning_printed = true;
        // }
    } else if (current_segment_shape == "logarithmic" || current_segment_shape == "log") {
        // Placeholder - behaves as linear
        out_voltage = current_segment_voltage_start + (current_segment_voltage_target - current_segment_voltage_start) * t;
    } else { // Unknown shape
        out_voltage = current_segment_voltage_start + (current_segment_voltage_target - current_segment_voltage_start) * t;
    }


    if (current_segment_time_elapsed >= current_segment_duration_total) {
        current_segment_voltage_start = current_segment_voltage_target; // Ensure target is reached
        out_voltage = current_segment_voltage_target; // Final value of this segment

        current_segment_index++;
        if (current_segment_index >= definition.size()) {
            running = false;
            // std::cout << "[ASL] Finished. Final V: " << out_voltage << std::endl;
            return false; // ASL program finished
        } else {
            // Setup next segment
            // current_segment_voltage_start is already set to previous target
            
            float target_override_next = definition[current_segment_index].target_voltage;
            float duration_override_next = definition[current_segment_index].duration_secs;

            if (env_params) {
                std::string target_key = "_asl_target_V"; 
                auto target_it = env_params->dynamics.find(target_key);
                if (target_it != env_params->dynamics.end()) {
                    target_override_next = target_it->second;
                    fprintf(stdout, "[ASL DEBUG] ASL::process() - Seg %zu: Found dyn._asl_target_V = %.2f\n", current_segment_index, target_override_next);
                } else {
                    // fprintf(stdout, "[ASL DEBUG] ASL::process() - Seg %zu: No dyn._asl_target_V, using def: %.2f\n", current_segment_index, target_override_next);
                }

                std::string duration_key = "_asl_duration_S"; 
                auto duration_it = env_params->dynamics.find(duration_key);
                if (duration_it != env_params->dynamics.end()) {
                    duration_override_next = duration_it->second;
                    fprintf(stdout, "[ASL DEBUG] ASL::process() - Seg %zu: Found dyn._asl_duration_S = %.2f\n", current_segment_index, duration_override_next);
                } else {
                    // fprintf(stdout, "[ASL DEBUG] ASL::process() - Seg %zu: No dyn._asl_duration_S, using def: %.2f\n", current_segment_index, duration_override_next);
                }
            }
            current_segment_voltage_target = target_override_next;
            current_segment_duration_total = duration_override_next;
            current_segment_shape = definition[current_segment_index].shape;
            current_segment_time_elapsed = 0.0f;

            if (current_segment_duration_total <= 0.00001f) {
                current_segment_time_elapsed = current_segment_duration_total; 
                out_voltage = current_segment_voltage_target; 
            }
            // std::cout << "[ASL] Next Segment (" << current_segment_index << "). StartV: " << current_segment_voltage_start << " Target: " << current_segment_voltage_target << " Dur: " << current_segment_duration_total << " Shape: " << current_segment_shape << std::endl;
        }
    }
    return true; // Still running
}

void ASL::stop() {
    running = false;
    // std::cout << "[ASL] Stopped." << std::endl;
}

} // namespace asl
