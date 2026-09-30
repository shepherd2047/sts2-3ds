// U3: which music and ambience play, from the run's state. Translated from the C#:
//  - NRunMusicController: per act one FMOD music event (ActModel.BgMusicOptions, picked with
//    Rng(seed, "bg_music") -> Run::actMusic) whose global "Progress" parameter selects the section
//    from the current room (GetTrack: Init, Enemy, Merchant, Rest, Unknown, Treasure, Elite,
//    CombatEnd, Elite2, MerchantEnd). UpdateMusic on act entry resets Progress to Init;
//    UpdateTrack runs on room entry, combat start and combat end; the map shown over a room keeps
//    that room's section (ToggleMerchantTrack: MerchantEnd when leaving the merchant).
//  - CombatManager.StartCombatInternal: a boss encounter with CustomBgm replaces the act music
//    (PlayCustomMusic); every boss event ends with the defeat stinger, and the act music only
//    comes back with the next act's UpdateMusic (so TheArchitect, after the last boss, is quiet).
//  - UpdateAmbience: the act's AmbientSfx, or the encounter's (TheInsatiableBoss); the ambience
//    events also hold the merchant and rest site beds, and Neow adds its own AmbientBgm loop.
//  - NMainMenu: menu music; CreatureCmd.Kill when every player is dead (also RunManager.WinRun,
//    which kills the players after recording the win): StopMusic + "event:/temp/sfx/game_over".
// The audio engine streams whole files (no FMOD timeline), so each FMOD section is one file of
// the event, chosen by name in the tables below (index.txt groups mix stems, intros and
// transitions). Intros, transition stingers, stems and boss phase parameters are not played.
// Debug: STS_MUSIC=<event|file id|path> / STS_AMB=<...> force a track (routing is then off for
// that bus), STS_MUSIC_LOG=1 prints every change.
#include "music_router.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../audio/audio.h"
#include "../core/game.h"

