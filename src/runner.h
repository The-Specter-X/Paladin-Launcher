#pragma once

#include "game.h"

/* Arguments use shell-style quoting; nothing is passed to a shell. */
GSubprocess *runner_start(const Game *game, const char *executable,
                          gboolean installer, GError **error);

