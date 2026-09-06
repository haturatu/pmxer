#include "EditorOperations.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace pmxer {
namespace {

class SnapshotCommand final : public EditorCommand {
  public:
    SnapshotCommand(mmd::PmxDocument before, mmd::PmxDocument after, std::uint64_t domain, std::string description)
        : before_(std::move(before)), after_(std::move(after)), domain_(domain), description_(std::move(description)) {}

    bool apply(mmd::PmxDocument &document) override {
        document.restoreSnapshot(after_, domain_);
        return true;
    }

    bool undo(mmd::PmxDocument &document) override {
        document.restoreSnapshot(before_, domain_);
        return true;
    }

    const std::string &description() const noexcept override {
        return description_;
    }

  private:
    mmd::PmxDocument before_;
    mmd::PmxDocument after_;
    std::uint64_t domain_{};
    std::string description_;
};

class MetadataCommand final : public EditorCommand {
  public:
    MetadataCommand(mmd::PmxMetadata before, mmd::PmxMetadata after, std::string description)
        : before_(std::move(before)), after_(std::move(after)), description_(std::move(description)) {}

    bool apply(mmd::PmxDocument &document) override {
        return document.replaceMetadata(after_).committed;
    }

    bool undo(mmd::PmxDocument &document) override {
        return document.replaceMetadata(before_).committed;
    }

    const std::string &description() const noexcept override {
        return description_;
    }

  private:
    mmd::PmxMetadata before_;
    mmd::PmxMetadata after_;
    std::string description_;
};

template <typename Handle, typename Value>
using PropertySetter = mmd::PmxTransactionResult (*)(mmd::PmxDocument &, Handle, const Value &);

template <typename Handle, typename Value>
class PropertyCommand final : public EditorCommand {
  public:
    PropertyCommand(Handle handle, Value before, Value after, PropertySetter<Handle, Value> setter,
                    std::string description)
        : handle_(handle), before_(std::move(before)), after_(std::move(after)), setter_(setter),
          description_(std::move(description)) {}

    bool apply(mmd::PmxDocument &document) override {
        return setter_(document, handle_, after_).committed;
    }

    bool undo(mmd::PmxDocument &document) override {
        return setter_(document, handle_, before_).committed;
    }

    const std::string &description() const noexcept override {
        return description_;
    }

  private:
    Handle handle_;
    Value before_;
    Value after_;
    PropertySetter<Handle, Value> setter_;
    std::string description_;
};

mmd::PmxTransactionResult setVertexValue(mmd::PmxDocument &document, mmd::VertexHandle handle,
                                         const mmd::PmxVertex &value) {
    return document.replaceVertex(handle, value);
}

mmd::PmxTransactionResult setTextureValue(mmd::PmxDocument &document, mmd::TextureHandle handle,
                                          const mmd::PmxTexture &value) {
    return document.replaceTexture(handle, value);
}

mmd::PmxTransactionResult setMaterialValue(mmd::PmxDocument &document, mmd::MaterialHandle handle,
                                           const mmd::PmxMaterial &value) {
    return document.replaceMaterial(handle, value);
}

mmd::PmxTransactionResult setBoneValue(mmd::PmxDocument &document, mmd::BoneHandle handle, const mmd::PmxBone &value) {
    return document.replaceBone(handle, value);
}

mmd::PmxTransactionResult setMorphValue(mmd::PmxDocument &document, mmd::MorphHandle handle,
                                        const mmd::PmxMorph &value) {
    return document.replaceMorph(handle, value);
}

mmd::PmxTransactionResult setDisplayFrameValue(mmd::PmxDocument &document, mmd::DisplayFrameHandle handle,
                                               const mmd::PmxDisplayFrame &value) {
    return document.replaceDisplayFrame(handle, value);
}

mmd::PmxTransactionResult setRigidBodyValue(mmd::PmxDocument &document, mmd::RigidBodyHandle handle,
                                            const mmd::PmxRigidBody &value) {
    return document.replaceRigidBody(handle, value);
}

mmd::PmxTransactionResult setJointValue(mmd::PmxDocument &document, mmd::JointHandle handle,
                                        const mmd::PmxJoint &value) {
    return document.replaceJoint(handle, value);
}

mmd::PmxTransactionResult setSoftBodyValue(mmd::PmxDocument &document, mmd::SoftBodyHandle handle,
                                           const mmd::PmxSoftBody &value) {
    return document.replaceSoftBody(handle, value);
}

template <typename Handle, typename Value, typename Resolver>
OperationResult applyProperty(DocumentSession &session, Handle handle, const Value &value, Resolver resolver,
                               PropertySetter<Handle, Value> setter, std::string description) {
    const auto *current = resolver(session.document, handle);
    if (current == nullptr)
        return {false, "対象が見つかりません"};
    const auto before = *current;

    const auto committed = setter(session.document, handle, value);
    if (!committed.committed)
        return {false, committed.errors.empty() ? "編集結果が検証に失敗しました" : committed.errors.front()};

    session.commands.recordApplied(
        std::make_unique<PropertyCommand<Handle, Value>>(handle, before, value, setter, std::move(description)));
    session.modified = session.commands.isModified();
    ++session.revision;
    session.validation = committed.validation;
    session.changes = committed.changes;
    return {true, {}};
}

} // namespace

