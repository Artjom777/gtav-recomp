// gtav - ReXGlue Recompiled Project

#if GTAV_GAME_DLL
// The game code lives in gtav_game.dll, built by the installer from the player's own discs.
#include <rex/image_info.h>
extern "C" __declspec(dllimport) const rex::PPCImageInfo* gtav_game_image_config();
#define PPCImageConfig (*gtav_game_image_config())
#else
#include "generated/default/gtav_init.h"
#endif

#include "gtav_app.h"

REX_DEFINE_APP(gtav, GtavApp::Create)
