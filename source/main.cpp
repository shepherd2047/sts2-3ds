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
  // Music and ambience follow the game state (ui/music_router.cpp, called from App::update).
  while (gfx::running() && !app.quitRequested()) {  // 退出 on the main menu (S02)
    gfx::Input in = gfx::input();
    if ((in.down & gfx::BTN_START) && (in.held & gfx::BTN_SELECT)) break;
    double dt = gfx::dt();
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
