// Geometry Dash Archipelago: Geode mod entry point and game hooks.
//
// Every Archipelago decision is made by apgd::Session (src/core, unit-tested without Geode). This file
// only turns game events into calls on it and acts on what it says:
//   * beating a level (and the secret coins picked up on that run) sends the checks;
//   * a level you haven't unlocked closes again with the reason when you try to play it;
//   * touching a portal for a gamemode you don't have yet kills you;
//   * received items pop up as notifications; traps are enforced on your next level.
// Only levels that are part of your seed are affected; anything else plays normally.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GauntletLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>

#include <cctype>
#include <climits>
#include <random>
#include <set>
#include <sstream>

#include "ap_mod.hpp"
#include "ws_client.hpp"

using namespace geode::prelude;

namespace apmod {
namespace {

apgd::Session g_session;
WsClient g_ws;
std::string g_lastError;
int g_connections = 0;            // bumped on every new socket, so saved coins are synced once per connection

// Geode v6: save paths are asp::fs::Path; read/write through Geode's file utils (no std::filesystem).
std::string stateFileName(const std::string& key) {
    std::string safe;
    for (char c : key) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') ? c : '_';
    return "run_" + safe + ".json";
}

void setupOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    g_session.loadState = [](const std::string& key) {
        auto name = stateFileName(key);
        auto text = utils::file::readString(Mod::get()->getSaveDir() / name.c_str());
        return text.isOk() ? text.unwrap() : std::string();       // no save yet = fresh start
    };
    g_session.saveState = [](const std::string& key, const std::string& text) {
        auto name = stateFileName(key);
        auto res = utils::file::writeString(Mod::get()->getSaveDir() / name.c_str(), text);
        if (res.isErr()) log::warn("Archipelago: could not save progress: {}", res.unwrapErr());
    };
    static std::mt19937 rng{std::random_device{}()};
    g_session.random = [] { return static_cast<uint32_t>(rng()); };
    // One id per install so the server recognises reconnects from the same game.
    std::string uuid = Mod::get()->getSavedValue<std::string>("uuid", "");
    if (uuid.empty()) {
        std::stringstream ss;
        ss << std::hex << rng() << rng() << rng() << rng();
        uuid = ss.str();
        Mod::get()->setSavedValue<std::string>("uuid", uuid);
    }
    g_session.uuid = uuid;

    g_ws.onOpen = [](const std::string& url) {
        geode::queueInMainThread([url] {
            log::info("Socket open: {}", url);
            g_lastError.clear();
            ++g_connections;
            g_session.onSocketOpen();
            refreshMenu();
        });
    };
    g_ws.onMessage = [](std::string text) {
        geode::queueInMainThread([text = std::move(text)] {
            g_session.onMessage(text);
            pump();
        });
    };
    g_ws.onClose = [](const std::string& reason) {
        geode::queueInMainThread([reason] {
            bool wasConnected = g_session.connected;
            g_session.onSocketClosed();
            g_lastError = reason;
            log::warn("Archipelago: {}", reason);
            Notification::create(std::string(wasConnected ? "Archipelago disconnected: " : "Archipelago: ") + reason,
                                 NotificationIcon::Error, 4.f)->show();
            refreshMenu();
        });
    };
    g_ws.onLog = [](const std::string& line) { log::info("{}", line); };
}

}  // namespace

apgd::Session& session() { return g_session; }
bool socketOpen() { return g_ws.isOpen(); }
std::string lastConnectionError() { return g_lastError; }

void connect() {
    setupOnce();
    auto server = Mod::get()->getSettingValue<std::string>("server");
    auto slot = Mod::get()->getSettingValue<std::string>("slot");
    if (slot.empty()) {
        g_lastError = "set your slot name first (Geode > Mods > Geometry Dash Archipelago > settings)";
        Notification::create("Archipelago: " + g_lastError, NotificationIcon::Warning, 4.f)->show();
        refreshMenu();
        return;
    }
    g_session.slotName = slot;
    g_session.password = Mod::get()->getSettingValue<std::string>("password");
    g_lastError.clear();
    Notification::create("Archipelago: connecting to " + server + "...", NotificationIcon::Loading, 2.f)->show();
    g_ws.connect(server);
    refreshMenu();
}

void disconnect() {
    g_ws.close();
    g_session.onSocketClosed();
    refreshMenu();
}

