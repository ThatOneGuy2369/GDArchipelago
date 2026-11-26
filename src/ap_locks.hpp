// Padlock icons drawn in code (no image files): one themed lock per gauntlet (Fire = orange lock with a
// flame, Ice = pale-blue lock with a snowflake and icicles, ...) and a gold padlock for locked levels.
// Same designs as the companion app's Gauntlets tab.
#pragma once
#include <Geode/Geode.hpp>

#include <string>

namespace aplocks {

// theme: "fire", "ice", "poison", "shadow", "lava", "bonus", "chaos", "demon", "time", "crystal", or "level".
// The lock is centred on the returned node's position; size is its height in points.
cocos2d::CCNode* makeLock(const std::string& theme, float size);
cocos2d::CCNode* makeCheck(float size);          // green check mark for finished gauntlets
std::string themeForGauntlet(const std::string& shortName);   // "Fire" -> "fire"

}  // namespace aplocks
