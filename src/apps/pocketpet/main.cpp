// 口袋宠物 · 墨灵 —— Cardputer ADV
// 养成 / 小游戏 / 对战 / 进化 / 存档。离线运行，按开机次数结算离线衰减。

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>
#include <math.h>

#include "cjk_font.h"
#include "keyboard_adv.h"
#include "sprites.h"
#include "ui.h"

using ui::canvas;

// ---------------- 数据 ----------------
enum Screen {
    S_HOME, S_MENU, S_FEED, S_TRAIN, S_STATUS, S_DEX, S_SETTINGS,
    S_GAME, S_BATTLE, S_EVOLVE, S_NAME
};

struct Pet {
    char name[12];
    uint8_t stage;      // 0 墨团 / 1 墨灵 / 2 墨麒麟
    uint16_t level;
    uint16_t exp;
    uint8_t hunger, mood, energy, clean, health, friendship;
    uint8_t poop;
    uint16_t str, intl, agi;
    uint16_t coins, wins;
    bool sick;
};

static Pet pet;
static AdvKeyboard keyboard;
static ui::ShakeDetector shake;

static Screen screen = S_HOME;
static uint32_t bootAt = 0;
static uint32_t lastTick = 0;
static uint32_t lastSave = 0;
static uint32_t lastTouch = 0;
static uint32_t toastUntil = 0;
static char toast[40] = "";
static bool loadedSave = false;

static const char* STAGE_NAMES[3] = {"墨丸", "砚兽", "墨麟"};

// 菜单
static int menuSel = 0;
static constexpr int MENU_N = 8;
static const char* MENU_LABELS[MENU_N] = {
    "喂食", "玩耍", "训练", "清洁", "睡觉", "外出", "状态", "设置",
};

static int feedSel = 0;
static int trainSel = 0;
static int settingsSel = 0;

// 小游戏
struct Fruit { float x, y; bool active; };
static Fruit fruits[14];
static float gamePetX = 120;
static int gameScore = 0;
static uint32_t gameEndAt = 0;
static uint32_t gameNextSpawn = 0;
static bool gameResult = false;
static uint32_t gameResultUntil = 0;

// 战斗
struct Enemy {
    char name[16];
    uint16_t color;
    int kind;
    int level, hp, maxHp, atk, def;
};
static Enemy enemy;
static int petHp = 20, petMaxHp = 20;
static int battleSel = 0;
static char battleMsg[48] = "";
static bool battleOver = false;
static bool battleWon = false;
static bool battleNext = false;
static int streak = 0;
static int petDefBuff = 0;
static uint8_t difficulty = 1;   // 0 普通 / 1 困难

// 进化
static uint8_t evolveTo = 0;
static uint32_t evolveAt = 0;
static bool pendingEvolve = false;

// 改名
static char nameBuf[12];
static int nameLen = 0;

// ---------------- 工具 ----------------
static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v)); }
static uint16_t clamp16(int v) { return (uint16_t)(v < 0 ? 0 : v); }

static void toastMsg(const char* s) {
    snprintf(toast, sizeof(toast), "%s", s);
    toastUntil = millis() + 1600;
}

static void savePet() {
    ui::prefs.putString("name", pet.name);
    ui::prefs.putUChar("stage", pet.stage);
    ui::prefs.putUShort("level", pet.level);
    ui::prefs.putUShort("exp", pet.exp);
    ui::prefs.putUChar("hunger", pet.hunger);
    ui::prefs.putUChar("mood", pet.mood);
    ui::prefs.putUChar("energy", pet.energy);
    ui::prefs.putUChar("clean", pet.clean);
    ui::prefs.putUChar("health", pet.health);
    ui::prefs.putUChar("friend", pet.friendship);
    ui::prefs.putUChar("poop", pet.poop);
    ui::prefs.putUShort("str", pet.str);
    ui::prefs.putUShort("intl", pet.intl);
    ui::prefs.putUShort("agi", pet.agi);
    ui::prefs.putUShort("coins", pet.coins);
    ui::prefs.putUShort("wins", pet.wins);
    ui::prefs.putBool("sick", pet.sick);
    ui::prefs.putUChar("diff", difficulty);
    ui::prefs.putBool("exists", true);
    lastSave = millis();
}

static void loadPet() {
    loadedSave = ui::prefs.getBool("exists", false);
    if (!loadedSave) {
        memset(&pet, 0, sizeof(pet));
        snprintf(pet.name, sizeof(pet.name), "墨丸");
        pet.level = 1;
        pet.hunger = 80;
        pet.mood = 80;
        pet.energy = 80;
        pet.clean = 100;
        pet.health = 100;
        pet.friendship = 30;
        pet.str = pet.intl = pet.agi = 1;
        pet.coins = 10;
        savePet();
        return;
    }

    String n = ui::prefs.getString("name", "墨丸");
    snprintf(pet.name, sizeof(pet.name), "%s", n.c_str());
    pet.stage = ui::prefs.getUChar("stage", 0);
    pet.level = ui::prefs.getUShort("level", 1);
    pet.exp = ui::prefs.getUShort("exp", 0);
    pet.hunger = ui::prefs.getUChar("hunger", 80);
    pet.mood = ui::prefs.getUChar("mood", 80);
    pet.energy = ui::prefs.getUChar("energy", 80);
    pet.clean = ui::prefs.getUChar("clean", 100);
    pet.health = ui::prefs.getUChar("health", 100);
    pet.friendship = ui::prefs.getUChar("friend", 30);
    pet.poop = ui::prefs.getUChar("poop", 0);
    pet.str = ui::prefs.getUShort("str", 1);
    pet.intl = ui::prefs.getUShort("intl", 1);
    pet.agi = ui::prefs.getUShort("agi", 1);
    pet.coins = ui::prefs.getUShort("coins", 0);
    pet.wins = ui::prefs.getUShort("wins", 0);
    pet.sick = ui::prefs.getBool("sick", false);
    difficulty = ui::prefs.getUChar("diff", 1);
    if (pet.stage > 2) pet.stage = 2;
    if (pet.level < 1) pet.level = 1;

    // 离线结算：按“睡了一觉”处理，不按真实时间（设备无 RTC）。
    pet.hunger = clamp8(pet.hunger - 12);
    pet.energy = clamp8(pet.energy + 15);
    pet.clean = clamp8(pet.clean - 6);
    pet.mood = clamp8(pet.mood - 4);
    if (pet.hunger < 10) pet.health = clamp8(pet.health - 4);
    if (pet.clean < 20) pet.health = clamp8(pet.health - 2);
}

