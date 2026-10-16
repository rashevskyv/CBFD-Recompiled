# Hardware tests (Switch, NVK build)

Eden can't run NVK builds: these run on a console only. Start every NRO with **title takeover**
(hold R while starting a game). Everything lives in `sdmc:/switch/cbfd/`: the NROs, `assets/`,
your ROM as `conker.z64`, and the logs (`switch/deploy.sh <IP> logs` fetches them). Send the logs
after each test, whatever happened: the last line is usually the answer.

Keep the `ConkerRecomp.elf` that matches the NRO you tested (it sits next to the NRO in the build
folder): crash addresses are read against it.

## 1. vktest.nro (NVK alone)

Expected: the screen cycles red, green, blue for 10 seconds (+ quits sooner), then the app exits.

`vktest_log.txt`, in order:

| Line | Means |
|---|---|
| `vkCreateInstance -> 0` | NVK is linked and starts |
| `GPU: ... Vulkan 1.x` and `features: dualSrcBlend 1, depthClamp 1, ...` | what RT64 needs (dualSrcBlend, depthClamp must be 1) |
| `vkCreateViSurfaceNN -> 0`, `vkCreateDevice -> 0` | surface on libnx's window, device |
| `surface: 1280x720 ...` (1920x1080 docked), `usage 0x..`, `transforms 0x..` | what the swapchain may use |
| `frame 1`, `frame 60 at 1.0x s`, ... `done: ~600 frames` | presenting works, ~60 fps |

A line ending in `-> -N` (an error) or a log that stops is the step that failed. A hang with a
black screen: note which line is last.

## 2. ConkerRecomp.nro

### Start-up (`cbfd_log.txt`)

| Line | Look for |
|---|---|
| `[nx] Conker's Bad Fur Day: Recompiled, Switch build` | the log works |
| `[nx] memory: N MB` | about 3000+ MB under title takeover; a `warning: started as an applet` line means it wasn't |
| `[nx_memory] reserved 0x100000000 bytes at 0x...` | the 4 GB window for the game's addresses |
| `[nx_memory] mapped 0x4000000 bytes at 0x...` | RDRAM: code memory mapped with MapOwner. **An error here (`could not map ...: 0x....`) is the first thing to fix**: send the code |
| `Initializing recomp heap ...` | the runtime runs in RDRAM |
| `[frontend] SDL 2.28.5`, `controller mappings: N added` | SDL is up |
| `Pipeline cache: N bytes read from sdmc:/switch/cbfd/vkpipecache.bin` | RT64's Vulkan device exists (N is 0 the first time) |
| a `vkCreateViSurfaceNN failed` / `vkCreate... failed with error code` line | the renderer failed: send the log |

Then the launcher should appear (RmlUi through RT64). Move with the left stick or D-pad, A to
pick. If it shows but doesn't react to the controller, that's input (below), not the renderer.

### In game

| Check | How | Look for |
|---|---|---|
| The game starts | Start Game | `[nx_memory] mapped ...` twice more (the game's TLB pages, .game and .debugger), then the intro |
| Picture | the intro, the bar scene | wrong colours, black textures, missing geometry: a photo helps (NVK compiler bugs show up as wrong colours) |
| Speed | Settings > Graphics > Show FPS | the number, handheld and docked |
| Sound | the intro music | `[audio] N Hz, 1024-frame chunks`; crackle or none |
| Joy-Con / Pro Controller | attached Joy-Cons, then detached, then a Pro Controller | each moves Conker; the launcher's Controls page lists them |
| Saving | play to the first save, quit, start again | `sdmc:/switch/cbfd/saves/` has a file, the save loads |
| Quitting | Settings (the controller's menu button in game) > the Quit button; and HOME > close | no crash line at the end (RDRAM stays mapped on quit now) |
| Second start | start it again | `Pipeline cache: N bytes read` with N > 0, fewer stutters the first minute |

### When it crashes

The log ends with:

```
[nx] crash: exception 0x..., pc 0x... (ELF +0x...), lr 0x... (ELF +0x...), far 0x..., sp 0x...
  x0 ... x28
```

Exception 0x101 (257) is a bad address (`far` is the address), 0x103 (259) an unaligned access.
Send the log and say which build it was: `aarch64-none-elf-addr2line -f -C -e ConkerRecomp.elf <ELF +0x...>`
names the function. A crash in a thread right after start, with `far` near `sp`, would be a stack
overflow (threads get 2 MB now, libnx's default was 128 KB).