// Coins the game has saved for a level (1..count). Main levels use secret coins, online levels user coins.
std::set<int> savedCoins(GJGameLevel* level, int count) {
    std::set<int> out;
    auto* gsm = GameStatsManager::sharedState();
    if (!level || !gsm) return out;
    bool official = static_cast<int>(level->m_levelType) == 1;
    for (int n = 1; n <= count && n <= 3; ++n) {
        const char* key = level->getCoinKey(n);
        if (!key) continue;
        bool have = official ? gsm->hasSecretCoin(key) : (gsm->hasUserCoin(key) || gsm->hasPendingUserCoin(key));
        if (have) out.insert(n);
    }
    return out;
}

// Once per connection: coins already in your save for official levels you've beaten count too
// (so a coin collected before, or one the pickup hook missed, isn't lost).
void syncSavedCoins() {
    static int syncedFor = -1;
    if (!g_session.connected || !g_session.hasConfig || !g_session.cfg.includeCoins) return;
    if (syncedFor == g_connections) return;
    syncedFor = g_connections;
    int counted = 0;
    for (auto& lv : g_session.cfg.levels) {
        if (lv.tier != "main" || lv.kind != "level" || lv.coinIds.empty() || !g_session.state.completed.count(lv.idx)) continue;
        auto* level = GameLevelManager::sharedState()->getMainLevel(lv.idx, true);
        for (int n : savedCoins(level, static_cast<int>(lv.coinIds.size())))
            if (g_session.reportCoin(lv.idx, n)) ++counted;
    }
    if (counted) log::info("Archipelago: counted {} coin(s) already in your save", counted);
}

void pump() {
    syncSavedCoins();
    for (auto& msg : g_session.takeOutgoing()) {
        if (!g_ws.send(msg)) log::warn("Archipelago: not connected, message kept for the next connection");
    }
    for (auto& n : g_session.takeNotices()) {
        using K = apgd::Notice::Kind;
        auto icon = n.kind == K::Error ? NotificationIcon::Error
                  : n.kind == K::Trap  ? NotificationIcon::Warning
                                       : NotificationIcon::Success;
        std::string text = n.kind == K::Item ? "Received: " + n.text : n.text;
        Notification::create(text, icon, n.kind == K::Trap ? 4.f : 2.5f)->show();
        log::info("Archipelago: {}", text);
    }
    refreshMenu();
}

}  // namespace apmod

using apmod::session;

// ------------------------------------------------------------------ gauntlets (tracked automatically)
// While a gauntlet's screen is open, its levels belong to that gauntlet: they count toward it once it's
// unlocked, they're locked while it isn't, and when all of them are beaten the gauntlet check is sent.
static int g_gauntletIdx = -1;
static std::vector<int64_t> g_gauntletLevels;

static const char* gauntletName(int type) {
    static const char* names[] = {"", "Fire", "Ice", "Poison", "Shadow", "Lava", "Bonus", "Chaos", "Demon", "Time", "Crystal"};
    return type >= 1 && type <= 10 ? names[type] : "";
}

class $modify(APGauntletLayer, GauntletLayer) {
    void setupGauntlet(CCArray* levels) {
        GauntletLayer::setupGauntlet(levels);
        g_gauntletLevels.clear();
        g_gauntletIdx = session().hasConfig ? session().gauntletIdx(gauntletName(static_cast<int>(m_gauntletType))) : -1;
        if (g_gauntletIdx < 0 || !levels) return;
        for (unsigned int i = 0; i < levels->count(); ++i)
            if (auto* lv = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i))) g_gauntletLevels.push_back(lv->m_levelID.value());
        auto block = session().gauntletBlock(g_gauntletIdx);
        if (!block.empty() && Mod::get()->getSettingValue<bool>("lock-levels")) {
            Notification::create("Locked by Archipelago: " + block, NotificationIcon::Error, 4.f)->show();
        } else if (!session().state.completed.count(g_gauntletIdx)) {
            int n = session().gauntletOpened(g_gauntletIdx, g_gauntletLevels);
            if (!session().state.completed.count(g_gauntletIdx))
                Notification::create(std::string(gauntletName(static_cast<int>(m_gauntletType))) + " Gauntlet: " + std::to_string(n) +
                                     "/" + std::to_string(g_gauntletLevels.size()) + " beaten for Archipelago",
                                     NotificationIcon::Info, 3.f)->show();
        }
        apmod::pump();
    }

    void onBack(CCObject* sender) {
        g_gauntletIdx = -1;
        g_gauntletLevels.clear();
        GauntletLayer::onBack(sender);
    }

    void keyBackClicked() {
        g_gauntletIdx = -1;
        g_gauntletLevels.clear();
        GauntletLayer::keyBackClicked();
    }
};

