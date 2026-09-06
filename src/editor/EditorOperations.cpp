#include "EditorOperations.hpp"

#include <algorithm>
#include <memory>

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
    session.validation = committed.validation;
    session.changes = committed.changes;
    return {true, {}};
}

OperationResult editVertex(DocumentSession &session, mmd::VertexHandle handle, const mmd::PmxVertex &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setVertex(handle, value); }, "頂点を編集");
}

OperationResult editMetadata(DocumentSession &session, const mmd::PmxMetadata &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setMetadata(value); }, "モデル情報を編集");
}

OperationResult editMaterial(DocumentSession &session, mmd::MaterialHandle handle, const mmd::PmxMaterial &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setMaterial(handle, value); }, "材質を編集");
}

OperationResult editBone(DocumentSession &session, mmd::BoneHandle handle, const mmd::PmxBone &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setBone(handle, value); }, "ボーンを編集");
}

OperationResult editMorph(DocumentSession &session, mmd::MorphHandle handle, const mmd::PmxMorph &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setMorph(handle, value); }, "モーフを編集");
}

OperationResult editDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle handle,
                                  const mmd::PmxDisplayFrame &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setDisplayFrame(handle, value); }, "表示枠を編集");
}

OperationResult editRigidBody(DocumentSession &session, mmd::RigidBodyHandle handle, const mmd::PmxRigidBody &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setRigidBody(handle, value); }, "剛体を編集");
}

OperationResult editJoint(DocumentSession &session, mmd::JointHandle handle, const mmd::PmxJoint &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setJoint(handle, value); }, "ジョイントを編集");
}

OperationResult editSoftBody(DocumentSession &session, mmd::SoftBodyHandle handle, const mmd::PmxSoftBody &value) {
    return applyTransaction(session, [&](auto &transaction) { return transaction.setSoftBody(handle, value); }, "ソフトボディを編集");
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
