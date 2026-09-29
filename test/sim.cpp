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

static int thrownFloor = -1;  // a Foul Potion throw is in flight (the potion has left the belt)

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  int runs = argc > 1 ? atoi(argv[1]) : 50;
  bool verbose = argc > 2;
  int wins = 0, floorsTotal = 0;
  int startSeed = getenv("SIM_SEED") ? atoi(getenv("SIM_SEED")) : 1;
  int endSeed = getenv("SIM_SEED") ? startSeed : runs;
  for (int s = startSeed; s <= endSeed; ++s) {
    std::vector<std::unique_ptr<Run>> keep;  // runs replaced by a load stay alive (their coroutines)
    keep.push_back(std::make_unique<Run>());
    Run* cur = keep.back().get();
    cur->start((uint64_t)s * 7919, getenv("SIM_CHAR") ? getenv("SIM_CHAR") : "Ironclad",
                getenv("SIM_ASC") ? atoi(getenv("SIM_ASC")) : 0);  // SIM_CHAR=Silent, SIM_ASC=10
    cur->freeMap = getenv("STS_FREE_MAP") != nullptr;
    // SIM_SAVELOAD=K: at floor K's map choice, save, load into a new run and carry on with it
    // (the result must match a run without SIM_SAVELOAD). Every save point also checks that
    // save -> load -> save gives the same text.
    std::string pendingSave;
    int saveFloor = getenv("SIM_SAVELOAD") ? atoi(getenv("SIM_SAVELOAD")) : -1;
    auto hook = [&](Run& r) {
      std::string a = r.save();
      Run check;
      if (!check.load(a) || check.save() != a) { printf("SAVE ROUNDTRIP MISMATCH floor %d\n", r.floor); }
      if (r.floor == saveFloor && pendingSave.empty()) pendingSave = a;
    };
    if (getenv("SIM_SAVELOAD")) cur->onSavePoint = hook;
    if (getenv("SIM_ALLCARDS")) {
      // Every pool card, plain and upgraded, so each one actually gets played.
      for (auto& id : db::character(cur->characterId).cardPool) {
        if (auto c = db::card(id)) cur->deck.push_back(std::move(c));
        if (auto c = db::card(id)) { c->upgrade(); cur->deck.push_back(std::move(c)); }
      }
      cur->player->hp = cur->player->maxHp = 999;
    }
    if (getenv("SIM_ENCHANT")) {
      // Every registered enchantment goes round the deck (amount 1-3), on each card it fits.
      const auto& ids = db::enchantmentIds();
      size_t k = 0;
      for (auto& c : cur->deck)
        for (size_t t = 0; t < ids.size() && !c->enchantment; ++t) {
          const std::string& id = ids[(k + t) % ids.size()];
          auto e = db::enchantment(id);
          if (e->canEnchant(*c)) { cur->enchantCard(c.get(), id, 1 + (int)(k % 3)); ++k; }
        }
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
      for (const auto* pool : {&db::sharedRelicPool(), &db::character(cur->characterId).relicPool, (const std::vector<std::string>*)&named})
        for (auto& id : *pool) {
          if (!db::relicRegistered(id) || cur->hasRelic(id)) continue;
          std::string only = getenv("SIM_ALLRELICS");  // "1" = all, else a comma list
          if (only != "1" && ("," + only + ",").find("," + id + ",") == std::string::npos) continue;
          auto rel = db::relic(id);
          rel->run = cur;
          cur->relics.push_back(std::move(rel));
        }
      cur->potions.resize(5);
    }
    thrownFloor = -1;
    Scheduler::get().spawn(cur->main());
    int frames = 0;
    while (cur->screen != Screen::GameOver && cur->screen != Screen::Victory && frames < 2000000) {
      Scheduler::get().update(0.05);
      ++frames;
      if (!pendingSave.empty() && saveFloor >= 0) {
        keep.push_back(std::make_unique<Run>());
        Run* next = keep.back().get();
        if (!next->load(pendingSave)) { printf("LOAD FAILED\n"); return 1; }
        next->freeMap = cur->freeMap;  // Run::start (called by load) resets the debug toggle
        next->onSavePoint = hook;
        cur = next;
        if (getenv("SIM_FIGHTS")) printf("  -- loaded at floor %d\n", cur->floor);
        saveFloor = -1;
        pendingSave.clear();
        Scheduler::get().spawn(cur->main());
        continue;
      }
      if (verbose && frames % 20000 == 0)
        printf("[frame %d] screen=%d floor=%d combat=%d phase=%d waiting=%d choice=%d idle=%d\n", frames, (int)cur->screen, cur->floor,
               cur->combat ? 1 : 0, cur->combat ? cur->combat->playerPhase : -1, cur->combat ? cur->combat->actions.waiting() : -1,
               cur->combat ? cur->combat->choice.active : -1, Scheduler::get().idle());
      // Deck choices can come up on any screen (relic pickups): answer them first.
      if (cur->deckChoice.active && cur->deckChoice.result.waiting() && cur->screen != Screen::Event && cur->screen != Screen::Shop) {
        std::vector<Card*> picked;
        for (int k = 0; k < cur->deckChoice.count && k < (int)cur->deckChoice.options.size(); ++k) picked.push_back(cur->deckChoice.options[k]);
        cur->deckChoice.result.fire(picked);
        continue;
      }
      if (cur->screen != Screen::Shop) thrownFloor = -1;
      switch (cur->screen) {
        case Screen::Map:
          if (cur->mapChoice.waiting()) {
            auto r = cur->reachableNodes();
            // Prefer rests when hurt, else fights.
            int pick = r[0];
            for (int i : r) if (cur->nodes[i].type == RoomType::Rest && cur->player->hp < 45) pick = i;
            if (getenv("SIM_MAPLOG") && cur->floor > 8 && cur->floor < 14) printf("  map pick %d type %d act %d cur %d\n", pick, (int)cur->nodes[pick].type, cur->actIndex, cur->currentNode);
            cur->mapChoice.fire(pick);
          }
          break;
        case Screen::Combat: {
          if (!cur->combat) break;
          Combat& c = *cur->combat;
          static Combat* lastC = nullptr;
          static int lastFloor = -1;  // a new fight (addresses can repeat)
          static int hpBefore = 0;
          static std::string encId;
          if (&c != lastC || cur->floor != lastFloor) {
            if (lastC && getenv("SIM_FIGHTS")) printf("  fight %-22s hp %d -> ...\n", encId.c_str(), hpBefore);
            lastC = &c; lastFloor = cur->floor; hpBefore = cur->player->hp; encId = c.encounterId;
            // SIM_ALLPOTIONS=1: every fight starts with the next three pool potions.
            if (getenv("SIM_ALLPOTIONS"))
              for (auto& slot : cur->potions) {
                auto pool = db::potionPool(cur->characterId);
                for (size_t k = 0; k < pool.size() && !slot; ++k) {
                  slot = db::potion(pool[potionCursor++ % pool.size()]);
                  if (slot) slot->run = cur;
                }
              }
          }
          if (getenv("SIM_FIGHTS") && c.over && c.turnNumber > 0) {
            static Combat* reported = nullptr;
            if (reported != &c) { reported = &c; printf("  fight %-22s hp %2d -> %2d  turns %d %s\n", encId.c_str(), hpBefore, cur->player->hp, c.turnNumber, c.won ? "" : "DIED"); }
          }
          if (c.choice.active && c.choice.result.waiting()) {
            c.choice.result.fire({c.choice.options[0]});
          } else if (c.playerPhase && c.actions.waiting()) {
            PlayerAction a;
            a.kind = PlayerAction::EndTurn;
            // Drink the first usable potion straight away.
            for (int k = 0; k < (int)cur->potions.size(); ++k)
              if (cur->canUsePotion(k) && !c.aliveEnemies().empty()) {
                a.kind = PlayerAction::UsePotion;
                a.potionSlot = k;
                if (cur->potions[k]->target == TargetType::AnyEnemy) a.target = c.aliveEnemies()[0];
                potionsUsed[cur->potions[k]->id]++;
                if (verbose) printf("  potion %s\n", cur->potions[k]->id.c_str());
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
            if (verbose && a.card) {
              const Card* k = a.card;
              std::string en = k->enchantment ? " [" + k->enchantment->id + ":" + std::to_string(k->enchantment->amount) + (k->enchantment->disabled() ? " off]" : "]") : "";
              printf("  play %s%s%s -> %s  E=%d\n", k->id.c_str(), en.c_str(), k->costWithLocalMods() != k->cost ? " (cost mod)" : "", a.target ? a.target->name.c_str() : "-", c.energy);
            }
            c.actions.fire(a);
          }
          break;
        }
        case Screen::Reward:
          // S14: claim every reward-list row top to bottom (a Card row opens the nested card
          // grid, where index 0 is picked as before), then Proceed once nothing is left.
          if (cur->rewardChoice.waiting()) cur->rewardChoice.fire(0);
          else if (cur->rewardListChoice.waiting()) {
            if (!cur->rewardItems.empty()) {
              auto& it = cur->rewardItems[0];
              // A full belt would otherwise leave this row stuck at index 0 forever (claiming
              // it is a no-op until there's room) -- discard one first, as the old PotionOffer
              // sim policy did.
              if (it.kind == Run::RewardKind::Potion && !cur->hasOpenPotionSlot()) cur->discardPotion(0);
              if (getenv("SIM_FIGHTS")) {
                const char* kind = it.kind == Run::RewardKind::Gold ? "gold" : it.kind == Run::RewardKind::Potion ? "potion"
                                  : it.kind == Run::RewardKind::Relic ? "relic" : "card";
                printf("  reward claim %s\n", kind);
              }
            }
            cur->rewardListChoice.fire(cur->rewardItems.empty() ? -1 : 0);
          }
          break;
        case Screen::Rest:
          if (cur->restChoice.waiting()) {
            // Heal when hurt or cannot smith, else smith; Lift/Dig when offered; leave once something was used.
            bool canSmith = false;
            for (auto& c : cur->deck) if (c->upgradable()) { canSmith = true; break; }
            int want = (cur->player->hp < 60 || !canSmith) ? 0 : 1;
            if (want == 1) for (int o : cur->restOptions) if (o >= 2) want = o;  // extras only when not healing
            bool used = std::find(cur->restUsed.begin(), cur->restUsed.end(), want) != cur->restUsed.end();
            cur->restChoice.fire(used || !cur->restUsed.empty() ? -1 : want);
          }
          break;
        case Screen::RestUpgrade:
          if (cur->upgradeChoice.waiting()) cur->upgradeChoice.fire(cur->upgradeOptions.empty() ? -1 : 0);
          break;
        case Screen::Event:
          if (cur->deckChoice.active && cur->deckChoice.result.waiting()) {
            std::vector<Card*> picked;
            for (int k = 0; k < cur->deckChoice.count && k < (int)cur->deckChoice.options.size(); ++k)
              picked.push_back(cur->deckChoice.options[k]);
            cur->deckChoice.result.fire(picked);
          } else if (cur->eventChoice.waiting() && cur->currentEvent) {
            // Rotate through the options so every one gets exercised over many runs.
            auto& opts = cur->currentEvent->options;
            int pick = 0;
            if (!opts.empty()) {
              pick = (s + cur->floor) % (int)opts.size();
              for (int k = 0; k < (int)opts.size() && opts[pick].locked(); ++k) pick = (pick + 1) % (int)opts.size();
            }
            if (getenv("SIM_FIGHTS")) printf("  event %s -> %d\n", cur->currentEvent->id.c_str(), pick);
            cur->eventChoice.fire(pick);
          }
          break;
        case Screen::Placeholder:
          if (cur->placeholderDone.waiting()) cur->placeholderDone.fire(0);
          break;
        case Screen::Shop:
          // Remove a Strike/Defend if the service is bought, else buy the first thing we can.
          if (cur->deckChoice.active && cur->deckChoice.result.waiting()) {
            Card* pick = cur->deckChoice.options[0];
            for (Card* k : cur->deckChoice.options) if (k->rarity == Rarity::Basic) { pick = k; break; }
            cur->deckChoice.result.fire({pick});
          } else if (cur->shopChoice.waiting()) {
            int pick = -1;
            // A Foul Potion on the belt is thrown at the (Fake)Merchant: the FakeMerchant fight starts.
            static const void* thrownRun = nullptr;  // the throw is in flight (the potion has left the belt)
            bool threw = thrownFloor == cur->floor && cur->currentEvent;
            for (int k = 0; k < (int)cur->potions.size() && !threw; ++k)
              if (cur->potions[k] && cur->potions[k]->id == "FoulPotion" && cur->currentEvent && cur->canUsePotion(k)) {
                if (getenv("SIM_FIGHTS")) printf("  foul potion thrown\n");
                Scheduler::get().spawn(cur->usePotion(k, nullptr));
                threw = true;
                thrownFloor = cur->floor;
              }
            if (threw) break;
            // Cap the buys per visit: with a refilling relic (The Courier) and endless gold the shop never empties.
            static int shopBuys = 0, shopFloor = -1;
            if (shopFloor != cur->floor) { shopFloor = cur->floor; shopBuys = 0; }
            for (int k = 0; k < (int)cur->shop.size() && pick < 0 && shopBuys < 40; ++k) {
              auto& it = cur->shop[k];
              if (it.stocked() && cur->shopPrice(it) <= cur->gold && (it.kind != ShopItem::PotionItem || cur->hasOpenPotionSlot())) pick = k;
            }
            if (pick >= 0) ++shopBuys;
            if (getenv("SIM_FIGHTS") && pick >= 0) printf("  shop buy %d (%d gold)\n", pick, cur->gold);
            cur->shopChoice.fire(pick);
          }
          break;
        case Screen::PotionOffer:
          if (cur->potionOfferChoice.waiting()) {
            if (!cur->hasOpenPotionSlot()) cur->discardPotion(0);
            cur->potionOfferChoice.fire(1);
          }
          break;
        case Screen::RelicOffer:
          if (cur->relicChoice.waiting()) {
            if (getenv("SIM_FIGHTS") && cur->relicOffer) printf("  relic %s\n", cur->relicOffer->id.c_str());
            cur->relicChoice.fire(1);
          }
          break;
        default:
          break;
      }
      if (cur->combat) {
        if (verbose)
          for (auto& e : cur->combat->events) {
            static const char* names[] = {"Damage", "Blocked", "Block", "Heal", "PowerUp", "PowerDown", "Death", "Exhaust", "Shuffle", "Banner", "Anim"};
            printf("    [%s] %s %d %s | hp=%d blk=%d\n", names[e.kind], e.who ? e.who->name.c_str() : "-", e.amount, e.text.c_str(),
                   e.who ? e.who->hp : 0, e.who ? e.who->block : 0);
          }
        cur->combat->events.clear();
      }
    }
    bool win = cur->screen == Screen::Victory;
    wins += win;
    floorsTotal += cur->floor;
    printf("seed %3d: %s floor %2d hp %d/%d deck %zu\n", s, win ? "WIN " : (cur->screen == Screen::GameOver ? "LOSS" : "STUCK"),
           cur->floor, cur->player->hp, cur->player->maxHp, cur->deck.size());
    if (cur->screen != Screen::GameOver && cur->screen != Screen::Victory) return 1;
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
    // PORT NOTE (X4.2): this used to hardcode db::ironcladPool(), so SIM_CHAR=<other> runs never
    // reported anything here even though their cards were in the deck and did get played. Use the
    // same character id the deck was built from (line ~26).
    for (auto& id : db::character(getenv("SIM_CHAR") ? getenv("SIM_CHAR") : "Ironclad").cardPool)
      for (const char* sfx : {"", "+"})
        if (!played.count(id + sfx)) { printf("never played: %s%s\n", id.c_str(), sfx); ++never; }
    printf("distinct cards played: %zu, never played: %d\n", played.size(), never);
  }
  return 0;
}
