// (test only, never a player feature) Start the game somewhere else: values of game_data.sav rewritten as the game
// reads the file, which itself stays as it is. With a scratch save folder (SAVAGEWIND_SAVE_DIR) and "Continue", a
// scripted test run loads straight into another region, at another hour or in other weather.
//
//   SAVAGEWIND_TEST_WARP=<place> | <x>,<y>,<z>[,<map type>/<map name>]   where Link appears (PlayerSavePos and the
//       map it is on; for MainField the section, e.g. D-6, follows from x and z). Places: see warp_places below.
//       A warp also hands Link the paraglider (IsGet_PlayerStole2), which opens the Great Plateau.
//   SAVAGEWIND_TEST_TIME=<hour 0-24>         the hour of the day, held there: the game takes no time of day from
//                                            the save on Continue (WM_Time is ignored), so test_warp_frame writes
//                                            both WM_Time buffers and stops TimeMgr's native clock (0..360 = 24 h)
//   SAVAGEWIND_TEST_WEATHER=<n>              the weather, held, and every forecast (climateWeather*): 0 clear, 1 cloudy,
//                                            2 rain, 3 heavy rain, 4 snow, 5 heavy snow, 6 thunderstorm, 7 thunder rain,
//                                            8 rain in sunshine (the forecasts alone change nothing at load)
//   SAVAGEWIND_TEST_FLAGS=<name>=<value>[,...]  any other s32 / bool (integer) or f32 (with a '.') value
//
// game_data.sav: a 0xC-byte header, then 8-byte entries (crc32 of the flag name, 32-bit value). Vectors, arrays and
// strings are runs of entries with the same hash (x, y, z / one per element / 4 characters each).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "../game_sites.h"
#include "../hle.h"

typedef struct { const char *name; float x, y, z; const char *map_type, *map_name; } WarpPlace;
// Positions are the game's own start positions near each place (Map/MainField/Static.smubin and
// Map/MainFieldDungeon/Static.smubin in Bootup.pack: StartPos entries).
static const WarpPlace warp_places[] = {
    {"plateau", -1041.6f, 250.2f, 1812.1f},          // the Great Plateau, outside the Shrine of Resurrection
    {"kakariko", 1811.7f, 218.5f, 982.6f},           // Kakariko Village (HorseGate)
    {"stable", 384.8f, 121.7f, 1035.6f},             // Riverside Stable (OldKorok_RiversideStable)
    {"hateno", 3437.5f, 235.9f, 2101.5f},            // Hateno Village, goddess statue (GoddessHateno)
    {"castle", -254.0f, 140.6f, -544.6f},            // Hyrule Castle, front (HyruleCastleExit)
    {"castle_in", -156.6f, 144.2f, -1159.3f},        // inside Hyrule Castle, by its shrine (StartPos_CDungeon089)
    {"desert", -3196.0f, 129.4f, 2549.3f},           // Gerudo Desert, Kara Kara Bazaar (Oasis001)
    {"gerudo", -3784.7f, 149.1f, 2864.5f},           // Gerudo Town gate (Gerudo001)
    {"hebra", -2165.2f, 529.2f, -2029.5f},           // Hebra, on top of its tower (StartPos_Tower01)
    {"zora", 3315.6f, 255.6f, -501.0f},              // Zora's Domain, goddess statue (GoddessZora)
    {"goron", 1719.0f, 498.9f, -2456.2f},            // Goron City (GoddessGoron)
    {"rito", -3607.2f, 286.3f, -1805.5f},            // Rito Village (GoddessRito)
    {"ruta", 48.0f, 6.3f, 3.0f, "MainFieldDungeon", "RemainsWater"},     // inside Divine Beast Vah Ruta (Entrance_1)
    {"rudania", 0.0f, 16.8f, 69.5f, "MainFieldDungeon", "RemainsFire"},  // inside Divine Beast Vah Rudania
};

