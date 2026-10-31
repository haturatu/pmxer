#pragma once
#include <filesystem>
#include <mmd/pmx.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace pmxer {
struct SoftwareRenderOptions {
    std::filesystem::path output, motion;
    std::string views{"front,back,left,right"}, overlay;
    std::vector<std::string> morphs;
    int width{1024}, height{1024};
    float frame{};
};
nlohmann::json renderSoftwareViews(const mmd::PmxModel &, const SoftwareRenderOptions &);
} // namespace pmxer
