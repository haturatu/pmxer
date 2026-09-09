#include "UiStatus.hpp"

#include "DocumentSession.hpp"

#include <utility>

namespace pmxer {

void setStatus(DocumentSession &session, std::string text, UiStatusKind kind,
               std::chrono::milliseconds lifetime, bool sticky) {
    session.ui.status = std::move(text);
    session.ui.statusKind = kind;
    session.ui.statusSticky = sticky;
    if (!sticky)
        session.ui.statusExpiresAt =
            std::chrono::steady_clock::now() + lifetime;
}

void setOperationStatus(DocumentSession &session, bool success,
                        std::string successText, std::string failureText) {
    if (success)
        setStatus(session, std::move(successText), UiStatusKind::success);
    else {
        if (failureText.empty())
            failureText = "操作に失敗しました";
        setStatus(session, std::move(failureText), UiStatusKind::error,
                  std::chrono::milliseconds::zero(), true);
    }
}

void updateStatusLifetime(DocumentSession &session) {
    const auto now = std::chrono::steady_clock::now();
    if (!session.ui.status.empty() && !session.ui.statusSticky &&
        now >= session.ui.statusExpiresAt) {
        session.ui.status.clear();
    }
}

} // namespace pmxer
