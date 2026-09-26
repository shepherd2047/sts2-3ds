// Headless auto-player: drives whole runs with a trivial policy to shake out
// crashes and rule bugs. Build: make -f Makefile.sdl sim
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include "../source/core/game.h"

using namespace sts;
#include <map>
static std::map<std::string, int> played;
static std::map<std::string, int> potionsUsed;
static size_t potionCursor = 0;

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  int runs = argc > 1 ? atoi(argv[1]) : 50;
  bool verbose = argc > 2;
  int wins = 0, floorsTotal = 0;
  for (int s = 1; s <= runs; ++s) {
    Run run;
    run.start((uint64_t)s * 7919);
    if (getenv("SIM_ALLCARDS")) {
      // Every pool card, plain and upgraded, so each one actually gets played.
      for (auto& id : db::ironcladPool()) {
        if (auto c = db::card(id)) run.deck.push_back(std::move(c));
        if (auto c = db::card(id)) { c->upgrade(); run.deck.push_back(std::move(c)); }
      }
      run.player->hp = run.player->maxHp = 999;
    }
    if (getenv("SIM_ALLRELICS")) {
      // Every registered pool relic (pickup effects skipped; Potion Belt applied by hand).
      // Ancient relics are in no pool: a comma list can name them too.
      std::vector<std::string> named;
      {
        std::string only = getenv("SIM_ALLRELICS");
        for (size_t a = 0; a < only.size();) {
          size_t b = only.find(',', a);
          if (b == std::string::npos) b = only.size();
          named.push_back(only.substr(a, b - a));
          a = b + 1;
        }
      }
      for (const auto* pool : {&db::sharedRelicPool(), &db::ironcladRelicPool(), (const std::vector<std::string>*)&named})
        for (auto& id : *pool) {
          if (!db::relicRegistered(id) || run.hasRelic(id)) continue;
          std::string only = getenv("SIM_ALLRELICS");  // "1" = all, else a comma list
          if (only != "1" && ("," + only + ",").find("," + id + ",") == std::string::npos) continue;
          auto rel = db::relic(id);
          rel->run = &run;
          run.relics.push_back(std::move(rel));
        }
      run.potions.resize(5);
    }
    Scheduler::get().spawn(run.main());
    int frames = 0;
    while (run.screen != Screen::GameOver && run.screen != Screen::Victory && frames < 2000000) {
      Scheduler::get().update(0.05);
      ++frames;
      if (verbose && frames % 20000 == 0)
        printf("[frame %d] screen=%d floor=%d combat=%d phase=%d waiting=%d choice=%d idle=%d\n", frames, (int)run.screen, run.floor,
               run.combat ? 1 : 0, run.combat ? run.combat->playerPhase : -1, run.combat ? run.combat->actions.waiting() : -1,
               run.combat ? run.combat->choice.active : -1, Scheduler::get().idle());
      // Deck choices can come up on any screen (relic pickups): answer them first.
      if (run.deckChoice.active && run.deckChoice.result.waiting() && run.screen != Screen::Event && run.screen != Screen::Shop) {
        std::vector<Card*> picked;
        for (int k = 0; k < run.deckChoice.count && k < (int)run.deckChoice.options.size(); ++k) picked.push_back(run.deckChoice.options[k]);
        run.deckChoice.result.fire(picked);
        continue;
      }
      switch (run.screen) {
        case Screen::Map:
          if (run.mapChoice.waiting()) {
            auto r = run.reachableNodes();
            // Prefer rests when hurt, else fights.
            int pick = r[0];
            for (int i : r) if (run.nodes[i].type == RoomType::Rest && run.player->hp < 45) pick = i;
            if (getenv("SIM_MAPLOG") && run.floor > 8 && run.floor < 14) printf("  map pick %d type %d act %d cur %d\n", pick, (int)run.nodes[pick].type, run.actIndex, run.currentNode);
            run.mapChoice.fire(pick);
          }
          break;
        case Screen::Combat: {
          if (!run.combat) break;
          Combat& c = *run.combat;
          static Combat* lastC = nullptr;
          static int hpBefore = 0;
          static std::string encId;
          if (&c != lastC) {
            if (lastC && getenv("SIM_FIGHTS")) printf("  fight %-22s hp %d -> ...\n", encId.c_str(), hpBefore);
            lastC = &c; hpBefore = run.player->hp; encId = c.encounterId;
            // SIM_ALLPOTIONS=1: every fight starts with the next three pool potions.
            if (getenv("SIM_ALLPOTIONS"))
              for (auto& slot : run.potions) {
                auto& pool = db::potionPool();
                for (size_t k = 0; k < pool.size() && !slot; ++k) {
                  slot = db::potion(pool[potionCursor++ % pool.size()]);
                  if (slot) slot->run = &run;
                }
              }
          }
          if (getenv("SIM_FIGHTS") && c.over && c.turnNumber > 0) {
            static Combat* reported = nullptr;
            if (reported != &c) { reported = &c; printf("  fight %-22s hp %2d -> %2d  turns %d %s\n", encId.c_str(), hpBefore, run.player->hp, c.turnNumber, c.won ? "" : "DIED"); }
          }
          if (c.choice.active && c.choice.result.waiting()) {
            c.choice.result.fire({c.choice.options[0]});
          } else if (c.playerPhase && c.actions.waiting()) {
            PlayerAction a;
            a.kind = PlayerAction::EndTurn;
            // Drink the first usable potion straight away.
            for (int k = 0; k < (int)run.potions.size(); ++k)
              if (run.canUsePotion(k) && !c.aliveEnemies().empty()) {
                a.kind = PlayerAction::UsePotion;
                a.potionSlot = k;
                if (run.potions[k]->target == TargetType::AnyEnemy) a.target = c.aliveEnemies()[0];
                potionsUsed[run.potions[k]->id]++;
                if (verbose) printf("  potion %s\n", run.potions[k]->id.c_str());
                c.actions.fire(a);
                break;
              }
            if (a.kind == PlayerAction::UsePotion) break;
            // Slightly sensible policy: block if the incoming hit exceeds block,
            // otherwise attack the weakest enemy; Bash first.
            int incoming = 0;
            for (auto* e : c.aliveEnemies())
              if (e->monster->nextMove)
                for (auto& in : e->monster->nextMove->intents)
                  if (in.kind == Intent::Attack)
                    incoming += std::max(0, c.modifyDamage(c.player, e, in.damage, kMove, nullptr).toInt()) * in.hits;
            bool wantBlock = incoming > c.player->block;
            Creature* weakest = nullptr;
            for (auto* e : c.aliveEnemies()) if (!weakest || e->hp < weakest->hp) weakest = e;
            Card* best = nullptr;
            int bestScore = -1;
            for (Card* card : c.hand) {
              if (!c.canPlay(card)) continue;
              int score = 1;
              if (card->id == "Bash") score = 50;
              else if (card->var("Block") && wantBlock) score = 40;
              else if (card->type == CardType::Attack) score = 20 + card->val("Damage").toInt();
              else if (card->var("Block")) score = 5;
              if (card->type == CardType::Status) score = card->id == "FranticEscape" ? 100 : 0;  // buys turns in the Insatiable's sandpit
              if (score > bestScore) { bestScore = score; best = card; }
            }
            if (best && bestScore > 0) {
              a.kind = PlayerAction::PlayCard;
              a.card = best;
              if (best->target == TargetType::AnyEnemy) a.target = weakest;
            }
            if (a.card) played[a.card->id + (a.card->upgraded() ? "+" : "")]++;
            if (verbose && a.card) printf("  play %s -> %s  E=%d\n", a.card->id.c_str(), a.target ? a.target->name.c_str() : "-", c.energy);
            c.actions.fire(a);
          }
          break;
        }
        case Screen::Reward:
          if (run.rewardChoice.waiting()) run.rewardChoice.fire(0);
          break;
        case Screen::Rest:
          if (run.restChoice.waiting()) {
            // Heal when hurt, else smith; Lift/Dig when offered; leave once something was used.
            int want = run.player->hp < 60 ? 0 : 1;
            if (want == 1) for (int o : run.restOptions) if (o >= 2) want = o;  // extras only when not healing
            bool used = std::find(run.restUsed.begin(), run.restUsed.end(), want) != run.restUsed.end();
            run.restChoice.fire(used || !run.restUsed.empty() ? -1 : want);
          }
          break;
        case Screen::RestUpgrade:
          if (run.upgradeChoice.waiting()) run.upgradeChoice.fire(run.upgradeOptions.empty() ? -1 : 0);
          break;
        case Screen::Event:
          if (run.deckChoice.active && run.deckChoice.result.waiting()) {
            std::vector<Card*> picked;
            for (int k = 0; k < run.deckChoice.count && k < (int)run.deckChoice.options.size(); ++k)
              picked.push_back(run.deckChoice.options[k]);
            run.deckChoice.result.fire(picked);
          } else if (run.eventChoice.waiting() && run.currentEvent) {
            // Rotate through the options so every one gets exercised over many runs.
            auto& opts = run.currentEvent->options;
            int pick = 0;
            if (!opts.empty()) {
              pick = (s + run.floor) % (int)opts.size();
              for (int k = 0; k < (int)opts.size() && opts[pick].locked(); ++k) pick = (pick + 1) % (int)opts.size();
            }
            if (getenv("SIM_FIGHTS")) printf("  event %s -> %d\n", run.currentEvent->id.c_str(), pick);
            run.eventChoice.fire(pick);
          }
          break;
        case Screen::Placeholder:
          if (run.placeholderDone.waiting()) run.placeholderDone.fire(0);
          break;
        case Screen::Shop:
          // Remove a Strike/Defend if the service is bought, else buy the first thing we can.
          if (run.deckChoice.active && run.deckChoice.result.waiting()) {
            Card* pick = run.deckChoice.options[0];
            for (Card* k : run.deckChoice.options) if (k->rarity == Rarity::Basic) { pick = k; break; }
            run.deckChoice.result.fire({pick});
          } else if (run.shopChoice.waiting()) {
            int pick = -1;
            for (int k = 0; k < (int)run.shop.size() && pick < 0; ++k) {
              auto& it = run.shop[k];
              if (it.stocked() && run.shopPrice(it) <= run.gold && (it.kind != ShopItem::PotionItem || run.hasOpenPotionSlot())) pick = k;
            }
            if (getenv("SIM_FIGHTS") && pick >= 0) printf("  shop buy %d (%d gold)\n", pick, run.gold);
            run.shopChoice.fire(pick);
          }
          break;
        case Screen::PotionOffer:
          if (run.potionOfferChoice.waiting()) {
            if (!run.hasOpenPotionSlot()) run.discardPotion(0);
            run.potionOfferChoice.fire(1);
          }
          break;
        case Screen::RelicOffer:
          if (run.relicChoice.waiting()) {
            if (getenv("SIM_FIGHTS") && run.relicOffer) printf("  relic %s\n", run.relicOffer->id.c_str());
            run.relicChoice.fire(1);
          }
          break;
        default:
          break;
      }
      if (run.combat) {
        if (verbose)
          for (auto& e : run.combat->events) {
            static const char* names[] = {"Damage", "Blocked", "Block", "Heal", "PowerUp", "PowerDown", "Death", "Exhaust", "Shuffle", "Banner", "Anim"};
            printf("    [%s] %s %d %s | hp=%d blk=%d\n", names[e.kind], e.who ? e.who->name.c_str() : "-", e.amount, e.text.c_str(),
                   e.who ? e.who->hp : 0, e.who ? e.who->block : 0);
          }
        run.combat->events.clear();
      }
    }
    bool win = run.screen == Screen::Victory;
    wins += win;
    floorsTotal += run.floor;
    printf("seed %3d: %s floor %2d hp %d/%d deck %zu\n", s, win ? "WIN " : (run.screen == Screen::GameOver ? "LOSS" : "STUCK"),
           run.floor, run.player->hp, run.player->maxHp, run.deck.size());
    if (run.screen != Screen::GameOver && run.screen != Screen::Victory) return 1;
    // Let the finished root task unwind before the Run goes away.
    Scheduler::get().update(0.05);
  }
  printf("wins %d/%d, avg floor %.1f\n", wins, runs, (double)floorsTotal / runs);
  if (!potionsUsed.empty()) {
    printf("potions used (%zu kinds):", potionsUsed.size());
    for (auto& [id, n] : potionsUsed) printf(" %s:%d", id.c_str(), n);
    printf("\n");
  }
  if (getenv("SIM_ALLCARDS")) {
    int never = 0;
    for (auto& id : db::ironcladPool())
      for (const char* sfx : {"", "+"})
        if (!played.count(id + sfx)) { printf("never played: %s%s\n", id.c_str(), sfx); ++never; }
    printf("distinct cards played: %zu, never played: %d\n", played.size(), never);
  }
  return 0;
}
