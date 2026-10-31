#include "editing/EditPipeline.hpp"
#include "platform/FileDigest.hpp"
#include "platform/Paths.hpp"
#include "render/SoftwareRenderer.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mmd/pmx.hpp>
using Json = nlohmann::json;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
mmd::PmxModel fixture() {
    mmd::PmxModel m;
    m.metadata.version = 2.1F;
    mmd::PmxBone bone;
    bone.name = "左腕";
    bone.englishName = "LeftArm";
    m.bones.push_back(bone);
    for (int t = 0; t < 2; ++t) {
        mmd::PmxMaterial material;
        material.name = t ? "顔" : "スカート";
        material.diffuse = {0.2F, 0.4F, 0.8F, 1};
        material.indexCount = 3;
        m.materials.push_back(material);
        for (const auto &p : std::array<mmd::Float3, 3>{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}) {
            mmd::PmxVertex v;
            v.position = p;
            v.position[0] += static_cast<float>(t) * 2;
            v.normal = {0, 0, 1};
            v.bones[0] = 0;
            m.indices.push_back(static_cast<std::uint32_t>(m.vertices.size()));
            m.vertices.push_back(v);
        }
    }
    return m;
}
Json plan() {
    return {{"schema_version", 1},
            {"selection", {{"type", "material"}, {"names", {"スカート"}}, {"expected_count", 1}}},
            {"operations",
             Json::array({{{"type", "transform"}, {"scale", {1, 0.85, 1}}, {"pivot", "top-center"}},
                          {{"type", "smooth"}, {"strength", 0.2}}})}};
}
void rejected(pmxer::DocumentSession &s, const Json &p, const std::string &code) {
    const auto before = s.document.model();
    try {
        (void)pmxer::editing::prepareEdit(s, p);
        throw std::runtime_error("Invalid plan accepted");
    } catch (const pmxer::editing::PlanError &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + code + ", got " + e.code + ": " + e.what());
    }
    require(mmd::pmx::semanticCompare(before, s.document.model()).equal(),
            "Failure modified source");
}
// Decode the stored DEFLATE blocks emitted by the dependency-free PNG backend.
std::vector<unsigned char> pngPixels(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {}), result;
    auto word = [&](std::size_t i) {
        return (static_cast<std::size_t>(bytes.at(i)) << 24) |
               (static_cast<std::size_t>(bytes.at(i + 1)) << 16) |
               (static_cast<std::size_t>(bytes.at(i + 2)) << 8) | bytes.at(i + 3);
    };
    for (std::size_t i = 8; i + 12 <= bytes.size();) {
        const auto n = word(i);
        require(i + 12 + n <= bytes.size(), "Truncated PNG chunk");
        if (std::string(bytes.begin() + static_cast<std::ptrdiff_t>(i + 4),
                        bytes.begin() + static_cast<std::ptrdiff_t>(i + 8)) == "IDAT") {
            auto p = i + 10;
            bool last = false;
            while (!last) {
                const auto flag = bytes.at(p++);
                require((flag & 6) == 0, "Unexpected DEFLATE block");
                last = (flag & 1) != 0;
                const auto length = static_cast<std::size_t>(bytes.at(p)) |
                                    (static_cast<std::size_t>(bytes.at(p + 1)) << 8);
                p += 4;
                require(p + length <= i + 8 + n, "Truncated DEFLATE data");
                result.insert(result.end(), bytes.begin() + static_cast<std::ptrdiff_t>(p),
                              bytes.begin() + static_cast<std::ptrdiff_t>(p + length));
                p += length;
            }
        }
        i += n + 12;
    }
    return result;
}
int main() {
    std::filesystem::path dir, recovery;
    try {
        using namespace pmxer;
        DocumentSession s(fixture());
        const auto original = s.document.model();
        editing::Query q;
        q.name = "*スカート*";
        auto found = editing::inspect(original, q);
        require(found["total"] == 1, "Japanese glob query failed");
        q.name = "*";
        q.limit = 1;
        found = editing::inspect(original, q);
        require(found["next_offset"] == 1, "Pagination failed");
        q.offset = 999;
        require(editing::inspect(original, q)["items"].empty(), "Offset beyond end");
        q = {};
        q.kind = "vertices";
        q.material = "スカート";
        q.bone = "左腕";
        q.includeWeights = true;
        require(editing::inspect(original, q)["total"] == 3, "Combined vertex filters failed");
        q.bone = "LeftArm";
        require(editing::inspect(original, q)["total"] == 3, "English bone weight query failed");
        auto p = plan();
        auto edit = editing::prepareEdit(s, p);
        require(s.commands.undoCount() == 0, "Preview modified history");
        require(edit.report["moved_vertices"] == 2, "Preview displacement report");
        require(edit.preview->document.model().vertices[3] == original.vertices[3],
                "Unselected vertex changed");
        require(ai::acceptEditPreview(s, edit.region, *edit.preview).success, "Accept failed");
        require(s.commands.undoCount() == 1, "Batch did not create one undo entry");
        require(s.undo(), "Undo failed");
        require(mmd::pmx::semanticCompare(original, s.document.model()).equal(),
                "Undo did not restore model");
        p = plan();
        p["selection"]["names"] = {"*"};
        p["selection"]["match"] = "glob";
        rejected(s, p, "selection_count_mismatch");
        p = plan();
        p["preconditions"] = {{"revision", 999}};
        rejected(s, p, "stale_revision");
        p["preconditions"] = {{"sha256", std::string(64, '0')}};
        rejected(s, p, "stale_source");
        p = plan();
        p["operations"][1]["extra"] = true;
        rejected(s, p, "unknown_field");
        p = plan();
        p["operations"] = {{{"type", "subdivide"}}};
        require(editing::prepareEdit(s, p).report["topology_changed"] == true,
                "Guarded subdivision failed");
        p = plan();
        p["operations"][1].erase("strength");
        p["operations"][1]["type"] = "unsupported";
        rejected(s, p, "invalid_operation");
        p = plan();
        p["operations"] = {{{"type", "subdivide"}}};
        p["guards"] = {
            {"preserve_unselected", false}, {"preserve_uv", false}, {"preserve_weights", false}};
        require(editing::prepareEdit(s, p).preview->document.model().vertices.size() >
                    original.vertices.size(),
                "Topology edit unavailable");
        auto duplicates = fixture();
        duplicates.materials[1].name = duplicates.materials[0].name;
        DocumentSession duplicateSession(duplicates);
        rejected(duplicateSession, plan(), "selection_count_mismatch");
        p = plan();
        p["selection"].erase("names");
        p["selection"]["indices"] = {0};
        require(editing::prepareEdit(duplicateSession, p).report["selection"].size() == 1,
                "Cannot disambiguate duplicate names by index");
        auto shared = fixture();
        shared.indices[3] = 0;
        DocumentSession sharedSession(shared);
        require(editing::prepareEdit(sharedSession, plan()).report["valid"] == true,
                "Split seam did not preserve unselected geometry");
        dir = std::filesystem::temp_directory_path() /
              ("pmxer editing-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        const auto hashFile = dir / "hash.txt";
        {
            std::ofstream f(hashFile);
            f << "abc";
        }
        require(sha256File(hashFile) ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 known vector");
        {
            std::ofstream f(hashFile);
        }
        require(sha256File(hashFile) ==
                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                "Empty SHA-256 vector");
        {
            std::ofstream f(hashFile);
            f << std::string(1000000, 'a');
        }
        require(sha256File(hashFile) ==
                    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                "Multiblock SHA-256 vector");
        const auto input = dir / "input.pmx", output = dir / "output.pmx",
                   planFile = dir / "edit.json", report = dir / "report.json";
        (void)mmd::pmx::save(input, original);
        recovery = recoveryPath(input);
        std::filesystem::create_directories(recovery.parent_path());
        {
            std::ofstream f(recovery);
            f << "GUI recovery must survive batch editing";
        }
        {
            std::ofstream f(planFile);
            f << plan();
        }
        auto quote = [](const std::filesystem::path &path) { return "\"" + path.string() + "\""; };
        auto run = [&](std::string args) {
            auto command = quote(PMXER_BINARY_PATH) + " " + args + " > " + quote(report);
#ifdef _WIN32
            // cmd /C strips the outermost quotes. Protect the quoted executable
            // and redirected paths by quoting the complete command once more.
            command = "\"" + command + "\"";
#endif
            return std::system(command.c_str());
        };
        auto read = [&]() {
            std::ifstream f(report);
            return Json::parse(f);
        };
        require(run("apply " + quote(input) + " --plan " + quote(planFile) + " --json") != 0 &&
                    read()["error"]["code"] == "invalid_arguments",
                "Missing output did not return JSON arguments error");
        require(run("inspect " + quote(input) + " materials --limit -1 --json") != 0 &&
                    read()["error"]["code"] == "invalid_arguments",
                "Invalid numeric option did not return JSON");
        require(run("inspect " + quote(input) + " --unknown-option --json") != 0 &&
                    read()["error"]["code"] == "invalid_arguments",
                "Unknown option did not return JSON");
        require(run("apply --help --json") == 0 && read()["help"].is_string(),
                "JSON help was not a single object");
        require(run("--version --json") == 0 && read()["version"].is_string(),
                "JSON version was not a single object");
        require(run("inspect " + quote(input) + " materials --json --limit 1") == 0,
                "CLI inspect failed");
        const auto sha = read()["source_sha256"].get<std::string>();
        require(sha.size() == 64, "Missing SHA-256");
        auto filePlan = plan();
        filePlan["preconditions"] = {{"sha256", sha}};
        {
            std::ofstream f(planFile);
            f << filePlan;
        }
        require(run("apply " + quote(input) + " --plan " + quote(planFile) + " --output " +
                    quote(output) + " --dry-run --json") == 0,
                "CLI dry run failed");
        require(!std::filesystem::exists(output), "Dry run wrote output");
        require(read()["dry_run"] == true, "Dry run report");
        require(run("apply " + quote(input) + " --plan " + quote(planFile) + " --output " +
                    quote(output) + " --json") == 0,
                "CLI apply failed");
        require(read()["output"] == output.string(), "Apply stdout is not JSON");
        require(std::filesystem::exists(recovery), "Batch save deleted another session recovery");
        std::filesystem::remove(recovery);
        require(mmd::pmx::validate(mmd::pmx::load(output)).valid(), "Saved output invalid");
        require(mmd::pmx::semanticCompare(original, mmd::pmx::load(input)).equal(),
                "CLI overwrote source");
        const auto existingHash = sha256File(output);
        const auto apply =
            "apply " + quote(input) + " --plan " + quote(planFile) + " --output " + quote(output);
        require(run(apply + " --json") != 0 && read()["error"]["code"] == "output_exists",
                "Existing output was not refused");
        require(sha256File(output) == existingHash, "Refused output was modified");
        filePlan["preconditions"]["output_sha256"] = std::string(64, '0');
        {
            std::ofstream f(planFile);
            f << filePlan;
        }
        require(run(apply + " --overwrite --json") != 0 &&
                    read()["error"]["code"] == "stale_output",
                "Stale output precondition accepted");
        require(sha256File(output) == existingHash, "Stale output was modified");
        filePlan["preconditions"]["output_sha256"] = existingHash;
        filePlan["operations"][0]["scale"] = {1, 0.7, 1};
        {
            std::ofstream f(planFile);
            f << filePlan;
        }
        require(run(apply + " --overwrite --json") == 0,
                "Explicit overwrite with matching SHA failed");
        require(sha256File(output) != existingHash,
                "Explicit overwrite did not publish the new edit");
        require(run("apply " + quote(input) + " --plan " + quote(planFile) + " --output " +
                    quote(input) + " --json") != 0,
                "Source overwrite accepted");
        require(read()["error"]["code"] == "source_overwrite", "Overwrite error not structured");
#if PMXER_TEST_SOFTWARE_RENDERER
        require(run("render " + quote(output) + " --output-dir " + quote(dir / "views") +
                    " --width 64 --height 64 --json") == 0,
                "Headless render failed");
        require(read()["files"].size() == 4, "Missing views");
        for (const auto &name : {"front", "back", "left", "right"}) {
            std::ifstream f(dir / "views" / (std::string(name) + ".png"), std::ios::binary);
            unsigned char signature[8]{};
            f.read(reinterpret_cast<char *>(signature), 8);
            require(signature[0] == 137 && signature[1] == 80, "Invalid PNG");
        }
#endif
#if PMXER_TEST_SOFTWARE_RENDERER
        const auto pixels = pngPixels(dir / "views" / "front.png");
        require(pixels.size() == 64 * (64 * 3 + 1), "Incorrect PNG dimensions or scanlines");
        std::size_t colored = 0;
        for (std::size_t y = 0; y < 64; ++y) {
            require(pixels[y * 193] == 0, "Invalid PNG row filter");
            for (std::size_t x = 0; x < 64; ++x)
                if (pixels[y * 193 + 1 + x * 3] != 238)
                    ++colored;
        }
        require(colored > 100, "Software rendering produced an empty image");
#endif
        require(run("plan --schema --json") == 0 && read()["properties"].contains("guards"),
                "Schema export failed");
        std::filesystem::remove_all(dir);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (!dir.empty())
            std::filesystem::remove_all(dir);
        if (!recovery.empty())
            std::filesystem::remove(recovery);
        return 1;
    }
}
