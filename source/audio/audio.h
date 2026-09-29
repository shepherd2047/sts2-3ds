// Audio engine (U2): music and ambience streamed from the SD card, SFX voices.
// Files are tools/audio_extract.py's "S2AD" DSP-ADPCM files plus index.txt, read from
// sdmc:/3ds/sts2-3ds/audio/ on the 3DS and from ./audio/ (or $STS_AUDIO_DIR) in the preview.
// Missing audio is harmless: init() returns false and every call is a cheap no-op.
// Backends: platform_3ds/audio_3ds.cpp (ndsp), platform_sdl/audio_sdl.cpp (SDL mixer).
#pragma once
#include <string>

namespace audio {

enum Bus { MASTER = 0, MUSIC, SFX, AMBIENCE, kBusCount };

// After gfx::init (reads debug env vars; the SDL backend relies on SDL being up).
bool init();
void shutdown();  // before gfx::shutdown
bool available();  // index loaded and the backend is running
void update(double dt);  // once per frame: fades

// A sound effect: an index event name ("event:/sfx/block_gain", "blunt_attack.mp3") or a file
// id. Each of the event's groups plays one randomly chosen alternative, all at once.
// Returns false when nothing could be played (unknown name, missing file, no audio).
bool playSfx(const std::string& eventOrId);

// Music / ambience loop whole files (the data has no loop points). id = file id or event name
// (an event plays the first file of its first group). Replaying the current id does nothing;
// another id crossfades over `fade` seconds.
bool playMusic(const std::string& id, float fade = 1.f);
void stopMusic(float fade = 1.f);
bool playAmbience(const std::string& id, float fade = 1.f);
void stopAmbience(float fade = 1.f);
const std::string& currentMusic();  // "" when none

// Bus volumes 0..1 (default 1); music/ambience follow immediately, SFX from their next play.
void setVolume(Bus bus, float v);
float volume(Bus bus);

}  // namespace audio
