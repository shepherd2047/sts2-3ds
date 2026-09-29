// M12: the offline daily run. See daily.h.
#include "daily.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "game.h"
#include "modifiers.h"
#include "progress.h"

namespace sts {
namespace daily {

namespace {
bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int daysIn(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leap(y) ? 29 : d[m - 1];
}
bool valid(const Date& d) {
  return d.year >= 1 && d.year <= 9999 && d.month >= 1 && d.month <= 12 && d.day >= 1 && d.day <= daysIn(d.year, d.month);
}

// ModifierModel.Pick2Good1Bad (ModelDb.GoodModifiers / BadModifiers / MutuallyExclusiveModifiers).
std::vector<std::string> pick2Good1Bad(Rng& rng, const std::vector<std::string>& excludedCharacters) {
  std::vector<std::string> out;
  std::vector<std::string> good = {"Draft", "SealedDeck", "Hoarder", "Specialized", "Insanity",
                                   "AllStar", "Flight", "Vintage", "CharacterCards"};
  std::vector<std::string> others;
  for (auto& ch : db::allCharacters())
    if (std::find(excludedCharacters.begin(), excludedCharacters.end(), ch) == excludedCharacters.end()) others.push_back(ch);
  if (others.empty()) good.erase(std::remove(good.begin(), good.end(), "CharacterCards"), good.end());
  const std::vector<std::string> exclusive = {"SealedDeck", "Draft", "Insanity"};
  for (int n = 0; n < 2 && !good.empty(); ++n) {
    std::string g = rng.nextItem(good);
    std::string k = g;
    if (g == "CharacterCards") k += ":" + rng.nextItem(others);
    out.push_back(k);
    good.erase(std::find(good.begin(), good.end(), g));
    if (std::find(exclusive.begin(), exclusive.end(), g) != exclusive.end())
      for (auto& x : exclusive) good.erase(std::remove(good.begin(), good.end(), x), good.end());
  }
  static const std::vector<std::string> bad = {"DeadlyEvents", "CursedRun", "BigGameHunter", "Midas",
                                               "Murderous", "NightTerrors", "Terminal"};
  out.push_back(rng.nextItem(bad));
  return out;
}
}  // namespace

bool parseDate(const std::string& text, Date& out) {
  Date d;
  char tail = 0;
  if (std::sscanf(text.c_str(), "%d-%d-%d%c", &d.year, &d.month, &d.day, &tail) != 3 || !valid(d)) return false;
  out = d;
  return true;
}

std::string key(const Date& d) {
  char b[16];
  std::snprintf(b, sizeof b, "%04d-%02d-%02d", d.year, d.month, d.day);
  return b;
}

Date today() {
  Date d;
  if (const char* e = std::getenv("STS_DAILY_DATE"))
    if (parseDate(e, d)) return d;
  // The 3DS clock (newlib) and the PC both give the local date here; the 3DS has no time zone,
  // its time() already is local time.
  std::time_t t = std::time(nullptr);
  if (const std::tm* tm = std::localtime(&t)) {
    d.year = tm->tm_year + 1900;
    d.month = tm->tm_mon + 1;
    d.day = tm->tm_mday;
  }
  return d;
}

Params forDate(const Date& d) {
  Params p;
  p.date = d;
  char b[32];
  std::snprintf(b, sizeof b, "%02d_%02d_%04d", d.day, d.month, d.year);  // ToString("dd_MM_yyyy")
  const std::string dayText = modifiers::canonicalizeSeed(b);
  p.seedText = modifiers::canonicalizeSeed(dayText + "_1p");  // $"dd_MM_yyyy_{Players.Count}p"
  Rng rng(deterministicHash(dayText));
  Rng rngChar(rng.raw().nextULong());
  Rng rngAsc(rng.raw().nextULong());
  Rng rngMods(rng.raw().nextULong());
  p.character = rngChar.nextItem(db::allCharacters());
  p.ascension = rngAsc.nextInt(0, 11);
  p.modifiers = pick2Good1Bad(rngMods, {p.character});
  return p;
}

int best(const Progress& p, const std::string& dateKey) {
  auto it = p.dailyBest.find(dateKey);
  return it == p.dailyBest.end() ? -1 : it->second;
}

int bestOverall(const Progress& p) {
  int b = -1;
  for (auto& [date, score] : p.dailyBest) b = std::max(b, score);
  return b;
}

bool recordScore(const std::string& dateKey, int score) {
  Progress& p = progress::state();
  score = std::max(0, score);
  if (best(p, dateKey) >= score) return false;
  p.dailyBest[dateKey] = score;
  return true;
}

}  // namespace daily
}  // namespace sts
