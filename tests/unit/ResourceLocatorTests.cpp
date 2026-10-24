#include "platform/ResourceLocator.hpp"
#include "render/TextureCache.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("pmxer-resource-case-" + std::to_string(
                                     std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace
int main() {
    try {
        namespace fs = std::filesystem;
        using namespace pmxer;
        TemporaryDirectory temporary;
        const auto root = temporary.path;
        fs::create_directories(root / "Spa");
        const auto asset = root / "Spa" / "A3.bmp";
        std::ofstream(asset) << "image";
        const auto requested = root / "spa" / "a3.bmp";
        const auto resolved = resolveFileCase(requested);
        require(fs::equivalent(resolved, asset), "Mixed-case model resource was not resolved");
        require(resolveFileCase(asset) == asset, "Exact resource path was changed");
        const auto missing = root / "spa" / "missing.bmp";
        require(resolveFileCase(missing) == missing && resolveFileCase({}).empty(),
                "Missing resource path was redirected");
        mmd::PmxModel model;
        model.sourcePath = root / "model.pmx";
        model.textures.push_back({"spa\\a3.bmp"});
        TextureCache cache;
        cache.inspect(model);
        require(cache.items().at("spa\\a3.bmp").exists &&
                    fs::equivalent(cache.items().at("spa\\a3.bmp").resolved, asset) &&
                    model.textures[0].storedPath == "spa\\a3.bmp",
                "Texture cache missed the resource or modified the stored PMX path");
        std::ofstream(root / "Spa" / "a3.bmp") << "other";
        if (!fs::equivalent(root / "Spa" / "a3.bmp", asset)) {
            const auto ambiguous = root / "Spa" / "a3.BMP";
            require(resolveFileCase(ambiguous) == ambiguous,
                    "Ambiguous case-insensitive resource was selected arbitrarily");
            require(resolveFileCase(asset) == asset, "Exact path lost precedence over ambiguity");
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
