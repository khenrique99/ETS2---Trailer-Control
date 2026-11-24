#pragma once
#include <scssdk.h>

namespace tc::log {

// Recebe o logger do jogo (aparece no game.log.txt e no console do jogo).
void init(scs_log_t game_logger);
void shutdown();

void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void error(const char *fmt, ...);

} // namespace tc::log
