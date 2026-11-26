// Shared state of the mod: one Archipelago session and one connection, used by the hooks and the menu.
#pragma once
#include <set>
#include <string>

class GJGameLevel;

#include "core/ap_core.hpp"

namespace apmod {

apgd::Session& session();
void connect();                    // (re)connect with the server/slot/password from the mod settings
void disconnect();
bool socketOpen();
void pump();                       // send queued packets, show queued notices; call after every change
std::string lastConnectionError();
std::set<int> savedCoins(GJGameLevel* level, int count);   // coin numbers the game has saved for a level
void openMenu();                   // the Archipelago panel (also opened from the main menu button)
void refreshMenu();

}  // namespace apmod
