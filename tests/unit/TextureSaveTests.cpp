#include "editor/DiffController.hpp"
#include "editor/EditorOperations.hpp"
#include "editor/RecoveryController.hpp"
#include "editor/SaveController.hpp"
#include "platform/FileDigest.hpp"
#include "platform/ResourceLocator.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace pmxer;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void file(const fs::path &path, const std::string &contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
    check(bool(out), "Cannot create texture fixture");
}
mmd::PmxModel model() {
    mmd::PmxModel m;
    m.metadata.version = 2.1F;
    for (auto position : std::array<mmd::Float3, 3>{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}) {
        mmd::PmxVertex v;
        v.position = position;
        v.normal = {0, 0, 1};
        m.vertices.push_back(v);
    }
    m.indices = {0, 1, 2};
    mmd::PmxMaterial mat;
    mat.name = "material";
    mat.diffuse = {1, 1, 1, 1};
    mat.indexCount = 3;
    mat.textureIndex = 0;
    mat.sphereTextureIndex = 1;
    mat.sphereMode = 1;
    mat.toonMode = 0;
    mat.toonTextureIndex = 2;
    m.materials.push_back(mat);
    m.textures = {{"textures/body.png"},
                  {"textures/sphere.sph"},
                  {"../shared/toon.bmp"},
                  {"textures\\body.png"}};
    return m;
}
std::size_t entries(const fs::path &dir) {
    return static_cast<std::size_t>(
        std::distance(fs::directory_iterator(dir), fs::directory_iterator{}));
}
int main() {
    fs::path root, recoverySource;
    try {
        root = fs::temp_directory_path() /
               ("pmxer texture saves " +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto source = root / "models" / "miku" / "model.pmx",
                   output = root / "output" / "edited.pmx", again = root / "other" / "again.pmx",
                   bundle = root / "bundle" / "portable.pmx";
        file(source.parent_path() / "textures" / "body.png", "original diffuse payload");
        file(source.parent_path() / "textures" / "alt.png", "alternate diffuse payload");
        file(source.parent_path() / "textures" / "sphere.sph", "sphere payload");
        file(root / "models" / "shared" / "toon.bmp", "toon payload");
        auto original = model();
        original.sourcePath = source;
        original.textures.push_back({(root / "models" / "shared" / "toon.bmp").generic_string()});
        (void)mmd::pmx::save(source, original);
        original = mmd::pmx::load(source);
        const auto inputHash = sha256File(source);
        DocumentSession session(original, source);
        const auto texture = session.document.textureHandle(0);
        check(editTexture(session, texture, {"textures/alt.png"}).success, "Texture edit failed");
        fs::create_directories(output.parent_path());
        fs::create_directories(again.parent_path());
        fs::create_directories(bundle.parent_path());
        SaveOptions options;
        options.quietSuccess = true;
        options.preserveRecoveryFiles = true;
        auto saved = saveDocument(session, output, options);
        check(saved.success, "Save to another folder failed");
        check(compareWithBaseline(session).equal(), "Save introduced a baseline diff");
        auto written = mmd::pmx::load(output);
        check(written.textures[0].storedPath != "textures/alt.png",
              "Relative path was not rebased");
        for (std::size_t i = 0; i < written.textures.size(); ++i)
            check(fs::equivalent(
                      resolveFileCase(mmd::pmx::resolveTexturePath(session.document.model(), i)),
                      mmd::pmx::resolveTexturePath(written, i)),
                  "Relocated PMX points at a different texture");
        check(session.document.model().sourcePath == source && session.path == output,
              "Live resource origin changed with save path");
        check(session.undo(), "Undo after Save As failed");
        check(fs::equivalent(mmd::pmx::resolveTexturePath(session.document.model(), 0),
                             source.parent_path() / "textures" / "body.png"),
              "Undo changed texture origin");
        recoverySource = output;
        check(writeRecovery(session).success, "Recovery save failed");
        const auto recovery = loadRecovery(output);
        check(recovery.has_value(), "Recovery load failed");
        check(fs::equivalent(mmd::pmx::resolveTexturePath(*recovery, 0),
                             source.parent_path() / "textures" / "body.png"),
              "Recovery changed texture origin");
        (void)discardRecovery(output);
        recoverySource.clear();
        check(session.redo(), "Redo after Save As failed");
        check(saveDocument(session, again, options).success, "Repeated Save As failed");
        check(fs::equivalent(mmd::pmx::resolveTexturePath(mmd::pmx::load(again), 0),
                             source.parent_path() / "textures" / "alt.png"),
              "Repeated save rebased against the wrong origin");
        {
            const auto alias = root / "output-alias";
            std::error_code error;
            fs::create_directory_symlink(output.parent_path(), alias, error);
            if (!error) {
                DocumentSession linked(original, source);
                const auto linkedOutput = alias / "linked.pmx";
                check(saveDocument(linked, linkedOutput, options).success,
                      "Save through symlinked output directory failed");
                const auto loaded = mmd::pmx::load(linkedOutput);
                check(fs::equivalent(mmd::pmx::resolveTexturePath(loaded, 0),
                                     source.parent_path() / "textures" / "body.png"),
                      "Symlinked output changed texture resolution");
            }
        }
        DocumentSession portable(original, source);
        options.copyResources = true;
        options.failIfDestinationExists = true;
        const auto copied = saveDocument(portable, bundle, options);
        check(copied.success, "Resource copy save failed");
        const auto packaged = mmd::pmx::load(bundle);
        std::vector<std::string> hashes;
        for (std::size_t i = 0; i < packaged.textures.size(); ++i) {
            const auto target = mmd::pmx::resolveTexturePath(packaged, i);
            const auto relative = target.lexically_relative(bundle.parent_path());
            check(fs::is_regular_file(target) && !relative.empty() && *relative.begin() != "..",
                  "Copied texture is outside the bundle");
            hashes.push_back(sha256File(target));
            check(hashes.back() ==
                      sha256File(resolveFileCase(mmd::pmx::resolveTexturePath(original, i))),
                  "Texture copy changed file contents");
        }
        check(packaged.textures[0].storedPath == packaged.textures[3].storedPath,
              "Duplicate texture was copied twice");
        check(copied.resources.size() == original.textures.size() && copied.resources[0].copied,
              "Resource report is missing");
        const auto before = entries(bundle.parent_path());
        auto missing = original;
        missing.textures.push_back({"missing.png"});
        DocumentSession broken(missing, source);
        const auto failed = saveDocument(broken, bundle.parent_path() / "failed.pmx", options);
        check(!failed.success && entries(bundle.parent_path()) == before,
              "Failed copy left partial resources or output");
        options.failIfDestinationExists = false;
        options.expectedDestinationSha256 = std::string(64, '0');
        const auto stale = saveDocument(portable, bundle, options);
        check(!stale.success && stale.destinationChanged && entries(bundle.parent_path()) == before,
              "Failed output precondition left copied resources");
        check(sha256File(source) == inputHash, "Source model was modified");
        fs::remove_all(root / "models");
        for (std::size_t i = 0; i < packaged.textures.size(); ++i)
            check(sha256File(mmd::pmx::resolveTexturePath(packaged, i)) == hashes[i],
                  "Portable bundle still depends on source files");
        fs::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (!recoverySource.empty())
            (void)discardRecovery(recoverySource);
        if (!root.empty())
            fs::remove_all(root);
        return 1;
    }
}