static uint16_t expNeed() { return (uint16_t)(10 + pet.level * 5); }

static void checkSick() {
    pet.sick = (pet.health == 0);
    if (pet.sick && pet.mood > 20) pet.mood = 20;
}

static void evolveIfNeeded() {
    uint8_t target = pet.stage;
    if (pet.level >= 25) target = 2;
    else if (pet.level >= 10) target = 1;
    if (target > pet.stage) {
        if (screen == S_BATTLE) {  // 战斗结束后再进化，别打断结算
            pendingEvolve = true;
            return;
        }
        evolveTo = target;
        evolveAt = millis();
        screen = S_EVOLVE;
        ui::beep(660, 90);
        delay(80);
        ui::beep(880, 90);
        delay(80);
        ui::beep(1180, 160);
    }
}

static void addExp(int amount) {
    if (amount <= 0) return;
    pet.exp += (uint16_t)amount;
    while (pet.exp >= expNeed() && pet.level < 99) {
        pet.exp -= expNeed();
        ++pet.level;
        pet.mood = clamp8(pet.mood + 8);
        toastMsg("升级了！");
    }
    evolveIfNeeded();
    savePet();
}

// ---------------- 时间推进 ----------------
static void tickPet() {
    if (pet.sick) pet.sick = false;

    if (pet.energy > 70 && pet.mood > 0) {
        // 精力好时慢慢恢复健康
        if ((millis() / 15000) % 6 == 0 && pet.health < 100 && pet.hunger > 50)
            pet.health = clamp8(pet.health + 1);
    }

    bool sleeping = pet.energy >= 100;
    (void)sleeping;

    pet.hunger = clamp8(pet.hunger - 1);
    if ((millis() / 15000) % 2 == 0) pet.energy = clamp8(pet.energy - 1);
    pet.clean = clamp8(pet.clean - 1);

    if (pet.hunger < 20) pet.mood = clamp8(pet.mood - 2);
    if (pet.clean < 25) pet.mood = clamp8(pet.mood - 2);
    if (pet.hunger > 60 && pet.clean > 60 && pet.energy > 30)
        pet.mood = clamp8(pet.mood + 1);

    if (pet.hunger < 10) pet.health = clamp8(pet.health - 1);
    if (pet.clean < 10) pet.health = clamp8(pet.health - 1);

    if (pet.hunger > 40 && (millis() / 15000) % 10 == 0 && pet.poop < 3) {
        ++pet.poop;
        pet.clean = clamp8(pet.clean - 8);
        pet.mood = clamp8(pet.mood - 3);
    }

    if (pet.poop > 0 && (millis() / 15000) % 4 == 0)
        pet.mood = clamp8(pet.mood - 1);

    if (pet.energy < 10) pet.mood = clamp8(pet.mood - 1);

    checkSick();
    if (millis() - lastSave > 15000) savePet();
}

// ---------------- 绘制基础 ----------------
static void bar(int x, int y, int w, int h, int v, uint16_t col) {
    canvas.drawRoundRect(x, y, w, h, 2, ui::C_LINE);
    int fw = (w - 2) * (v < 0 ? 0 : (v > 100 ? 100 : v)) / 100;
    if (fw > 0) canvas.fillRect(x + 1, y + 1, fw, h - 2, col);
}

static uint16_t barColor(int v) {
    if (v < 25) return ui::C_NO;
    if (v < 50) return ui::C_WAIT;
    return ui::C_YES;
}

// ---------------- 32x32 像素精灵 ----------------
struct Palette {
    uint16_t main, shade, outline, white, pupil, accent, ink;
};

static const Palette PAL_PET = {
    ui::C_PAPER, ui::rgb(206, 192, 170), ui::rgb(30, 40, 60),
    ui::rgb(250, 248, 240), ui::rgb(22, 28, 42), ui::C_GOLD, ui::rgb(40, 50, 72)};
static const Palette PAL_GRASS = {
    ui::rgb(90, 170, 90), ui::rgb(66, 132, 70), ui::rgb(24, 40, 28),
    ui::rgb(250, 248, 240), ui::rgb(22, 28, 42), ui::rgb(170, 220, 120),
    ui::rgb(40, 70, 40)};
static const Palette PAL_FIRE = {
    ui::rgb(232, 120, 60), ui::rgb(190, 84, 44), ui::rgb(48, 24, 18),
    ui::rgb(250, 248, 240), ui::rgb(30, 20, 20), ui::rgb(250, 210, 90),
    ui::rgb(120, 50, 30)};
static const Palette PAL_WATER = {
    ui::rgb(90, 150, 230), ui::rgb(64, 112, 186), ui::rgb(20, 32, 50),
    ui::rgb(250, 248, 240), ui::rgb(20, 28, 44), ui::rgb(150, 220, 240),
    ui::rgb(40, 70, 110)};

static uint16_t spriteColor(char c, const Palette& p) {
    switch (c) {
        case 'a': return p.main;
        case 'b': return p.shade;
        case 'o': return p.outline;
        case 'w': return p.white;
        case 'p': return p.pupil;
        case 'g': return p.accent;
        case 'c': return p.ink;
        default: return 0;
    }
}

static void drawSprite(const Sprite& s, int x, int y, int scale, const Palette& p,
                       bool blink) {
    for (int row = 0; row < 32; ++row) {
        const char* r = s.rows[row];
        int col = 0;
        while (col < 32) {
            const char c = r[col];
            if (c == '.') {
                ++col;
                continue;
            }
            int start = col;
            while (col < 32 && r[col] == c) ++col;
            canvas.fillRect(x + start * scale, y + row * scale,
                            (col - start) * scale, scale, spriteColor(c, p));
        }
    }
    if (blink) {
        const int ly1 = s.eyeY1 + s.eyeH / 2;
        canvas.fillRect(x + s.eyeX1 * scale, y + ly1 * scale, s.eyeW * scale, scale,
                        p.outline);
        const int ly2 = s.eyeY2 + s.eyeH / 2;
        canvas.fillRect(x + s.eyeX2 * scale, y + ly2 * scale, s.eyeW * scale, scale,
                        p.outline);
    }
}

