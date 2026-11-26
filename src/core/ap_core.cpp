#include "ap_core.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace apgd {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string modeLabel(const std::string& mode) {
    static const std::map<std::string, std::string> labels = {
        {"cube", "Cube"}, {"ship", "Ship"}, {"ball", "Ball"}, {"ufo", "UFO"}, {"wave", "Wave"},
        {"robot", "Robot"}, {"spider", "Spider"}, {"swing", "Swing"}};
    auto it = labels.find(mode);
    return it == labels.end() ? mode : it->second;
}

// ---------------------------------------------------------------- config
SlotConfig SlotConfig::fromJson(const Json& sd) {
    if (!sd.isObject() || !sd["levels"].isArray() || !sd.contains("goal_count"))
        throw std::runtime_error("slot data is not from the Geometry Dash world");
    SlotConfig c;
    c.goalCount = static_cast<int>(sd["goal_count"].asInt());
    c.includeCoins = sd["include_coins"].asBool(true);
    c.coinLocks = sd["coin_locks"].asBool(false);
    c.levelUnlocks = sd["level_unlocks"].asBool(true);
    c.keysRequired = static_cast<int>(sd["keys_required"].asInt(0));
    for (auto& j : sd["levels"].items()) {
        Level lv;
        lv.idx = static_cast<int>(j["idx"].asInt());
        lv.name = j["name"].asString();
        for (auto& m : j["modes"].items()) lv.modes.push_back(m.asString());
        lv.gate = static_cast<int>(j["gate"].asInt(0));
        lv.tier = j["tier"].asString("main");
        lv.kind = j["kind"].asString("level");
        lv.keys = static_cast<int>(j["keys"].asInt(0));
        lv.needsUnlock = j["needs_unlock"].asBool(false);
        lv.locId = j["loc_id"].asInt();
        for (auto& id : j["coin_ids"].items()) lv.coinIds.push_back(id.asInt());
        c.levels.push_back(std::move(lv));
    }
    for (auto& kv : sd["items"].members()) {
        ItemInfo it;
        it.kind = kv.second["kind"].asString();
        it.mode = kv.second["mode"].asString();
        it.name = kv.second["name"].asString();
        it.text = kv.second["text"].asString();
        it.idx = static_cast<int>(kv.second["idx"].asInt(-1));
        try {
            c.items[std::stoll(kv.first)] = it;
        } catch (...) {
        }
    }
    return c;
}

const Level* SlotConfig::level(int idx) const {
    for (auto& lv : levels)
        if (lv.idx == idx) return &lv;
    return nullptr;
}

Held Held::from(const SlotConfig& cfg, const std::vector<int64_t>& received) {
    Held h;
    for (auto id : received) {
        auto it = cfg.items.find(id);
        if (it == cfg.items.end()) { ++h.orbs; continue; }
        const auto& info = it->second;
        if (info.kind == "mode") h.modes.insert(info.mode);
        else if (info.kind == "unlock") h.unlocks.insert(info.idx);
        else if (info.kind == "key") { ++h.keys; h.keyIdxs.insert(info.idx); }
    }
    return h;
}

// ---------------------------------------------------------------- save data
Json RunState::toJson() const {
    Json done = Json::array(), coinList = Json::array();
    for (int i : completed) done.push(i);
    for (auto& c : coins) coinList.push(c);
    Json j = Json::object();
    j.set("completed", done).set("coins", coinList).set("goal_sent", goalSent)
        .set("items_seen", static_cast<int64_t>(itemsSeen))
        .set("no_practice", noPractice).set("mute", mute).set("random_level", randomLevel).set("hands_up", handsUp)
        .set("forced_level", forcedLevel).set("hands_up_level", handsUpLevel).set("blocked_level", blockedLevel);
    Json beaten = Json::object(), sizes = Json::object();
    for (auto& kv : gauntletBeaten) {
        Json ids = Json::array();
        for (auto id : kv.second) ids.push(id);
        beaten.set(std::to_string(kv.first), ids);
    }
    for (auto& kv : gauntletSize) sizes.set(std::to_string(kv.first), kv.second);
    j.set("gauntlet_beaten", beaten).set("gauntlet_size", sizes);
    return j;
}

