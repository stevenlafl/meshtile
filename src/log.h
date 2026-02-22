#pragma once
#include <cstdio>
#include <cstdarg>

namespace meshtile {

enum class LogLevel { Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
void log_msg(LogLevel level, const char* fmt, ...);

} // namespace meshtile

#define LOG_DEBUG(...) ::meshtile::log_msg(::meshtile::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::meshtile::log_msg(::meshtile::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::meshtile::log_msg(::meshtile::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::meshtile::log_msg(::meshtile::LogLevel::Error, __VA_ARGS__)
