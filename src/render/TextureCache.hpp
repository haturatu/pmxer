#pragma once

#include <mmd/pmx.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace pmxer {

struct TextureStatus {
    std::filesystem::path stored;
    std::filesystem::path resolved;
    bool exists{};
};

class TextureCache {
  public:
    void inspect(const mmd::PmxModel &model);
    [[nodiscard]] const std::unordered_map<std::string, TextureStatus> &items() const noexcept;
    void clear() noexcept;

  private:
    std::unordered_map<std::string, TextureStatus> items_;
};

} // namespace pmxer

