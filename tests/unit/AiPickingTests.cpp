#include "editor/DocumentSession.hpp"
#include "ui/ViewportPicking.hpp"
#include <cstdio>
#include <stdexcept>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        mmd::PmxModel model;
        model.metadata.version = 2.1F;
        mmd::PmxBone bone;
        bone.name = "root";
        model.bones.push_back(bone);
        for (float z : {2.0F, 0.0F})
            for (auto p : std::array<mmd::Float3, 3>{{{-3, -3, z}, {3, -3, z}, {0, 3, z}}}) {
                mmd::PmxVertex v;
                v.position = p;
                v.bones[0] = 0;
                model.vertices.push_back(v);
            }
        model.indices = {0, 1, 2, 3, 4, 5};
        mmd::PmxMaterial material;
        material.name = "mesh";
        material.indexCount = 6;
        model.materials.push_back(material);
        pmxer::DocumentSession session(model);
        session.ui.selectionMode = pmxer::ViewportSelectionMode::face;
        const pmxer::CameraState camera{{0, 0, 0}, 0, 0, 10, false};
        const std::vector<ImVec2> polygon{{250, 250}, {262, 250}, {262, 262}, {250, 262}};
        const auto all = pmxer::pickViewportRegion(session, model.vertices, camera, {0, 0},
                                                   {512, 512}, polygon, true);
        check(all.size() == 2, "Region inside a large triangle must intersect both faces");
        const auto front = pmxer::pickViewportRegion(session, model.vertices, camera, {0, 0},
                                                     {512, 512}, polygon, false);
        check(front.size() == 1 && front[0].face == 0,
              "Visible-only selection included the occluded face");
        check(pmxer::pickViewportRegion(session, model.vertices, camera, {0, 0}, {512, 512},
                                        {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, true)
                  .empty(),
              "Outside region selected a face");
        check(
            pmxer::pickViewportRegion(session, model.vertices, camera, {0, 0}, {512, 512}, {}, true)
                .empty(),
            "Empty polygon selected faces");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