OperationResult applyTransaction(DocumentSession &session,
                                  const std::function<bool(mmd::PmxDocument::Transaction &)> &callback,
                                  std::string description) {
    const mmd::PmxDocument before(session.document);
    auto transaction = session.document.transaction();
    if (!callback(transaction))
        return {false, "対象が見つかりません"};
    const auto committed = transaction.commit();
    if (!committed.committed)
        return {false, committed.errors.empty() ? "編集結果が検証に失敗しました" : committed.errors.front()};
    const mmd::PmxDocument after(session.document);
    session.commands.recordApplied(
        std::make_unique<SnapshotCommand>(before, after, session.document.domain(), std::move(description)));
    session.modified = session.commands.isModified();
    ++session.revision;
    session.validation = committed.validation;
    session.changes = committed.changes;
    return {true, {}};
}

OperationResult editVertex(DocumentSession &session, mmd::VertexHandle handle, const mmd::PmxVertex &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::VertexHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setVertexValue, "頂点を編集");
}

OperationResult editTexture(DocumentSession &session, mmd::TextureHandle handle, const mmd::PmxTexture &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::TextureHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setTextureValue, "テクスチャを編集");
}

OperationResult editMetadata(DocumentSession &session, const mmd::PmxMetadata &value) {
    const auto before = session.document.model().metadata;
    const auto committed = session.document.replaceMetadata(value);
    if (!committed.committed)
        return {false, committed.errors.empty() ? "編集結果が検証に失敗しました" : committed.errors.front()};
    session.commands.recordApplied(
        std::make_unique<MetadataCommand>(before, value, "モデル情報を編集"));
    session.modified = session.commands.isModified();
    ++session.revision;
    session.validation = committed.validation;
    session.changes = committed.changes;
    return {true, {}};
}

OperationResult editMaterial(DocumentSession &session, mmd::MaterialHandle handle, const mmd::PmxMaterial &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::MaterialHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setMaterialValue, "材質を編集");
}

OperationResult editBone(DocumentSession &session, mmd::BoneHandle handle, const mmd::PmxBone &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::BoneHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setBoneValue, "ボーンを編集");
}

OperationResult editMorph(DocumentSession &session, mmd::MorphHandle handle, const mmd::PmxMorph &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::MorphHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setMorphValue, "モーフを編集");
}

OperationResult editDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle handle,
                                  const mmd::PmxDisplayFrame &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::DisplayFrameHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setDisplayFrameValue, "表示枠を編集");
}

OperationResult editRigidBody(DocumentSession &session, mmd::RigidBodyHandle handle, const mmd::PmxRigidBody &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::RigidBodyHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setRigidBodyValue, "剛体を編集");
}

OperationResult editJoint(DocumentSession &session, mmd::JointHandle handle, const mmd::PmxJoint &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::JointHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setJointValue, "ジョイントを編集");
}

OperationResult editSoftBody(DocumentSession &session, mmd::SoftBodyHandle handle, const mmd::PmxSoftBody &value) {
    return applyProperty(session, handle, value,
                         [](const mmd::PmxDocument &document, mmd::SoftBodyHandle valueHandle) {
                             return document.resolve(valueHandle);
                         },
                         setSoftBodyValue, "ソフトボディを編集");
}

OperationResult normalizeWeights(DocumentSession &session, float threshold) {
    std::vector<mmd::PmxVertex> values = session.document.model().vertices;
    for (auto &vertex : values) {
            const auto count = vertex.weightType == mmd::PmxWeightType::bdef1
                                   ? std::size_t{1}
                                   : (vertex.weightType == mmd::PmxWeightType::bdef2 ||
                                              vertex.weightType == mmd::PmxWeightType::sdef
                                          ? std::size_t{2}
                                          : std::size_t{4});
            float total{};
            for (std::size_t i = 0; i < count; ++i) {
                if (vertex.weights[i] < threshold)
                    vertex.weights[i] = 0.0F;
                total += std::max(0.0F, vertex.weights[i]);
            }
            if (total > 0.0F)
                for (std::size_t i = 0; i < count; ++i)
                    vertex.weights[i] = std::max(0.0F, vertex.weights[i]) / total;
    }
    return applyTransaction(session, [&](auto &transaction) {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!transaction.setVertex(session.document.vertexHandle(i), values[i]))
                return false;
        return true;
    }, "ウェイトを正規化");
}

OperationResult setVertexSkin(DocumentSession &session, mmd::VertexHandle handle, const mmd::PmxVertex &value) {
    return editVertex(session, handle, value);
}

} // namespace pmxer
