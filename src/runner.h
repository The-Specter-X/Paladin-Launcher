#pragma once
#include "game.h"

gboolean runner_validate(const Game *game, GError **error);
char *runner_preflight_report(void);
GSubprocess *runner_start(const Game *game, const char *executable,
                          gboolean utility, GError **error);
/* NULL means success; also safe for a child terminated by a signal. */
char *runner_result(GSubprocess *process);
void runner_record_success(Game *game);