static int gauntletOf(GJGameLevel* level) {
    if (!level || g_gauntletIdx < 0) return -1;
    int64_t id = level->m_levelID.value();
    for (auto l : g_gauntletLevels) if (l == id) return g_gauntletIdx;
    return -1;
}

// ------------------------------------------------------------------ which seed level is this?
static int seedLevelIdx(GJGameLevel* level) {
    auto& s = session();
    if (!level || !s.hasConfig) return -1;
    int id = level->m_levelID.value();
    int type = static_cast<int>(level->m_levelType);       // 1 = official level, 2 = editor, 3 = online
    if (type == 2) return -1;                              // your own editor levels never count
    if (type == 1) {
        auto* lv = s.matchMainLevel(id);
        return lv ? lv->idx : -1;
    }
    auto* lv = s.matchOnlineLevel(std::string(level->m_levelName), level->m_stars.value());
    return lv ? lv->idx : -1;
}

// ------------------------------------------------------------------ PlayLayer
class $modify(APPlayLayer, PlayLayer) {
public:                                        // the coin and portal hooks below call into this class
    struct Fields {
        int idx = -1;
        int gauntlet = -1;                    // set when this level is played from a gauntlet in the seed
        bool started = false;
        bool completed = false;
        bool practice = false;
        bool leaving = false;
        bool warnedMode = false;
        std::set<int> coins;
        apgd::AttemptRules rules;
    };

    // Runs once per visit to a level (from whichever of startGame/resetLevel comes first).
    void apEnter() {
        if (m_fields->started) return;
        m_fields->started = true;
        m_fields->idx = seedLevelIdx(m_level);
        if (m_fields->idx < 0) {
            m_fields->gauntlet = gauntletOf(m_level);
            if (m_fields->gauntlet >= 0 && Mod::get()->getSettingValue<bool>("lock-levels")) {
                auto block = session().gauntletBlock(m_fields->gauntlet);
                if (!block.empty()) {
                    m_fields->leaving = true;
                    Notification::create("Locked by Archipelago: " + block, NotificationIcon::Error, 5.f)->show();
                    Ref<PlayLayer> self = this;
                    geode::queueInMainThread([self] { self->onQuit(); });
                }
            }
            return;
        }
        auto block = Mod::get()->getSettingValue<bool>("lock-levels") ? session().playBlock(m_fields->idx) : std::string();
        if (!block.empty()) {
            m_fields->leaving = true;
            Notification::create("Locked by Archipelago: " + block, NotificationIcon::Error, 5.f)->show();
            Ref<PlayLayer> self = this;
            geode::queueInMainThread([self] { self->onQuit(); });
            return;
        }
        m_fields->rules = session().attemptStart(m_fields->idx);
        if (m_fields->rules.noPractice)
            Notification::create("No Practice Mode Trap: no practice mode on this level", NotificationIcon::Warning, 4.f)->show();
        if (m_fields->rules.mute)
            Notification::create("Mute Trap: turn music and sound effects off for this level", NotificationIcon::Warning, 4.f)->show();
        apmod::pump();
    }

    void startGame() {
        PlayLayer::startGame();
        apEnter();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        apEnter();
        m_fields->coins.clear();              // coins only count on the run that reaches the end
        m_fields->warnedMode = false;
    }

    void togglePracticeMode(bool practiceMode) {
        if (practiceMode && m_fields->rules.noPractice) {
            Notification::create("No Practice Mode Trap: practice mode is off for this level", NotificationIcon::Warning, 3.f)->show();
            return;
        }
        m_fields->practice = practiceMode;
        PlayLayer::togglePracticeMode(practiceMode);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (m_fields->leaving) return;
        if (m_fields->gauntlet >= 0 && !m_fields->practice) {
            session().gauntletLevelBeaten(m_fields->gauntlet, m_level->m_levelID.value(), g_gauntletLevels);
            apmod::pump();
            return;
        }
        if (m_fields->idx < 0) return;
        if (m_fields->practice) {
            Notification::create("Archipelago: practice runs don't count", NotificationIcon::Info, 3.f)->show();
            return;
        }
        std::string why;
        bool counted = session().reportLevel(m_fields->idx, &why);
        if (counted || why == "already counted") {
            m_fields->completed = true;
            // Coins picked up on this run, plus every coin the game has saved for this level.
            auto coins = m_fields->coins;
            if (auto* lv = session().cfg.level(m_fields->idx))
                for (int n : apmod::savedCoins(m_level, static_cast<int>(lv->coinIds.size()))) coins.insert(n);
            for (int n : coins) session().reportCoin(m_fields->idx, n);
        } else {
            Notification::create("Archipelago: did not count - " + why, NotificationIcon::Error, 4.f)->show();
        }
        apmod::pump();
    }

