// Skip Intro and Skip Any Cutscene (mods/skip_intro, mods/skip_cutscenes), built in for the Switch,
// which can't patch the recompiled code for mods. host/CMakeLists.txt renames the game's own
// definitions of the hooked functions to <name>_orig, so the calls reach these instead. The
// mods' comments explain the addresses; the logic here is theirs, line for line. Both are
// settings on the General tab instead of mods.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "recomp.h"
#include "recompui/config.h"

extern "C" {
    void func_151DE6D4_orig(uint8_t* rdram, recomp_context* ctx);
    void func_15007A70_orig(uint8_t* rdram, recomp_context* ctx);
    void func_1501E05C_orig(uint8_t* rdram, recomp_context* ctx);
    void func_1501C730(uint8_t* rdram, recomp_context* ctx);
}

namespace {
    const std::string skip_intro_id = "skip_intro";
    enum class SkipIntro : uint32_t { Off, On };
    const std::string skip_cutscenes_id = "skip_cutscenes";
    enum class SkipCutscenes : uint32_t { Off, On, All };

    template <typename T>
    T option(const std::string& id) {
        return static_cast<T>(std::get<uint32_t>(recompui::config::get_general_config().get_option_value(id)));
    }

    gpr addr(uint32_t a) {
        return (gpr)(int32_t)a;
    }

    // Skip Intro.
    constexpr int32_t scene_bar = 0x1D;
    constexpr int32_t arm_frames = 10;
    bool bar_requested = false;
    bool bar_set_up = false;

    // Skip Any Cutscene.
    constexpr uint16_t button_start = 0x1000;
    constexpr uint16_t button_l = 0x0020;
}

namespace conker::switch_mods {
    void add_options(recomp::config::Config& config) {
        using EnumOptions = const std::vector<recomp::config::ConfigOptionEnumOption>;
        static EnumOptions intro = {
            {SkipIntro::Off, "Off", "Off"},
            {SkipIntro::On, "On", "On"},
        };
        static EnumOptions cutscenes = {
            {SkipCutscenes::Off, "Off", "Off"},
            {SkipCutscenes::On, "On", "On"},
            {SkipCutscenes::All, "All", "All"},
        };
        config.add_enum_option(skip_intro_id, "Skip Intro",
            "Starts the game outside the bar, one button press away from the main menu: skips the notices, "
            "the Nintendo logo and the chainsaw opening. Takes effect the next time the game starts. "
            "Off by default: Skip Cutscenes lets you skip them one by one instead.",
            intro, SkipIntro::Off);
        config.add_enum_option(skip_cutscenes_id, "Skip Cutscenes",
            "<recomp-color primary>On</recomp-color> lets L skip a cutscene even the first time it plays, and "
            "Start skip the chainsaw opening. <recomp-color primary>All</recomp-color> also lets L skip the "
            "cutscenes the game never lets you skip, like the throne room and the hangover scene. "
            "<recomp-color primary>Off</recomp-color> is the original game.",
            cutscenes, SkipCutscenes::All);
    }
}

// The logos (phase 0) each frame: ARM_FRAMES in, ask for the bar instead of the opening.
extern "C" void func_151DE6D4(uint8_t* rdram, recomp_context* ctx) {
    func_151DE6D4_orig(rdram, ctx);
    if (bar_requested || MEM_W(0, addr(0x800E0A90)) < arm_frames || option<SkipIntro>(skip_intro_id) == SkipIntro::Off) {
        return;
    }
    bar_requested = true;
    MEM_B(0, addr(0x8008FE28)) = 2;
    MEM_B(0, addr(0x800E0B94)) = 1;
    MEM_B(0, addr(0x800D2E40)) = 0;
    // func_1501C730(6, SCENE_BAR, 0, 0, 1), from a frame of its own below $sp (the fifth argument
    // goes on the stack); the registers are put back after.
    const recomp_context saved = *ctx;
    ctx->r29 = ADD32(ctx->r29, -0x20);
    ctx->r4 = 6;
    ctx->r5 = scene_bar;
    ctx->r6 = 0;
    ctx->r7 = 0;
    MEM_W(0x10, ctx->r29) = 1;
    func_1501C730(rdram, ctx);
    *ctx = saved;
    MEM_B(0, addr(0x800E0B96)) = (int8_t)0xFF;
}

