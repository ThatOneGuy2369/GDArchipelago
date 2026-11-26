#include "ap_locks.hpp"

#include <cmath>
#include <map>
#include <vector>

using namespace geode::prelude;

namespace aplocks {
namespace {

ccColor4F hex(unsigned int rgb, float a = 1.f) {
    return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}

struct Theme { unsigned int main, dark, emblem; };

const std::map<std::string, Theme>& themes() {
    static const std::map<std::string, Theme> t = {
        {"fire", {0xff7a2f, 0x5a1a0c, 0xffd23f}},    {"ice", {0x9fe3ff, 0x1d3f57, 0xffffff}},
        {"poison", {0x8fe34a, 0x1f3d12, 0xd6ff8a}},  {"shadow", {0xa98bff, 0x211a3d, 0xe2d8ff}},
        {"lava", {0xff4d2e, 0x4a0f08, 0xffb347}},    {"bonus", {0xffd84a, 0x4a3a08, 0xfff6b0}},
        {"chaos", {0xff4fd8, 0x3d0f36, 0xffd0f5}},   {"demon", {0xe0313c, 0x3d0a10, 0xff9da3}},
        {"time", {0xe0b97a, 0x3d2d14, 0xfff1d6}},    {"crystal", {0x5ff0e0, 0x0f3d3a, 0xe0fffb}},
        {"level", {0xffc83d, 0x3a2a08, 0x1a1205}},
    };
    return t;
}

// Shapes are given in a -1..1 box (y up) and scaled by s/2 around (cx, cy).
struct Painter {
    CCDrawNode* d;
    void poly(float cx, float cy, float s, std::vector<CCPoint> pts, ccColor4F fill) {
        for (auto& p : pts) p = {cx + p.x * s / 2, cy + p.y * s / 2};
        d->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0.f, fill);
    }
    void tri(CCPoint a, CCPoint b, CCPoint c, ccColor4F fill) {
        CCPoint pts[3] = {a, b, c};
        d->drawPolygon(pts, 3, fill, 0.f, fill);
    }
    void circle(float cx, float cy, float r, ccColor4F fill, int n = 24) {
        std::vector<CCPoint> pts;
        for (int i = 0; i < n; ++i) {
            float a = 6.2831853f * i / n;
            pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
        }
        d->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0.f, fill);
    }
    void ring(float cx, float cy, float r, float w, ccColor4F col, float from = 0.f, float to = 6.2831853f, int n = 24) {
        for (int i = 0; i < n; ++i) {
            float a0 = from + (to - from) * i / n, a1 = from + (to - from) * (i + 1) / n;
            d->drawSegment({cx + std::cos(a0) * r, cy + std::sin(a0) * r}, {cx + std::cos(a1) * r, cy + std::sin(a1) * r}, w / 2, col);
        }
    }
    void line(CCPoint a, CCPoint b, float w, ccColor4F col) { d->drawSegment(a, b, w / 2, col); }
    void rect(float x0, float y0, float x1, float y1, ccColor4F fill) {
        CCPoint pts[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        d->drawPolygon(pts, 4, fill, 0.f, fill);
    }
};

void emblem(Painter& p, const std::string& theme, float cx, float cy, float s, ccColor4F col, ccColor4F bg) {
    float h = s / 2;
    if (theme == "fire") {
        p.poly(cx, cy, s, {{0, 1}, {.55f, .15f}, {.62f, -.45f}, {.3f, -.9f}, {-.3f, -.9f}, {-.62f, -.45f}, {-.55f, .15f}}, col);
        p.poly(cx, cy - h * .25f, s * .5f, {{0, 1}, {.55f, .1f}, {.5f, -.6f}, {0, -.9f}, {-.5f, -.6f}, {-.55f, .1f}}, bg);
    } else if (theme == "ice") {
        for (int k = 0; k < 3; ++k) {
            float a = 3.14159265f * k / 3;
            p.line({cx - std::cos(a) * h, cy - std::sin(a) * h}, {cx + std::cos(a) * h, cy + std::sin(a) * h}, std::max(2.f, s / 9), col);
        }
        p.circle(cx, cy, h * .2f, col);
    } else if (theme == "poison") {
        p.poly(cx, cy, s, {{0, 1}, {.6f, -.15f}, {.55f, -.6f}, {0, -1}, {-.55f, -.6f}, {-.6f, -.15f}}, col);
    } else if (theme == "shadow") {
        p.circle(cx, cy, h * .85f, col);
        p.circle(cx + h * .42f, cy + h * .3f, h * .68f, bg);
    } else if (theme == "lava") {
        p.poly(cx, cy, s, {{-1, .75f}, {1, .75f}, {1, .4f}, {-1, .4f}}, col);
        for (float dx : {-.6f, 0.f, .6f}) {
            float depth = dx == 0.f ? -.95f : -.55f;
            p.tri({cx + (dx - .28f) * h, cy + .45f * h}, {cx + (dx + .28f) * h, cy + .45f * h}, {cx + dx * h, cy + depth * h}, col);
        }
    } else if (theme == "bonus") {
        std::vector<CCPoint> pts;
        for (int k = 0; k < 10; ++k) {
            float r = (k % 2 == 0 ? 1.f : .45f) * h, a = 1.5707963f + k * 3.14159265f / 5;
            pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
        }
        for (int k = 0; k < 10; ++k) p.tri({cx, cy}, pts[k], pts[(k + 1) % 10], col);   // a star as triangles
    } else if (theme == "chaos") {
        p.poly(cx, cy, s, {{.25f, 1}, {-.5f, -.1f}, {0, -.1f}, {.35f, .35f}}, col);
        p.poly(cx, cy, s, {{.05f, .2f}, {.5f, .2f}, {-.25f, -1}, {-.05f, .2f}}, col);
    } else if (theme == "demon") {
        p.circle(cx, cy - h * .2f, h * .6f, col);
        p.tri({cx - .55f * h, cy + .1f * h}, {cx - .9f * h, cy + 1.f * h}, {cx - .2f * h, cy + .35f * h}, col);
        p.tri({cx + .55f * h, cy + .1f * h}, {cx + .9f * h, cy + 1.f * h}, {cx + .2f * h, cy + .35f * h}, col);
    } else if (theme == "time") {
        p.ring(cx, cy, h * .9f, std::max(2.f, s / 10), col);
        p.line({cx, cy}, {cx, cy + h * .6f}, std::max(2.f, s / 12), col);
        p.line({cx, cy}, {cx + h * .45f, cy - h * .2f}, std::max(2.f, s / 12), col);
    } else if (theme == "crystal") {
        p.poly(cx, cy, s, {{0, 1}, {.65f, .3f}, {0, -1}, {-.65f, .3f}}, col);
        p.line({cx - .65f * h, cy + .3f * h}, {cx + .65f * h, cy + .3f * h}, std::max(1.f, s / 16), bg);
    } else {                                                   // a keyhole
        p.circle(cx, cy + h * .25f, h * .38f, col);
        p.poly(cx, cy, s, {{-.2f, .2f}, {.2f, .2f}, {.32f, -.85f}, {-.32f, -.85f}}, col);
    }
}

}  // namespace

