#!/bin/bash
# Records a batch of comparison scenes (original game + our preview) and builds the film-strip sheets.
#   run_scenes.sh BATCH [SCENE-REGEX] [--list|--dry|--ours-only|--orig-only|--no-newrun]
# Scenes: tools/ref/scenes/BATCH.txt (format in tools/ref/scenes/README.md).
# Output: ../ref-captures/BATCH/{index.md,run.log,sheets/<scene>.png,<scene>/} (game-derived, not in git).
# Needs the game running on the test profile (see game.sh) and a built preview (ours.sh).
exec python3 "$(dirname "$0")/run_scenes.py" "$@"