// After a scene loads: once the bar has, what the opening's end would have left behind.
extern "C" void func_15007A70(uint8_t* rdram, recomp_context* ctx) {
    func_15007A70_orig(rdram, ctx);
    if (!bar_requested || bar_set_up || MEM_W(0, addr(0x800BE9F0)) != scene_bar) {
        return;
    }
    bar_set_up = true;
    MEM_B(0, addr(0x800D2E40)) = 0;
    MEM_B(0, addr(0x800E0B94)) = 3;
    MEM_B(0, addr(0x8008FD80)) = 1;
    MEM_B(0, addr(0x8008FE28)) = 2;
    MEM_B(0, addr(0x8008FDA4)) = 0;
    const gpr game_state = addr((uint32_t)MEM_W(0, addr(0x8008FDD4)));
    if (game_state != 0) {
        MEM_B(0x3E, game_state) = 0;
        MEM_B(0x2B, game_state) = 5;
        MEM_B(0x2C, game_state) = MEM_B(0x2B, game_state);
    }
}

// Whether the playing cutscene i should be skipped this frame (the mod's RECOMP_PATCH).
static int32_t skip_cutscene(uint8_t* rdram, int32_t i, SkipCutscenes mode) {
    const int32_t scene = MEM_W(0, addr(0x800BE9F0));
    const uint16_t pressed = MEM_HU(0, addr(0x800BE710));
    const gpr timers = addr(0x800C35B0);
    if (scene == 0x1D) {
        if (MEM_B(0, addr(0x8008FD84)) != 0) {
            MEM_B(0, addr(0x8008FD84)) = 0;
            return 1;
        }
        // The game (and the mod) skips only cutscene 5 here. All also lets L skip the others, such as the
        // one New Game starts with (the user's report: it could not be skipped).
        if (MEM_BU(0, addr(0x800C35E8)) != 5 && mode != SkipCutscenes::All) {
            return 0;
        }
    }
    if (scene == 0x21) {
        return (pressed & button_start) && MEM_W(0, timers) >= 0x12D;
    }
    if (!(pressed & button_l)) {
        return 0;
    }
    if (MEM_BU(0, addr(0x800C3C9C)) != 0 && mode != SkipCutscenes::All) {
        return 0;
    }
    if (MEM_BU(0, addr(0x800D2E40)) != 0 || MEM_BU(0, addr(0x800C3C99)) != 0) {
        return 1;
    }
    return MEM_W(i * 4, timers) + 0x1E < MEM_W(i * 4, addr(0x800C3640));
}

extern "C" void func_1501E05C(uint8_t* rdram, recomp_context* ctx) {
    const SkipCutscenes mode = option<SkipCutscenes>(skip_cutscenes_id);
    if (mode == SkipCutscenes::Off) {
        func_1501E05C_orig(rdram, ctx);
        return;
    }
    const int32_t i = (int32_t)ctx->r4;
    ctx->r2 = skip_cutscene(rdram, i, mode);

    // The log: each cutscene once, and what L or Start did, to find a cutscene that won't skip.
    static int32_t logged_scene = -1;
    static int32_t logged_id = -1;
    const int32_t scene = MEM_W(0, addr(0x800BE9F0));
    const int32_t id = MEM_BU(i, addr(0x800C35E8));
    if (scene != logged_scene || id != logged_id) {
        logged_scene = scene;
        logged_id = id;
        std::printf("[skip] cutscene %d in scene 0x%X: length %d, unskippable %u\n", id, scene,
            MEM_W(i * 4, addr(0x800C3640)), MEM_BU(0, addr(0x800C3C9C)));
    }
    const uint16_t pressed = MEM_HU(0, addr(0x800BE710));
    // Every skip too, with no button (scene 0x1D's own flag): a context action's first B was lost (the user).
    if (ctx->r2 || (pressed & (button_l | button_start))) {
        const char* by = (pressed & button_l) ? "L" : (pressed & button_start) ? "Start" : "no button";
        std::printf("[skip] %s (buttons 0x%04X) at frame %d of cutscene %d in scene 0x%X: %s\n", by, pressed,
            MEM_W(i * 4, addr(0x800C35B0)), id, scene, ctx->r2 ? "skipped" : "not skipped");
    }
}