std::string themeForGauntlet(const std::string& shortName) {
    std::string t;
    for (char c : shortName) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return themes().count(t) ? t : "bonus";
}

CCNode* makeLock(const std::string& theme, float size) {
    auto it = themes().find(theme);
    Theme th = it != themes().end() ? it->second : themes().at("level");
    auto* d = CCDrawNode::create();
    Painter p{d};
    ccColor4F main = hex(th.main), dark = hex(th.dark), emb = hex(th.emblem);
    float h = size / 2, thick = std::max(3.f, size / 9);
    float bodyTop = h * .1f;
    // soft shadow so it reads on any background
    p.circle(0, -h * .2f, h * 1.15f, hex(0x000000, .35f), 32);
    // shackle: a half ring with legs down into the body
    p.ring(0, h * .5f, h * .5f, thick, main, 0.f, 3.14159265f, 16);
    p.line({-h * .5f, h * .5f}, {-h * .5f, bodyTop}, thick, main);
    p.line({h * .5f, h * .5f}, {h * .5f, bodyTop}, thick, main);
    // body
    float edge = std::max(2.f, size / 16);
    p.rect(-h * .8f - edge, -h - edge, h * .8f + edge, bodyTop + edge, main);
    p.rect(-h * .8f, -h, h * .8f, bodyTop, dark);
    emblem(p, theme, 0, -h * .45f, size * .42f, emb, dark);
    if (theme == "fire") {                                   // flames on the sides of the fire lock
        for (float side : {-1.f, 1.f})
            p.poly(side * h * .8f, bodyTop - h * .05f, size * .42f,
                   {{0, 1}, {.5f, .2f}, {.6f, -.3f}, {.3f, -.85f}, {-.3f, -.85f}, {-.6f, -.3f}, {-.5f, .2f}}, emb);
    } else if (theme == "ice") {                             // icicles hanging off the ice lock
        for (float dx : {-.5f, 0.f, .5f})
            p.tri({dx * h - size * .06f, -h}, {dx * h + size * .06f, -h}, {dx * h, -h - size * .16f}, main);
    }
    return d;
}

CCNode* makeCheck(float size) {
    auto* d = CCDrawNode::create();
    Painter p{d};
    float h = size / 2;
    p.circle(0, 0, h, hex(0x1f6b2a, .9f), 24);
    ccColor4F g = hex(0x8fe388);
    p.line({-h * .5f, 0}, {-h * .1f, -h * .4f}, std::max(3.f, size / 7), g);
    p.line({-h * .1f, -h * .4f}, {h * .55f, h * .45f}, std::max(3.f, size / 7), g);
    return d;
}

}  // namespace aplocks
