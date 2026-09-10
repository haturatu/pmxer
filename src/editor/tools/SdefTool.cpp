#include "SdefTool.hpp"

#include "../EditorOperations.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace pmxer {
namespace {

std::optional<mmd::BoneHandle> mirroredBone(const mmd::PmxDocument &document,
                                            std::int32_t index) {
  if (index < 0 ||
      static_cast<std::size_t>(index) >= document.model().bones.size())
    return std::nullopt;
  auto name = document.model().bones[static_cast<std::size_t>(index)].name;
  const auto suffix =
      name.size() >= 2 ? name.substr(name.size() - 2) : std::string{};
  if (suffix == "_l")
    name.replace(name.size() - 1, 1, "r");
  else if (suffix == "_r")
    name.replace(name.size() - 1, 1, "l");
  else {
    const auto left = name.find("左");
    const auto right = name.find("右");
    if (left != std::string::npos)
      name.replace(left, std::string("左").size(), "右");
    else if (right != std::string::npos)
      name.replace(right, std::string("右").size(), "左");
    else
      return document.boneHandle(static_cast<std::size_t>(index));
  }
  for (std::size_t candidate = 0; candidate < document.model().bones.size();
       ++candidate)
    if (document.model().bones[candidate].name == name)
      return document.boneHandle(candidate);
  return document.boneHandle(static_cast<std::size_t>(index));
}

} // namespace

SdefReport convertBdef2ToSdef(DocumentSession &session,
                              const std::vector<mmd::VertexHandle> &handles) {
  SdefReport report;
  struct Edit {
    mmd::VertexHandle handle;
    mmd::PmxVertexSkin value;
  };
  std::vector<Edit> edits;
  for (const auto handle : handles) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr || source->weightType != mmd::PmxWeightType::bdef2) {
      ++report.rejected;
      continue;
    }
    mmd::PmxVertexSkin value;
    value.type = mmd::PmxWeightType::sdef;
    value.weights = source->weights;
    const auto &bones = session.document.model().bones;
    if (source->bones[0] < 0 || source->bones[1] < 0 ||
        static_cast<std::size_t>(source->bones[0]) >= bones.size() ||
        static_cast<std::size_t>(source->bones[1]) >= bones.size()) {
      ++report.rejected;
      continue;
    }
    const auto first =
        source->bones[0] >= 0 &&
                static_cast<std::size_t>(source->bones[0]) < bones.size()
            ? bones[static_cast<std::size_t>(source->bones[0])].position
            : mmd::Float3{};
    const auto second =
        source->bones[1] >= 0 &&
                static_cast<std::size_t>(source->bones[1]) < bones.size()
            ? bones[static_cast<std::size_t>(source->bones[1])].position
            : first;
    for (std::size_t i = 0; i < 2; ++i)
      value.bones[i] = session.document.boneHandle(
          static_cast<std::size_t>(source->bones[i]));
    for (std::size_t i = 0; i < 3; ++i) {
      value.sdefC[i] = (first[i] + second[i]) * 0.5F;
      value.sdefR0[i] = first[i];
      value.sdefR1[i] = second[i];
    }
    edits.push_back({handle, value});
  }
  const auto result = applyTransaction(
      session,
      [&](auto &transaction) {
        for (const auto &edit : edits)
          if (!transaction.setVertexSkin(edit.handle, edit.value))
            return false;
        return true;
      },
      "BDEF2からSDEFへ変換");
  if (result.success)
    report.converted = edits.size();
  else
    report.rejected += edits.size();
  return report;
}

SdefReport convertSdefToBdef2(DocumentSession &session,
                              const std::vector<mmd::VertexHandle> &handles) {
  SdefReport report;
  struct Edit {
    mmd::VertexHandle handle;
    mmd::PmxVertexSkin value;
  };
  std::vector<Edit> edits;
  for (const auto handle : handles) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef) {
      ++report.rejected;
      continue;
    }
    mmd::PmxVertexSkin value;
    value.type = mmd::PmxWeightType::bdef2;
    value.weights = source->weights;
    for (std::size_t i = 0; i < 2; ++i) {
      if (source->bones[i] < 0 || static_cast<std::size_t>(source->bones[i]) >=
                                      session.document.model().bones.size()) {
        ++report.rejected;
        value.bones[i] = {};
      } else {
        value.bones[i] = session.document.boneHandle(
            static_cast<std::size_t>(source->bones[i]));
      }
    }
    edits.push_back({handle, value});
  }
  const auto result = applyTransaction(
      session,
      [&](auto &transaction) {
        for (const auto &edit : edits)
          if (!transaction.setVertexSkin(edit.handle, edit.value))
            return false;
        return true;
      },
      "SDEFからBDEF2へ変換");
  if (result.success)
    report.converted = edits.size();
  else
    report.rejected += edits.size();
  return report;
}

SdefReport repairSuspiciousSdef(DocumentSession &session,
                                const std::vector<mmd::VertexHandle> &handles) {
  struct Edit {
    mmd::VertexHandle handle;
    mmd::PmxVertex value;
  };
  std::vector<Edit> edits;
  edits.reserve(handles.size());
  for (const auto handle : handles) {
    const auto *vertex = session.document.resolve(handle);
    if (vertex == nullptr || !hasSuspiciousSdef(*vertex))
      continue;
    auto value = *vertex;
    value.weightType = mmd::PmxWeightType::bdef2;
    value.sdefC = {};
    value.sdefR0 = {};
    value.sdefR1 = {};
    edits.push_back({handle, value});
  }
  SdefReport report;
  const auto result = applyTransaction(
      session,
      [&](auto &transaction) {
        for (const auto &edit : edits)
          if (!transaction.setVertex(edit.handle, edit.value))
            return false;
        return true;
      },
      "不正なSDEFをBDEF2へ修復");
  if (result.success)
    report.converted = edits.size();
  else
    report.rejected = edits.size();
  report.rejected += handles.size() - edits.size();
  return report;
}

bool mirrorSdef(DocumentSession &session,
                const std::vector<mmd::VertexHandle> &handles) {
  struct Edit {
    mmd::VertexHandle handle;
    mmd::PmxVertexSkin value;
  };
  std::vector<Edit> edits;
  for (const auto handle : handles) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef)
      continue;
    mmd::PmxVertexSkin value;
    value.type = source->weightType;
    value.weights = source->weights;
    value.sdefC = source->sdefC;
    value.sdefR0 = source->sdefR0;
    value.sdefR1 = source->sdefR1;
    for (std::size_t i = 0; i < 4; ++i)
      if (const auto bone = mirroredBone(session.document, source->bones[i]))
        value.bones[i] = *bone;
    value.sdefC[0] = -value.sdefC[0];
    value.sdefR0[0] = -value.sdefR0[0];
    value.sdefR1[0] = -value.sdefR1[0];
    edits.push_back({handle, value});
  }
  return applyTransaction(
             session,
             [&](auto &transaction) {
               for (const auto &edit : edits)
                 if (!transaction.setVertexSkin(edit.handle, edit.value))
                   return false;
               return true;
             },
             "SDEFをミラー")
      .success;
}

bool hasSuspiciousSdef(const mmd::PmxVertex &vertex) noexcept {
  if (vertex.weightType != mmd::PmxWeightType::sdef)
    return false;
  for (const auto &value : vertex.sdefC)
    if (!std::isfinite(value))
      return true;
  for (const auto &value : vertex.sdefR0)
    if (!std::isfinite(value))
      return true;
  for (const auto &value : vertex.sdefR1)
    if (!std::isfinite(value))
      return true;
  return false;
}

} // namespace pmxer
