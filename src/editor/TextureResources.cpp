#include "TextureResources.hpp"
#include "platform/ResourceLocator.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>
namespace pmxer {
namespace {
std::string utf8(const std::filesystem::path &path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
std::filesystem::path sourceTexture(const mmd::PmxModel &model, std::size_t index) {
    auto stored = model.textures.at(index).storedPath;
    std::replace(stored.begin(), stored.end(), '\\', '/');
    const auto path = model.sourcePath.parent_path() /
                      std::filesystem::path(std::u8string(stored.begin(), stored.end()));
    return std::filesystem::absolute(resolveFileCase(path)).lexically_normal();
}
std::filesystem::path relativeOrAbsolute(const std::filesystem::path &source,
                                         const std::filesystem::path &root) {
    // resolveTexturePath lexically normalizes before filesystem traversal. A
    // '..' reference through a symlinked output parent would change its meaning.
    if (std::filesystem::weakly_canonical(root) != root.lexically_normal())
        return source;
    std::error_code error;
    const auto relative = std::filesystem::relative(source, root, error);
    return error || relative.empty() ? source : relative;
}
void copyTexture(const std::filesystem::path &source, const std::filesystem::path &destination) {
    // Create ordinary writable output files so failed publication can clean up
    // the private directory even when the source has read-only attributes.
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary);
    if (!input || !output)
        throw std::runtime_error("Cannot copy texture: " + utf8(source));
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        output.write(buffer.data(), input.gcount());
    }
    output.close();
    if (!input.eof() || !output)
        throw std::runtime_error("Cannot copy texture: " + utf8(source));
}
std::filesystem::path newDirectory(const std::filesystem::path &root) {
    static std::atomic_uint64_t sequence{};
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path = root / ("pmxer-resources-" + std::to_string(stamp) + "-" +
                                  std::to_string(sequence.fetch_add(1)));
        if (std::filesystem::create_directory(path))
            return path;
    }
    throw std::runtime_error("Cannot create a unique resource directory");
}
} // namespace
mmd::PmxModel rebaseTexturePaths(const mmd::PmxModel &model,
                                 const std::filesystem::path &destination) {
    auto result = model;
    const auto root = std::filesystem::absolute(destination).parent_path();
    const auto parent = model.sourcePath.parent_path();
    const auto originalRoot =
        std::filesystem::absolute(parent.empty() ? std::filesystem::path(".") : parent)
            .lexically_normal();
    if (std::filesystem::weakly_canonical(root) != std::filesystem::weakly_canonical(originalRoot))
        for (std::size_t i = 0; i < result.textures.size(); ++i)
            if (!result.textures[i].storedPath.empty())
                result.textures[i].storedPath =
                    utf8(relativeOrAbsolute(sourceTexture(model, i), root));
    result.sourcePath = destination;
    return result;
}
SaveResourceSnapshot::~SaveResourceSnapshot() {
    if (!published && !directory.empty()) {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
}
std::unique_ptr<SaveResourceSnapshot> prepareSaveResources(const mmd::PmxModel &model,
                                                           const std::filesystem::path &destination,
                                                           bool copyResources) {
    auto snapshot = std::make_unique<SaveResourceSnapshot>();
    snapshot->model = rebaseTexturePaths(model, destination);
    std::map<std::filesystem::path, std::filesystem::path> copied;
    const auto root = std::filesystem::absolute(destination).parent_path();
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        if (model.textures[i].storedPath.empty())
            continue;
        const auto source = sourceTexture(model, i);
        const bool exists = std::filesystem::is_regular_file(source);
        auto target = source;
        if (copyResources) {
            if (!exists)
                throw std::runtime_error("Cannot copy missing texture: " + utf8(source));
            const auto canonical = std::filesystem::canonical(source);
            if (!copied.contains(canonical)) {
                if (snapshot->directory.empty())
                    snapshot->directory = newDirectory(root);
                auto extension = source.extension().generic_u8string();
                if (extension.size() > 16 || extension.empty() ||
                    !std::all_of(extension.begin(), extension.end(), [](char8_t c) {
                        return c == u8'.' || (c >= u8'a' && c <= u8'z') ||
                               (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9');
                    }))
                    extension = u8".bin";
                const auto file =
                    snapshot->directory / ("texture-" + std::to_string(i) +
                                           std::string(extension.begin(), extension.end()));
                copyTexture(source, file);
                copied[canonical] = file;
            }
            target = copied.at(canonical);
            snapshot->model.textures[i].storedPath = utf8(target.lexically_relative(root));
        }
        snapshot->resources.push_back({i, source, target, exists, copyResources});
    }
    return snapshot;
}
} // namespace pmxer
