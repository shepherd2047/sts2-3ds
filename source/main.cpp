#include <cstdlib>

#include "audio/audio.h"
#include "core/task.h"
#include "gfx/gfx.h"
#include "ui/res.h"
#include "ui/ui.h"

int main() {
  if (!gfx::init()) return 1;
  ui::App app;
  bool ok = app.init();
  audio::init();  // silent no-op when the audio folder is missing
  // U2 debug hook until U3/U4 route game events: STS_MUSIC=<id> plays at startup (audio::init)
  // and every tap plays STS_TAP_SFX (default: a UI click).
  const char* tapSfx = getenv("STS_MUSIC") ? (getenv("STS_TAP_SFX") ? getenv("STS_TAP_SFX") : "event:/sfx/ui/clicks/ui_click") : nullptr;
  while (gfx::running()) {
    gfx::Input in = gfx::input();
    if ((in.down & gfx::BTN_START) && (in.held & gfx::BTN_SELECT)) break;
    double dt = gfx::dt();
    if (tapSfx && in.touchDown) audio::playSfx(tapSfx);
    audio::update(dt);
    gfx::beginFrame();
    if (ok) {
      app.update(in, dt);
      sts::Scheduler::get().update(dt);
      app.draw();
    } else {
      gfx::screen(gfx::TOP, 0x400000FF);
      gfx::screen(gfx::BOTTOM, 0x400000FF);
    }
    gfx::endFrame();
  }
  audio::shutdown();
  gfx::shutdown();
  return 0;
}
