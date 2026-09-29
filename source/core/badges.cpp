// End-of-run badges (package M7). See badges.h.
#include "badges.h"

#include <algorithm>
#include <map>

#include "game.h"

namespace sts {
namespace badges {

using history::MapPoint;
using history::Room;
using history::RoomKind;
using history::RunRecord;

namespace {

const std::vector<Info> kAll = {
    {"CCCCOMBO", false, "play 20 cards in a single turn"},
    {"CURSES", true, "win with 5 or more Curses"},
    {"DOUBLE_SNECKO", false, "obtain both Snecko Eye and Fake Snecko Eye"},
    {"ELITE", false, "defeat 3 / 6 / 9 Elites"},
    {"FAMISHED", true, "win with Max HP under half the starting HP"},
    {"GLUTTON", true, "win with Max HP 15 / 30 / 50 above the starting HP"},
    {"HIGHLANDER", true, "every non-Basic card in the deck is unique"},
    {"HONED", true, "5+ copies of one non-Basic card"},
    {"BIG_DECK", true, "win with 40 / 60 / 100 cards"},
    {"ILIKESHINY", false, "25 or more relics"},
    {"KACHING", false, "spend 1000+ gold at the Merchant"},
    {"MONEY_MONEY", true, "win with 200 / 400 / 600 gold"},
    {"MYSTERY_MACHINE", false, "visit 15 or more ? rooms"},
    {"PERFECT", false, "defeat 1 / 2 / 3 bosses without taking damage"},
    {"RESTFUL", true, "heal at every rest site"},
    {"RESTLESS", true, "win without ever healing at a rest site"},
    {"SPEEDY", true, "win in 3000 / 2400 / 1800 seconds"},
    {"TABLET", true, "win with 1 Max HP"},
    {"TINY_DECK", true, "win with 20 / 10 / 5 or fewer cards"},
};

BadgeRarity tier(int value, int bronze, int silver, int gold) {
  return value >= gold ? BadgeRarity::Gold : value >= silver ? BadgeRarity::Silver : value >= bronze ? BadgeRarity::Bronze : BadgeRarity::None;
}
BadgeRarity bronzeIf(bool b) { return b ? BadgeRarity::Bronze : BadgeRarity::None; }

// The definition of a deck entry (type / rarity), cached per call.
struct CardInfo { CardType type; Rarity rarity; };
CardInfo cardInfo(const std::string& id, std::map<std::string, CardInfo>& cache) {
  auto it = cache.find(id);
  if (it != cache.end()) return it->second;
  CardInfo ci{CardType::Skill, Rarity::Common};  // unknown ids: an ordinary card
  if (auto c = db::card(id)) ci = {c->type, c->rarity};
  cache[id] = ci;
  return ci;
}

bool hasChoice(const MapPoint& p, const char* id) {
  return std::find(p.restChoices.begin(), p.restChoices.end(), id) != p.restChoices.end();
}

bool hasRelic(const RunRecord& rec, const char* id) {
  return std::find(rec.relics.begin(), rec.relics.end(), id) != rec.relics.end();
}

}  // namespace

const std::vector<Info>& all() { return kAll; }

const Info* find(const std::string& id) {
  for (auto& i : kAll) if (id == i.id) return &i;
  return nullptr;
}

BadgeRarity rarityOf(const std::string& id, const RunRecord& rec) {
  const history::Path& path = rec.path;
  if (id == "CCCCOMBO") return bronzeIf(rec.cccCombo);
  if (id == "CURSES") {
    std::map<std::string, CardInfo> cache;
    int n = 0;
    for (auto& c : rec.deck) n += cardInfo(c.id, cache).type == CardType::Curse;
    return bronzeIf(n >= 5);
  }
  if (id == "HIGHLANDER" || id == "HONED") {
    std::map<std::string, CardInfo> cache;
    std::map<std::string, int> copies;  // the deck without Basic cards
    int total = 0;
    for (auto& c : rec.deck)
      if (cardInfo(c.id, cache).rarity != Rarity::Basic) { ++copies[c.id]; ++total; }
    if (id == "HIGHLANDER") return bronzeIf((int)copies.size() == total);
    for (auto& kv : copies) if (kv.second >= 5) return BadgeRarity::Bronze;
    return BadgeRarity::None;
  }
  if (id == "DOUBLE_SNECKO") return bronzeIf(hasRelic(rec, "SneckoEye") && hasRelic(rec, "FakeSneckoEye"));
  if (id == "ELITE") return tier(history::elitesKilled(path), 3, 6, 9);
  if (id == "FAMISHED") return bronzeIf(rec.maxHp < (db::character(rec.character).startingHp + 1) / 2);
  if (id == "GLUTTON") return tier(rec.maxHp - db::character(rec.character).startingHp, 15, 30, 50);
  if (id == "BIG_DECK") return tier((int)rec.deck.size(), 40, 60, 100);
  if (id == "ILIKESHINY") return bronzeIf(rec.relics.size() >= 25);
  if (id == "KACHING") {
    int spent = 0;
    for (auto& act : path)
      for (auto& p : act)
        for (auto& r : p.rooms) if (r.type == RoomKind::Shop) spent += p.goldSpent;
    return bronzeIf(spent >= 1000);
  }
  if (id == "MONEY_MONEY") return tier(rec.gold, 200, 400, 600);
  if (id == "MYSTERY_MACHINE") {
    int n = 0;
    for (auto& act : path) for (auto& p : act) n += p.type == history::PointType::Unknown;
    return bronzeIf(n >= 15);
  }
  if (id == "PERFECT") {
    // A boss room does not count if it is the room that killed the player.
    const Room* killer = nullptr;
    if (!rec.win && !path.empty() && !path.back().empty() && !path.back().back().rooms.empty())
      killer = &path.back().back().rooms.back();
    int n = 0;
    for (auto& act : path)
      for (auto& p : act)
        for (auto& r : p.rooms)
          if (r.type == RoomKind::Boss && &r != killer && p.tracked && p.damageTaken <= 0) ++n;
    return n >= 3 ? BadgeRarity::Gold : n == 2 ? BadgeRarity::Silver : n == 1 ? BadgeRarity::Bronze : BadgeRarity::None;
  }
  if (id == "RESTFUL" || id == "RESTLESS") {
    bool wantHeal = id == "RESTFUL";
    int rests = 0;
    for (auto& act : path)
      for (auto& p : act)
        for (auto& r : p.rooms) {
          if (r.type != RoomKind::RestSite) continue;
          ++rests;
          if (!p.tracked) return BadgeRarity::None;  // choices unknown (resumed from run.sav)
          if (hasChoice(p, "HEAL") != wantHeal) return BadgeRarity::None;
        }
    return bronzeIf(rests > 0);
  }
  if (id == "SPEEDY") {
    // _run.WinTime: the run's time when it was won (RunRecord::runTime holds it for a win).
    long t = rec.win ? rec.runTime : 0;
    return t <= 1800 ? BadgeRarity::Gold : t <= 2400 ? BadgeRarity::Silver : t <= 3000 ? BadgeRarity::Bronze : BadgeRarity::None;
  }
  if (id == "TABLET") return rec.maxHp == 1 ? BadgeRarity::Gold : BadgeRarity::None;
  if (id == "TINY_DECK") {
    int n = (int)rec.deck.size();
    return n <= 5 ? BadgeRarity::Gold : n <= 10 ? BadgeRarity::Silver : n <= 20 ? BadgeRarity::Bronze : BadgeRarity::None;
  }
  return BadgeRarity::None;
}

std::vector<history::BadgeEntry> compute(const RunRecord& rec) {
  std::vector<history::BadgeEntry> out;
  if (rec.abandoned) return out;
  for (auto& info : kAll) {
    if (info.requiresWin && !rec.win) continue;
    BadgeRarity r = rarityOf(info.id, rec);
    if (r != BadgeRarity::None) out.push_back({info.id, (int)r});
  }
  return out;
}

const char* rarityName(int rarity) {
  return rarity == 1 ? "bronze" : rarity == 2 ? "silver" : rarity == 3 ? "gold" : "ERROR";
}

LocKeys locKeys(const history::BadgeEntry& b) {
  std::string pre = "badges." + b.id + ".";
  std::string rn = rarityName(b.rarity);
  return {pre + rn + "Title", pre + rn + "Description", pre + "title", pre + "description"};
}

const history::RunRecord* lastRecord() { return history::last(); }

// ---- run-time counters ---------------------------------------------------------------------

namespace {
MapPoint* current(Run& run) {
  if (run.mapHistory.empty() || run.mapHistory.back().empty()) return nullptr;
  return &run.mapHistory.back().back();
}
}  // namespace

void noteDamageTaken(Run& run, int unblocked) {
  if (unblocked > 0)
    if (MapPoint* p = current(run)) p->damageTaken += unblocked;
}

void noteGoldSpent(Run& run, int gold) {
  if (gold > 0)
    if (MapPoint* p = current(run)) p->goldSpent += gold;
}

void noteRestChoice(Run& run, int restOption) {
  static const char* const ids[] = {"HEAL", "SMITH", "LIFT", "DIG", "COOK", "KINDLE", "CLONE"};
  if (restOption < 0 || restOption >= 7) return;
  if (MapPoint* p = current(run)) p->restChoices.push_back(ids[restOption]);
}

void noteCardsPlayedThisTurn(Run& run, int cardsPlayed) {
  if (cardsPlayed >= 20) run.cccCombo = true;
}

}  // namespace badges
}  // namespace sts
