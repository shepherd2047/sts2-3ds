// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ developer menu

namespace {
const char* kDevActions[] = {"无敌", "回满血", "金币 +100", "最大生命 +10", "获得遗物…",
                             "加入卡牌…", "升级全部卡牌", "指定下一场战斗…", "秒杀敌人", "自由地图",
                             "跳到下一幕"};
constexpr int kDevActionCount = 11;
constexpr int kDevRows = 9;  // encounter list rows per page
}  // namespace

void App::drawDev(bool top) {
  Run& r = *run_;
  if (devRelics_.empty()) {
    std::vector<std::string> ids = db::sharedRelicPool();
    for (auto& id : run_->character().relicPool) ids.push_back(id);
    for (auto& id : ids) if (auto rel = db::relic(id)) { rel->run = run_.get(); devRelics_.push_back(std::move(rel)); }
    for (auto& id : run_->character().cardPool) if (auto c = db::card(id)) devCards_.push_back(std::move(c));
    for (auto& a : db::acts())
      for (auto* list : {&a.weak, &a.normal, &a.elites, &a.bosses})
        for (auto& id : *list) if (db::encounter(id)) devEncounters_.push_back(id);
  }
  for (auto& rel : devRelics_) rel->run = run_.get();
  if (top) {
    drawSceneBg(true, 0.75f);
    drawTopBar();
    R().text(kTop / 2, 22, "开发者模式", ts(F16, col::gold, CENTER));
    if (devPage_ == 1 && sel_ >= 0 && sel_ < (int)devRelics_.size()) drawRelicDetail(devRelics_[sel_].get(), 90);
    else if (devPage_ == 2 && sel_ >= 0 && sel_ < (int)devCards_.size()) drawCard(devCards_[sel_].get(), (kTop - 132) / 2, 44, 1.1f, false, true);
    else if (devPage_ == 3 && sel_ >= 0 && sel_ < (int)devEncounters_.size()) {
      R().text(kTop / 2, 100, devEncounters_[sel_], ts(F16, col::white, CENTER));
      R().text(kTop / 2, 124, "再点一次：下一场战斗就是它", ts(F12, col::gray, CENTER));
    } else {
      auto line = [&](float y, const std::string& k, const std::string& v, uint32_t c) {
        R().text(120, y, k, ts(F12, col::gray, RIGHT));
        R().text(130, y, v, ts(F12, c));
      };
      line(60, "无敌", r.devGod ? "开" : "关", r.devGod ? col::green : col::white);
      line(80, "自由地图", r.freeMap ? "开（任意房间可进）" : "关", r.freeMap ? col::green : col::white);
      line(100, "下一场战斗", r.devNextEncounter.empty() ? "随机" : r.devNextEncounter, col::white);
      line(140, "当前", "第 " + num(r.actIndex + 1) + " 幕 · " + r.act().name, col::white);
      line(120, "牌组 / 遗物", num((int)r.deck.size()) + " 张 / " + num((int)r.relics.size()) + " 个", col::white);
      R().text(kTop / 2, 160, devPage_ == 0 ? "SELECT 或 B 关闭" : "点一下选中，再点一次确认", ts(F12, col::gray, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.75f);
  if (devPage_ == 0) {
    for (int i = 0; i < kDevActionCount; ++i) {
      float x = i % 2 ? 164 : 6, y = 6 + (i / 2) * 38;
      if (i == 10) { x = 164; y = 200; }  // beside 关闭
      std::string label = kDevActions[i];
      bool on = (i == 0 && r.devGod) || (i == 9 && r.freeMap);
      if (i == 0 || i == 9) label += on ? "：开" : "：关";
      bool enabled = i == 8 ? (r.combat && r.combat->playerPhase && r.combat->actions.waiting())
                   : i == 10 ? (r.screen == Screen::Map && r.actIndex + 1 < Run::kActs) : true;
      button(x, y, 150, 32, label, ID_DEV0 + i, enabled, on);
    }
    button(10, 200, 100, 34, "关闭", ID_BACK);
  } else if (devPage_ == 1) {
    const int cols = 6;
    const float cell = 48, x0 = (kBot - cols * cell) / 2;
    for (int i = 0; i < (int)devRelics_.size(); ++i) {
      float x = x0 + (i % cols) * cell, y = 6 + (i / cols - scroll_) * cell;
      if (y < 0 || y > 150) continue;
      if (i == sel_) gfx::rect(x + 2, y + 2, cell - 4, cell - 4, 0xFFE07060);
      bool owned = r.hasRelic(devRelics_[i]->id);
      spr(R().sprite("relic/" + devRelics_[i]->icon), x + 6, y + 6, cell - 12, cell - 12, owned ? 0x000000FF : 0xFFFFFFFF, owned ? 0.6f : 0.f);
      hits_.push_back({x, y, cell, cell, ID_DEVITEM0 + i});
    }
  } else if (devPage_ == 2) {
    std::vector<Card*> cards;
    for (auto& c : devCards_) cards.push_back(c.get());
    drawCardGrid(cards, sel_, 0, 196, scroll_);
  } else {
    for (int k = 0; k < kDevRows; ++k) {
      int i = scroll_ * kDevRows + k;
      if (i >= (int)devEncounters_.size()) break;
      float y = 4 + k * 21;
      bool hl = i == sel_;
      gfx::rect(6, y, kBot - 12, 19, hl ? 0x8A5A20F0 : 0x00000080);
      R().text(12, y + 2, devEncounters_[i], ts(F12, hl ? col::gold : col::white));
      hits_.push_back({6, y, kBot - 12.f, 19, ID_DEVITEM0 + i});
    }
  }
  if (devPage_ != 0) {
    gfx::rect(0, 196, kBot, 44, 0x000000A0);
    button(10, 200, 90, 34, "返回", ID_BACK);
    button(kBot - 150, 200, 66, 34, "上页", ID_PGUP);
    button(kBot - 78, 200, 66, 34, "下页", ID_PGDN);
  }
  if (toastT_ > 0) {
    float w = R().measure(toast_, ts(F12)) + 16;
    gfx::rect((kBot - w) / 2, 172, w, 20, 0x000000C0);
    R().text(kBot / 2, 175, toast_, ts(F12, col::gold, CENTER));
  }
}

void App::devApply(int page, int i) {
  Run& r = *run_;
  auto say = [&](const std::string& s) { toast_ = s; toastT_ = 1.2f; };
  if (page == 1 && i < (int)devRelics_.size()) {
    const std::string& id = devRelics_[i]->id;
    if (r.hasRelic(id)) { say("已经有了"); return; }
    Scheduler::get().spawn(r.obtainRelic(db::relic(id)));
    say("获得遗物：" + L("relics." + devRelics_[i]->locKey + ".title"));
  } else if (page == 2 && i < (int)devCards_.size()) {
    r.deck.push_back(db::card(devCards_[i]->id));
    say("加入牌组：" + cardTitle(devCards_[i].get()));
  } else if (page == 3 && i < (int)devEncounters_.size()) {
    r.devNextEncounter = devEncounters_[i];
    say("下一场战斗：" + devEncounters_[i]);
    devPage_ = 0; sel_ = -1; scroll_ = 0;
  }
}

void App::updateDev(const gfx::Input& in) {
  Run& r = *run_;
  auto close = [&] { devOpen_ = false; devPage_ = 0; sel_ = -1; scroll_ = 0; };
  auto toPage = [&](int p) { devPage_ = p; sel_ = -1; scroll_ = 0; };
  if (in.down & gfx::BTN_B) { if (devPage_ == 0) close(); else toPage(0); return; }
  int count = devPage_ == 1 ? (int)devRelics_.size() : devPage_ == 2 ? (int)devCards_.size() : (int)devEncounters_.size();
  int perRow = devPage_ == 1 ? 6 : devPage_ == 2 ? 5 : 1;
  int pageRows = devPage_ == 1 ? 3 : devPage_ == 2 ? 2 : 1;
  int maxScroll = devPage_ == 3 ? (count - 1) / kDevRows : std::max(0, (count + perRow - 1) / perRow - pageRows);
  if (devPage_ != 0) {
    if (in.down & gfx::BTN_RIGHT) sel_ = std::min(count - 1, sel_ + 1);
    if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
    if (in.down & gfx::BTN_DOWN) sel_ = std::min(count - 1, sel_ + perRow);
    if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - perRow);
    if ((in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) && sel_ >= 0)
      scroll_ = devPage_ == 3 ? sel_ / kDevRows : std::clamp(sel_ / perRow - 1, 0, maxScroll);
    if ((in.down & gfx::BTN_A) && sel_ >= 0) { devApply(devPage_, sel_); return; }
  }
  if (!in.touchDown) return;
  int id = hitAt(in.tx, in.ty);
  if (id == ID_BACK) { if (devPage_ == 0) close(); else toPage(0); return; }
  if (id == ID_PGUP) { scroll_ = std::max(0, scroll_ - (devPage_ == 3 ? 1 : pageRows)); return; }
  if (id == ID_PGDN) { scroll_ = std::min(maxScroll, scroll_ + (devPage_ == 3 ? 1 : pageRows)); return; }
  if (devPage_ == 0 && id >= ID_DEV0 && id < ID_DEV0 + kDevActionCount) {
    Creature* p = r.player.get();
    switch (id - ID_DEV0) {
      case 0: r.devGod = !r.devGod; break;
      case 1: p->hp = p->maxHp; break;
      case 2: r.gold += 100; break;
      case 3: p->maxHp += 10; p->hp += 10; break;
      case 4: toPage(1); break;
      case 5: toPage(2); break;
      case 6: for (auto& c : r.deck) c->upgrade(); toast_ = "牌组已全部升级"; toastT_ = 1.2f; break;
      case 7: toPage(3); break;
      case 8:
        if (r.combat && r.combat->playerPhase && r.combat->actions.waiting()) {
          PlayerAction a;
          a.kind = PlayerAction::DevKillAll;
          r.combat->actions.fire(a);
          close();
        }
        break;
      case 9: r.freeMap = !r.freeMap; break;
      case 10:
        if (r.screen == Screen::Map && r.actIndex + 1 < Run::kActs) {
          r.devSkipAct = true;  // Run::main enters the next act at the map choice
          r.mapChoice.fire(-1);
          mapScroll_ = 0;
          close();
        }
        break;
    }
    return;
  }
  int card = devPage_ == 2 && id >= ID_GRID0 ? id - ID_GRID0 : -1;
  int item = devPage_ != 2 && id >= ID_DEVITEM0 && id < ID_GRID0 ? id - ID_DEVITEM0 : card;
  if (item >= 0 && item < count) {
    if (sel_ == item) devApply(devPage_, item);
    else sel_ = item;
  }
}

}  // namespace ui
