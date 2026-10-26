// The player's game folder: which files are there, which version they are, and whether the port runs the
// shareware program in its registered mode. The port always runs the recompiled shareware v2.0 program;
// registered files only switch the mode (docs/decisions.md 2026-10-25).
#pragma once
#include <string>
#include <vector>

#include "machine_api.h"

namespace host {

enum class Edition { Shareware, Registered, Unusable };

struct GameFiles {
    Edition edition = Edition::Unusable;
    std::string message;   // for the player: what was found, or what is missing / not supported
};

// Checks data_dir; for Edition::Registered adds the registration-state poke to `o` (the flag the program
// sets after its own BALLOONC.OV2 check, DGROUP 010A = 1 at 0824:AEFE).
GameFiles check_game_files(const std::string& data_dir, rc::MachineOptions& o);

std::string sha256_file(const std::string& path);  // lowercase hex, empty if unreadable

}  // namespace host
