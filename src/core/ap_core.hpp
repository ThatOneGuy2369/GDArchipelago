// Archipelago logic for the Geometry Dash mod: slot config, unlock rules, traps, save data and the
// network protocol. Plain C++17 with no Geode or Windows code, so it is unit-tested on its own.
// The Geode layer feeds it websocket text and game events, and acts on what it returns.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json.hpp"

namespace apgd {

constexpr const char* GAME = "Geometry Dash";
constexpr int CLIENT_GOAL = 30;

std::string modeLabel(const std::string& mode);               // "ufo" -> "UFO"
std::string lower(std::string s);

struct Level {
    int idx = 0;
    std::string name;
    std::vector<std::string> modes;     // non-cube gamemodes the level uses
    int gate = 0;                       // secret coins needed first (with coin locks)
    std::string tier = "main";          // main | easy_demon | medium_demon | hard_demon | gauntlet
    std::string kind = "level";         // level | gauntlet
    int keys = 0;                       // Gauntlet Keys needed
    bool needsUnlock = false;
    int64_t locId = 0;
    std::vector<int64_t> coinIds;
};

struct ItemInfo {
    std::string kind;                   // mode | unlock | key | trap
    std::string mode, name, text;
    int idx = -1;
};

struct SlotConfig {
    int goalCount = 0;
    bool includeCoins = true;
    bool coinLocks = false;
    bool levelUnlocks = true;
    int keysRequired = 0;
    std::vector<Level> levels;
    std::map<int64_t, ItemInfo> items;

    static SlotConfig fromJson(const Json& slotData);         // throws std::runtime_error when unusable
    const Level* level(int idx) const;
};

// What the player owns, worked out from the received item list.
struct Held {
    std::set<std::string> modes;
    std::set<int> unlocks;
    std::set<int> keyIdxs;
    int keys = 0;
    int orbs = 0;
    static Held from(const SlotConfig& cfg, const std::vector<int64_t>& received);
};

// Everything saved per (seed, slot).
struct RunState {
    std::set<int> completed;            // level idx
    std::set<std::string> coins;        // "idx:n"
    bool goalSent = false;
    size_t itemsSeen = 0;               // received items already processed for traps
    int noPractice = 0, mute = 0, randomLevel = 0, handsUp = 0;   // traps waiting for the next attempt
    int forcedLevel = -1;               // Random Level Trap: must attempt this level next
    int handsUpLevel = -1;              // Hands Up Trap: level being attempted under the trap
    int blockedLevel = -1;              // Hands Up Trap: can't re-enter until another level is beaten

    Json toJson() const;
    static RunState fromJson(const Json& j);
};

struct Notice {
    enum class Kind { Item, Trap, Info, Error, Goal };
    Kind kind;
    std::string text;
};

// Traps that apply to the attempt the player just started.
struct AttemptRules {
    bool noPractice = false;
    bool mute = false;
};

class Session {
public:
    // The Geode layer plugs these in: where to keep save data, and a random number source for traps.
    std::function<std::string(const std::string& key)> loadState;
    std::function<void(const std::string& key, const std::string& text)> saveState;
    std::function<uint32_t()> random;

    std::string slotName, password, seed, uuid = "geometry-dash-mod";
    bool connected = false;
    bool hasConfig = false;
    SlotConfig cfg;
    RunState state;
    std::vector<int64_t> received;
    std::set<int64_t> checked;
    std::map<int, std::string> players;
    int mySlot = -1;
    std::string lastItem;

    // --- network -------------------------------------------------------------------------------
    void onSocketOpen();                                      // resets per-connection state
    void onSocketClosed();
    void onMessage(const std::string& text);                  // one websocket message (a JSON array)
    std::vector<std::string> takeOutgoing();                  // messages to send, in order
    std::vector<Notice> takeNotices();

    // --- game events ---------------------------------------------------------------------------
    const Level* matchMainLevel(int gdLevelId) const;         // official levels: GD id == idx (1..22)
    const Level* matchOnlineLevel(const std::string& name, int stars) const;   // rated demons by name
    std::string playBlock(int idx) const;                     // "" = may enter, else why not (locks + traps)
    std::string lockReason(const Level& lv) const;            // items / keys / coins only
    AttemptRules attemptStart(int idx);                       // entering a level
    void attemptEnd(int idx, bool completed);                 // leaving it (or finishing it)
    bool modeAllowed(int idx, const std::string& mode) const; // touching a gamemode portal
    bool reportLevel(int idx, std::string* why = nullptr);    // beat a level
    bool reportCoin(int idx, int n);                          // collected coin n (1-3) and finished
    bool reportGauntlet(int idx, std::string* why = nullptr); // honor: finished every level of a gauntlet

    // --- display -------------------------------------------------------------------------------
    Held held() const { return Held::from(cfg, received); }
    int coinsTotal() const { return static_cast<int>(state.coins.size()); }
    int levelsDone() const;
    bool goalComplete() const;
    std::string statusLine() const;
    std::string itemLabel(int64_t itemId) const;

private:
    void send(Json packet);
    void notify(Notice::Kind kind, std::string text);
    void evaluate();
    void save();
    void processNewItems();
    void pickForcedLevel();
    std::string stateKey() const { return seed + "__" + slotName; }

    std::vector<std::string> m_out;
    std::vector<Notice> m_notices;
};

}  // namespace apgd