RunState RunState::fromJson(const Json& j) {
    RunState s;
    for (auto& v : j["completed"].items()) s.completed.insert(static_cast<int>(v.asInt()));
    for (auto& v : j["coins"].items()) if (v.isString()) s.coins.insert(v.asString());
    auto clampInt = [&](const char* k, int def) { return static_cast<int>(std::max<int64_t>(j[k].asInt(def), -1)); };
    s.goalSent = j["goal_sent"].asBool(false);
    s.itemsSeen = static_cast<size_t>(std::max<int64_t>(j["items_seen"].asInt(0), 0));
    s.noPractice = std::max(clampInt("no_practice", 0), 0);
    s.mute = std::max(clampInt("mute", 0), 0);
    s.randomLevel = std::max(clampInt("random_level", 0), 0);
    s.handsUp = std::max(clampInt("hands_up", 0), 0);
    s.forcedLevel = clampInt("forced_level", -1);
    s.handsUpLevel = clampInt("hands_up_level", -1);
    s.blockedLevel = clampInt("blocked_level", -1);
    auto keyInt = [](const std::string& k) { try { return std::stoi(k); } catch (...) { return -1; } };
    for (auto& kv : j["gauntlet_beaten"].members()) {
        int g = keyInt(kv.first);
        if (g < 0) continue;
        for (auto& id : kv.second.items()) if (id.isNumber()) s.gauntletBeaten[g].insert(id.asInt());
    }
    for (auto& kv : j["gauntlet_size"].members()) {
        int g = keyInt(kv.first);
        if (g >= 0 && kv.second.isNumber()) s.gauntletSize[g] = static_cast<int>(kv.second.asInt());
    }
    return s;
}

// ---------------------------------------------------------------- helpers
void Session::send(Json packet) {
    Json arr = Json::array();
    arr.push(std::move(packet));
    m_out.push_back(arr.dump());
}

void Session::notify(Notice::Kind kind, std::string text) { m_notices.push_back({kind, std::move(text)}); }

std::vector<std::string> Session::takeOutgoing() { auto out = std::move(m_out); m_out.clear(); return out; }
std::vector<Notice> Session::takeNotices() { auto out = std::move(m_notices); m_notices.clear(); return out; }

void Session::save() {
    if (hasConfig && saveState) saveState(stateKey(), state.toJson().dump());
}

std::string Session::itemLabel(int64_t itemId) const {
    auto it = cfg.items.find(itemId);
    if (it == cfg.items.end()) return "Orb";
    const auto& info = it->second;
    if (info.kind == "mode") return modeLabel(info.mode) + " Gamemode";
    if (info.kind == "unlock" || info.kind == "key") {
        const Level* lv = cfg.level(info.idx);
        std::string name = lv ? lv->name : "level " + std::to_string(info.idx);
        if (info.kind == "unlock") return name + " Unlock";
        auto pos = name.find(" Gauntlet");
        if (pos != std::string::npos) name = name.substr(0, pos);
        return name + " Gauntlet Key";
    }
    if (info.kind == "trap") return info.name;
    return "Orb";
}

// ---------------------------------------------------------------- rules
std::string Session::lockReason(const Level& lv) const {
    Held h = held();
    std::vector<std::string> missing;
    for (auto& m : lv.modes)
        if (!h.modes.count(m)) missing.push_back(modeLabel(m));
    if (!missing.empty()) {
        std::string out = "needs ";
        for (size_t i = 0; i < missing.size(); ++i) out += (i ? ", " : "") + missing[i];
        return out + " Gamemode";
    }
    if (lv.needsUnlock && !h.unlocks.count(lv.idx)) return "needs " + lv.name + " Unlock";
    if (lv.keys > h.keys)
        return "needs " + std::to_string(lv.keys) + " Gauntlet Keys (" + std::to_string(h.keys) + " held)";
    if (cfg.coinLocks && cfg.includeCoins && lv.gate && coinsTotal() < lv.gate)
        return "needs " + std::to_string(lv.gate) + " secret coins (" + std::to_string(coinsTotal()) + " reported)";
    return "";
}

std::string Session::playBlock(int idx) const {
    const Level* lv = cfg.level(idx);
    if (!hasConfig || !lv) return "";
    if (state.completed.count(idx)) return "";                    // finished levels can always be replayed
    std::string why = lockReason(*lv);
    if (!why.empty()) return why;
    if (state.blockedLevel == idx) return "Hands Up Trap: beat another level before trying this one again";
    if (state.forcedLevel >= 0 && state.forcedLevel != idx) {
        const Level* f = cfg.level(state.forcedLevel);
        return "Random Level Trap: play " + (f ? f->name : std::string("the chosen level")) + " first";
    }
    return "";
}

