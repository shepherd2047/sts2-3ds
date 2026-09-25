#include "core/task.h"
#include "gfx/gfx.h"
#include "ui/res.h"
#include "ui/ui.h"

int main() {
  if (!gfx::init()) return 1;
  ui::App app;
  bool ok = app.init();
  while (gfx::running()) {
    gfx::Input in = gfx::input();
    if ((in.down & gfx::BTN_START) && (in.held & gfx::BTN_SELECT)) break;
    double dt = gfx::dt();
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
  gfx::shutdown();
  return 0;
}
