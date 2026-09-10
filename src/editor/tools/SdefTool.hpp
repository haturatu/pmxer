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
// Converts only SDEF vertices whose C/R parameters are non-finite.  BDEF2
// retains the two bone influences while removing the corrupt SDEF payload.
[[nodiscard]] SdefReport repairSuspiciousSdef(DocumentSession &, const std::vector<mmd::VertexHandle> &);
[[nodiscard]] bool mirrorSdef(DocumentSession &, const std::vector<mmd::VertexHandle> &);
[[nodiscard]] bool hasSuspiciousSdef(const mmd::PmxVertex &vertex) noexcept;

} // namespace pmxer
