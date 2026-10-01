#pragma once

#include "settings.hpp"

namespace portal {

enum class JoinState { Idle, Connecting, Connected, Failed };

bool init();
void poll();

// Saved settings shown on the settings page (password is never sent). May be nullptr.
void set_saved(const settings::Settings* saved);
// Returns true once for each validated settings submission.
bool take_submission(settings::Settings& out);
// message must be a string literal or otherwise outlive the next report.
void report_join(JoinState state, const char* message, const char* ip);

}  // namespace portal