bool Session::modeAllowed(int idx, const std::string& mode) const {
    if (!hasConfig || !cfg.level(idx) || mode == "cube") return true;
    return held().modes.count(mode) > 0;
}

const Level* Session::matchMainLevel(int gdLevelId) const {
    const Level* lv = cfg.level(gdLevelId);
    return lv && lv->tier == "main" && lv->kind == "level" ? lv : nullptr;
}

const Level* Session::matchOnlineLevel(const std::string& name, int stars) const {
    if (stars < 10) return nullptr;                                // the real demons are rated; copies rarely are
    std::string want = lower(name);
    for (auto& lv : cfg.levels)
        if (lv.kind == "level" && lv.tier != "main" && lower(lv.name) == want) return &lv;
    return nullptr;
}

int Session::levelsDone() const {
    int n = 0;
    for (int i : state.completed) {
        const Level* lv = cfg.level(i);
        if (lv && lv->kind == "level") ++n;
    }
    return n;
}

bool Session::goalComplete() const {
    return hasConfig && levelsDone() >= cfg.goalCount && held().keys >= cfg.keysRequired;
}

std::string Session::statusLine() const {
    if (!connected || !hasConfig) return connected ? "Connected, waiting for slot data" : "Not connected";
    std::string s = "Connected as " + slotName + "   Goal " + std::to_string(levelsDone()) + "/" +
                    std::to_string(cfg.goalCount) + " levels";
    if (cfg.includeCoins) s += "   Coins " + std::to_string(coinsTotal());
    if (cfg.keysRequired) s += "   Keys " + std::to_string(held().keys) + "/" + std::to_string(cfg.keysRequired);
    if (goalComplete()) s += "   GOAL COMPLETE";
    return s;
}

// ---------------------------------------------------------------- traps
void Session::pickForcedLevel() {
    if (state.randomLevel <= 0 || state.forcedLevel >= 0 || !hasConfig) return;
    std::vector<int> options;
    for (auto& lv : cfg.levels)
        if (lv.kind == "level" && !state.completed.count(lv.idx) && lockReason(lv).empty() &&
            state.blockedLevel != lv.idx)
            options.push_back(lv.idx);
    if (options.empty()) {                                         // nothing to pick: the trap fizzles
        state.randomLevel = 0;
        return;
    }
    uint32_t r = random ? random() : 0;
    state.forcedLevel = options[r % options.size()];
    const Level* lv = cfg.level(state.forcedLevel);
    notify(Notice::Kind::Trap, "Random Level Trap: your next level is " + (lv ? lv->name : std::string("?")));
}

AttemptRules Session::attemptStart(int idx) {
    AttemptRules rules;
    if (!hasConfig || !cfg.level(idx)) return rules;
    if (state.noPractice > 0) { --state.noPractice; rules.noPractice = true; }
    if (state.mute > 0) { --state.mute; rules.mute = true; }
    if (state.forcedLevel == idx) {
        state.forcedLevel = -1;
        state.randomLevel = std::max(state.randomLevel - 1, 0);
    }
    if (state.handsUp > 0 && state.handsUpLevel < 0) {
        --state.handsUp;
        state.handsUpLevel = idx;
    }
    pickForcedLevel();
    save();
    return rules;
}

void Session::attemptEnd(int idx, bool completed) {
    if (!hasConfig) return;
    if (state.handsUpLevel == idx) {
        if (!completed) state.blockedLevel = idx;
        state.handsUpLevel = -1;
    }
    if (completed && state.blockedLevel >= 0 && state.blockedLevel != idx) state.blockedLevel = -1;
    save();
}

// ---------------------------------------------------------------- reports
void Session::evaluate() {
    if (goalComplete() && !state.goalSent && connected) {
        Json p = Json::object();
        p.set("cmd", "StatusUpdate").set("status", CLIENT_GOAL);
        send(p);
        state.goalSent = true;
        notify(Notice::Kind::Goal, "GOAL COMPLETE!");
    }
    save();
}

static Json locationChecks(const std::vector<int64_t>& ids) {
    Json arr = Json::array();
    for (auto id : ids) arr.push(id);
    Json p = Json::object();
    p.set("cmd", "LocationChecks").set("locations", arr);
    return p;
}

