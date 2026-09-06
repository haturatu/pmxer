#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <vector>

namespace pmxer {

struct SdefReport {
    std::size_t converted{};
    std::size_t rejected{};
};

[[nodiscard]] SdefReport convertBdef2ToSdef(DocumentSession &, const std::vector<mmd::VertexHandle> &);
[[nodiscard]] SdefReport convertSdefToBdef2(DocumentSession &, const std::vector<mmd::VertexHandle> &);
[[nodiscard]] bool mirrorSdef(DocumentSession &, const std::vector<mmd::VertexHandle> &);
[[nodiscard]] bool hasSuspiciousSdef(const mmd::PmxVertex &vertex) noexcept;

} // namespace pmxer

