#include "UiStatus.hpp"

#include "DocumentSession.hpp"

#include <utility>

namespace pmxer {

void setStatus(DocumentSession &session, std::string text, UiStatusKind kind,
               std::chrono::milliseconds lifetime, bool sticky) {
    session.ui.status = std::move(text);
    session.ui.statusObserved = session.ui.status;
    session.ui.statusKind = kind;
    session.ui.statusSticky = sticky;
    session.ui.statusExpiresAt =
        std::chrono::steady_clock::now() + lifetime;
}

void updateStatusLifetime(DocumentSession &session) {
    const auto now = std::chrono::steady_clock::now();
    if (session.ui.status != session.ui.statusObserved) {
        session.ui.statusObserved = session.ui.status;
        session.ui.statusKind = UiStatusKind::info;
        session.ui.statusSticky = false;
        session.ui.statusExpiresAt = now + std::chrono::seconds(4);
    }
    if (!session.ui.status.empty() && !session.ui.statusSticky &&
        now >= session.ui.statusExpiresAt) {
        session.ui.status.clear();
        session.ui.statusObserved.clear();
    }
}

} // namespace pmxer
