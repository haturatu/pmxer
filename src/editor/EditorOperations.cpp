#include "EditorOperations.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace pmxer {
namespace {

class SnapshotCommand final : public EditorCommand {
  public:
    SnapshotCommand(mmd::PmxModel before, mmd::PmxModel after, std::string description)
        : before_(std::move(before)), after_(std::move(after)), description_(std::move(description)) {}

    bool apply(mmd::PmxDocument &document) override {
        document = mmd::PmxDocument(after_);
        return true;
    }

    bool undo(mmd::PmxDocument &document) override {
        document = mmd::PmxDocument(before_);
        return true;
    }

    const std::string &description() const noexcept override {
        return description_;
    }

  private:
    mmd::PmxModel before_;
    mmd::PmxModel after_;
    std::string description_;
};

template <typename Handle, typename Value>
using PropertySetter = bool (*)(mmd::PmxDocument::Transaction &, Handle, const Value &);

template <typename Handle, typename Value>
class PropertyCommand final : public EditorCommand {
  public:
    PropertyCommand(Handle handle, Value before, Value after, PropertySetter<Handle, Value> setter,
                    std::string description)
        : handle_(handle), before_(std::move(before)), after_(std::move(after)), setter_(setter),
          description_(std::move(description)) {}

    bool apply(mmd::PmxDocument &document) override {
        auto transaction = document.transaction();
        if (!setter_(transaction, handle_, after_))
            return false;
        return transaction.commit().committed;
    }

    bool undo(mmd::PmxDocument &document) override {
        auto transaction = document.transaction();
        if (!setter_(transaction, handle_, before_))
            return false;
        return transaction.commit().committed;
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

bool setVertexValue(mmd::PmxDocument::Transaction &transaction, mmd::VertexHandle handle,
                    const mmd::PmxVertex &value) {
    return transaction.setVertex(handle, value);
}

bool setMaterialValue(mmd::PmxDocument::Transaction &transaction, mmd::MaterialHandle handle,
                      const mmd::PmxMaterial &value) {
    return transaction.setMaterial(handle, value);
}

bool setBoneValue(mmd::PmxDocument::Transaction &transaction, mmd::BoneHandle handle, const mmd::PmxBone &value) {
    return transaction.setBone(handle, value);
}

bool setMorphValue(mmd::PmxDocument::Transaction &transaction, mmd::MorphHandle handle,
                   const mmd::PmxMorph &value) {
    return transaction.setMorph(handle, value);
}

bool setDisplayFrameValue(mmd::PmxDocument::Transaction &transaction, mmd::DisplayFrameHandle handle,
                          const mmd::PmxDisplayFrame &value) {
    return transaction.setDisplayFrame(handle, value);
}

bool setRigidBodyValue(mmd::PmxDocument::Transaction &transaction, mmd::RigidBodyHandle handle,
                       const mmd::PmxRigidBody &value) {
    return transaction.setRigidBody(handle, value);
}

bool setJointValue(mmd::PmxDocument::Transaction &transaction, mmd::JointHandle handle, const mmd::PmxJoint &value) {
    return transaction.setJoint(handle, value);
}

bool setSoftBodyValue(mmd::PmxDocument::Transaction &transaction, mmd::SoftBodyHandle handle,
                      const mmd::PmxSoftBody &value) {
    return transaction.setSoftBody(handle, value);
}

template <typename Handle, typename Value, typename Resolver>
OperationResult applyProperty(DocumentSession &session, Handle handle, const Value &value, Resolver resolver,
                               PropertySetter<Handle, Value> setter, std::string description) {
    const auto *current = resolver(session.document, handle);
    if (current == nullptr)
        return {false, "対象が見つかりません"};
    const auto before = *current;

    auto transaction = session.document.transaction();
    if (!setter(transaction, handle, value))
        return {false, "対象が見つかりません"};
    const auto committed = transaction.commit();
    if (!committed.committed)
        return {false, committed.errors.empty() ? "編集結果が検証に失敗しました" : committed.errors.front()};

    session.commands.recordApplied(
        std::make_unique<PropertyCommand<Handle, Value>>(handle, before, value, setter, std::move(description)));
    session.modified = true;
    ++session.revision;
    session.validation = committed.validation;
    session.changes = committed.changes;
    return {true, {}};
}

} // namespace

OperationResult applyTransaction(DocumentSession &session,
                                  const std::function<bool(mmd::PmxDocument::Transaction &)> &callback,
                                  std::string description) {
    const auto before = session.document.model();
    auto transaction = session.document.transaction();
    if (!callback(transaction))
        return {false, "対象が見つかりません"};
    const auto committed = transaction.commit();
    if (!committed.committed)
        return {false, committed.errors.empty() ? "編集結果が検証に失敗しました" : committed.errors.front()};
    const auto after = session.document.model();
    session.commands.recordApplied(std::make_unique<SnapshotCommand>(before, after, std::move(description)));
    session.modified = true;
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

OperationResult editMetadata(DocumentSession &session, const mmd::PmxMetadata &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setMetadata(value); }, "モデル情報を編集");
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
