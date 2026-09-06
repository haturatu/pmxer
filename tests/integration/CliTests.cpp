#include <mmd/pmx.hpp>

#include <cassert>

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
    return 0;
}
