#pragma once
#include <mmd/pmx.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace pmxer::editing {
struct Query {
    std::string kind{"materials"}, name{"*"}, material, bone;
    std::optional<mmd::Float3> minimum, maximum;
    std::size_t offset{}, limit{100};
    bool includeBones{}, includeWeights{};
};
bool globMatch(std::string_view pattern, std::string_view value);
std::vector<std::size_t> findComponents(const mmd::PmxModel &, const Query &);
nlohmann::json inspect(const mmd::PmxModel &, const Query &);
std::vector<std::size_t> materialFaces(const mmd::PmxModel &, const std::vector<std::size_t> &);
} // namespace pmxer::editing
