#include "SoftwareRenderer.hpp"
#include "platform/AtomicFile.hpp"
#include "preview/PreviewController.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
namespace pmxer {
namespace {
using Bytes = std::vector<unsigned char>;
void word(Bytes &v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(static_cast<unsigned char>(x >> s));
}
void chunk(Bytes &png, const char *type, const Bytes &data) {
    word(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (int i = 0; i < 4; ++i)
        png.push_back(static_cast<unsigned char>(type[i]));
    png.insert(png.end(), data.begin(), data.end());
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    word(png, ~crc);
}
void savePng(const std::filesystem::path &path, int w, int h, const Bytes &rgb) {
    Bytes raw;
    raw.reserve(rgb.size() + static_cast<std::size_t>(h));
    const auto stride = static_cast<std::size_t>(w) * 3;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const auto start = static_cast<std::size_t>(y) * stride;
        raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(start),
                   rgb.begin() + static_cast<std::ptrdiff_t>(start + stride));
    }
    Bytes z{0x78, 0x01};
    for (std::size_t i = 0; i < raw.size();) {
        const auto n = std::min<std::size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size() ? 1 : 0);
        const auto len = static_cast<std::uint16_t>(n), inv = static_cast<std::uint16_t>(~len);
        z.push_back(static_cast<unsigned char>(len));
        z.push_back(static_cast<unsigned char>(len >> 8));
        z.push_back(static_cast<unsigned char>(inv));
        z.push_back(static_cast<unsigned char>(inv >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(i),
                 raw.begin() + static_cast<std::ptrdiff_t>(i + n));
        i += n;
    }
    std::uint32_t a = 1, b = 0;
    for (auto v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    word(z, (b << 16) | a);
    Bytes png{137, 80, 78, 71, 13, 10, 26, 10}, header;
    word(header, static_cast<std::uint32_t>(w));
    word(header, static_cast<std::uint32_t>(h));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    const auto temporary = temporarySibling(path);
    try {
        std::ofstream file(temporary, std::ios::binary);
        file.write(reinterpret_cast<const char *>(png.data()),
                   static_cast<std::streamsize>(png.size()));
        file.close();
        if (!file || !atomicReplace(temporary, path))
            throw std::runtime_error("Cannot write PNG: " + path.string());
    } catch (...) {
        std::filesystem::remove(temporary);
        throw;
    }
}
} // namespace
nlohmann::json renderSoftwareViews(const mmd::PmxModel &model, const SoftwareRenderOptions &c) {
    if (c.width < 16 || c.height < 16 || c.width > 4096 || c.height > 4096)
        throw std::runtime_error("Render dimensions must be 16..4096");
    if (!mmd::pmx::validate(model).valid())
        throw std::runtime_error("Cannot render an invalid model");
    std::vector<std::string> views;
    std::stringstream stream(c.views);
    std::string view;
    while (std::getline(stream, view, ',')) {
        if (view != "front" && view != "back" && view != "left" && view != "right")
            throw std::runtime_error("Unknown view: " + view);
        if (std::find(views.begin(), views.end(), view) != views.end())
            throw std::runtime_error("Duplicate view");
        views.push_back(view);
    }
    if (views.empty())
        throw std::runtime_error("At least one view is required");
    std::int32_t weightBone = -1;
    if (c.overlay.starts_with("weights:")) {
        const auto name = c.overlay.substr(8);
        for (std::size_t i = 0; i < model.bones.size(); ++i)
            if (model.bones[i].name == name) {
                if (weightBone >= 0)
                    throw std::runtime_error("Ambiguous overlay bone");
                weightBone = static_cast<std::int32_t>(i);
            }
        if (weightBone < 0)
            throw std::runtime_error("Unknown overlay bone");
    } else if (!c.overlay.empty() && c.overlay != "wireframe" && c.overlay != "bones")
        throw std::runtime_error("Unknown overlay");
    PreviewController preview(model);
    preview.setPhysicsEnabled(false);
    std::optional<mmd::VmdMotion> motion;
    if (!c.motion.empty()) {
        motion = mmd::vmd::load(c.motion);
        preview.setMotion(&*motion);
    }
    for (const auto &setting : c.morphs) {
        const auto eq = setting.rfind('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Morph syntax: NAME=WEIGHT");
        const auto name = setting.substr(0, eq);
        std::size_t count = 0;
        for (const auto &m : model.morphs)
            if (m.name == name)
                ++count;
        if (count != 1)
            throw std::runtime_error("Unknown or ambiguous morph");
        std::size_t used = 0;
        const auto value = std::stof(setting.substr(eq + 1), &used);
        if (used != setting.size() - eq - 1 || !std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Morph weight must be 0..1");
        preview.setMorphPreview(name, value);
    }
    preview.setFrame(c.frame);
    const auto frame = preview.evaluate();
    const auto &vertices = frame.vertices;
    if (vertices.empty())
        throw std::runtime_error("Model has no vertices");
    mmd::Float3 low = vertices.front().position, high = low;
    for (const auto &v : vertices)
        for (std::size_t a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], v.position[a]);
            high[a] = std::max(high[a], v.position[a]);
        }
    const double extent =
        std::max({static_cast<double>(high[0]) - low[0], static_cast<double>(high[1]) - low[1],
                  static_cast<double>(high[2]) - low[2], 0.001});
    const double scale = 0.9 * std::min(c.width, c.height) / extent;
    std::filesystem::create_directories(c.output);
    nlohmann::json files = nlohmann::json::array();
    for (const auto &name : views) {
        const bool side = name == "left" || name == "right";
        const auto axis = side ? 2U : 0U;
        const float sign = (name == "back" || name == "right") ? -1.0F : 1.0F;
        auto project = [&](const mmd::Float3 &p) {
            return mmd::Float3{
                static_cast<float>(c.width * 0.5 +
                                   sign *
                                       (static_cast<double>(p[axis]) -
                                        (static_cast<double>(low[axis]) + high[axis]) * 0.5) *
                                       scale),
                static_cast<float>(
                    c.height * 0.5 -
                    (static_cast<double>(p[1]) - (static_cast<double>(low[1]) + high[1]) * 0.5) *
                        scale),
                (side ? sign : -sign) * p[side ? 0 : 2]};
        };
        const auto pixels = static_cast<std::size_t>(c.width) * static_cast<std::size_t>(c.height);
        Bytes rgb(pixels * 3, 238);
        std::vector<float> depth(pixels, -std::numeric_limits<float>::infinity());
        auto paint = [&](int x, int y, const mmd::Float3 &color) {
            if (x < 0 || y < 0 || x >= c.width || y >= c.height)
                return;
            const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c.width) +
                            static_cast<std::size_t>(x)) *
                           3;
            for (std::size_t a = 0; a < 3; ++a)
                rgb[i + a] = static_cast<unsigned char>(std::clamp(color[a], 0.0F, 1.0F) * 255);
        };
        auto line = [&](mmd::Float3 a, mmd::Float3 b, mmd::Float3 color) {
            for (std::size_t axisIndex = 0; axisIndex < 2; ++axisIndex) {
                const float bound = static_cast<float>(axisIndex == 0 ? c.width : c.height);
                a[axisIndex] = std::clamp(a[axisIndex], -bound, bound * 2);
                b[axisIndex] = std::clamp(b[axisIndex], -bound, bound * 2);
            }
            const auto steps =
                static_cast<int>(std::ceil(std::max(std::abs(b[0] - a[0]), std::abs(b[1] - a[1]))));
            for (int i = 0; i <= steps; ++i) {
                const auto t = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0.0F;
                paint(static_cast<int>(a[0] + t * (b[0] - a[0])),
                      static_cast<int>(a[1] + t * (b[1] - a[1])), color);
            }
        };
        std::size_t start = 0;
        for (const auto &material : model.materials) {
            const auto end = start + material.indexCount;
            if (material.diffuse[3] <= 0) {
                start = end;
                continue;
            }
            for (auto f = start; f + 2 < end; f += 3) {
                std::array<mmd::Float3, 3> p;
                std::array<float, 3> weights{};
                mmd::Float3 normal{};
                for (std::size_t j = 0; j < 3; ++j) {
                    const auto &v = vertices.at(model.indices.at(f + j));
                    p[j] = project(v.position);
                    for (std::size_t axisIndex = 0; axisIndex < 3; ++axisIndex)
                        normal[axisIndex] += v.normal[axisIndex];
                    if (weightBone >= 0)
                        for (std::size_t a = 0; a < 4; ++a)
                            if (v.bones[a] == weightBone)
                                weights[j] += v.weights[a];
                }
                const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                                              normal[2] * normal[2]);
                const float shade =
                    length > 1e-8F
                        ? 0.4F + 0.6F * std::max(0.0F, (normal[0] * 0.4F + normal[1] * 0.7F +
                                                        normal[2] * 0.6F) /
                                                           (length * 1.005F))
                        : 1.0F;
                auto edge = [](const mmd::Float3 &a, const mmd::Float3 &b, float x, float y) {
                    return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
                };
                const auto area = edge(p[0], p[1], p[2][0], p[2][1]);
                if (std::abs(area) < 1e-6F)
                    continue;
                const int xmin = std::max(0, static_cast<int>(std::floor(
                                                 std::min({p[0][0], p[1][0], p[2][0]})))),
                          xmax = std::min(c.width - 1, static_cast<int>(std::ceil(
                                                           std::max({p[0][0], p[1][0], p[2][0]}))));
                const int ymin = std::max(0, static_cast<int>(std::floor(
                                                 std::min({p[0][1], p[1][1], p[2][1]})))),
                          ymax = std::min(
                              c.height - 1,
                              static_cast<int>(std::ceil(std::max({p[0][1], p[1][1], p[2][1]}))));
                for (int y = ymin; y <= ymax; ++y)
                    for (int x = xmin; x <= xmax; ++x) {
                        const float px = static_cast<float>(x) + 0.5F,
                                    py = static_cast<float>(y) + 0.5F;
                        const float a = edge(p[1], p[2], px, py) / area,
                                    b = edge(p[2], p[0], px, py) / area, d = 1 - a - b;
                        if (a < 0 || b < 0 || d < 0)
                            continue;
                        const float z = a * p[0][2] + b * p[1][2] + d * p[2][2];
                        const auto index =
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(c.width) +
                            static_cast<std::size_t>(x);
                        if (z < depth[index])
                            continue;
                        depth[index] = z;
                        mmd::Float3 color{material.diffuse[0] * shade, material.diffuse[1] * shade,
                                          material.diffuse[2] * shade};
                        if (weightBone >= 0) {
                            const auto w = std::clamp(
                                a * weights[0] + b * weights[1] + d * weights[2], 0.0F, 1.0F);
                            color = {w, 0.2F, 1 - w};
                        }
                        if (c.overlay == "wireframe" && std::min({a, b, d}) < 0.025F)
                            color = {0.08F, 0.08F, 0.08F};
                        paint(x, y, color);
                    }
            }
            start = end;
        }
        if (c.overlay == "bones")
            for (const auto &bone : model.bones)
                if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < model.bones.size())
                    line(project(bone.position),
                         project(model.bones[static_cast<std::size_t>(bone.parent)].position),
                         {1, 0.2F, 0.1F});
        const auto path = c.output / (name + ".png");
        savePng(path, c.width, c.height, rgb);
        files.push_back(path.string());
    }
    return {{"backend", "software"},
            {"width", c.width},
            {"height", c.height},
            {"files", files},
            {"limitations",
             {"diffuse colors only; textures and physics simulation are not rendered",
              "bone overlay shows rest positions"}}};
}
} // namespace pmxer
