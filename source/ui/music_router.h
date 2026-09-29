// U3: music and ambience routing (music_router.cpp). One call per frame from App::update;
// it watches the run's screen / room / act and calls audio:: only when the wanted track changes.
#pragma once

namespace sts {
struct Run;
}

namespace ui {

void routeMusic(const sts::Run& run);

}  // namespace ui
