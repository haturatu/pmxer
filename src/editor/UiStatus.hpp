#pragma once

#include <chrono>
#include <string>

namespace pmxer {

struct DocumentSession;

enum class UiStatusKind { info, success, warning, error };

void setStatus(DocumentSession &session, std::string text,
               UiStatusKind kind = UiStatusKind::info,
               std::chrono::milliseconds lifetime = std::chrono::seconds(4),
               bool sticky = false);
void setOperationStatus(DocumentSession &session, bool success,
                        std::string successText, std::string failureText);
void updateStatusLifetime(DocumentSession &session);

} // namespace pmxer