static uint32_t crc32_of(const char *s) {
    uint32_t c = 0xFFFFFFFFu;
    for (; *s; s++) {
        c ^= (uint8_t)*s;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

// Every entry of `name` from the first, in order: n values (u32 bits) copied in; returns how many entries matched.
static int set_entries(uint8_t *d, uint64_t size, const char *name, const uint32_t *v, int n, bool repeat_last) {
    uint32_t h = crc32_of(name);
    int k = 0;
    for (uint64_t o = 0xC; o + 8 <= size; o += 8) {
        if (*(uint32_t *)(d + o) != h) continue;
        if (k < n) *(uint32_t *)(d + o + 4) = v[k];
        else if (repeat_last && n) *(uint32_t *)(d + o + 4) = v[n - 1];
        k++;
    }
    if (!k) LOG("[warp] %s is not in the save\n", name);
    return k;
}
static void set_f32(uint8_t *d, uint64_t size, const char *name, const float *f, int n) {
    uint32_t v[4];
    memcpy(v, f, (size_t)n * 4);
    set_entries(d, size, name, v, n, false);
}
static void set_u32(uint8_t *d, uint64_t size, const char *name, uint32_t v) { set_entries(d, size, name, &v, 1, false); }
static void set_str(uint8_t *d, uint64_t size, const char *name, const char *s) {
    uint32_t v[16] = {0};  // (string_data: 32 bytes, 8 entries)
    strncpy((char *)v, s, sizeof v - 1);
    set_entries(d, size, name, v, 16, false);
}

bool test_warp_active(void) {
    return getenv("SAVAGEWIND_TEST_WARP") || getenv("SAVAGEWIND_TEST_TIME") || getenv("SAVAGEWIND_TEST_WEATHER") ||
           getenv("SAVAGEWIND_TEST_FLAGS");
}

void test_warp_patch(uint8_t *d, uint64_t size) {
    const char *w = getenv("SAVAGEWIND_TEST_WARP");
    if (w && *w) {
        WarpPlace p = {0};
        bool found = false;
        for (size_t i = 0; i < sizeof warp_places / sizeof warp_places[0]; i++)
            if (!strcmp(w, warp_places[i].name)) { p = warp_places[i]; found = true; }
        static char type[32], name[32];
        if (!found && sscanf(w, "%f,%f,%f", &p.x, &p.y, &p.z) == 3) {
            found = true;
            const char *m = strchr(w, '/') ? strrchr(w, ',') : NULL;  // ",<map type>/<map name>" after the third number
            if (m && sscanf(m + 1, "%31[^/]/%31s", type, name) == 2) { p.map_type = type; p.map_name = name; }
        }
        if (!found) {
            LOG("[warp] unknown place '%s'; places:", w);
            for (size_t i = 0; i < sizeof warp_places / sizeof warp_places[0]; i++) LOG(" %s", warp_places[i].name);
            LOG("\n");
        } else {
            char section[8];
            if (!p.map_type) {  // MainField sections: 1000 x 1000, columns A-J from x = -5000, rows 1-8 from z = -4000
                int col = (int)((p.x + 5000.0f) / 1000.0f), row = (int)((p.z + 4000.0f) / 1000.0f) + 1;
                snprintf(section, sizeof section, "%c-%d", 'A' + (col < 0 ? 0 : col > 9 ? 9 : col), row < 1 ? 1 : row > 8 ? 8 : row);
                p.map_type = "MainField";
                p.map_name = section;
            }
            float pos[3] = {p.x, p.y, p.z};
            set_f32(d, size, "PlayerSavePos", pos, 3);
            set_str(d, size, "PlayerSavePosMapType", p.map_type);
            set_str(d, size, "PlayerSavePosMapName", p.map_name);
            if (!getenv("SAVAGEWIND_WARP_ONCE")) set_u32(d, size, "IsGet_PlayerStole2", 1);  // (not for a player's teleport)
            LOG("[warp] Link starts at %.1f, %.1f, %.1f on %s/%s\n", p.x, p.y, p.z, p.map_type, p.map_name);
        }
    }
    if (w && getenv("SAVAGEWIND_WARP_ONCE")) {  // (a player's teleport: this one load)
        extern void teleport_done(void);
        teleport_done();
    }
    const char *t = getenv("SAVAGEWIND_TEST_TIME");
    if (t && *t) {
        float wm = (float)atof(t) * 15.0f;
        set_f32(d, size, "WM_Time", &wm, 1);
        LOG("[warp] hour %s (WM_Time %.1f)\n", t, wm);
    }
    const char *wx = getenv("SAVAGEWIND_TEST_WEATHER");
    if (wx && *wx) {
        uint32_t v = (uint32_t)atoi(wx);
        set_entries(d, size, "climateWeather", &v, 1, true);
        set_entries(d, size, "climateWeather2", &v, 1, true);
        set_entries(d, size, "climateWeather3", &v, 1, true);
        LOG("[warp] weather %u in every forecast\n", v);
    }
    const char *fl = getenv("SAVAGEWIND_TEST_FLAGS");
    for (const char *s = fl; s && *s;) {
        char name[128], val[64];
        if (sscanf(s, "%127[^=]=%63[^,]", name, val) != 2) break;
        if (strchr(val, '.')) { float f = (float)atof(val); set_f32(d, size, name, &f, 1); }
        else set_u32(d, size, name, (uint32_t)strtol(val, NULL, 0));
        LOG("[warp] %s = %s\n", name, val);
        s = strchr(s, ',');
        if (s) s++;
    }
}

// From Savage Wind (ed9279a): TimeMgr::calc reloads WM_Time from GameDataMgr each update, so writing TimeMgr's own time
// every frame fought it: for a frame now and then the sun stood where the game's clock said, and every shadow jumped.
// Instead both WM_Time buffers are written once and TimeMgr's native clock is stopped.
static bool mem_ok(uint64_t addr, size_t n) { return addr >= GUEST_LO && addr + n <= GUEST_HI; }
static bool mem_peek(uint64_t addr, void *buf, size_t n) { return mem_ok(addr, n) && (memcpy(buf, (const void *)(uintptr_t)addr, n), true); }
static bool mem_poke(uint64_t addr, const void *buf, size_t n) { return mem_ok(addr, n) && (memcpy((void *)(uintptr_t)addr, buf, n), true); }
static uint64_t read_pointer(uint64_t addr) {
    uint64_t p = 0;
    return mem_peek(addr, &p, sizeof p) ? p : 0;
}

// TimeMgr::calc reloads WM_Time from GameDataMgr each update. Its vtable's calc slot starts with an ADRP/ADD reference
// to that singleton: decoded here rather than pinning a 1.9.0 address. The whole load/branch sequence is required, and a
// singleton in main's data segment.
static uint64_t time_data_site(uint64_t tm) {
    static uint64_t sampled_manager, game_data_site;
    if (tm == sampled_manager && game_data_site) return game_data_site;
    uint64_t calc = read_pointer(read_pointer(tm) + 0x30);
    uint32_t code[32];
    if (calc < MAIN_BASE || calc >= g_site.main_data || !mem_peek(calc, code, sizeof code)) return 0;
    for (unsigned i = 0; i + 3 < sizeof code / sizeof *code; i++) {
        if ((code[i] & 0x9f00001f) != 0x90000008 || (code[i + 1] & 0xffc003ff) != 0x91000108 ||
            code[i + 2] != 0xf9400114 || (code[i + 3] & 0xff00001f) != 0xb5000014) continue;
        uint32_t imm = ((code[i] >> 5) & 0x7ffff) << 2 | ((code[i] >> 29) & 3);
        int64_t pages = (int64_t)(imm ^ 0x100000) - 0x100000;
        uint64_t site = ((calc + 4 * i) & ~0xfffull) + pages * 4096 + ((code[i + 1] >> 10) & 0xfff);
        if (site < g_site.main_data || site + 8 > g_site.main_end) continue;
        sampled_manager = tm;
        return game_data_site = site;
    }
    return 0;
}

// The pending-write buffer uses proxy flags (value at +0x10, primary flag at +8); the current-read buffer uses primary
// flags (value at +0x18, name hash at +0x1c). Each float getter and the primary's WM_Time hash are checked.
static bool time_flags(uint64_t tm, uint64_t flags[2]) {
    uint64_t site = time_data_site(tm), mgr = site ? read_pointer(site) : 0;
    uint32_t handle;
    if (!mgr || !mem_peek(tm + 0xd4, &handle, sizeof handle) || handle == UINT32_MAX) return false;
    handle &= 0xffffff;
    for (int i = 0; i < 2; i++) {
        uint64_t buffer = read_pointer(read_pointer(mgr + 0xbd8 + 8 * i));
        uint32_t count, hash;
        if (!buffer || !mem_peek(buffer + 0x28, &count, sizeof count) || handle >= count) return false;
        uint64_t array = read_pointer(buffer + 0x30);
        uint64_t flag = array ? read_pointer(array + 8 * handle) : 0;
        uint64_t getter = read_pointer(read_pointer(flag) + 0x88), primary = flag;
        uint32_t code[2], value_offset;
        if (!getter || !mem_peek(getter, code, sizeof code) || code[1] != 0xd65f03c0) return false;
        if (code[0] == 0xbd401800) value_offset = 0x18;  // ldr s0, [x0, #0x18]; ret
        else if (code[0] == 0xbd401000) {               // ldr s0, [x0, #0x10]; ret
            value_offset = 0x10;
            primary = read_pointer(flag + 8);
        } else return false;
        if (!primary || !mem_peek(primary + 0x1c, &hash, sizeof hash) || hash != crc32_of("WM_Time")) return false;
        flags[i] = flag + value_offset;
    }
    return true;
}

// Once per TimeMgr: its clock stopped (+0xc4, what calc adds each frame), the hour in both WM_Time buffers and in TimeMgr.
// The world may expose its TimeMgr before the game-data handles are ready: then again next frame. Before the paraglider
// TimeMgr caps its clock at 11:00: this also lifts that cached gate (+0x14e), so any hour holds; the quest's flag stays.
static uint64_t frozen_manager;
static void hold_time(uint64_t tm, float tod) {
    if (frozen_manager == tm) return;
    uint64_t flags[2];
    float *now = (float *)(uintptr_t)(tm + 0xb8), stopped = 0, value = tod >= 360 ? 0 : tod;
    if (!time_flags(tm, flags)) return;
    mem_poke(tm + 0xc4, &stopped, sizeof stopped);
    mem_poke(flags[0], &value, sizeof value);
    mem_poke(flags[1], &value, sizeof value);
    uint8_t running = 1;
    mem_poke(tm + 0x14e, &running, sizeof running);
    LOG("[warp] time of day %.1f -> %.1f (clock stopped)\n", *now, value);
    *now = value;
    frozen_manager = tm;
}
RT_ON_RESET(test_warp_time_reset) { frozen_manager = 0; }

// Every frame (SAVAGEWIND_TEST_TIME): TimeMgr's time of day held at the hour. ksys::world::Manager [world_mgr]
// (+0x5c0 count, +0x5c8 its managers; [0] = TimeMgr, the time at +0xb8), as SAVAGEWIND_CLOCK_PROBE reads it
// (runtime/hle/framepace.c).
void test_warp_frame(uint64_t frame) {
    static float tod = -2;
    static int weather = -2;
    if (tod == -2) {
        const char *t = getenv("SAVAGEWIND_TEST_TIME");
        tod = t && *t ? (float)atof(t) * 15.0f : -1;
        const char *w = getenv("SAVAGEWIND_TEST_WEATHER");
        weather = w && *w && atoi(w) >= 0 && atoi(w) <= 8 ? atoi(w) : -1;
    }
    int hold = weather >= 0 ? weather : g_settings.weather >= 3 ? g_settings.weather - 3 : -1;  // (Cheats > Weather)
    int avoid = weather >= 0 ? 0 : g_settings.weather;  // (1 no rain, 2 no thunderstorms: what the game picks, but for those)
    if ((tod < 0 && hold < 0 && avoid != 1 && avoid != 2 && !getenv("SAVAGEWIND_TEST_WEATHER_ONCE")) || !g_site.world_mgr) return;
    uint64_t wm = *(uint64_t *)(uintptr_t)g_site.world_mgr;
    if (!wm || !*(uint32_t *)(uintptr_t)(wm + 0x5c0)) return;
    // world::Manager::setWeatherType, each frame: mWeatherType at +0x7d1, mWeatherTypeTimer at +0x794; a cutscene's own
    // weather (mWeatherSetForDemo at +0x7d6) stays
    {   // SAVAGEWIND_TEST_WEATHER_ONCE=<n>: that weather once, as if the game had picked it, so the settings' answer can be seen
        static int once = -2;
        if (once == -2) once = getenv("SAVAGEWIND_TEST_WEATHER_ONCE") ? atoi(getenv("SAVAGEWIND_TEST_WEATHER_ONCE")) : -1;
        if (once >= 0 && once <= 8) {
            *(volatile uint8_t *)(uintptr_t)(wm + 0x7d1) = (uint8_t)once;
            *(volatile int32_t *)(uintptr_t)(wm + 0x794) = 4;
            LOG("[warp] once: weather %d at frame %llu\n", once, (unsigned long long)frame);
            once = -1;
        }
    }
    if (avoid == 1 || avoid == 2) {  // 0 clear, 1 cloudy, 2 rain, 3 heavy rain, 4 snow, 5 heavy snow, 6 thunderstorm, 7 thunder rain, 8 rain in sunshine
        uint8_t cur = *(volatile uint8_t *)(uintptr_t)(wm + 0x7d1);
        if (avoid == 1 && (cur == 2 || cur == 3 || cur == 6 || cur == 7 || cur == 8)) hold = 1;
        if (avoid == 2 && cur == 6) hold = 1;
        if (avoid == 2 && cur == 7) hold = 2;
    }
    if (hold >= 0 && !*(volatile uint8_t *)(uintptr_t)(wm + 0x7d6)) {
        volatile uint8_t *now = (volatile uint8_t *)(uintptr_t)(wm + 0x7d1);
        static bool logged;
        if (!logged && *now != hold) {
            LOG("[warp] weather %d -> %d at frame %llu (held)\n", *now, hold, (unsigned long long)frame);
            logged = true;
        }
        *now = (uint8_t)hold;
        *(volatile int32_t *)(uintptr_t)(wm + 0x794) = 4;
    }
    if (tod < 0) return;
    uint64_t mgrs = *(uint64_t *)(uintptr_t)(wm + 0x5c8), tm = mgrs ? *(uint64_t *)(uintptr_t)mgrs : 0;
    if (!tm) return;
    hold_time(tm, tod);
}
