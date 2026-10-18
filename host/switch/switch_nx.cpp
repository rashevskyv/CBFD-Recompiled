// Nintendo Switch (libnx) start-up: the log on the SD card, and a report when the game crashes.
// Everything lives in conker::switch_data_directory (sdmc:/switch/cbfd/).

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "../src/conker.hpp"

// librecomp's: ultramodern::quit sets it.
extern std::atomic_bool exited;

extern "C" {
    // libnx's (init.c): closes the services, as its own exit does before the process ends.
    void __appExit(void);

    // The program's start, from libnx's linker script: crash addresses are given relative to it,
    // for addr2line on the ELF.
    extern char __start__[];

    // __libnx_exception_handler runs on this stack when a thread faults.
    alignas(16) u8 __nx_exception_stack[0x4000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
    // Report crashes even when a debugger is attached.
    u32 __nx_exception_ignoredebug = 1;
    // nvdrv's transfer memory: libnx gives 8 MB, DK64Recomp-NX needed 32 MB on hardware.
    u32 __nx_nv_transfermem_size = 0x2000000;
}

namespace {
    Thread log_flush_thread;
    volatile bool log_flush_running = false;

    // The CPU at 1785 MHz (the highest, which the system itself uses while loading; games get 1020), the GPU
    // at 768 MHz (docked's rate; handheld gives 384-460) and memory at 1600 MHz, as antoxa2584x's Xbox ports
    // do: the game and RT64's CPU side, and at high resolutions the GPU, limit the frame rate. The old rates
    // come back on exit.
    struct Clock {
        const char* name;
        PcvModuleId module;
        u32 wanted;
        ClkrstSession session;
        bool open;
        u32 old;
        volatile u32 set;  // read by the log thread, which starts first
    };
    Clock clocks[] = {
        { "CPU", PcvModuleId_CpuBus, 1785000000 },
        { "GPU", PcvModuleId_GPU, 768000000 },
        { "memory", PcvModuleId_EMC, 1600000000 },
    };
    bool clkrst_up = false;

    // Is another process serving this name? (sys-clk's own check: registering it fails as already registered.)
    bool service_running(const char* name) {
        Handle handle;
        const Result rc = smRegisterService(&handle, smEncodeName(name), false, 1);
        if (R_SUCCEEDED(rc)) {
            smUnregisterService(smEncodeName(name));
            svcCloseHandle(handle);
            return false;
        }
        return rc == MAKERESULT(21, 4);
    }

    void raise_clocks() {
        // sys-clk and Horizon OC set the clocks themselves, and fighting them froze consoles (NFSU2's port).
        if (service_running("sys:clk") || service_running("hoc:clk")) {
            std::printf("[clock] sys-clk or Horizon OC is running: the clocks are left to it\n");
            return;
        }
        if (!hosversionAtLeast(8, 0, 0) || R_FAILED(clkrstInitialize())) {
            std::printf("[clock] no clkrst: the clocks are unchanged\n");
            return;
        }
        clkrst_up = true;
        for (Clock& clock : clocks) {
            if (R_FAILED(clkrstOpenSession(&clock.session, clock.module, 3))) {
                std::printf("[clock] no clkrst session for the %s: unchanged\n", clock.name);
                continue;
            }
            clock.open = true;
            clkrstGetClockRate(&clock.session, &clock.old);
            // Never lower a rate (docked memory, or someone else's overclock).
            const Result rc = clock.old < clock.wanted ? clkrstSetClockRate(&clock.session, clock.wanted) : 0;
            u32 now = 0;
            clkrstGetClockRate(&clock.session, &now);
            clock.set = now;
            std::printf("[clock] %s %u -> %u MHz%s\n", clock.name, clock.old / 1000000, now / 1000000,
                R_FAILED(rc) ? " (refused)" : "");
        }
    }

    // HOME, docking and sleep put the system's clocks back: set ours again.
    void keep_clocks() {
        for (Clock& clock : clocks) {
            u32 now = 0;
            if (clock.set == 0 || R_FAILED(clkrstGetClockRate(&clock.session, &now)) || now >= clock.set) {
                continue;
            }
            clkrstSetClockRate(&clock.session, clock.set);
            std::printf("[clock] %s was put back to %u MHz: set to %u MHz again\n", clock.name, now / 1000000,
                clock.set / 1000000);
        }
    }

    void restore_clocks() {
        if (!clkrst_up) {
            return;
        }
        for (Clock& clock : clocks) {
            if (clock.open) {
                if (clock.old != 0 && clock.set != clock.old) {
                    clkrstSetClockRate(&clock.session, clock.old);
                }
                clkrstCloseSession(&clock.session);
            }
        }
        clkrstExit();
    }

    // Writing to the SD card on every line is very slow (a ported game took 33s to boot that way):
    // the log is buffered, and flushed twice a second. fsync commits it to the card: after a hang and a
    // hard reboot, a log that was only flushed ended in garbage. Every 2 s a line gives the time and the
    // memory, so a hang shows when it happened.
    void log_flush_loop(void*) {
        const u64 start = armGetSystemTick();
        unsigned quit_tick = 0;
        for (unsigned tick = 1; log_flush_running; tick++) {
            svcSleepThread(500'000'000LL);
            // Quitting (ultramodern::quit sets `exited`) must end in userAppExit. If it hasn't in 5 s, the
            // shutdown is stuck (a thread that won't join): end the process from here.
            if (quit_tick == 0 && exited.load()) {
                quit_tick = tick;
                std::printf("[nx] quitting\n");
            } else if (quit_tick != 0 && tick - quit_tick >= 10) {
                std::printf("[nx] the shutdown is stuck after 5 s: ending the process\n");
                restore_clocks();
                std::fflush(stdout);
                fsync(fileno(stdout));
                __appExit();
                svcExitProcess();
            }
            if (tick % 4 == 0) {
                u64 used = 0;
                svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
                std::printf("[nx] %llu s, %llu MB used\n", (unsigned long long)(armTicksToNs(armGetSystemTick() - start) / 1'000'000'000ULL),
                    (unsigned long long)(used >> 20));
                keep_clocks();
            }
            std::fflush(stdout);
            fsync(fileno(stdout));
        }
    }

    void open_log() {
        mkdir("sdmc:/switch", 0777);
        mkdir(conker::switch_data_directory, 0777);
        char path[256];
        std::snprintf(path, sizeof(path), "%s/cbfd_log.txt", conker::switch_data_directory);
        if (std::freopen(path, "w", stdout) == nullptr) {
            return;
        }
        std::setvbuf(stdout, nullptr, _IOFBF, 64 * 1024);
        // stderr goes to the same file, through stdout's buffer, so the lines stay in order.
        std::fclose(stderr);
        stderr = stdout;
        log_flush_running = true;
        // Above the game's threads (0x29-0x2E), on core 0 (never core 3, the system's): at 0x3B it got no CPU
        // for 6 s while the launcher started, so a hang there left no lines. It sleeps between flushes.
        if (R_SUCCEEDED(threadCreate(&log_flush_thread, log_flush_loop, nullptr, nullptr, 0x4000, 0x20, 0))) {
            threadStart(&log_flush_thread);
        }
    }
}

extern "C" void userAppInit(void) {
    open_log();
    // The build's time tells which NRO ran (switch_nx.cpp is rebuilt with every change to it).
    std::printf("[nx] Conker's Bad Fur Day: Recompiled, Switch build of " __DATE__ " " __TIME__ " UTC\n");
    AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication) {
        // An applet (the album) only has ~400MB, not enough for the game.
        std::printf("[nx] warning: started as an applet (type %d): start it with title takeover "
            "(hold R while starting a game) for the full memory\n", (int)type);
    }
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("[nx] memory: %llu MB, %llu MB used at start\n", (unsigned long long)(total >> 20),
        (unsigned long long)(used >> 20));
    // Don't let the console sleep while the game runs.
    appletSetAutoSleepDisabled(true);
    raise_clocks();
}

// The game's threads (the runtime's detached timer, RT64's, SDL's audio) outlive main. Returning would hand the
// process back to hbl, which unmaps the NRO, and the first of them to wake ran unmapped code: an Instruction Abort,
// Atmosphère report 01792248723, 2026-10-17. Running exit() first is no better: the static destructors run while
// those threads still use what they free, and stdio is closed before userAppExit (the log lost its last lines).
// So main calls this right after recomp::start (the saves are written by then): the whole process ends, and the
// console goes back to the HOME menu.
void conker::switch_exit() {
    log_flush_running = false;
    threadWaitForExit(&log_flush_thread);
    threadClose(&log_flush_thread);
    appletSetAutoSleepDisabled(false);
    restore_clocks();
    std::printf("[nx] exit\n");
    std::fflush(stdout);
    fsync(fileno(stdout));
    // libnx's own exit closes the services first (applet among them, which tells the system the application
    // ended), then ends the process; hbloader ends this way when hbmenu quits. svcExitProcess alone made the
    // system say "the software was closed because an error occurred" (the user, 2026-10-17).
    __appExit();
    svcExitProcess();
}

extern "C" void userAppExit(void) {
    // Only when the program ends another way (std::exit on an error): stdio is closed by now. Not back to hbl
    // (the threads), but out as conker::switch_exit does.
    restore_clocks();
    __appExit();
    svcExitProcess();
}

// libnx gives a thread made without a stack size (every std::thread: the game's threads, RT64's,
// the launcher's) only 128 KB, too little for the recompiled code (Windows gives 1 MB). The linker
// sends pthread_create's call here (host/CMakeLists.txt, --wrap), to raise it to 2 MB.
constexpr size_t min_thread_stack = 2 * 1024 * 1024;

extern "C" int __real___syscall_thread_create(void** thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size);

extern "C" int __wrap___syscall_thread_create(void** thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size) {
    if (stack_addr == nullptr && stack_size < min_thread_stack) {
        stack_size = min_thread_stack;
    }
    return __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size);
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(__start__);
    std::fprintf(stdout, "[nx] crash: exception 0x%X, pc 0x%llX (ELF +0x%llX), lr 0x%llX (ELF +0x%llX), "
        "far 0x%llX, sp 0x%llX\n",
        ctx->error_desc,
        (unsigned long long)ctx->pc.x, (unsigned long long)(ctx->pc.x - base),
        (unsigned long long)ctx->lr.x, (unsigned long long)(ctx->lr.x - base),
        (unsigned long long)ctx->far.x, (unsigned long long)ctx->sp.x);
    for (int i = 0; i < 29; i++) {
        std::fprintf(stdout, "  x%-2d 0x%016llX%s", i, (unsigned long long)ctx->cpu_gprs[i].x, (i % 4 == 3) ? "\n" : "");
    }
    std::fprintf(stdout, "\n  fp 0x%016llX\n", (unsigned long long)ctx->fp.x);
    std::fflush(stdout);
    fsync(fileno(stdout));
}
