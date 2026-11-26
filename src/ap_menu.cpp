// The Archipelago panel: connection status, goal progress, the last item, gauntlet reporting and a
// summary of what's locked. Built from plain cocos2d nodes so it works across Geode versions.
#include <Geode/Geode.hpp>

#include "ap_mod.hpp"

using namespace geode::prelude;

namespace {

class APPanel : public CCLayerColor {
public:
    static APPanel* s_open;

    static APPanel* create() {
        auto* p = new APPanel();
        if (p->init()) { p->autorelease(); return p; }
        delete p;
        return nullptr;
    }

    bool init() override {
        if (!CCLayerColor::initWithColor({0, 0, 0, 160})) return false;
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        build();
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true);   // swallow clicks behind the panel
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }
    void keyBackClicked() override { onClose(nullptr); }

    void onExit() override {
        if (s_open == this) s_open = nullptr;
        CCLayerColor::onExit();
    }

    void build() {
        this->removeAllChildren();
        auto& s = apmod::session();
        auto win = CCDirector::get()->getWinSize();
        float w = 420.f, h = 270.f;
        auto* bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({w, h});
        bg->setPosition({win.width / 2, win.height / 2});
        this->addChild(bg);
        float left = win.width / 2 - w / 2, top = win.height / 2 + h / 2;

        auto label = [&](const std::string& text, float x, float y, float scale, ccColor3B color = {255, 255, 255},
                         const char* font = "bigFont.fnt") {
            auto* l = CCLabelBMFont::create(text.c_str(), font);
            l->setScale(scale);
            l->setColor(color);
            l->setPosition({x, y});
            this->addChild(l);
            return l;
        };
        label("Archipelago", win.width / 2, top - 22.f, .7f, {255, 255, 255}, "goldFont.fnt");

        std::string status = s.statusLine();
        if (!s.connected && apmod::socketOpen()) status = "Connecting...";
        if (!s.connected && !apmod::lastConnectionError().empty()) status = apmod::lastConnectionError();
        auto* st = label(status, win.width / 2, top - 50.f, .32f, s.connected ? ccColor3B{120, 255, 120} : ccColor3B{255, 170, 120});
        st->limitLabelWidth(w - 30.f, .32f, .15f);
        if (!s.lastItem.empty())
            label("Last item: " + s.lastItem, win.width / 2, top - 68.f, .3f, {255, 220, 120})->limitLabelWidth(w - 30.f, .3f, .15f);

        auto* menu = CCMenu::create();
        menu->setPosition({0, 0});
        menu->setTouchPriority(-501);
        this->addChild(menu);
        auto button = [&](const std::string& text, SEL_MenuHandler sel, float x, float y, float scale, int tag = 0,
                          const char* bgFile = "GJ_button_01.png") {
            auto* spr = ButtonSprite::create(text.c_str(), "goldFont.fnt", bgFile, .8f);
            spr->setScale(scale);
            auto* b = CCMenuItemSpriteExtra::create(spr, this, sel);
            b->setPosition({x, y});
            b->setTag(tag);
            menu->addChild(b);
            return b;
        };
        button(s.connected ? "Reconnect" : "Connect", menu_selector(APPanel::onConnect), win.width / 2 - 70.f, top - 95.f, .6f);
        button("Close", menu_selector(APPanel::onClose), win.width / 2 + 70.f, top - 95.f, .6f, 0, "GJ_button_06.png");

        if (!s.hasConfig) {
            label("Set Server and Slot in Geode > Mods > Geometry Dash Archipelago > settings,", win.width / 2,
                  top - 140.f, .25f)->limitLabelWidth(w - 30.f, .25f, .1f);
            label("then press Connect.", win.width / 2, top - 155.f, .25f);
            return;
        }
        // gauntlets: honor-system reporting (the game doesn't announce a finished gauntlet)
        std::vector<const apgd::Level*> gauntlets;
        for (auto& lv : s.cfg.levels) if (lv.kind == "gauntlet") gauntlets.push_back(&lv);
        if (gauntlets.empty()) {
            label("Beat levels in the game: checks are sent automatically.", win.width / 2, top - 140.f, .28f);
            return;
        }
        label("Gauntlets: tracked automatically. Buttons are a backup if one didn't register.", win.width / 2, top - 122.f, .25f, {200, 200, 255});
        for (size_t i = 0; i < gauntlets.size(); ++i) {
            auto* g = gauntlets[i];
            std::string shortName = g->name;
            auto pos = shortName.find(" Gauntlet");
            if (pos != std::string::npos) shortName = shortName.substr(0, pos);
            bool done = s.state.completed.count(g->idx) > 0;
            bool locked = !done && !s.lockReason(*g).empty();
            float x = left + 50.f + static_cast<float>(i % 5) * 80.f;
            float y = top - 150.f - static_cast<float>(i / 5) * 34.f;
            auto beaten = s.state.gauntletBeaten.count(g->idx) ? s.state.gauntletBeaten.at(g->idx).size() : 0;
            auto size = s.state.gauntletSize.count(g->idx) ? s.state.gauntletSize.at(g->idx) : 0;
            std::string text = done ? shortName + " OK" : size ? shortName + " " + std::to_string(beaten) + "/" + std::to_string(size) : shortName;
            auto* b = button(text, menu_selector(APPanel::onGauntlet), x, y, .45f, g->idx,
                             done ? "GJ_button_02.png" : locked ? "GJ_button_04.png" : "GJ_button_01.png");
            b->setEnabled(!done && !locked);
        }
        label("Green = done, grey = locked (needs items), blue = open: press only if it didn't count", win.width / 2, top - 230.f, .22f, {180, 180, 180});
    }

    void onConnect(CCObject*) { apmod::connect(); }
    void onClose(CCObject*) { this->removeFromParent(); }

    void onGauntlet(CCObject* sender) {
        int idx = static_cast<CCNode*>(sender)->getTag();
        std::string why;
        if (!apmod::session().reportGauntlet(idx, &why))
            Notification::create("Archipelago: " + why, NotificationIcon::Error, 3.f)->show();
        apmod::pump();
    }
};

APPanel* APPanel::s_open = nullptr;

}  // namespace

namespace apmod {

void openMenu() {
    if (APPanel::s_open) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto* panel = APPanel::create();
    if (!panel) return;
    APPanel::s_open = panel;
    scene->addChild(panel, 1000);
}

void refreshMenu() {
    // Rebuild on the next frame, never inside a button's own click (that would delete the button mid-click).
    static bool queued = false;
    if (!APPanel::s_open || queued) return;
    queued = true;
    geode::queueInMainThread([] {
        queued = false;
        if (APPanel::s_open) APPanel::s_open->build();
    });
}

}  // namespace apmod
