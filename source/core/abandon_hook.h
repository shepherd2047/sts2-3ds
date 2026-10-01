// Run.abandon from inside a run: the C# Trial event's DOUBLE_DOWN opens NAbandonRunConfirmPopup
// (confirm, then the run is abandoned and the game returns to the title). The core has no UI, so
// the UI installs this hook (ui/ui.cpp App::init: confirm::ask -> Run::abandon -> returnTitle);
// it stays empty in the headless sim and the tests.
#pragma once
#include <functional>

namespace sts {
inline std::function<void()> askAbandonRun;
}  // namespace sts