static const Sprite* const PET_SPRITES[3] = {&MOWAN, &YANSHOU, &MOLIN};

static void drawPetArt(int cx, int footY, int stage, int mood, bool sleeping,
                       bool blink, bool eating, uint32_t t, float scale) {
    (void)eating;
    if (stage < 0 || stage > 2) stage = 0;
    const int px = (int)(2.0f * scale);
    const int bob = sleeping ? 0 : (int)(sinf(t * 0.003f) * 2);
    const int top = footY - 32 * px + bob;

    canvas.fillEllipse(cx, footY + 2, 16 * px, 4, ui::rgb(17, 25, 40));
    drawSprite(*PET_SPRITES[stage], cx - 16 * px, top, px, PAL_PET, blink);

    if (mood >= 65 && !sleeping) {
        int hx = cx + 18, hy = top - 7;
        canvas.fillRect(hx, hy, 3, 3, ui::rgb(240, 110, 150));
        canvas.fillRect(hx + 4, hy, 3, 3, ui::rgb(240, 110, 150));
        canvas.fillRect(hx, hy + 3, 7, 2, ui::rgb(240, 110, 150));
        canvas.fillRect(hx + 2, hy + 5, 3, 2, ui::rgb(240, 110, 150));
    } else if (mood < 30) {
        canvas.fillEllipse(cx + 22, top + 4, 2, 3, ui::C_MAYBE);
    }

    if (sleeping) {
        ui::drawAscii(canvas, cx + 20, top - 8, "z", ui::C_MUTED, 1);
        ui::drawAscii(canvas, cx + 28, top - 14, "Z", ui::C_MUTED, 1);
    }
}

static const Sprite* enemySpriteFor(int kind) {
    if (kind == 1) return &FIRE;
    if (kind == 2) return &WATER;
    return &GRASS;
}

static const Palette& enemyPaletteFor(int kind) {
    if (kind == 1) return PAL_FIRE;
    if (kind == 2) return PAL_WATER;
    return PAL_GRASS;
}

static void drawEnemyArt(int cx, int footY, int kind, uint32_t t) {
    int bob = (int)(sinf(t * 0.005f) * 2);
    canvas.fillEllipse(cx, footY + 2, 16, 3, ui::rgb(17, 25, 40));
    drawSprite(*enemySpriteFor(kind), cx - 16, footY - 32 + bob, 1,
               enemyPaletteFor(kind), false);
}

// ---------------- 主界面 ----------------
static void drawHome() {
    canvas.fillScreen(ui::C_BG);
    for (int y = 0; y < ui::H; y += 4) canvas.drawFastHLine(0, y, ui::W, ui::C_BG2);

    cf::drawText(canvas, 8, 2, pet.name, ui::C_PAPER, cf::font16);
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%d", pet.level);
    ui::drawAscii(canvas, 150, 6, lv, ui::C_GOLD, 1);
    snprintf(lv, sizeof(lv), "G%d", pet.coins);
    ui::drawAscii(canvas, 206, 6, lv, ui::C_MUTED, 1);

    bar(8, 24, 64, 6, pet.hunger, ui::C_GOLD);
    bar(88, 24, 64, 6, pet.mood, ui::C_YES);
    bar(168, 24, 64, 6, pet.energy, ui::C_MAYBE);
    ui::drawAscii(canvas, 22, 32, "FOOD", ui::C_MUTED, 1);
    ui::drawAscii(canvas, 104, 32, "MOOD", ui::C_MUTED, 1);
    ui::drawAscii(canvas, 178, 32, "ENERGY", ui::C_MUTED, 1);

    {
        const bool blink = ((millis() / 90) % 42 == 0);
        drawPetArt(120, 112, pet.stage, pet.mood, false, blink, false, millis(), 1.0f);
    }

    for (int i = 0; i < pet.poop && i < 3; ++i) {
        int x = 206 + i * 10;
        canvas.fillTriangle(x, 120, x - 5, 126, x + 5, 126, ui::rgb(90, 70, 40));
    }

    if (pet.sick) {
        canvas.fillRoundRect(30, 42, 180, 20, 5, ui::C_BG);
        cf::drawCentered(canvas, ui::W / 2, 44, "不舒服…", ui::C_NO, cf::font16);
    }

    if (millis() < toastUntil) {
        canvas.fillRoundRect(20, 42, 200, 20, 5, ui::C_BG);
        cf::drawCentered(canvas, ui::W / 2, 44, toast, ui::C_GOLD, cf::font16);
    }

    ui::drawAsciiCentered(canvas, ui::W / 2, 126,
                          "ENTER:MENU   ,/.:PET   S:SAVE", ui::rgb(72, 86, 108), 1);
}

