// Shows the dialog for the interaction a panel session is waiting for. Called by the wait loop in fs/ (main thread)
// when CPanelSession::InteractionEvent() is signalled.
#pragma once

#include "panel_session.h"

// Takes the pending interaction (if any), shows the host key or prompt dialog and ALWAYS answers it: Reject/cancel
// when the dialog cannot be shown or anything throws, so the session thread is never left blocked. Never throws.
void HandleInteraction(HWND parent, CPanelSession& session);
