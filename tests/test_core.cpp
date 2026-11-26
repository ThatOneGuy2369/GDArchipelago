// Unit tests for the mod's Archipelago core (no Geode needed). Build & run: see run_tests.sh
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "../src/core/ap_core.hpp"

using namespace apgd;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (_a == _b) ++g_pass; else { ++g_fail; std::cerr << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " failed: [" << _a << "] vs [" << _b << "]\n"; } } while (0)

static Json loadFixtures() {
    std::ifstream f(FIXTURES);
    std::stringstream ss;
    ss << f.rdbuf();
    return Json::parse(ss.str());
}

struct Harness {
    Session s;
    std::map<std::string, std::string> disk;
    std::vector<std::string> sent;
    uint32_t rnd = 0;
    Harness() {
        s.slotName = "Me";
        s.loadState = [this](const std::string& k) { return disk.count(k) ? disk[k] : std::string(); };
        s.saveState = [this](const std::string& k, const std::string& v) { disk[k] = v; };
        s.random = [this]() { return rnd++; };
    }
    void feed(const std::string& text) {
        s.onMessage(text);
        for (auto& m : s.takeOutgoing()) sent.push_back(m);
    }
    void connect(const Json& slotData, const std::vector<int64_t>& checkedLocs = {}) {
        s.onSocketOpen();
        feed(R"([{"cmd":"RoomInfo","seed_name":"S1","datapackage_checksums":{}}])");
        Json checked = Json::array();
        for (auto id : checkedLocs) checked.push(id);
        Json p = Json::object();
        Json players = Json::array();
        Json me = Json::object(); me.set("slot", 1).set("name", "Me"); players.push(me);
        Json al = Json::object(); al.set("slot", 2).set("name", "Alice"); players.push(al);
        p.set("cmd", "Connected").set("slot", 1).set("slot_data", slotData).set("checked_locations", checked)
            .set("players", players);
        Json arr = Json::array(); arr.push(p);
        feed(arr.dump());
    }
    void items(int64_t index, const std::vector<int64_t>& ids) {
        Json list = Json::array();
        for (auto id : ids) { Json it = Json::object(); it.set("item", id).set("player", 2); list.push(it); }
        Json p = Json::object(); p.set("cmd", "ReceivedItems").set("index", index).set("items", list);
        Json arr = Json::array(); arr.push(p);
        feed(arr.dump());
    }
    void drain() { for (auto& m : s.takeOutgoing()) sent.push_back(m); }
    int count(const std::string& cmd) {
        drain();
        int n = 0;
        for (auto& m : sent) { Json arr = Json::parse(m); for (auto& p : arr.items()) if (p["cmd"].asString() == cmd) ++n; }
        return n;
    }
    std::vector<int64_t> checks() {
        drain();
        std::vector<int64_t> out;
        for (auto& m : sent) {
            Json arr = Json::parse(m);
            for (auto& p : arr.items())
                if (p["cmd"].asString() == "LocationChecks") for (auto& l : p["locations"].items()) out.push_back(l.asInt());
        }
        return out;
    }
};

static int64_t itemOf(const Session& s, const std::string& kind, const std::string& mode = "", int idx = -1,
                      const std::string& name = "") {
    for (auto& kv : s.cfg.items)
        if (kv.second.kind == kind && (mode.empty() || kv.second.mode == mode) && (idx < 0 || kv.second.idx == idx) &&
            (name.empty() || kv.second.name == name))
            return kv.first;
    return -1;
}

