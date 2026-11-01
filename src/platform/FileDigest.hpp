#pragma once
#include <filesystem>
#include <string>
namespace pmxer {
std::string sha256File(const std::filesystem::path &);
}