namespace ui {
namespace {

using sts::Screen;

// MusicProgressTrack (NRunMusicController).
enum class Section { Init, Enemy, Merchant, Rest, Unknown, Treasure, Elite, CombatEnd, Elite2, MerchantEnd };
const char* kSectionNames[] = {"Init", "Enemy", "Merchant", "Rest", "Unknown", "Treasure", "Elite", "CombatEnd", "Elite2", "MerchantEnd"};

// One act music event: the file standing for each section (names under music/, no extension).
// Init, Rest and Treasure have no file of their own and use the NonCombat bed, as does CombatEnd
// (Underdocks' "combatend" file is its NonCombat bed).
struct ActTrack {
  const char* event;
  const char* nonCombat;
  const char* combat;
  const char* unknown;
  const char* elite;
  const char* elite2;  // Elite2 (TriggerEliteSecondPhase), null = keep the elite track
  const char* merchant;
  const char* merchantMap;  // MerchantEnd
};
const ActTrack kActTracks[] = {
    {"event:/music/act1_a1_v1", "sts2_overgrowth_1-a_noncombat_v2", "sts2_overgrowth_1-a_combat_v2",
     "sts2_overgrowth_1-a_unknown_v2", "sts2_overgrowth_elite_v2", "sts2_overgrowth_elite_phase2_v2",
     "sts2_merchant_act1_v2", "sts2_merchant_map_v4"},
    {"event:/music/act1_a2_v2", "sts2_overgrowth_1-b_map_v2", "sts2_overgrowth_1-b_combat_v2",
     "sts2_overgrowth_1-b_unknown_v2", "sts2_overgrowth_elite_v2", "sts2_overgrowth_elite_phase2_v2",
     "sts2_merchant_act1_v2", "sts2_merchant_map_v4"},
    {"event:/music/act1_b1_v1", "sts2_underdocks_1b-a_combatend_v2", "sts2_underdocks_1b-a_combat_v2-2",
     "sts2_underdocks_1b-a_unknown_v2", "sts2_1b_elite_v3", nullptr, "sts2_merchant_act1_v2",
     "sts2_merchant_map_v4"},
    {"event:/music/act2_a1_v2", "sts2_hive_2-a_noncombat_v4", "sts2_hive_2-a_combat_v4", "sts2_hive_2-a_unknown_v4",
     "sts2_hive_elite_v1", nullptr, "sts2_merchant_act2_v2", "sts2_merchant_act2_map_v4"},
    {"event:/music/act2_a2_v2", "sts2_hive_2-a2_noncombat_v6", "sts2_hive_2-a2_combat_v5",
     "sts2_hive_2-a2_unknown_v5", "sts2_hive_elite_v1", nullptr, "sts2_merchant_act2_v2",
     "sts2_merchant_act2_map_v4"},
    {"event:/music/act3_a1_v1", "sts2_glory_3-a_v2_noncombat_v1", "sts2_glory_3-a_combat_v1",
     "sts2_glory_3-a_v2_unknown_v1", "sts2_glory_elite_scientists_v1", nullptr, "sts2_merchant_act3_v3",
     "sts2_merchant_act3_map_v3"},
    {"event:/music/act3_a2_v1", "sts2_glory_3-b_v2_noncombat", "sts2_glory_3-b_v2_combat", "sts2_glory_3-b_v2_unknown",
     "sts2_glory_elite_scientists_v1", nullptr, "sts2_merchant_act3_v3", "sts2_merchant_act3_map_v3"},
};

// EncounterModel.CustomBgm per boss encounter: the event's opening section.
struct BossTrack {
  const char* encounter;
  const char* file;
};
const BossTrack kBossTracks[] = {
    {"CeremonialBeastBoss", "sts2_a1_boss_ceremonialbeast_a1_v2"},    // act1_boss_ceremonial_beast
    {"TheKinBoss", "sts2_overgrowth_boss_kin_v2"},                    // act1_boss_the_kin
    {"VantomBoss", "sts2_a1_boss_vantom_a3_v4"},                      // act1_boss_vantom
    {"WaterfallGiantBoss", "sts2_underdocks_boss_waterfallgiant_v3_layer1_base"},  // act1_b_boss_waterfall_giant
    {"SoulFyshBoss", "sts2_underdocks_soulfysh_v4_full"},             // act1_b_boss_soul_fysh
    {"KaiserCrabBoss", "sts2_hive_boss_kaisercrab_v9_phase1_centre"}, // act2_boss_kaiser_crab
    {"KnowledgeDemonBoss", "sts2_a2_boss_knowledgedemon_v2_combatlayer"},  // act2_boss_knowledge_demon
    {"TheInsatiableBoss", "sts2_hive_boss_insaitable_v2_layer1"},     // act2_boss_the_insatiable
    {"QueenBoss", "sts2_a3_boss_thequeen_a1_v1"},                     // act3_boss_queen
    {"AeonglassBoss", "sts2_a3_boss_thequeen_a1_v1"},                 // act3_boss_queen
    {"TestSubjectBoss", "sts2_glory_testsubject_v1_a1"},              // act3_boss_test_subject
};
const char* kBossStinger = "sts2_overgrowth_boss_defeat_stinger_2_v2";  // last group of every boss event
const char* kMenu = "sts2_menu_v3_body";  // event:/music/menu_update's loop (menu/menu_1's body)
const char* kGameOver = "event:/temp/sfx/game_over";

// ActModel.AmbientSfx beds (the base layer of each ambience event; Underdocks uses act 3's).
const char* ambienceFor(const std::string& act) {
  if (act == "Overgrowth") return "sts2_act1_sfx_base_amb_v2";  // act1_ambience
  if (act == "Hive") return "sts2_act2_sfx_base_amb_v1";        // act2_ambience
  return "sts2_act3_sfx_amb_base_v1";                           // act3_ambience (Glory, Underdocks)
}
const char* kAmbMerchant = "sts2_act1_sfx_merchant_amb_v2";  // in every act's ambience event
const char* kAmbRest = "sts2_act1_sfx_restsite_amb_v2";      // campfire on (update_campfire_ambience 0)
const char* kAmbRestOut = "sts2_act1_sfx_restsite_amb_nofire_v1";  // TriggerCampfireGoingOut (update_campfire_ambience 1)
const char* kAmbNeow = "sts2_act1_sfx_neow_amb_v2";          // Neow.AmbientBgm (act1_neow)
const char* kAmbInsatiable = "sts2_act2_sfx_amb_insaitable_v2";  // TheInsatiableBoss.AmbientSfx

std::string musicPath(const char* name) { return std::string("music/") + name + ".adpcm"; }
std::string ambPath(const char* name) { return std::string("amb/") + name + ".adpcm"; }

// Crossfade lengths (seconds). FMOD's transitions are beat-synced; these approximate them.
constexpr float kSectionFade = 1.5f, kActFade = 2.f, kBossFade = 1.f, kStopFade = 0.6f, kAmbFade = 1.5f;

struct State {
  bool inRun = false;
  std::string actEvent;       // the run's current act music event ("" = none yet)
  Section section = Section::Init;
  const char* boss = nullptr;  // custom boss music playing
  bool bossDone = false;       // stinger played: silent until the next act
  bool ended = false;          // game over / victory handled
  std::string music, amb;      // what the router last asked for ("" = stopped)
};
State st;

bool logOn() {
  static int on = -1;
  if (on < 0) on = getenv("STS_MUSIC_LOG") ? 1 : 0;
  return on == 1;
}

void setMusic(const std::string& id, float fade, const char* why) {
  if (id == st.music) return;
  st.music = id;
  if (logOn()) printf("music: %s -> %s\n", why, id.empty() ? "(stop)" : id.c_str());
  fflush(stdout);
  if (getenv("STS_MUSIC")) return;
  if (id.empty()) audio::stopMusic(fade);
  else audio::playMusic(id, fade);
}

void setAmbience(const std::string& id, float fade) {
  if (id == st.amb) return;
  st.amb = id;
  if (logOn()) printf("ambience: %s\n", id.empty() ? "(stop)" : id.c_str());
  fflush(stdout);
  if (getenv("STS_AMB")) return;
  if (id.empty()) audio::stopAmbience(fade);
  else audio::playAmbience(id, fade);
}

const ActTrack* actTrack(const std::string& event) {
  for (const ActTrack& t : kActTracks)
    if (event == t.event) return &t;
  return nullptr;
}

const char* bossTrack(const std::string& encounter) {
  for (const BossTrack& b : kBossTracks)
    if (encounter == b.encounter) return b.file;
  return nullptr;
}

const char* sectionFile(const ActTrack& t, Section s) {
  switch (s) {
    case Section::Enemy: return t.combat;
    case Section::Unknown: return t.unknown;
    case Section::Elite: return t.elite;
    case Section::Elite2: return t.elite2 ? t.elite2 : t.elite;
    case Section::Merchant: return t.merchant;
    case Section::MerchantEnd: return t.merchantMap;
    default: return t.nonCombat;  // Init, Rest, Treasure, CombatEnd
  }
}

// NRunMusicController.GetTrack for the room on screen; the map (and pages shown over a room)
// keep the last room's section.
Section roomSection(const sts::Run& run, Section prev) {
  switch (run.screen) {
    case Screen::Combat:
      if (run.combat && !run.combat->over)
        return run.combat->isElite || run.combat->isBoss ? Section::Elite : Section::Enemy;
      return Section::CombatEnd;
    case Screen::Shop: return Section::Merchant;
    case Screen::Rest:
    case Screen::RestUpgrade: return Section::Rest;
    case Screen::Event:  // AncientEventModel -> Init; TheArchitect is a plain EventModel (its layout aside)
      return run.currentEvent && run.currentEvent->ancient && run.currentEvent->id != "TheArchitect" ? Section::Init
                                                                                                      : Section::Unknown;
    case Screen::Placeholder: return Section::Unknown;  // an event room without a ported event
    case Screen::RelicOffer: return run.relicOfferFromChest ? Section::Treasure : prev;
    case Screen::Map: return prev == Section::Merchant ? Section::MerchantEnd : prev;
    default: return prev;
  }
}

}  // namespace

void routeMusic(const sts::Run& run) {
  // STS_MUSIC / STS_AMB: play the forced track once and leave that bus alone.
  static bool forced = false;
  if (!forced) {
    forced = true;
    if (const char* m = getenv("STS_MUSIC")) audio::playMusic(m, 0);
    if (const char* a = getenv("STS_AMB")) audio::playAmbience(a, 0);
  }

  if (run.screen == Screen::Title) {
    st.inRun = false;
    st.actEvent.clear();
    st.boss = nullptr;
    st.bossDone = st.ended = false;
    st.section = Section::Init;
    setMusic(musicPath(kMenu), kSectionFade, "title");
    setAmbience("", kStopFade);
    return;
  }
  st.inRun = true;

  // Death, or the win (WinRun kills the players): StopMusic, then the game-over stinger.
  if (run.screen == Screen::GameOver || run.screen == Screen::Victory) {
    if (!st.ended) {
      st.ended = true;
      setMusic("", kStopFade, run.screen == Screen::Victory ? "victory" : "death");
      setAmbience("", kStopFade);
      if (!getenv("STS_MUSIC")) audio::playSfx(kGameOver);
    }
    return;
  }
  st.ended = false;

  // UpdateMusic: a new act (or run) starts its track at Init and drops any boss music.
  std::string event = run.actMusic();
  bool newAct = event != st.actEvent;
  if (newAct) {
    st.actEvent = event;
    st.section = Section::Init;
    st.boss = nullptr;
    st.bossDone = false;
  }
  st.section = roomSection(run, st.section);

  // PlayCustomMusic when a boss fight with its own music starts; its end plays the stinger.
  if (run.screen == Screen::Combat && run.combat && run.combat->isBoss) {
    const char* b = bossTrack(run.combat->encounterId);
    if (b && !run.combat->over) {
      st.boss = b;
      st.bossDone = false;
    } else if (b && run.combat->over && st.boss && !st.bossDone && run.combat->won) {
      st.bossDone = true;
      setMusic("", kStopFade, "boss defeated");
      if (!getenv("STS_MUSIC")) audio::playSfx(musicPath(kBossStinger));
    }
  }

  const ActTrack* t = actTrack(event);
  if (st.bossDone) {
    // silent until the next act's UpdateMusic
  } else if (st.boss) {
    setMusic(musicPath(st.boss), kBossFade, "boss");
  } else if (t) {
    setMusic(musicPath(sectionFile(*t, st.section)), newAct ? kActFade : kSectionFade, kSectionNames[(int)st.section]);
  } else {
    setMusic("", kStopFade, "no act music");
  }

  // UpdateAmbience: the act's bed, the encounter's own, or the room's (merchant, campfire, Neow).
  const char* amb = ambienceFor(run.act().name);
  if (run.screen == Screen::Combat && run.combat && run.combat->encounterId == "TheInsatiableBoss")
    amb = kAmbInsatiable;
  else if (st.section == Section::Merchant || st.section == Section::MerchantEnd)
    amb = kAmbMerchant;
  else if (st.section == Section::Rest) {  // the map over the rest site keeps the campfire
    // NRestSiteRoom.ExtinguishFireIfAble: the fire goes out once no option is left to take.
    bool out = !run.restUsed.empty();
    if (out && run.hasRelic("MiniatureTent"))
      for (int o : run.restOptions)
        if (std::find(run.restUsed.begin(), run.restUsed.end(), o) == run.restUsed.end()) out = false;
    amb = out ? kAmbRestOut : kAmbRest;
  }
  else if (run.screen == Screen::Event && run.currentEvent && run.currentEvent->id == "Neow")
    amb = kAmbNeow;
  setAmbience(ambPath(amb), kAmbFade);
}

}  // namespace ui
