#pragma once

#include <vector>
#include <string>
#include <map>

namespace asl {

// Environment parameters for ASL, e.g., dynamics for shapes
struct Env {
    std::map<std::string, float> dynamics;
    // Example: dynamics["exponential"] = 2.0 (power for exponential shape)
    // For now, this is unused by the C++ ASL but available for Lua.
};

// A single segment in an ActionSlang (ASL) definition
struct ASLSegment {
    float target_voltage;
    float duration_secs;
    std::string shape = "linear"; // "linear", "exponential", "logarithmic"
    // Potentially add other parameters per segment if needed later

    ASLSegment(float tv, float dur, std::string sh = "linear") :
        target_voltage(tv), duration_secs(dur), shape(sh) {}
};

// An ASL definition is a sequence of segments
using ASLDefinition = std::vector<ASLSegment>;

} // namespace asl
