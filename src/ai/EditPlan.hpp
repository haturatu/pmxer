#pragma once

#include <mmd/pmx.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pmxer::ai {

enum class OperationType { recolor, transform, generate };
enum class Shape { none, loafers, shirt, skirt };

struct Operation {
    OperationType type{OperationType::recolor};
    Shape shape{Shape::none};
    mmd::Float4 color{0.1F, 0.1F, 0.1F, 1.0F};
    mmd::Float3 translation{};
    mmd::Float3 scale{1.0F, 1.0F, 1.0F};
    mmd::Float3 rotation{}; // Degrees, XYZ, about the captured selection center.
    bool replace{true};
    int detail{24};
};

struct EditPlan {
    std::string summary;
    std::vector<Operation> operations;
};

[[nodiscard]] nlohmann::json planSchema();
[[nodiscard]] EditPlan parsePlan(std::string_view json);
[[nodiscard]] std::string plannerInstructions();

} // namespace pmxer::ai
