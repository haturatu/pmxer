#include "WorkspaceLayout.hpp"

#include "../platform/AtomicFile.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace pmxer {
namespace {

constexpr std::string_view headerPrefix = "PMXER_WORKSPACE_LAYOUT ";

} // namespace

WorkspaceLayoutLoadResult loadWorkspaceLayout(const std::filesystem::path &path,
                                              WorkspaceLayout &layout) {
    layout = {};
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return WorkspaceLayoutLoadResult::missing;
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (contents.empty())
        return WorkspaceLayoutLoadResult::corrupt;
    if (!contents.starts_with(headerPrefix))
        return WorkspaceLayoutLoadResult::legacy;

    const auto lineEnd = contents.find('\n');
    if (lineEnd == std::string::npos)
        return WorkspaceLayoutLoadResult::corrupt;
    auto versionText = std::string_view(contents).substr(
        headerPrefix.size(), lineEnd - headerPrefix.size());
    if (!versionText.empty() && versionText.back() == '\r')
        versionText.remove_suffix(1U);
    std::uint32_t version{};
    const auto parsed = std::from_chars(versionText.data(),
                                        versionText.data() + versionText.size(),
                                        version);
    if (parsed.ec != std::errc{} || parsed.ptr != versionText.data() + versionText.size())
        return WorkspaceLayoutLoadResult::corrupt;
    if (version != kWorkspaceLayoutVersion)
        return WorkspaceLayoutLoadResult::unsupportedVersion;
    const auto ini = std::string_view(contents).substr(lineEnd + 1U);
    if (ini.empty() || (ini.find("[Window]") == std::string_view::npos &&
                        ini.find("[Docking]") == std::string_view::npos))
        return WorkspaceLayoutLoadResult::corrupt;
    layout.version = version;
    layout.imguiIni.assign(ini);
    return WorkspaceLayoutLoadResult::loaded;
}

bool saveWorkspaceLayout(const std::filesystem::path &path,
                         std::string_view imguiIni) {
    if (imguiIni.empty())
        return false;
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;
    }
    const auto temporary = temporarySibling(path);
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    stream << "PMXER_WORKSPACE_LAYOUT " << kWorkspaceLayoutVersion << '\n';
    stream.write(imguiIni.data(), static_cast<std::streamsize>(imguiIni.size()));
    stream.close();
    if (!stream || !atomicReplace(temporary, path)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

bool backupWorkspaceLayout(const std::filesystem::path &path) {
    std::error_code error;
    const auto backup = std::filesystem::path(path.string() + ".bak");
    std::filesystem::copy_file(path, backup,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    return !error;
}

} // namespace pmxer