static void testJson() {
    auto j = Json::parse(R"({"a":[1,-2,3.5,true,null,"x\"y\\z\n"],"big":1300000123456,"u":"é😀","e":{}})");
    CHECK_EQ(j["a"][1].asInt(), -2);
    CHECK(j["a"][2].asDouble() == 3.5);
    CHECK(j["a"][3].asBool());
    CHECK(j["a"][4].isNull());
    CHECK_EQ(j["a"][5].asString(), std::string("x\"y\\z\n"));
    CHECK_EQ(j["big"].asInt(), (int64_t)1300000123456);
    CHECK_EQ(j["u"].asString(), std::string("\xC3\xA9\xF0\x9F\x98\x80"));
    CHECK(j["e"].isObject() && j["e"].size() == 0);
    CHECK(j["missing"]["deeper"].isNull());                        // safe chained lookups
    auto again = Json::parse(j.dump());
    CHECK_EQ(again.dump(), j.dump());
    Json copy = j;
    copy.set("a", 5);                                               // copies don't share edits
    CHECK(j["a"].isArray());
    for (auto bad : {"", "{", "[1,]x", "{\"a\" 1}", "tru", "\"unterminated", "[1] 2"}) {
        bool threw = false;
        try { Json::parse(bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

static void testAgainstPython(const Json& fixtures) {
    int compared = 0;
    for (auto& fx : fixtures.items()) {
        for (auto& c : fx["cases"].items()) {
            Harness h;
            h.connect(fx["slot_data"]);
            h.s.received.clear();
            for (auto& r : c["received"].items()) h.s.received.push_back(r.asInt());
            h.s.state.completed.clear();
            h.s.state.coins.clear();
            for (auto& v : c["completed"].items()) h.s.state.completed.insert((int)v.asInt());
            for (auto& v : c["coins"].items()) h.s.state.coins.insert(v.asString());
            for (auto& kv : c["reasons"].members()) {
                const Level* lv = h.s.cfg.level(std::stoi(kv.first));
                CHECK(lv != nullptr);
                if (!lv) continue;
                std::string mine = h.s.lockReason(*lv), theirs = kv.second.asString();
                if (mine != theirs) std::cerr << "  level " << lv->name << ": C++ [" << mine << "] python [" << theirs << "]\n";
                CHECK(mine == theirs);
                ++compared;
            }
            CHECK_EQ(h.s.levelsDone(), (int)c["levels_done"].asInt());
            CHECK_EQ(h.s.goalComplete(), c["goal"].asBool());
        }
    }
    std::cout << "  compared " << compared << " lock decisions with the Python companion\n";
    CHECK(compared > 1000);
}

static void testProtocolAndReports(const Json& sd) {
    Harness h;
    h.connect(sd);
    Json sent0 = Json::parse(h.sent[0]);
    Json connect = sent0[0];
    CHECK_EQ(connect["cmd"].asString(), std::string("Connect"));
    CHECK_EQ(connect["game"].asString(), std::string("Geometry Dash"));
    CHECK_EQ(connect["name"].asString(), std::string("Me"));
    CHECK_EQ(connect["items_handling"].asInt(), (int64_t)7);
    CHECK(connect["slot_data"].asBool());
    CHECK(h.s.connected && h.s.hasConfig);
    CHECK_EQ(h.s.seed, std::string("S1"));

    // a level needing only Ship, without an unlock: find one
    const Level* start = nullptr;
    for (auto& lv : h.s.cfg.levels) if (lv.kind == "level" && !lv.needsUnlock && lv.modes.size() == 1) { start = &lv; break; }
    CHECK(start != nullptr);
    if (!start) return;
    int64_t ship = itemOf(h.s, "mode", "ship");
    h.items(0, {});                                                 // first sync: nothing yet
    std::string why;
    CHECK(!h.s.reportLevel(start->idx, &why));                      // no Ship yet
    CHECK(why.find("Ship Gamemode") != std::string::npos);
    CHECK(!h.s.playBlock(start->idx).empty());
    CHECK(!h.s.modeAllowed(start->idx, "ship"));
    CHECK(h.s.modeAllowed(start->idx, "cube"));
    CHECK(h.s.modeAllowed(9999, "ship"));                           // non-AP level: never blocked
    h.items(0, {ship});                                             // the very first item arrives at index 0
    auto notes = h.s.takeNotices();
    bool sawShip = false;
    for (auto& n : notes) if (n.text == "Ship Gamemode") sawShip = true;
    CHECK(sawShip);
    CHECK(h.s.playBlock(start->idx).empty());
    CHECK(h.s.reportLevel(start->idx));
    CHECK_EQ(h.checks().size(), (size_t)1);
    CHECK(!h.s.reportLevel(start->idx, &why));                      // never twice
    CHECK_EQ(why, std::string("already counted"));
    CHECK_EQ(h.checks().size(), (size_t)1);
    if (!start->coinIds.empty()) {
        CHECK(h.s.reportCoin(start->idx, 2));
        CHECK(!h.s.reportCoin(start->idx, 2));
        CHECK(!h.s.reportCoin(start->idx, 4));
        CHECK_EQ(h.checks().back(), start->coinIds[1]);
    }
    // gap -> Sync
    h.items(50, {ship});
    CHECK_EQ(h.count("Sync"), 1);
    // resync with the same list: no new notices
    h.s.takeNotices();
    h.items(0, {ship});
    CHECK(h.s.takeNotices().empty());

    // reconnect with a lost local save: server's checks restore progress; unsent local ones are resent
    Harness h2;
    h2.connect(sd, {start->locId});
    CHECK(h2.s.state.completed.count(start->idx) == 1);
    Harness h3;
    h3.disk = h.disk;
    h3.connect(sd, {});                                              // server never got them (offline play)
    auto resent = h3.checks();
    CHECK(std::find(resent.begin(), resent.end(), start->locId) != resent.end());

    // matching game levels
    CHECK(h.s.matchMainLevel(1) != nullptr);
    CHECK(h.s.matchMainLevel(9999) == nullptr);
    const Level* demon = nullptr;
    for (auto& lv : h.s.cfg.levels) if (lv.tier == "easy_demon") { demon = &lv; break; }
    if (demon) {
        CHECK(h.s.matchOnlineLevel(lower(demon->name), 10) == demon);
        CHECK(h.s.matchOnlineLevel(demon->name, 3) == nullptr);      // unrated copy
        CHECK(h.s.matchOnlineLevel("Not A Real Level", 10) == nullptr);
    }
    // refused connection
    Harness h4;
    h4.feed(R"([{"cmd":"ConnectionRefused","errors":["InvalidSlot"]}])");
    auto n4 = h4.s.takeNotices();
    CHECK(!n4.empty() && n4[0].kind == Notice::Kind::Error && n4[0].text.find("InvalidSlot") != std::string::npos);
    h4.feed("not json");
    CHECK(h4.s.takeNotices()[0].kind == Notice::Kind::Error);
}

static void testGoalAndGauntlet(const Json& sd) {
    Harness h;
    h.connect(sd);
    std::vector<int64_t> everything;
    for (auto& kv : h.s.cfg.items) if (kv.second.kind != "trap") everything.push_back(kv.first);
    h.items(0, everything);
    int done = 0;
    for (auto& lv : h.s.cfg.levels) {
        if (lv.kind != "level") continue;
        CHECK(h.s.reportLevel(lv.idx));
        for (size_t n = 1; n <= lv.coinIds.size(); ++n) h.s.reportCoin(lv.idx, (int)n);   // coin gates
        if (++done >= h.s.cfg.goalCount + 3) break;
    }
    CHECK(h.s.goalComplete());
    CHECK_EQ(h.count("StatusUpdate"), 1);
    for (auto& lv : h.s.cfg.levels) h.s.reportLevel(lv.idx);
    CHECK_EQ(h.count("StatusUpdate"), 1);                           // only once
    const Level* g = nullptr;
    for (auto& lv : h.s.cfg.levels) if (lv.kind == "gauntlet") { g = &lv; break; }
    if (g) {
        Harness h5;
        h5.connect(sd);
        h5.items(0, everything);
        std::string why;
        CHECK(!h5.s.reportGauntlet(1, &why));                       // a normal level is not a gauntlet
        CHECK(h5.s.reportGauntlet(g->idx));
        CHECK_EQ(h5.s.levelsDone(), 0);                             // gauntlets don't count toward the goal
    }
}

static void testTraps(const Json& sd) {
    Harness h;
    h.connect(sd);
    std::vector<int64_t> all;
    for (auto& kv : h.s.cfg.items) if (kv.second.kind != "trap") all.push_back(kv.first);
    h.items(0, all);
    int64_t noPractice = itemOf(h.s, "trap", "", -1, "No Practice Mode Trap");
    int64_t mute = itemOf(h.s, "trap", "", -1, "Mute Trap");
    int64_t randomT = itemOf(h.s, "trap", "", -1, "Random Level Trap");
    int64_t handsUp = itemOf(h.s, "trap", "", -1, "Hands Up Trap");
    CHECK(noPractice > 0 && mute > 0 && randomT > 0 && handsUp > 0);
    h.s.takeNotices();
    h.items((int64_t)h.s.received.size(), {noPractice, mute});
    CHECK_EQ(h.s.state.noPractice, 1);
    CHECK_EQ(h.s.takeNotices().size(), (size_t)2);
    auto rules = h.s.attemptStart(1);
    CHECK(rules.noPractice && rules.mute);
    CHECK(!h.s.attemptStart(1).noPractice);                         // used up
    // a resync of the same items never re-arms them
    auto copy = h.s.received;
    h.items(0, copy);
    CHECK_EQ(h.s.state.noPractice, 0);

    // Random Level: forces one open, unfinished level
    h.items((int64_t)h.s.received.size(), {randomT});
    int forced = h.s.state.forcedLevel;
    CHECK(forced > 0);
    int other = forced == 1 ? 2 : 1;
    CHECK(h.s.playBlock(other).find("Random Level Trap") != std::string::npos);
    CHECK(h.s.playBlock(forced).empty());
    h.s.attemptStart(forced);
    CHECK_EQ(h.s.state.forcedLevel, -1);
    CHECK(h.s.playBlock(other).empty());
    h.s.attemptEnd(forced, false);

    // Hands Up: leave the next level unbeaten -> it's blocked until another level is beaten
    h.items((int64_t)h.s.received.size(), {handsUp});
    h.s.attemptStart(3);
    h.s.attemptEnd(3, false);
    CHECK(h.s.playBlock(3).find("Hands Up") != std::string::npos);
    CHECK(h.s.reportLevel(4));
    h.s.attemptEnd(4, true);
    CHECK(h.s.playBlock(3).empty());
    // ...but beating it in that attempt clears the trap
    h.items((int64_t)h.s.received.size(), {handsUp});
    h.s.attemptStart(5);
    h.s.attemptEnd(5, true);
    CHECK_EQ(h.s.state.blockedLevel, -1);

    // traps survive a restart
    Harness h2;
    h2.disk = h.disk;
    h2.items(0, {});
    h2.connect(sd);
    CHECK_EQ(h2.s.state.itemsSeen, h.s.state.itemsSeen);
    auto back = RunState::fromJson(Json::parse(h.s.state.toJson().dump()));
    CHECK(back.toJson().dump() == h.s.state.toJson().dump());
    CHECK_EQ(RunState::fromJson(Json::parse(R"({"no_practice":-5,"forced_level":"x"})")).noPractice, 0);
}

static void testFirstConnectFloodIsSummarised(const Json& sd) {
    Harness h;
    h.connect(sd);
    h.s.takeNotices();
    std::vector<int64_t> all;
    for (auto& kv : h.s.cfg.items) all.push_back(kv.first);
    h.items(0, all);
    int itemNotes = 0, summary = 0;
    for (auto& n : h.s.takeNotices()) {
        if (n.kind == Notice::Kind::Item) ++itemNotes;
        if (n.text.rfind("Received ", 0) == 0) ++summary;
    }
    CHECK_EQ(itemNotes, 1);                                          // one summary, not one popup per item
    CHECK_EQ(summary, 1);
}

static void testGauntletTracking(const Json& sd) {
    Harness h;
    h.connect(sd);
    int fire = h.s.gauntletIdx("Fire");
    if (fire < 0) return;                                           // seed without gauntlets
    CHECK_EQ(h.s.gauntletIdx("fire"), fire);                       // case doesn't matter
    CHECK_EQ(h.s.gauntletIdx("Magic"), -1);                        // not in the world
    std::vector<int64_t> ids = {101, 102, 103, 104, 105};
    h.items(0, {});
    CHECK(!h.s.gauntletBlock(fire).empty());                        // nothing unlocked yet
    CHECK_EQ(h.s.gauntletLevelBeaten(fire, 101, ids), -1);          // beaten while locked: doesn't count
    CHECK(h.s.state.gauntletBeaten[fire].empty());
    std::vector<int64_t> all;
    for (auto& kv : h.s.cfg.items) if (kv.second.kind != "trap") all.push_back(kv.first);
    h.items(0, all);
    CHECK(h.s.gauntletBlock(fire).empty());
    CHECK_EQ(h.s.gauntletOpened(fire, ids), 0);
    CHECK_EQ(h.s.state.gauntletSize[fire], 5);
    for (int i = 0; i < 4; ++i) CHECK_EQ(h.s.gauntletLevelBeaten(fire, ids[i], ids), i + 1);
    CHECK_EQ(h.s.gauntletLevelBeaten(fire, ids[0], ids), 4);         // a replay doesn't double-count
    CHECK(!h.s.state.completed.count(fire));
    size_t before = h.checks().size();
    CHECK_EQ(h.s.gauntletLevelBeaten(fire, ids[4], ids), 5);
    CHECK(h.s.state.completed.count(fire) == 1);                    // check sent automatically
    CHECK_EQ(h.checks().size(), before + 1);
    CHECK_EQ(h.s.levelsDone(), 0);                                   // gauntlets don't count toward the goal
    CHECK(h.s.gauntletBlock(fire).empty());
    // progress survives a restart
    auto back = RunState::fromJson(Json::parse(h.s.state.toJson().dump()));
    CHECK_EQ(back.gauntletBeaten[fire].size(), (size_t)5);
    CHECK_EQ(back.gauntletSize[fire], 5);
    CHECK(RunState::fromJson(Json::parse(R"({"gauntlet_beaten":{"x":[1],"41":["bad",7]}})")).gauntletBeaten[41].count(7) == 1);
    // levels beaten earlier (all already recorded) send the check when the gauntlet is opened once it's unlocked
    Harness h2;
    h2.connect(sd);
    int ice = h2.s.gauntletIdx("Ice");
    h2.s.state.gauntletBeaten[ice] = {1, 2, 3};
    h2.items(0, all);
    CHECK_EQ(h2.s.gauntletOpened(ice, {1, 2, 3}), 3);
    CHECK(h2.s.state.completed.count(ice) == 1);
}

int main() {
    Json fx = loadFixtures();
    testJson();
    testAgainstPython(fx);
    for (auto& f : fx.items()) {
        testProtocolAndReports(f["slot_data"]);
        testGoalAndGauntlet(f["slot_data"]);
    }
    testTraps(fx[0]["slot_data"]);
    testGauntletTracking(fx[0]["slot_data"]);
    testFirstConnectFloodIsSummarised(fx[0]["slot_data"]);
    std::cout << g_pass << " checks passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
