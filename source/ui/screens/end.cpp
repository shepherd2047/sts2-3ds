// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

void App::drawEnd(bool top, bool won) {
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, won ? 0x302000FF : 0x200000FF, 0.6f);
    TextStyle t = ts(F16, won ? col::gold : col::red, CENTER);
    t.scale = 2.f;
    R().text(kTop / 2, 70, won ? L("game_over_screen.BANNER.trueWin") : L("game_over_screen.BANNER.lose0"), t);
    std::string q = won ? "第三幕首领已被击败。" : L("game_over_screen.QUOTES.0" + num((int)(run_->seed % 10)));
    R().text(kTop / 2, 130, q, ts(F16, col::white, CENTER));
    R().text(kTop / 2, 169, "到达第 " + num(run_->floor) + " 层", ts(F12, col::gold, CENTER));
    return;
  }
  gfx::rectGradient(0, 0, kBot, kH, 0x201810FF, 0x0B0B12FF);
  panel(12, 10, kBot - 24, 165);
  R().text(kBot / 2, 20, "本局记录", ts(F16, col::gold, CENTER));
  auto row = [&](float y, const std::string& label, const std::string& value) {
    R().text(38, y, label, ts(F12, col::gray));
    R().text(kBot - 38, y, value, ts(F12, col::white, RIGHT));
  };
  row(53, "进度", "第 " + num(run_->actIndex + 1) + " 幕 · 第 " + num(run_->floor) + " 层");
  row(78, "生命", num(run_->player->hp) + "/" + num(run_->player->maxHp));
  row(103, "金币", num(run_->gold));
  row(128, "牌组", num((int)run_->deck.size()) + " 张");
  row(153, "遗物", num((int)run_->relics.size()) + " 个");
  button(12, 191, 140, 39, "主菜单", ID_TITLE);
  button(168, 191, 140, 39, "再来一局", ID_RESTART, true, true);
}

void App::updateEnd(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if ((in.down & gfx::BTN_B) || id == ID_TITLE) { returnTitle(); return; }
  if ((in.down & (gfx::BTN_A | gfx::BTN_START)) || id == ID_RESTART) startRun();
}

}  // namespace ui
