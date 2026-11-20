#include "packs.h"

#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <strings.h>

namespace packs {

Entry entries[MAX_ENTRIES];
int count = 0;
char loadedName[PACK_NAME_MAX] = "";
Info list[MAX_PACKS];
int listCount = 0;

static char paths[MAX_PACKS][64];
static bool sdReady = false;

static constexpr int PIN_SCK = 40;
static constexpr int PIN_MISO = 39;
static constexpr int PIN_MOSI = 14;
static constexpr int PIN_CS = 12;

static void copyUtf8(char* dst, const char* src, int maxBytes) {
    int n = 0;
    while (src[n] && n < maxBytes - 1) ++n;
    // 不要把多字节字符切成两半
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) --n;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void trim(char* s) {
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' ||
                     s[n - 1] == '\t'))
        s[--n] = 0;
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') ++start;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

static bool endsWithTxt(const char* name) {
    int n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
}

static const char* baseName(const char* name) {
    const char* slash = strrchr(name, '/');
    return slash ? slash + 1 : name;
}

void scan() {
    listCount = 0;
    if (!sdReady) {
        SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
        sdReady = SD.begin(PIN_CS, SPI, 4000000);
    }
    if (!sdReady) return;

    File dir = SD.open("/answerbook");
    if (!dir || !dir.isDirectory()) return;

    while (listCount < MAX_PACKS) {
        File f = dir.openNextFile();
        if (!f) break;
        const char* nm = f.name();
        if (!f.isDirectory() && endsWithTxt(nm)) {
            copyUtf8(list[listCount].name, baseName(nm), PACK_NAME_MAX);
            char* dot = strrchr(list[listCount].name, '.');
            if (dot) *dot = 0;
            snprintf(paths[listCount], sizeof(paths[0]), "/answerbook/%s",
                     baseName(nm));
            ++listCount;
        }
        f.close();
    }
    dir.close();
}

bool load(int index) {
    if (index < 0 || index >= listCount) {
        count = 0;
        loadedName[0] = 0;
        return false;
    }

    File f = SD.open(paths[index]);
    if (!f) return false;

    count = 0;
    copyUtf8(loadedName, list[index].name, PACK_NAME_MAX);

    while (f.available() && count < MAX_ENTRIES) {
        char line[160];
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = 0;
        trim(line);
        if (!line[0]) continue;

        if (line[0] == '#') {
            if (strncasecmp(line, "#name ", 6) == 0) {
                copyUtf8(loadedName, line + 6, PACK_NAME_MAX);
            }
            continue;
        }

        // zh|why|tone|cats
        char* parts[4] = {line, nullptr, nullptr, nullptr};
        int p = 1;
        for (char* c = line; *c && p < 4; ++c) {
            if (*c == '|') {
                *c = 0;
                parts[p++] = c + 1;
            }
        }

        Entry& e = entries[count];
        copyUtf8(e.zh, parts[0], TEXT_MAX);
        copyUtf8(e.why, parts[1] ? parts[1] : "", TEXT_MAX);
        e.tone = TONE_MAYBE;
        e.cats = C_ALL;

        if (parts[2]) {
            trim(parts[2]);
            if (!strcasecmp(parts[2], "yes") || !strcmp(parts[2], "是"))
                e.tone = TONE_YES;
            else if (!strcasecmp(parts[2], "no") || !strcmp(parts[2], "否"))
                e.tone = TONE_NO;
            else if (!strcasecmp(parts[2], "wait") || !strcmp(parts[2], "等"))
                e.tone = TONE_WAIT;
            else
                e.tone = TONE_MAYBE;
        }

        if (parts[3]) {
            uint16_t mask = 0;
            char* save = nullptr;
            trim(parts[3]);
            for (char* tok = strtok_r(parts[3], ",", &save); tok;
                 tok = strtok_r(nullptr, ",", &save)) {
                trim(tok);
                if (!strcasecmp(tok, "all") || !strcmp(tok, "全部")) mask = C_ALL;
                else if (!strcasecmp(tok, "love") || !strcmp(tok, "爱情")) mask |= CL;
                else if (!strcasecmp(tok, "work") || !strcmp(tok, "事业")) mask |= CW;
                else if (!strcasecmp(tok, "study") || !strcmp(tok, "学业")) mask |= CS;
                else if (!strcasecmp(tok, "money") || !strcmp(tok, "财运")) mask |= CM;
                else if (!strcasecmp(tok, "health") || !strcmp(tok, "健康")) mask |= CH;
                else if (!strcasecmp(tok, "people") || !strcmp(tok, "人际")) mask |= CP;
                else if (!strcasecmp(tok, "travel") || !strcmp(tok, "出行")) mask |= CT;
                else if (!strcasecmp(tok, "choice") || !strcmp(tok, "选择")) mask |= CC;
            }
            if (mask) e.cats = mask;
        }

        if (e.zh[0]) ++count;
    }
    f.close();
    return count > 0;
}

const char* name(int index) {
    if (index < 0) return "内置";
    if (index >= listCount) return "内置";
    return list[index].name;
}

}  // namespace packs