// ---------------- 菜单 ----------------
static void drawMenu() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "做什么？", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 8, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    for (int i = 0; i < MENU_N; ++i) {
        int col = i % 2, row = i / 2;
        int x = 8 + col * 116, y = 30 + row * 24;
        if (i == menuSel) {
            canvas.fillRoundRect(x - 2, y - 2, 110, 21, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(x - 2, y - 2, 110, 21, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, x + 8, y, MENU_LABELS[i],
                     i == menuSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
    }
}

// ---------------- 喂食 / 训练 ----------------
static void drawFeed() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "喂什么？", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 8, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    static const char* names[3] = {"果子", "饭团", "蛋糕"};
    static const char* desc[3] = {"+18饱食 +3心情", "+30饱食 +1心情", "+12饱食 +12心情"};
    for (int i = 0; i < 3; ++i) {
        int y = 32 + i * 30;
        if (i == feedSel) {
            canvas.fillRoundRect(6, y - 2, ui::W - 12, 27, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 2, ui::W - 12, 27, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, 16, y, names[i],
                     i == feedSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
        cf::drawText(canvas, 78, y, desc[i],
                     i == feedSel ? ui::C_PAPER : ui::C_MUTED, cf::font16);
    }
    ui::drawAsciiCentered(canvas, ui::W / 2, 126, "ENTER:FEED", ui::C_MUTED, 1);
}

static void drawTrain() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "训练什么？", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 8, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    static const char* names[3] = {"体力", "智力", "敏捷"};
    static const char* desc[3] = {"攻击与生命", "墨弹威力", "防御与先手"};
    for (int i = 0; i < 3; ++i) {
        int y = 32 + i * 30;
        if (i == trainSel) {
            canvas.fillRoundRect(6, y - 2, ui::W - 12, 27, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 2, ui::W - 12, 27, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, 16, y, names[i],
                     i == trainSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
        cf::drawText(canvas, 78, y, desc[i],
                     i == trainSel ? ui::C_PAPER : ui::C_MUTED, cf::font16);
    }
    ui::drawAsciiCentered(canvas, ui::W / 2, 126, "COST 10 ENERGY", ui::C_MUTED, 1);
}

// ---------------- 状态 / 图鉴 ----------------
static bool dexPage = false;

static void drawStatus() {
    canvas.fillScreen(ui::C_BG);
    if (!dexPage) {
        cf::drawText(canvas, 8, 2, pet.name, ui::C_PAPER, cf::font16);
        char buf[24];
        snprintf(buf, sizeof(buf), "Lv.%d", pet.level);
        ui::drawAscii(canvas, 96, 6, buf, ui::C_GOLD, 1);
        cf::drawText(canvas, 132, 2, STAGE_NAMES[pet.stage], ui::C_GOLD, cf::font16);

        int need = expNeed();
        bar(8, 22, 80, 5, need ? (pet.exp * 100 / need) : 0, ui::C_GOLD);
        snprintf(buf, sizeof(buf), "EXP %d/%d", pet.exp, need);
        ui::drawAscii(canvas, 94, 21, buf, ui::C_MUTED, 1);

        struct Row { const char* label; int v; uint16_t col; };
        Row rows[6] = {
            {"饱", pet.hunger, ui::C_GOLD}, {"心", pet.mood, ui::C_YES},
            {"力", pet.energy, ui::C_MAYBE}, {"净", pet.clean, ui::C_PAPER},
            {"康", pet.health, ui::C_NO}, {"亲", pet.friendship, ui::rgb(240, 150, 190)},
        };
        for (int i = 0; i < 6; ++i) {
            int col = i % 2, row = i / 2;
            int x = 8 + col * 118, y = 34 + row * 24;
            cf::drawText(canvas, x, y, rows[i].label, ui::C_MUTED, cf::font16);
            bar(x + 20, y + 5, 52, 6, rows[i].v, rows[i].col);
        }

        snprintf(buf, sizeof(buf), "力%d 智%d 敏%d 胜%d 币%d", pet.str, pet.intl,
                 pet.agi, pet.wins, pet.coins);
        cf::drawText(canvas, 8, 104, buf, ui::C_PAPER, cf::font16);
        ui::drawAscii(canvas, ui::W - 84, 126, "RIGHT:DEX  ESC", ui::C_MUTED, 1);
    } else {
        cf::drawText(canvas, 8, 2, "图鉴", ui::C_PAPER, cf::font16);
        ui::drawAscii(canvas, ui::W - 62, 6, "LEFT:STAT", ui::C_MUTED, 1);
        canvas.drawFastHLine(8, 20, ui::W - 16, ui::C_LINE);

        for (int i = 0; i < 3; ++i) {
            int y = 28 + i * 34;
            bool unlocked = pet.stage >= i;
            cf::drawText(canvas, 12, y, unlocked ? STAGE_NAMES[i] : "？？？",
                         unlocked ? ui::C_GOLD : ui::C_MUTED, cf::font16);
            const char* cond = i == 0 ? "初始形态" : (i == 1 ? "Lv.10 进化" : "Lv.25 进化");
            cf::drawText(canvas, 96, y, cond, ui::C_MUTED, cf::font16);
            if (unlocked && i == pet.stage) {
                cf::drawText(canvas, 196, y, "当前", ui::C_YES, cf::font16);
            }
        }
    }
}

// ---------------- 设置 / 改名 ----------------
static void drawSettings() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 4, "设置", ui::C_PAPER, cf::font16);
    ui::drawAscii(canvas, ui::W - 52, 8, "ESC:BACK", ui::C_MUTED, 1);
    canvas.drawFastHLine(8, 22, ui::W - 16, ui::C_LINE);

    static const char* labels[5] = {"音效", "亮度", "难度", "改名", "存档"};
    char vals[5][12];
    snprintf(vals[0], 12, "%s", ui::soundOn ? "ON" : "OFF");
    snprintf(vals[1], 12, "%d%%", ui::brightPct);
    snprintf(vals[2], 12, "%s", difficulty ? "HARD" : "NORMAL");
    snprintf(vals[3], 12, "%s", pet.name);
    snprintf(vals[4], 12, "SAVE");

    for (int i = 0; i < 5; ++i) {
        int y = 28 + i * 19;
        if (i == settingsSel) {
            canvas.fillRoundRect(6, y - 2, ui::W - 12, 20, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(6, y - 2, ui::W - 12, 20, 4, ui::C_GOLD_D);
        }
        cf::drawText(canvas, 16, y, labels[i],
                     i == settingsSel ? ui::C_GOLD : ui::C_PAPER, cf::font16);
        if (i == 3) {
            int tw = cf::textWidth(vals[i], cf::font16);
            cf::drawText(canvas, ui::W - 16 - tw, y, vals[i],
                         settingsSel == i ? ui::C_PAPER : ui::C_MUTED, cf::font16);
        } else {
            ui::drawAscii(canvas, ui::W - 16 - 6 * (int)strlen(vals[i]), y + 4, vals[i],
                          settingsSel == i ? ui::C_PAPER : ui::C_MUTED, 1);
        }
    }
    ui::drawAsciiCentered(canvas, ui::W / 2, 126, "ENTER:CHANGE", ui::C_MUTED, 1);
}

static void drawName() {
    canvas.fillScreen(ui::C_BG);
    cf::drawText(canvas, 8, 6, "给它起个名字", ui::C_PAPER, cf::font16);
    canvas.drawFastHLine(8, 28, ui::W - 16, ui::C_LINE);
    canvas.drawRoundRect(12, 44, ui::W - 24, 34, 5, ui::C_GOLD_D);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    canvas.setTextColor(ui::C_PAPER);
    canvas.drawString(nameBuf, 20, 54);
    if ((millis() / 450) % 2 == 0) {
        int w = canvas.textWidth(nameBuf);
        canvas.fillRect(20 + w + 1, 54, 2, 16, ui::C_GOLD);
    }
    cf::drawCentered(canvas, 120, 92, "字母/数字，最多 10 个", ui::C_MUTED, cf::font16);
    ui::drawAsciiCentered(canvas, 120, 126, "ENTER:OK   ESC:CANCEL", ui::rgb(72, 86, 108), 1);
}

// ---------------- 小游戏 ----------------
static void startGame() {
    if (pet.energy < 10) {
        toastMsg("太累了，先休息");
        return;
    }
    for (auto& f : fruits) f.active = false;
    gamePetX = 120;
    gameScore = 0;
    gameResult = false;
    gameEndAt = millis() + 20000;
    gameNextSpawn = millis();
    screen = S_GAME;
    ui::beep(880, 40);
}

static void endGame() {
    int score = gameScore;
    int moodGain = 4 + score * 2;
    if (moodGain > 25) moodGain = 25;
    pet.mood = clamp8(pet.mood + moodGain);
    pet.hunger = clamp8(pet.hunger + (score > 8 ? 8 : score));
    pet.energy = clamp8(pet.energy - 10);
    pet.coins = clamp16(pet.coins + score / 3);
    addExp(3 + score * 2);
    gameResult = true;
    gameResultUntil = millis() + 1500;
    toastMsg("玩得很开心！");
    savePet();
}

static void updateGame(uint32_t now) {
    if (gameResult) {
        if (now > gameResultUntil) screen = S_HOME;
        return;
    }

    if (keyboard.leftHeld()) gamePetX -= 3.2f;
    if (keyboard.rightHeld()) gamePetX += 3.2f;
    if (gamePetX < 24) gamePetX = 24;
    if (gamePetX > ui::W - 24) gamePetX = ui::W - 24;

    if (now >= gameNextSpawn) {
        for (auto& f : fruits) {
            if (!f.active) {
                f.x = 22 + (esp_random() % (ui::W - 44));
                f.y = 40;
                f.active = true;
                break;
            }
        }
        gameNextSpawn = now + 480 + esp_random() % 260;
    }

    for (auto& f : fruits) {
        if (!f.active) continue;
        f.y += 2.4f;
        if (f.y > 104 && fabsf(f.x - gamePetX) < 20) {
            f.active = false;
            ++gameScore;
            ui::beep(1400, 12);
        } else if (f.y > 138) {
            f.active = false;
        }
    }

    if (now >= gameEndAt) endGame();
}

static void drawGame() {
    canvas.fillScreen(ui::C_BG);
    char buf[24];
    snprintf(buf, sizeof(buf), "得分 %d", gameScore);
    cf::drawText(canvas, 8, 4, buf, ui::C_PAPER, cf::font16);

    int remain = (int)((gameEndAt - millis()) / 1000);
    if (remain < 0) remain = 0;
    snprintf(buf, sizeof(buf), "%ds", remain);
    ui::drawAscii(canvas, ui::W - 30, 8, buf, ui::C_MUTED, 1);

    int total = 20000;
    int left = (int)(gameEndAt - millis());
    if (left < 0) left = 0;
    canvas.drawRoundRect(8, 22, ui::W - 16, 4, 2, ui::C_LINE);
    canvas.fillRect(8, 22, (ui::W - 16) * left / total, 4, ui::C_GOLD);

    for (auto& f : fruits) {
        if (!f.active) continue;
        canvas.fillCircle((int)f.x, (int)f.y, 5, ui::C_GOLD);
        canvas.drawCircle((int)f.x, (int)f.y, 5, ui::C_GOLD_D);
        canvas.drawPixel((int)f.x - 2, (int)f.y - 2, ui::C_PAPER);
    }

    drawPetArt((int)gamePetX, 120, pet.stage, pet.mood, false, false, false,
               millis(), 0.55f);

    if (gameResult) {
        canvas.fillRoundRect(40, 44, 160, 46, 8, ui::rgb(20, 30, 48));
        canvas.drawRoundRect(40, 44, 160, 46, 8, ui::C_GOLD_D);
        snprintf(buf, sizeof(buf), "得分 %d", gameScore);
        cf::drawCentered(canvas, 120, 54, buf, ui::C_GOLD, cf::font16);
        cf::drawCentered(canvas, 120, 74, "玩得很开心！", ui::C_PAPER, cf::font16);
    } else {
        ui::drawAsciiCentered(canvas, ui::W / 2, 126, "< > MOVE", ui::C_MUTED, 1);
    }
}

// ---------------- 战斗 ----------------
static const char* MOVE_NAMES[3][4] = {
    {"墨点", "泼墨", "静心", "逃跑"},
    {"笔锋", "泼墨", "砚甲", "逃跑"},
    {"墨龙击", "泼墨", "静心", "逃跑"},
};

static int enemyHeals = 0;

static void startBattle() {
    static const char* names[3] = {"野草团", "火苗", "水泡"};
    int k = (int)(esp_random() % 3);
    snprintf(enemy.name, sizeof(enemy.name), "野生的%s", names[k]);
    enemy.kind = k;

    int lv;
    if (difficulty) lv = (int)pet.level + 2 + (int)(esp_random() % 4) + streak;
    else lv = (int)pet.level + (int)(esp_random() % 3) + streak;
    if (lv < 1) lv = 1;
    enemy.level = lv;
    enemy.maxHp = enemy.hp = 18 + lv * 4 + (difficulty ? 8 : 0);
    enemy.atk = 4 + lv + (difficulty ? 1 : 0);
    enemy.def = 2 + lv / 2;
    enemyHeals = difficulty ? 2 : 1;

    petMaxHp = 30 + pet.level * 3 + pet.str * 2 + pet.stage * 6;
    petHp = petMaxHp;
    petDefBuff = 0;
    battleSel = 0;
    battleOver = false;
    battleWon = false;
    battleNext = false;

    if (streak > 0)
        snprintf(battleMsg, sizeof(battleMsg), "连战 %d：%s 出现了！", streak + 1,
                 enemy.name);
    else
        snprintf(battleMsg, sizeof(battleMsg), "%s 出现了！", enemy.name);
    screen = S_BATTLE;
}

static void battleWin() {
    battleOver = true;
    battleWon = true;
    ++streak;
    int exp = 14 + enemy.level * 3 + streak * 3;
    int coins = 6 + (int)(esp_random() % 10) + streak * 2;
    pet.coins = clamp16(pet.coins + coins);
    ++pet.wins;
    pet.mood = clamp8(pet.mood + 6);
    snprintf(battleMsg, sizeof(battleMsg), "胜利！+%d经验 +%d币", exp, coins);
    addExp(exp);
    battleNext = (streak < 4) && (esp_random() % 100 < 45);
    savePet();
}

static void battleLose() {
    battleOver = true;
    battleWon = false;
    streak = 0;
    pet.health = clamp8(pet.health - 12);
    pet.mood = clamp8(pet.mood - 8);
    snprintf(battleMsg, sizeof(battleMsg), "输了…带它回去休息");
    checkSick();
    savePet();
}

static void enemyTurn() {
    if (enemy.hp <= 0) return;
    char tmp[48];

    if (enemy.hp * 100 / enemy.maxHp < 35 && enemyHeals > 0 &&
        (int)(esp_random() % 100) < (difficulty ? 70 : 45)) {
        int heal = enemy.maxHp / 3;
        enemy.hp += heal;
        if (enemy.hp > enemy.maxHp) enemy.hp = enemy.maxHp;
        --enemyHeals;
        snprintf(tmp, sizeof(tmp), "对手回复了体力");
    } else {
        bool special = (int)(esp_random() % 100) < (difficulty ? 35 : 20);
        int power = special ? 7 : 3;
        if (petDefBuff > 0) power = power / 2;
        int def = 2 + pet.level / 3 + pet.agi / 2;
        int dmg = enemy.atk + power - def / 2 + (int)(esp_random() % 5) - 2;
        if (dmg < 1) dmg = 1;
        petHp -= dmg;
        if (petHp < 0) petHp = 0;
        if (special)
            snprintf(tmp, sizeof(tmp), "%s吐出气息，-%d", enemy.name, dmg);
        else
            snprintf(tmp, sizeof(tmp), "%s撞过来，-%d", enemy.name, dmg);
        ui::beep(420, 25);
    }

    if (petDefBuff > 0) --petDefBuff;
    snprintf(battleMsg, sizeof(battleMsg), "%s", tmp);
}

static void drawBattle();

static void battleMove(int m) {
    if (battleOver) return;
    const int stage = pet.stage;

    if (m == 3) {  // 逃跑
        if ((int)(esp_random() % 100) < 60) {
            battleOver = true;
            battleWon = false;
            streak = 0;
            snprintf(battleMsg, sizeof(battleMsg), "成功逃走了");
            savePet();
            return;
        }
        snprintf(battleMsg, sizeof(battleMsg), "没逃掉！");
    } else if (m == 2) {  // 辅助
        if (stage == 1) {
            petDefBuff = 3;
            snprintf(battleMsg, sizeof(battleMsg), "砚甲！3 回合内受伤减半");
            ui::beep(700, 40);
        } else {
            int heal = petMaxHp * 35 / 100;
            petHp += heal;
            if (petHp > petMaxHp) petHp = petMaxHp;
            pet.energy = clamp8(pet.energy + 10);
            snprintf(battleMsg, sizeof(battleMsg), "静心，回复%d点体力", heal);
            ui::beep(760, 50);
        }
    } else {  // 攻击
        int acc = 100, power = 0, cost = 0;
        if (m == 0) {
            if (stage == 2) {
                power = 6 + pet.level + pet.intl;
                acc = 80;
                cost = 8;
            } else {
                power = 3 + pet.level / 2 + pet.str;
            }
        } else {
            power = 7 + pet.intl;
            acc = 85;
            cost = 6;
        }

        if (pet.energy < cost) {
            snprintf(battleMsg, sizeof(battleMsg), "精力不够，用不了这招");
            return;
        }
        pet.energy = clamp8(pet.energy - cost);

        if ((int)(esp_random() % 100) >= acc) {
            snprintf(battleMsg, sizeof(battleMsg), "%s没有命中…",
                     MOVE_NAMES[stage][m]);
            ui::beep(300, 40);
        } else {
            int atk = 6 + pet.level + pet.str;
            int dmg = atk + power - enemy.def / 2 + (int)(esp_random() % 5) - 2;
            bool crit = (int)(esp_random() % 100) <
                        ((m == 0 && stage == 1) ? 25 : 10);
            if (crit) dmg = dmg * 3 / 2;
            if (dmg < 1) dmg = 1;
            enemy.hp -= dmg;
            if (enemy.hp < 0) enemy.hp = 0;
            snprintf(battleMsg, sizeof(battleMsg), "%s%s！-%d点",
                     crit ? "会心！" : "", MOVE_NAMES[stage][m], dmg);
            ui::beep(crit ? 1400 : 1000, crit ? 60 : 30);
        }
    }

    // 先把这一击画出来，等一会，再让对手出手
    drawBattle();
    canvas.pushSprite(0, 0);
    delay(650);
    KeyEvent drain;
    while (keyboard.pop(drain)) {}

    if (enemy.hp <= 0) {
        battleWin();
        return;
    }
    enemyTurn();
    drawBattle();
    canvas.pushSprite(0, 0);
    delay(550);
    if (petHp <= 0) battleLose();
}

static void drawBattle() {
    canvas.fillScreen(ui::C_BG);

    // 敌
    cf::drawText(canvas, 8, 4, enemy.name, ui::C_PAPER, cf::font16);
    char buf[20];
    snprintf(buf, sizeof(buf), "Lv.%d", enemy.level);
    ui::drawAscii(canvas, 120, 8, buf, ui::C_MUTED, 1);
    bar(8, 22, 100, 5, enemy.maxHp ? enemy.hp * 100 / enemy.maxHp : 0, ui::C_NO);

    // 我方
    cf::drawText(canvas, 8, 42, pet.name, ui::C_PAPER, cf::font16);
    snprintf(buf, sizeof(buf), "Lv.%d", pet.level);
    ui::drawAscii(canvas, 120, 46, buf, ui::C_MUTED, 1);
    bar(8, 60, 100, 5, petMaxHp ? petHp * 100 / petMaxHp : 0, ui::C_YES);

    drawEnemyArt(200, 48, enemy.kind, millis());
    drawPetArt(200, 100, pet.stage, pet.mood, false, false, false, millis(), 0.5f);

    // 连战 / 防御状态
    if (streak >= 2) {
        char st[12];
        snprintf(st, sizeof(st), "x%d", streak);
        ui::drawAscii(canvas, 150, 4, st, ui::C_WAIT, 1);
    }
    if (petDefBuff > 0) {
        cf::drawText(canvas, 150, 58, "盾", ui::C_MAYBE, cf::font16);
    }

    // 日志
    cf::drawText(canvas, 8, 70, battleMsg, ui::C_GOLD, cf::font16);

    // 指令（名字随阶段变化）
    for (int i = 0; i < 4; ++i) {
        int x = 6 + i * 58, y = 104;
        if (i == battleSel && !battleOver) {
            canvas.fillRoundRect(x, y, 54, 22, 4, ui::rgb(24, 36, 56));
            canvas.drawRoundRect(x, y, 54, 22, 4, ui::C_GOLD_D);
        } else if (!battleOver) {
            canvas.drawRoundRect(x, y, 54, 22, 4, ui::C_LINE);
        }
        cf::drawCentered(canvas, x + 27, y + 3, MOVE_NAMES[pet.stage][i],
                         i == battleSel && !battleOver ? ui::C_GOLD : ui::C_MUTED,
                         cf::font16);
    }

    if (battleOver) {
        const char* hint = "ENTER:BACK";
        if (battleWon && battleNext) hint = "ENTER:CHAIN  ESC:HOME";
        ui::drawAsciiCentered(canvas, ui::W / 2, 126, hint, ui::C_PAPER, 1);
    } else {
        ui::drawAsciiCentered(canvas, ui::W / 2, 126, "< > SELECT  ENTER:GO",
                              ui::C_MUTED, 1);
    }
}

// ---------------- 进化 ----------------
static void drawEvolve() {
    canvas.fillScreen(ui::C_BG);
    uint32_t t = millis() - evolveAt;
    bool flash = (t / 180) % 2 == 0;
    if (flash) canvas.fillRect(0, 0, ui::W, ui::H, ui::rgb(30, 44, 66));

    cf::drawCentered(canvas, 120, 18, "进化中…", ui::C_GOLD, cf::font24);
    drawPetArt(120, 112, flash ? pet.stage : evolveTo, 100, false, false, false,
               millis(), 1.0f);
}

// ---------------- 交互 ----------------
static void cleanPoop() {
    if (pet.poop == 0 && pet.clean > 90) {
        toastMsg("已经很干净了");
        return;
    }
    pet.poop = 0;
    pet.clean = 100;
    pet.mood = clamp8(pet.mood + 5);
    addExp(4);
    toastMsg("清理干净了");
    savePet();
}

static void feed(int i) {
    if (pet.hunger > 92) {
        toastMsg("吃不下了");
        return;
    }
    if (i == 0) {
        pet.hunger = clamp8(pet.hunger + 18);
        pet.mood = clamp8(pet.mood + 3);
    } else if (i == 1) {
        pet.hunger = clamp8(pet.hunger + 30);
        pet.mood = clamp8(pet.mood + 1);
    } else {
        pet.hunger = clamp8(pet.hunger + 12);
        pet.mood = clamp8(pet.mood + 12);
        pet.health = clamp8(pet.health - 2);
    }
    pet.friendship = clamp8(pet.friendship + 1);
    addExp(3);
    toastMsg("吃得很香");
    savePet();
    screen = S_HOME;
}

static void train(int i) {
    if (pet.energy < 10) {
        toastMsg("精力不够，先睡会");
        return;
    }
    if (pet.sick) {
        toastMsg("生病了，不能训练");
        return;
    }
    pet.energy = clamp8(pet.energy - 10);
    if (i == 0) ++pet.str;
    else if (i == 1) ++pet.intl;
    else ++pet.agi;
    pet.mood = clamp8(pet.mood - 2);
    pet.friendship = clamp8(pet.friendship + 1);
    addExp(8);
    toastMsg("训练完成");
    savePet();
    screen = S_HOME;
}

static void toggleSleep() {
    if (pet.energy >= 95) {
        toastMsg("精神得很");
        return;
    }
    // 直接补一觉：消耗 20 饱食，恢复 45 精力
    pet.hunger = clamp8(pet.hunger - 20);
    pet.energy = clamp8(pet.energy + 45);
    pet.health = clamp8(pet.health + 3);
    pet.mood = clamp8(pet.mood + 3);
    addExp(2);
    toastMsg("睡了一觉");
    savePet();
    screen = S_HOME;
}

static void goOut() {
    if (pet.energy < 10) {
        toastMsg("太累了，走不动");
        return;
    }
    if (pet.sick) {
        toastMsg("生病了，先养好");
        return;
    }
    pet.energy = clamp8(pet.energy - 8);
    if (esp_random() % 100 < 65) {
        startBattle();
    } else {
        int coins = 3 + esp_random() % 8;
        pet.coins = clamp16(pet.coins + coins);
        pet.hunger = clamp8(pet.hunger + 5);
        addExp(4);
        toastMsg("捡到了一点东西");
        savePet();
        screen = S_HOME;
    }
}

static void handleKey(const KeyEvent& ev) {
    lastTouch = millis();
    const bool confirm = (ev.kind == KEY_ENTER);

    switch (screen) {
        case S_HOME:
            if (confirm) {
                menuSel = 0;
                screen = S_MENU;
            } else if (ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT) {
                pet.mood = clamp8(pet.mood + 2);
                pet.friendship = clamp8(pet.friendship + 1);
                toastMsg("蹭了蹭你");
            } else if (ev.kind == KEY_CHAR && ev.ch == 's') {
                savePet();
                toastMsg("已存档");
            }
            break;

        case S_MENU:
            if (ev.kind == KEY_UP) menuSel = (menuSel + MENU_N - 1) % MENU_N;
            else if (ev.kind == KEY_DOWN) menuSel = (menuSel + 1) % MENU_N;
            else if (ev.kind == KEY_LEFT || ev.kind == KEY_RIGHT) {
                if (ev.kind == KEY_LEFT && menuSel % 2 == 1) --menuSel;
                if (ev.kind == KEY_RIGHT && menuSel % 2 == 0) ++menuSel;
            } else if (confirm) {
                switch (menuSel) {
                    case 0: feedSel = 0; screen = S_FEED; break;
                    case 1: startGame(); break;
                    case 2: trainSel = 0; screen = S_TRAIN; break;
                    case 3: cleanPoop(); screen = S_HOME; break;
                    case 4: toggleSleep(); break;
                    case 5: goOut(); break;
                    case 6: dexPage = false; screen = S_STATUS; break;
                    case 7: settingsSel = 0; screen = S_SETTINGS; break;
                }
            } else if (ev.kind == KEY_ESC || ev.kind == KEY_BACKSPACE) {
                screen = S_HOME;
            }
            break;

        case S_FEED:
            if (ev.kind == KEY_UP) feedSel = (feedSel + 2) % 3;
            else if (ev.kind == KEY_DOWN) feedSel = (feedSel + 1) % 3;
            else if (confirm) feed(feedSel);
            else if (ev.kind == KEY_ESC) screen = S_MENU;
            break;

        case S_TRAIN:
            if (ev.kind == KEY_UP) trainSel = (trainSel + 2) % 3;
            else if (ev.kind == KEY_DOWN) trainSel = (trainSel + 1) % 3;
            else if (confirm) train(trainSel);
            else if (ev.kind == KEY_ESC) screen = S_MENU;
            break;

        case S_STATUS:
            if (ev.kind == KEY_RIGHT) dexPage = true;
            else if (ev.kind == KEY_LEFT) dexPage = false;
            else if (ev.kind == KEY_ESC) screen = S_MENU;
            break;

        case S_SETTINGS:
            if (ev.kind == KEY_UP) settingsSel = (settingsSel + 4) % 5;
            else if (ev.kind == KEY_DOWN) settingsSel = (settingsSel + 1) % 5;
            else if (ev.kind == KEY_ESC) {
                ui::saveSoundBright();
                screen = S_MENU;
            } else if (confirm) {
                if (settingsSel == 0) {
                    ui::soundOn = !ui::soundOn;
                    ui::beep(900, 30);
                } else if (settingsSel == 1) {
                    int b = ui::brightPct + 20;
                    if (b > 100) b = 20;
                    ui::brightPct = (uint8_t)b;
                    ui::applyBrightness();
                } else if (settingsSel == 2) {
                    difficulty = difficulty ? 0 : 1;
                    ui::prefs.putUChar("diff", difficulty);
                    toastMsg(difficulty ? "困难模式" : "普通模式");
                } else if (settingsSel == 3) {
                    snprintf(nameBuf, sizeof(nameBuf), "%s", pet.name);
                    nameLen = strlen(nameBuf);
                    screen = S_NAME;
                } else {
                    savePet();
                    toastMsg("已存档");
                }
            }
            break;

        case S_NAME:
            if (confirm) {
                if (nameLen > 0) {
                    snprintf(pet.name, sizeof(pet.name), "%s", nameBuf);
                    savePet();
                }
                screen = S_SETTINGS;
            } else if (ev.kind == KEY_ESC) {
                screen = S_SETTINGS;
            } else if (ev.kind == KEY_BACKSPACE) {
                if (nameLen > 0) {
                    int n = nameLen - 1;
                    while (n > 0 && ((uint8_t)nameBuf[n] & 0xC0) == 0x80) --n;
                    nameLen = n;
                    nameBuf[nameLen] = 0;
                }
            } else if (ev.kind == KEY_CHAR) {
                if (nameLen < (int)sizeof(nameBuf) - 1) {
                    nameBuf[nameLen++] = ev.ch;
                    nameBuf[nameLen] = 0;
                }
            }
            break;

        case S_GAME:
            if (ev.kind == KEY_ESC) {
                endGame();
            }
            break;

        case S_BATTLE:
            if (battleOver) {
                if (confirm) {
                    if (battleWon && battleNext) {
                        startBattle();
                    } else {
                        streak = 0;
                        screen = S_HOME;
                    }
                } else if (ev.kind == KEY_ESC) {
                    streak = 0;
                    screen = S_HOME;
                }
                break;
            }
            if (ev.kind == KEY_LEFT) battleSel = (battleSel + 3) % 4;
            else if (ev.kind == KEY_RIGHT) battleSel = (battleSel + 1) % 4;
            else if (confirm) battleMove(battleSel);
            else if (ev.kind == KEY_ESC) {
                battleOver = true;
                battleWon = false;
                snprintf(battleMsg, sizeof(battleMsg), "跑掉了");
            }
            break;

        case S_EVOLVE:
            break;
    }
}

// ---------------- Arduino ----------------
void setup() {
    if (!ui::begin("pet")) return;
    keyboard.begin();
    loadPet();
    bootAt = millis();
    lastTick = millis();
    lastSave = millis();

    Serial.printf("[pet] rot=%d %dx%d kb=%d level=%d stage=%d\n",
                  (int)M5.Display.getRotation(), (int)M5.Display.width(),
                  (int)M5.Display.height(), keyboard.started() ? 1 : 0,
                  pet.level, pet.stage);
}

void loop() {
    static uint32_t lastDraw = 0;
    static bool dirty = true;

    M5.update();
    keyboard.poll();

    KeyEvent ev;
    while (keyboard.pop(ev)) {
        handleKey(ev);
        dirty = true;
    }

    uint32_t now = millis();

    if (now - lastTick > 15000) {
        lastTick = now;
        tickPet();
        dirty = true;
    }

    if (screen == S_HOME && now < toastUntil) dirty = true;

    if (screen == S_GAME) {
        updateGame(now);
        dirty = true;
    }

    if (pendingEvolve && screen == S_HOME) {
        pendingEvolve = false;
        evolveIfNeeded();
        dirty = true;
    }

    if (screen == S_EVOLVE && now - evolveAt > 2200) {
        pet.stage = evolveTo;
        pet.mood = clamp8(pet.mood + 20);
        pet.health = 100;
        savePet();
        toastMsg("进化成功！");
        screen = S_HOME;
        dirty = true;
    }

    // 首页空闲：偶尔眨眼/晃动
    const uint32_t interval = (screen == S_GAME || screen == S_BATTLE ||
                               screen == S_EVOLVE) ? 33 : 200;
    if (!dirty && now - lastDraw < interval) {
        delay(8);
        return;
    }

    switch (screen) {
        case S_HOME:     drawHome(); break;
        case S_MENU:     drawMenu(); break;
        case S_FEED:     drawFeed(); break;
        case S_TRAIN:    drawTrain(); break;
        case S_STATUS:   drawStatus(); break;
        case S_SETTINGS: drawSettings(); break;
        case S_NAME:     drawName(); break;
        case S_GAME:     drawGame(); break;
        case S_BATTLE:   drawBattle(); break;
        case S_EVOLVE:   drawEvolve(); break;
    }

    canvas.pushSprite(0, 0);
    lastDraw = now;
    dirty = false;
}
