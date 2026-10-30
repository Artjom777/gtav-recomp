// gtav - entry point of the game DLL built on the player's machine (see gtav_pch.h).
#include "gtav_pch.h"

extern "C" __declspec(dllexport) const rex::PPCImageInfo* gtav_game_image_config() {
  return &PPCImageConfig;
}
