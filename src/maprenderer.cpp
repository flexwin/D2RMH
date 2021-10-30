/*
 * Copyright (c) 2021 Soar Qin<soarchin@gmail.com>
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "maprenderer.h"

#include "window.h"

#include "cfg.h"

static std::wstring utf8toucs4(const std::string &s) {
    std::wstring ws;
    wchar_t wc;
    for (int i = 0; i < s.length();) {
        char c = s[i];
        if ((c & 0x80) == 0) {
            wc = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            wc = (s[i] & 0x1F) << 6;
            wc |= (s[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            wc = (s[i] & 0xF) << 12;
            wc |= (s[i + 1] & 0x3F) << 6;
            wc |= (s[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            wc = (s[i] & 0x7) << 18;
            wc |= (s[i + 1] & 0x3F) << 12;
            wc |= (s[i + 2] & 0x3F) << 6;
            wc |= (s[i + 3] & 0x3F);
            i += 4;
        } else if ((c & 0xFC) == 0xF8) {
            wc = (s[i] & 0x3) << 24;
            wc |= (s[i] & 0x3F) << 18;
            wc |= (s[i] & 0x3F) << 12;
            wc |= (s[i] & 0x3F) << 6;
            wc |= (s[i] & 0x3F);
            i += 5;
        } else if ((c & 0xFE) == 0xFC) {
            wc = (s[i] & 0x1) << 30;
            wc |= (s[i] & 0x3F) << 24;
            wc |= (s[i] & 0x3F) << 18;
            wc |= (s[i] & 0x3F) << 12;
            wc |= (s[i] & 0x3F) << 6;
            wc |= (s[i] & 0x3F);
            i += 6;
        }
        ws += wc;
    }
    return ws;
}

static JsonLng::LNG lngFromString(const std::string &language) {
    if (language == "enUS") return JsonLng::LNG_enUS;
    if (language == "zhTW") return JsonLng::LNG_zhTW;
    if (language == "deDE") return JsonLng::LNG_deDE;
    if (language == "esES") return JsonLng::LNG_esES;
    if (language == "frFR") return JsonLng::LNG_frFR;
    if (language == "itIT") return JsonLng::LNG_itIT;
    if (language == "koKR") return JsonLng::LNG_koKR;
    if (language == "plPL") return JsonLng::LNG_plPL;
    if (language == "esMX") return JsonLng::LNG_esMX;
    if (language == "jaJP") return JsonLng::LNG_jaJP;
    if (language == "ptBR") return JsonLng::LNG_ptBR;
    if (language == "ruRU") return JsonLng::LNG_ruRU;
    if (language == "zhCN") return JsonLng::LNG_zhCN;
    return JsonLng::LNG_enUS;
}

MapRenderer::MapRenderer(Renderer &renderer) :
    renderer_(renderer),
    mapPipeline_(renderer),
    framePipeline_(renderer),
    ttfgl_(renderer),
    ttf_(ttfgl_),
    walkableColor_(cfg->walkableColor) {
    ttf_.add(cfg->fontFilePath);
    mapPipeline_.setTexture(mapTex_);
    lng_ = lngFromString(cfg->language);
    d2rProcess_.setWindowPosCallback([this](int left, int top, int right, int bottom) {
        d2rRect = {left, top, right, bottom};
        updateWindowPos();
    });
    objColors_[TypeWayPoint] = cfg->waypointColor;
    objColors_[TypePortal] = cfg->portalColor;
    objColors_[TypeChest] = cfg->chestColor;
    objColors_[TypeQuest] = cfg->questColor;
    objColors_[TypeWell] = cfg->wellColor;
    ttf_.setColor(cfg->textColor & 0xFF, (cfg->textColor >> 8) & 0xFF, (cfg->textColor >> 16) & 0xFF);
}
void MapRenderer::update() {
    d2rProcess_.updateData();
    if (!d2rProcess_.available()) {
        enabled_ = false;
        return;
    }
    switch (cfg->show) {
    case 0:
        enabled_ = !d2rProcess_.mapEnabled();
        break;
    case 1:
        enabled_ = d2rProcess_.mapEnabled();
        break;
    default:
        enabled_ = true;
        break;
    }
    if (!enabled_) {
        return;
    }
    bool changed = session_.update(d2rProcess_.seed(), d2rProcess_.difficulty());
    uint32_t levelId;
    if (changed || (levelId = d2rProcess_.levelId()) != currLevelId_) {
        textStrings_.clear();
        lines_.clear();
        currLevelId_ = levelId;
        currMap_ = session_.getMap(levelId);
        if (!currMap_) {
            enabled_ = false;
            return;
        }
        int x0 = currMap_->cropX, y0 = currMap_->cropY,
            x1 = currMap_->cropX2, y1 = currMap_->cropY2;
        int width = std::max(0, x1 - x0);
        int height = std::max(0, y1 - y0);
        auto totalWidth = currMap_->totalWidth;
        auto *pixels = new uint32_t[width * height];
        auto *ptr = pixels;
        for (int y = y0; y < y1; ++y) {
            int idx = y * totalWidth + x0;
            for (int x = x0; x < x1; ++x) {
                auto clr = currMap_->map[idx++] & 1 ? 0 : walkableColor_;
                *ptr++ = clr;
            }
        }
        mapTex_.setData(width, height, pixels);
        delete[] pixels;

        const std::set<int> *guides;
        {
            auto gdite = gamedata->guides.find(levelId);
            if (gdite != gamedata->guides.end()) {
                guides = &gdite->second;
            } else {
                guides = nullptr;
            }
        }

        PipelineSquad2D squadPip(mapTex_);
        squadPip.setOrtho(0, mapTex_.width(), 0, mapTex_.height());
        auto originX = currMap_->levelOrigin.x, originY = currMap_->levelOrigin.y;
        auto widthf = float(width) * .5f, heightf = float(height) * .5f;
        for (auto &p: currMap_->adjacentLevels) {
            auto ite = gamedata->levels.find(p.first);
            if (ite == gamedata->levels.end()) { continue; }
            if (p.second.exits.empty()) {
                continue;
            }
            auto &e = p.second.exits[0];
            auto px = float(e.x - originX - x0);
            auto py = float(e.y - originY - y0);
            auto strite = gamedata->strings.find(ite->second);
            std::string name = strite != gamedata->strings.end() ? strite->second[lng_] : "";
            /* Check for TalTombs */
            if (p.first >= 66 && p.first <= 72) {
                auto *m = session_.getMap(p.first);
                if (m && m->objects.find(152) != m->objects.end()) {
                    name = ">>> " + name + " <<<";
                    lines_.emplace_back(px - widthf, py - heightf);
                }
            }
            squadPip.pushQuad(px - 4, py - 4, px + 4, py + 4, objColors_[TypePortal]);
            auto namew = utf8toucs4(name);
            textStrings_.emplace_back(px - widthf, py - heightf, namew, float(ttf_.stringWidth(namew, cfg->fontSize)) * .5f);
            if (guides && (*guides).find(p.first) != (*guides).end()) {
                lines_.emplace_back(px - widthf, py - heightf);
            }
        }
        std::map<uint32_t, std::vector<Point>> *objs[2] = {&currMap_->objects, &currMap_->npcs};
        for (int i = 0; i < 2; ++i) {
            for (const auto &[id, vec]: *objs[i]) {
                auto ite = gamedata->objects[i].find(id);
                if (ite == gamedata->objects[i].end()) { continue; }
                for (auto &pt: vec) {
                    auto ptx = float(pt.x - originX - x0);
                    auto pty = float(pt.y - originY - y0);
                    auto tp = ite->second.type;
                    switch (tp) {
                    case TypeWayPoint:
                    case TypeQuest:
                    case TypePortal:
                    case TypeChest:
                    case TypeShrine:
                    case TypeWell: {
                        squadPip.pushQuad(ptx - 4, pty - 4, ptx + 4, pty + 4, objColors_[tp]);
                        auto strite = gamedata->strings.find(ite->second.name);
                        std::string name = strite != gamedata->strings.end() ? strite->second[lng_] : "";
                        auto namew = utf8toucs4(name);
                        textStrings_.emplace_back(ptx - widthf, pty - heightf, namew, float(ttf_.stringWidth(namew, cfg->fontSize)) * .5f);
                        if (guides && (*guides).find(id | (0x10000 * (i + 1))) != (*guides).end()) {
                            lines_.emplace_back(ptx - widthf, pty - heightf);
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
        squadPip.render();
        updateWindowPos();
    }
    enabled_ = true;
}
void MapRenderer::render() {
    if (enabled_) {
        updatePlayerPos();
        mapPipeline_.render();
        framePipeline_.render();
        auto fontSize = cfg->fontSize;
        for (const auto &[x, y, text, offX]: textStrings_) {
            if (text.empty()) { continue; }
            auto coord = transform_ * HMM_Vec4(x - 4.f, y - 4.f, 0, 1);
            ttf_.render(text, coord.X - offX, coord.Y - fontSize, false, fontSize);
        }
    }
}
void MapRenderer::updateWindowPos() {
    if (!currMap_) { return; }
    int x0 = currMap_->cropX, y0 = currMap_->cropY, x1 = currMap_->cropX2,
        y1 = currMap_->cropY2;
    int width = x1 - x0;
    int height = y1 - y0;
    auto windowSize = (int)lroundf(cfg->scale * (float)(width + height) * 0.75) + 8;
    const auto bear = 16;
    if (windowSize + bear * 2 > d2rRect.right - d2rRect.left) {
        windowSize = d2rRect.right - d2rRect.left - bear * 2;
    }
    if (windowSize / 2 + bear * 2 > d2rRect.bottom - d2rRect.top) {
        windowSize = (d2rRect.bottom - d2rRect.top - bear * 2) * 2;
    }
    float w, h;
    switch (cfg->position) {
    case 0:
        renderer_.owner()->move(d2rRect.left + bear, d2rRect.top + bear, windowSize, windowSize / 2);
        w = (float)windowSize;
        h = w / 2;
        break;
    case 1:
        renderer_.owner()->move(d2rRect.right - windowSize - bear, d2rRect.top + bear, windowSize, windowSize / 2);
        w = (float)windowSize;
        h = w / 2;
        break;
    default:
        renderer_.owner()->move(d2rRect.left + bear, d2rRect.top + bear, d2rRect.right - d2rRect.left - bear * 2, d2rRect.bottom - d2rRect.top - bear * 2);
        w = (float)(d2rRect.right - d2rRect.left - bear * 2);
        h = (float)(d2rRect.bottom - d2rRect.top - bear * 2);
        break;
    }
    auto widthf = (float)width * 0.5f, heightf = (float)height * 0.5f;
    mapPipeline_.reset();
    mapPipeline_.setOrtho(-w / 2, w / 2, h / 2, -h / 2);
    mapPipeline_.pushQuad(-widthf, -heightf, widthf, heightf);
    framePipeline_.reset();
    framePipeline_.setOrtho(-w / 2, w / 2, h / 2, -h / 2);
    ttfgl_.pipeline()->setOrtho(-w / 2, w / 2, h / 2, -h / 2);
    updatePlayerPos();
}
void MapRenderer::updatePlayerPos() {
    if (!currMap_) { return; }
    auto posX = d2rProcess_.posX();
    auto posY = d2rProcess_.posY();
    if (posX == playerPosX_ && posY == playerPosY_) {
        return;
    }
    playerPosX_ = posX;
    playerPosY_ = posY;
    int x0 = currMap_->cropX, y0 = currMap_->cropY, x1 = currMap_->cropX2,
        y1 = currMap_->cropY2;
    auto originX = currMap_->levelOrigin.x, originY = currMap_->levelOrigin.y;
    posX -= originX + x0;
    posY -= originY + y0;
    auto widthf = (float)(x1 - x0) * 0.5f, heightf = (float)(y1 - y0) * 0.5f;
    auto oxf = float(posX) - widthf;
    auto oyf = float(posY) - heightf;
    framePipeline_.reset();
    framePipeline_.pushQuad(oxf - 4, oyf - 4, oxf + 4, oyf + 4, cfg->playerOuterColor);
    framePipeline_.pushQuad(oxf - 2, oyf - 2, oxf + 2, oyf + 2, cfg->playerInnerColor);

    auto c = cfg->lineColor;
    for (auto [x, y]: lines_) {
        if (cfg->fullLine) {
            auto line = HMM_Vec2(x, y) - HMM_Vec2(oxf, oyf);
            auto len = HMM_Length(line);
            auto sx = oxf + line.X / len * 8.f;
            auto sy = oyf + line.Y / len * 8.f;
            auto ex = x - line.X / len * 8.f;
            auto ey = y - line.Y / len * 8.f;
            if (len < 17.f) {
                ex = sx; ey = sy;
            }
            framePipeline_.drawLine(sx, sy, ex, ey, 1.5f, c);
        } else {
            const float mlen = 78.f;
            const float gap = 12.f;
            float sx, sy, ex, ey;
            auto line = HMM_Vec2(x, y) - HMM_Vec2(oxf, oyf);
            auto len = HMM_Length(line);
            sx = oxf + line.X / len * 8.f;
            sy = oyf + line.Y / len * 8.f;
            if (len > mlen) {
                ex = oxf + line.X / len * (mlen - gap);
                ey = oyf + line.Y / len * (mlen - gap);
            } else if (len > gap) {
                ex = x - line.X / len * gap;
                ey = y - line.Y / len * gap;
            } else {
                ex = sx;
                ey = sy;
            }

            const float angle = 35.f;
            /* Draw the line */
            framePipeline_.drawLine(sx, sy, ex, ey, 1.5f, c);

            /* Draw the dot */
            if (ex != sx) {
                ex += line.X / len * gap;
                ey += line.Y / len * gap;
                framePipeline_.pushQuad(ex - 3, ey - 3, ex + 1.5f, ey - 1.5f, ex + 3, ey + 3, ex - 1.5f, ey + 1.5f, c);
            }
        }
    }

    transform_ = HMM_Scale(HMM_Vec3(1, .5f, 1.f))
        * HMM_Rotate(45.f, HMM_Vec3(0, 0, 1))
        * HMM_Scale(HMM_Vec3(cfg->scale, cfg->scale, 1));
    if (cfg->mapCentered) {
        transform_ = transform_ * HMM_Translate(HMM_Vec3(-oxf, -oyf, 0));
    }
    mapPipeline_.setTransform(&transform_.Elements[0][0]);
    framePipeline_.setTransform(&transform_.Elements[0][0]);
}
