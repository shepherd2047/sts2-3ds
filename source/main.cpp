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
  gfx::memoryLog("startup");  // 3DS: linear heap left after the UI atlas, fonts and audio
  // Y5: HOME menu / lid closed. The loop itself is blocked in gfx::running() (aptMainLoop) while
  // suspended, and the suspended time never reaches gfx::dt(), so the run timer and the
  // scheduler just stop; only the audio (streamed by a background thread) needs pausing.
  // gfx::running() turns false on HOME -> Close: the loop ends and nothing is saved (run.sav
  // only changes at a map save point, so an exit can never leave a half-updated save).
  gfx::onSystemPause([](bool paused) { audio::setPaused(paused); });
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