    void onQuit() {
        if (m_fields->idx >= 0 && !m_fields->leaving) session().attemptEnd(m_fields->idx, m_fields->completed);
        m_fields->leaving = true;
        apmod::pump();
        PlayLayer::onQuit();
    }

    // Called by the coin hooks below.
    void apCoin(GameObject* coin) {
        if (m_fields->idx < 0) return;
        int n = 0;
        if (m_coinArray) {
            unsigned int i = m_coinArray->indexOfObject(coin);
            if (i != UINT_MAX) n = static_cast<int>(i) + 1;          // coins are numbered in level order
        }
        if (n <= 0) n = static_cast<int>(m_fields->coins.size()) + 1;       // fallback: next number
        if (n <= 3) m_fields->coins.insert(n);
    }

    // Called by the gamemode hooks below. True = this portal is not allowed.
    bool apBlockMode(PlayerObject* player, const char* mode) {
        int level = m_fields->idx >= 0 ? m_fields->idx : m_fields->gauntlet;     // gauntlet levels use its gamemodes
        if (level < 0 || m_fields->leaving || !Mod::get()->getSettingValue<bool>("lock-gamemodes")) return false;
        if (session().modeAllowed(level, mode)) return false;
        if (!m_fields->warnedMode) {
            m_fields->warnedMode = true;
            Notification::create("Locked gamemode: you don't have " + apgd::modeLabel(mode) + " yet",
                                 NotificationIcon::Error, 3.f)->show();
        }
        Ref<PlayLayer> self = this;
        Ref<PlayerObject> who = player;
        geode::queueInMainThread([self, who] { self->destroyPlayer(who, nullptr); });
        return true;
    }
};

// ------------------------------------------------------------------ coins
static bool isCoin(EffectGameObject* obj) {
    return obj && (obj->m_objectType == GameObjectType::SecretCoin || obj->m_objectType == GameObjectType::UserCoin);
}

class $modify(APGameLayer, GJBaseGameLayer) {
    // (collectedObject is inlined in GD 2.2091 and can't be hooked; pickupItem handles coins.)
    void pickupItem(EffectGameObject* object) {
        GJBaseGameLayer::pickupItem(object);
        auto* pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && isCoin(object)) static_cast<APPlayLayer*>(pl)->apCoin(object);
    }
};

// ------------------------------------------------------------------ gamemode portals
#define AP_MODE_HOOK(fn, mode)                                                                \
    void fn(bool enable, bool noEffects) {                                                    \
        if (enable) {                                                                         \
            auto* pl = PlayLayer::get();                                                      \
            if (pl && static_cast<APPlayLayer*>(pl)->apBlockMode(this, mode)) return;         \
        }                                                                                     \
        PlayerObject::fn(enable, noEffects);                                                  \
    }

class $modify(APPlayerObject, PlayerObject) {
    AP_MODE_HOOK(toggleFlyMode, "ship")
    AP_MODE_HOOK(toggleRollMode, "ball")
    AP_MODE_HOOK(toggleBirdMode, "ufo")
    AP_MODE_HOOK(toggleDartMode, "wave")
    AP_MODE_HOOK(toggleRobotMode, "robot")
    AP_MODE_HOOK(toggleSpiderMode, "spider")
    AP_MODE_HOOK(toggleSwingMode, "swing")
};

// ------------------------------------------------------------------ main menu button + auto-connect
class $modify(APMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto* sprite = ButtonSprite::create("AP", "goldFont.fnt", "GJ_button_01.png", .8f);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(APMenuLayer::onArchipelago));
        button->setID("archipelago-button"_spr);
        if (auto* menu = this->getChildByID("bottom-menu")) {
            menu->addChild(button);
            menu->updateLayout();
        } else {                                                   // fallback: own menu in the corner
            auto* own = CCMenu::create();
            own->addChild(button);
            auto size = CCDirector::get()->getWinSize();
            own->setPosition({size.width - 40.f, 40.f});
            this->addChild(own);
        }
        static bool autoConnected = false;                         // once per game launch
        if (!autoConnected && Mod::get()->getSettingValue<bool>("auto-connect") &&
            !Mod::get()->getSettingValue<std::string>("slot").empty()) {
            autoConnected = true;
            apmod::connect();
        }
        return true;
    }

    void onArchipelago(CCObject*) { apmod::openMenu(); }
};
