#pragma once

#include <mmd/pmx.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pmxer::ai {

enum class OperationType { recolor, transform, generate, subdivide, smooth, inflate };
enum class Shape { none, loafers, shirt, skirt, dress, pants, boots, coat };

struct Operation {
    OperationType type{OperationType::recolor};
    Shape shape{Shape::none};
    mmd::Float4 color{0.1F, 0.1F, 0.1F, 1.0F};
    mmd::Float3 translation{};
    mmd::Float3 scale{1.0F, 1.0F, 1.0F};
    mmd::Float3 rotation{}; // Degrees, XYZ, about the captured selection center.
    bool replace{true};
    int detail{24};
    float amount{0.25F}; // Smoothing strength or normal displacement in PMX units.
    int iterations{1};
};

struct EditTarget {
    std::vector<std::size_t> materialIndices; // Empty means all materials within the capture.
    bool completeMaterials{}; // Complete a touched garment only when the capture permits it.
    mmd::Float3 minimum{0, 0, 0}, maximum{1, 1, 1}; // Relative to captured bounds.
};

struct EditPlan {
    std::string summary;
    std::vector<Operation> operations;
    EditTarget target{};
};

[[nodiscard]] nlohmann::json planSchema();
[[nodiscard]] EditPlan parsePlan(std::string_view json);
[[nodiscard]] std::string plannerInstructions();

} // namespace pmxer::ai