bool Session::reportLevel(int idx, std::string* why) {
    const Level* lv = hasConfig ? cfg.level(idx) : nullptr;
    if (!lv) { if (why) *why = "not an Archipelago level"; return false; }
    if (state.completed.count(idx)) { if (why) *why = "already counted"; return false; }
    std::string reason = lockReason(*lv);
    if (!reason.empty()) { if (why) *why = reason; return false; }
    state.completed.insert(idx);
    if (connected && !checked.count(lv->locId)) send(locationChecks({lv->locId}));
    checked.insert(lv->locId);
    notify(Notice::Kind::Info, "Check sent: " + lv->name + " (" + std::to_string(levelsDone()) + "/" +
                                   std::to_string(cfg.goalCount) + " toward goal)");
    pickForcedLevel();
    evaluate();
    return true;
}

bool Session::reportGauntlet(int idx, std::string* why) {
    const Level* lv = hasConfig ? cfg.level(idx) : nullptr;
    if (!lv || lv->kind != "gauntlet") { if (why) *why = "not a gauntlet"; return false; }
    return reportLevel(idx, why);
}

int Session::gauntletIdx(const std::string& shortName) const {
    std::string want = lower(shortName + " Gauntlet");
    for (auto& lv : cfg.levels)
        if (lv.kind == "gauntlet" && lower(lv.name) == want) return lv.idx;
    return -1;
}

std::string Session::gauntletBlock(int gIdx) const {
    const Level* g = hasConfig ? cfg.level(gIdx) : nullptr;
    if (!g || state.completed.count(gIdx)) return "";
    return lockReason(*g);
}

int Session::gauntletOpened(int gIdx, const std::vector<int64_t>& ids) {
    const Level* g = hasConfig ? cfg.level(gIdx) : nullptr;
    if (!g || g->kind != "gauntlet" || ids.empty()) return -1;
    state.gauntletSize[gIdx] = static_cast<int>(ids.size());
    auto& beaten = state.gauntletBeaten[gIdx];
    int n = 0;
    for (auto id : ids) n += beaten.count(id) ? 1 : 0;
    if (n == static_cast<int>(ids.size()) && !state.completed.count(gIdx) && lockReason(*g).empty())
        reportLevel(gIdx);
    save();
    return n;
}

int Session::gauntletLevelBeaten(int gIdx, int64_t levelId, const std::vector<int64_t>& ids) {
    const Level* g = hasConfig ? cfg.level(gIdx) : nullptr;
    if (!g || g->kind != "gauntlet") return -1;
    if (!state.completed.count(gIdx) && !lockReason(*g).empty()) return -1;     // locked: doesn't count
    state.gauntletBeaten[gIdx].insert(levelId);
    int n = gauntletOpened(gIdx, ids);
    if (n >= 0 && !state.completed.count(gIdx))
        notify(Notice::Kind::Info, g->name + ": " + std::to_string(n) + "/" + std::to_string(ids.size()) + " levels beaten");
    return n;
}

bool Session::reportCoin(int idx, int n) {
    const Level* lv = hasConfig ? cfg.level(idx) : nullptr;
    if (!lv || !cfg.includeCoins || n < 1 || n > static_cast<int>(lv->coinIds.size())) return false;
    std::string key = std::to_string(idx) + ":" + std::to_string(n);
    if (state.coins.count(key) || !state.completed.count(idx)) return false;
    state.coins.insert(key);
    int64_t loc = lv->coinIds[n - 1];
    if (connected && !checked.count(loc)) send(locationChecks({loc}));
    checked.insert(loc);
    notify(Notice::Kind::Info, "Coin " + std::to_string(n) + " of " + lv->name + " counted (" +
                                   std::to_string(coinsTotal()) + " total)");
    evaluate();
    return true;
}

// ---------------------------------------------------------------- protocol
void Session::onSocketOpen() {
    connected = false;
    received.clear();
}

void Session::onSocketClosed() { connected = false; }

void Session::processNewItems() {
    // Everything past itemsSeen is new to this save: announce it and arm traps. Using the saved count
    // (not the packet index) means a reconnect/resync never re-fires old traps or popups.
    if (state.itemsSeen > received.size()) state.itemsSeen = received.size();   // new seed or reset server
    size_t fresh = received.size() - state.itemsSeen;
    for (size_t i = state.itemsSeen; i < received.size(); ++i) {
        auto it = cfg.items.find(received[i]);
        if (it != cfg.items.end() && it->second.kind == "trap") {
            const auto& n = it->second.name;
            if (n == "No Practice Mode Trap") ++state.noPractice;
            else if (n == "Mute Trap") ++state.mute;
            else if (n == "Random Level Trap") ++state.randomLevel;
            else if (n == "Hands Up Trap") ++state.handsUp;
            if (fresh <= 5) notify(Notice::Kind::Trap, it->second.name + ": " + it->second.text);
        } else if (fresh <= 5) {
            notify(Notice::Kind::Item, itemLabel(received[i]));
        }
        lastItem = itemLabel(received[i]);
    }
    if (fresh > 5) notify(Notice::Kind::Item, "Received " + std::to_string(fresh) + " items");
    state.itemsSeen = received.size();
    pickForcedLevel();
}

