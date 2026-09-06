#include <mmd/pmx.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef PMXER_CLI_PATH
#define PMXER_CLI_PATH "pmxer-cli"
#endif

namespace {

std::string quote(const std::filesystem::path &path) {
    return "\"" + path.string() + "\"";
}

std::string quote(const char *path) {
    return "\"" + std::string(path) + "\"";
}

mmd::PmxModel sampleModel() {
    mmd::PmxModel model;
    model.metadata.version = 2.1F;
    model.metadata.modelName = "cli-test";
    mmd::PmxMaterial material;
    material.name = "material";
    material.indexCount = 3;
    model.materials.push_back(material);
    for (const auto position : {mmd::Float3{0.0F, 0.0F, 0.0F}, mmd::Float3{1.0F, 0.0F, 0.0F},
                                mmd::Float3{0.0F, 1.0F, 0.0F}}) {
        mmd::PmxVertex vertex;
        vertex.position = position;
        model.vertices.push_back(vertex);
    }
    model.indices = {0, 1, 2};
    return model;
}

} // namespace

int main() {
    mmd::PmxModel model;
    model.metadata.version = 2.0F;
    mmd::PmxVertex vertex;
    vertex.weightType = mmd::PmxWeightType::qdef;
    model.vertices.push_back(vertex);
    assert(!mmd::pmx::validate(model).valid());
    model.metadata.version = 2.1F;
    model.vertices.clear();
    model.materials.clear();
    const auto result = mmd::pmx::validate(model);
    assert(result.valid());

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto input = std::filesystem::temp_directory_path() / ("pmxer-cli-" + std::to_string(suffix) + ".pmx");
    const auto output = input.string() + ".json";
    const auto saved = mmd::pmx::save(input, sampleModel());
    (void)saved;
    const auto command = quote(PMXER_CLI_PATH) + " info --json " + quote(input) + " > " + quote(output);
    assert(std::system(command.c_str()) == 0);
    std::ifstream stream(output);
    std::stringstream contents;
    contents << stream.rdbuf();
    assert(contents.str().find("\"vertices\":3") != std::string::npos);
    std::filesystem::remove(input);
    std::filesystem::remove(output);
    return 0;
}