void Session::onMessage(const std::string& text) {
    Json packets;
    try {
        packets = Json::parse(text);
    } catch (const std::exception& e) {
        notify(Notice::Kind::Error, std::string("Bad message from server: ") + e.what());
        return;
    }
    for (auto& pkt : packets.items()) {
        std::string cmd = pkt["cmd"].asString();
        if (cmd == "RoomInfo") {
            seed = pkt["seed_name"].asString();
            Json version = Json::object();
            version.set("major", 0).set("minor", 6).set("build", 8).set("class", "Version");
            Json p = Json::object();
            p.set("cmd", "Connect").set("password", password).set("game", GAME).set("name", slotName)
                .set("uuid", uuid).set("version", version).set("items_handling", 7).set("tags", Json::array())
                .set("slot_data", true);
            send(p);
        } else if (cmd == "ConnectionRefused") {
            std::string errs;
            for (auto& e : pkt["errors"].items()) errs += (errs.empty() ? "" : ", ") + e.asString();
            notify(Notice::Kind::Error, "Connection refused: " + (errs.empty() ? std::string("unknown error") : errs));
            connected = false;
        } else if (cmd == "Connected") {
            try {
                cfg = SlotConfig::fromJson(pkt["slot_data"]);
            } catch (const std::exception& e) {
                notify(Notice::Kind::Error, e.what());
                continue;
            }
            hasConfig = true;
            connected = true;
            mySlot = static_cast<int>(pkt["slot"].asInt(-1));
            players.clear();
            for (auto& pl : pkt["players"].items()) {
                std::string alias = pl["alias"].asString();
                players[static_cast<int>(pl["slot"].asInt())] = alias.empty() ? pl["name"].asString() : alias;
            }
            state = RunState();
            if (loadState) {
                std::string saved = loadState(stateKey());
                if (!saved.empty()) {
                    try { state = RunState::fromJson(Json::parse(saved)); } catch (...) {}
                }
            }
            checked.clear();
            for (auto& id : pkt["checked_locations"].items()) checked.insert(id.asInt());
            // The server is the truth for what was sent: restore lost saves, then resend what never arrived.
            std::vector<int64_t> resend;
            for (auto& lv : cfg.levels) {
                if (checked.count(lv.locId)) state.completed.insert(lv.idx);
                else if (state.completed.count(lv.idx)) resend.push_back(lv.locId);
                for (size_t n = 0; n < lv.coinIds.size(); ++n) {
                    std::string key = std::to_string(lv.idx) + ":" + std::to_string(n + 1);
                    if (checked.count(lv.coinIds[n])) state.coins.insert(key);
                    else if (state.coins.count(key)) resend.push_back(lv.coinIds[n]);
                }
            }
            if (!resend.empty()) {
                send(locationChecks(resend));
                for (auto id : resend) checked.insert(id);
            }
            notify(Notice::Kind::Info, "Connected as " + slotName + ". Goal: beat " + std::to_string(cfg.goalCount) +
                                           " level(s).");
            evaluate();
        } else if (cmd == "ReceivedItems") {
            std::vector<int64_t> ids;
            for (auto& it : pkt["items"].items()) ids.push_back(it["item"].asInt());
            int64_t index = pkt["index"].asInt(0);
            if (index == 0) {
                received = ids;
            } else if (index == static_cast<int64_t>(received.size())) {
                received.insert(received.end(), ids.begin(), ids.end());
            } else {
                Json p = Json::object();
                p.set("cmd", "Sync");
                send(p);
                continue;
            }
            if (hasConfig) {
                processNewItems();
                evaluate();
            }
        } else if (cmd == "RoomUpdate") {
            for (auto& id : pkt["checked_locations"].items()) checked.insert(id.asInt());
        }
        // PrintJSON, Bounced, DataPackage and the rest are not needed by the mod.
    }
}

}  // namespace apgd
