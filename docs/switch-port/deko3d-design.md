# A deko3d backend for plume/RT64 on Switch: design notes (2026-10-16)

Sources: `tools/rt64` @4337374 (no patch touches shaders or descriptors), deko3d @350f2b0, uam @5a5afc2, ARMSX2-NX @b1799c1.

## Summary

- **I tested the shader path on the host. It works.** I built uam and spirv-cross on x86 and ran them on RT64's real SPIR-V from `host/build/src/shaders`. The chain was DXC SPIR-V → `spirv-cross --vulkan-semantics` → a Python rewrite (`switch/deko3d/vk2dk_prototype.py`) → uam.
  - **49 of 50 non-spec-constant RT64 shaders compile to DKSH**, plus recompui's InterfaceVS/PS. That includes the `RasterPSDynamic` ubershader: 99 KB DKSH, 1.6 s compile.
  - `DebugPS` is the one that fails. It is a raytracing debug view with 10 storage images, over uam's limit of 8.
  - A re-spirv-specialised `RasterPSSpecConstant` compiles in 0.57 s to 10 KB.
  - The outputs were not kept (scratch files of that session).
- **The 8192-texture array does not need redesigning.** uam accepts `GL_ARB_bindless_texture`, and a deko3d texture handle is just `imageId | samplerId<<20`. So `gTextures[i]` with sampler S becomes `sampler2D(uvec2(i | S, 0))`, and the plume texture set becomes a range of the global image descriptor table. Draws need no per-draw texture binding.
- **Three things are real work, not blockers:**
  1. DX-layout ("scalar") structs need std430 padding.
  2. uam's integer div/mod is imprecise.
  3. Spec constants mean either ubershaders only, or uam linked into the NRO for runtime specialisation. ARMSX2-NX already does the second.

## 1. The plume API surface a backend must implement

`plume_render_interface.h:17-259` has about 20 interfaces and about 110 virtual methods:

| Group | Interfaces (methods) | What RT64 actually uses | Size of the deko3d version |
|---|---|---|---|
| Resources | `RenderBuffer` (map/unmap/formattedView/setName/getDeviceAddress), `RenderTexture` (+View), `RenderBufferFormattedView`, `RenderSampler`, `RenderPool` | Formatted buffers (4 uses, needed for RSP `Buffer<float>`), texture views (5), one pool (texture uploads). `getDeviceAddress` and root descriptors: 0 uses | ~700 lines plus a memblock suballocator (~300) |
| Descriptors | `RenderDescriptorSet` (setBuffer/setTexture/setSampler/setAS), `RenderPipelineLayout` | All sets are in `render/rt64_descriptor_sets.h`. The large ones: the common set (`:216-290`, 18 immutable samplers, CBVs, SRVs, plus RT-only UAVs) and the "boundless" texture set (`:293-312`, `addTexture(0, 8192)` and `end(true, size)`) | ~500 |
| Pipelines | `RenderShader`, `RenderPipeline`, compute and graphics `create*Pipeline` | Graphics: dual-source blend, depth clip/clamp, MSAA with sample locations (`render/rt64_raster_shader.cpp:309-393`). Compute: about 20 shaders. Raytracing: never (Linux/Switch has no `RT_ENABLED`) | ~600 |
| Commands | `RenderCommandList` (37 methods) | barriers (79 call sites), dispatch, draw/drawIndexed, push constants (graphics and compute), set/vertex/index buffers, viewports/scissors, setFramebuffer, clearColor/clearDepth with rects, copyBufferRegion/copyTextureRegion/copyTexture, resolveTexture(+Region), `writeTimestamp`/`resetQueryPool`. Unused or stubbed: traceRays, AS builds, `setDepthBias`, `setGraphicsRootDescriptor`, `discardTexture` (D3D12 only) | ~900 |
| Submission | `RenderCommandQueue`, Fence, Semaphore, `RenderSwapChain` (14 methods), `RenderQueryPool` | Six workers each create a queue (`hle/rt64_application.cpp:319-323`, plus a texture stream worker). One query pool with 2 timestamps | ~500 |
| Device | `RenderDevice` (24), `RenderInterface` (3) | Capabilities (`plume_render_interface_types.h:1764-1815`) | ~300 |

Total: about 3,000-3,800 lines (`plume_vulkan.cpp` is 4,641, `plume_metal.cpp` 4,229). Use Metal as the template: no layouts, no render passes.

Formats in real use:
- R8/R16/R32 UINT (TMEM and formatted buffers).
- R32_FLOAT, D32_FLOAT (deko3d `ZF32`) and D16.
- RGBA8 or RGBA16_UNORM colour targets (`rt64_render_target.cpp:508`).
- RGBA32F.
- BGRA8 for the swapchain.
- BC1-7 for texture packs.

deko3d has every one of these (`deko3d.h:433-509`).

## 2. How RT64 binds textures

**What RT64 does:**
- The draw loop is `rt64_framebuffer_renderer.cpp:477-580`. It binds sets 0-3 once (`:483-486`). Then, for each N64 draw call, it sets the pipeline (only when it changes), the scissor, a push constant (`renderIndex`), and issues one `drawIndexedInstanced`. There is no per-draw descriptor work.
- The pixel shader reads `instanceRenderIndices[renderIndex].rdpTileIndex/rdpTileCount` and `GPUTiles[tile].textureIndex`, then indexes:
  - `gTextures[NonUniformResourceIndex(idx)]` for `Load` and native `SampleGrad`/`SampleLevel` (`TextureSampler.hlsli:104,108,171,276`), declared at `FbRendererCommon.hlsli:43`.
  - `gTMEM` (`Texture1D<uint>`, the same descriptor set bound again as set 2, `:46`).
- **One draw uses at most `tileCount` textures**: 2 normally, up to 8 with LOD (`hle/rt64_state.cpp:224-237`). The tile, and so the texture, is chosen **per pixel** (`computeLOD`, `RasterPS.hlsl:131-160`), so the index is not uniform within a draw.
- The CPU writes the texture cache index (`rt64_framebuffer_renderer.cpp:246,268`). The set is recreated at 1.5× when it grows (`:313-316`).

**What deko3d and uam provide:**
- Bindings are per-stage handle slots (`dkCmdBufBindTextures`, `deko3d.h:1270`; 32 per stage).
- The real descriptors sit in a GPU-memory image and sampler table (`dkCmdBufBindImageDescriptorSet`, `:1272`). The table can hold up to 2^20 images and 4096 samplers (`dkMakeTextureHandle`, `:721`).
- uam has `ARB_bindless_texture` turned on (`mesa-imported/glsl/standalone_scaffolding.cpp:321`). Its codegen turns a handle in a register into a bindless TEX/TXD (`nv50_ir_from_tgsi.cpp` `setTexRS`, about line 2323).
- Indirectly indexed sampler arrays are lowered to a handle loaded from the constant buffer, also bindless (`nv50_ir_lowering_nvc0.cpp:1026-1034`).
- There is no `GL_NV_bindless_texture`, no `GL_EXT_nonuniform_qualifier`, no scalar layout, and no SPIR-V input.

**Recommended approach: keep the array model (option A).**
- **Backend:**
  - One global image descriptor table and one global sampler table, bound once per command list.
  - The boundless texture set reserves a contiguous slot range `[base, base+N)`, and `setTexture(i)` writes a `DkImageDescriptor` at `base+i`.
  - Immutable samplers get fixed sampler IDs.
- **Shaders:**
  - `gTextures[i]` combined with sampler S becomes `sampler2D(uvec2(base+i | S<<20, 0))`.
  - `gTMEM[i]` becomes `usampler1D(uvec2(base+i,0))`.
  - `base` goes in a UBO, or is 0 if the set always starts at slot 0.
- **Changes to RT64 itself:** none in C++, only the shader build step.
- **I verified this compiles:** the prototype emits TGSI `TXD TEMP…` (bindless).
- **Not yet verified:** whether divergent handles in one warp are correct on GM20B. NVIDIA hardware handles non-uniform indices natively, and NVK advertises it on Maxwell, but it needs a hardware test.

**Fallback (option B): per-draw slots.**
- Declare `sampler2D gTiles[8]` and use `slot = tile - rdpTileIndex`.
- Bind up to 8 handles per draw with `dkCmdBufBindTextures`, choosing the sampler per tile. This also removes the 18-way sampler `switch`.
- It needs a new per-draw plume call or a hook in `submitRasterScene`. Use it only if option A fails on hardware.

## 3. Shaders

**What RT64 builds** (`CMakeLists.txt:500-556`, 56 entries):
- Raster VS and PS in Dynamic, SpecConstant, Flat and MS variants.
- About 20 compute shaders (RSP*, Fb*, TextureDecode, Histogram, filters).
- Fullscreen, copy and resolve pixel shaders.
- VI, Im3D, Compose and PostProcess shaders.

All are built with DXC (`-fspv-target-env=vulkan1.0 -fvk-use-dx-layout`, `:107-121`) and embedded by `file_to_c`. Blobs are chosen with `CREATE_SHADER_INPUTS` (`rt64_shader_library.cpp:157-174`). recompui does the same with `GET_SHADER_BLOB` (`recompui/src/renderer/ui_renderer.cpp:37-53`).

**How specialisation works at runtime:**
- `RasterShader` runs re-spirv with 5 spec constants: otherMode L/H, combiner L/H and flags (`rt64_raster_shader.cpp:99-138`). It does this on background threads (`rt64_raster_shader_cache.*`).
- Until a specialised shader is ready, draws use `RasterShaderUber::getPipeline(zCmp,zUpd,cvgAdd)` (`rt64_framebuffer_renderer.cpp:1610-1617`).
- `ubershadersOnly` is at `hle/rt64_workload_queue.h:83`. It can be toggled from the debug UI (`rt64_state.cpp:2545`). On deko3d, force it to true at first.

**Proposed build pipeline:** HLSL → DXC SPIR-V (unchanged) → `spirv_cross_dksh` → uam → DKSH, plus a reflection table.
- `spirv_cross_dksh` is a new tool next to `src/tools/spirv_cross_msl`.
- The reflection table maps plume (set, binding) to the uam class and slot, for each stage. It has to be emitted per shader, because RT64's bindings go up to 67 and uam's slots are 0-15 (UBO/SSBO), 0-31 (samplers) and 0-7 (images).
- Add `RenderShaderFormat::DKSH` and a `__SWITCH__` branch in both blob macros.

**What breaks, and the fix for each:**
1. **Non-Vulkan GLSL output fails** ("Buffer block cannot be expressed…") because of DX packing.
   - Scalar-layout blocks: `RDPParams` (two `float3`), `GPUTile` (a `float3` after a `uint`), `RSPLight/LookAt/Viewport`, and the `RasterParams` push constant (`uint3 padding`).
   - Fix: pad these shared structs in `src/shared/*.h` so std430/std140 match (about 6 structs). Then emit Vulkan-flavoured GLSL and rewrite it, as the prototype does.
   - The prototype only switches scalar to std430, so **its data layouts are not yet correct**.
2. **Rewrites needed in the GLSL:**
   - `set=` must go and bindings must be remapped.
   - Push constants become an explicit-binding UBO. deko3d updates it inline with `dkCmdBufPushConstants`, `deko3d.h:1319`.
   - Separate texture and sampler pairs become combined bindings. A deko3d handle is image plus sampler, so the backend combines them at bind time.
   - `gl_VertexIndex` becomes `gl_VertexID`.
   - Drop `nonuniformEXT`.
   - `spvNMin/NMax` (the `spirv_instruction` intrinsics) become `min/max`.
   - Prototype: text regex. Production: a spirv-cross `CompilerGLSL` subclass.
3. **Spec constants:** uam rejects `layout(constant_id)`. Run re-spirv first, which removes them, or use ubershaders.
4. **Integer `/` and `%` with a non-constant divisor** compile to `trunc(float(a)*rcp(float(b)))` with no correction step (`uam nv50_ir_lowering_nvc0.cpp:45-62`).
   - `RasterPSDynamic` and `TextureDecodeCS` trigger this (TMEM addressing and `modulo` for wrap and mirror). It can be off by one.
   - Fix: patch uam to add a correction step (q-=1 when q*b>a, q+=1 when (q+1)*b<=a). That is about 10 IR lines in our uam fork.
5. **Works as is:**
   - Dual-source blending: `layout(location=0,index=1)` compiled. uam maps secondary outputs (`tgsi_support.cpp:469`), and deko3d has `DkBlendFactor_Src1*` (`deko3d.h:963-966`).
   - Depth clamp: `DkRasterizerState.depthClampEnable`, `:803`.
   - MSAA textures and sample locations: `dkMultisampleStateSetLocations`, `:1359`.
   - Texel buffers and typed storage images: formats come from DXC.
   - Sample shading: RT64 doesn't use it.
6. **Raytracing and `DebugPS`:** excluded.

**Running uam on the Switch at runtime is feasible, and has been done.**
- ARMSX2-NX links uam into its NRO (`switch/app/cmake/Uam.cmake`).
- It uses `ld -r` and `objcopy --keep-global-symbol` to hide mesa's symbols from NXVK.
- It keeps mesa's builtin table alive between compiles, caches DKSH on SD, and falls back to ubershaders. That is the same structure RT64 already has.
- Chain on the console: re-spirv (already there) → spirv-cross (pure C++) → uam.
- Cost: 0.57 s per specialised RasterPS on a 2.1 GHz Xeon, so expect roughly 2-3 s on the A57. This is an estimate. Background threads plus an SD cache make it acceptable.
- Alternative: precompile at build time. Log `ShaderDescription`s (5×u32) during PC playthroughs and ship about 10 KB of DKSH for each.

## 4. deko3d capabilities against RT64's needs

| Need | deko3d |
|---|---|
| Dual-source blend, logic op, depth clamp, depth bias | Yes (`:963`, `:871-902`, `:803`, `dkCmdBufSetDepthBias`) |
| MSAA 2/4/8×, custom sample locations, resolve | Yes. `dkCmdBufResolveImage` resolves the whole image, so set `resolveRegion=false` and RT64 uses its shader resolve (`rt64_render_target.cpp:220`) |
| BC1-7 | Yes (`:446-462`) |
| Compute | Yes. Compute UBO slots 6-15 are emulated as SSBOs (uam README) |
| Timestamps | `dkCmdBufReportCounter(DkCounter_Timestamp)` and `dkTimestampToNs` (`:229`, `:1327`). Note: each report flushes the tiled cache |
| Queues | Several `DkQueue`s, each with Graphics or Compute and a priority (`:263-270`); sync with `DkFence`. Use one or two queues, not six (each is an nvhost channel) |
| Present | `dkSwapchainCreate(nwindowGetDefault())`, RGBA8 or BGRA8, swap interval, crop (`:1212`, `:1361-1365`; formats in `source/dk_swapchain.cpp:19-29`) |
| Memory | `DkMemBlock` with 4 KB alignment and CPU/GPU cache flags, plus Image and Code blocks. **A suballocator is needed**, because RT64 makes many small buffers |
| Barriers and layouts | There are no layouts. `dkCmdBufBarrier(Primitives/Fragments, Image\|L2\|Descriptors)`, plus `dkCmdBufTiledCacheOp` when a render target is sampled |
| Conventions | `DkDeviceFlags_DepthZeroToOne\|OriginUpperLeft` matches D3D/Vulkan (`:116-121`) |
| Formatted buffers | `DkImageType_Buffer` (`:366`) |

## 5. Existing projects to learn from

- **ARMSX2-NX** (`app/src/main/cpp/pcsx2/GS/Renderers/DK3D/`, GPL-3: ideas only).
  - Size: `GSDeviceDK.cpp` 2,651 lines, `DKShaderCompiler.cpp` 479, `GSTextureDK` 297.
  - Shaders: hand-ported GLSL (`shaders/*.glsl`, ImGui included), with prebuilt variants plus runtime uam.
  - Textures: a ring of image descriptors (`PushImage`) and a per-draw `dkMakeTextureHandle` with `dkCmdBufBindTextures` (`GSDeviceDK.cpp:729-734,1970`). This is option B.
- **ftpd** (`source/switch/imgui_deko3d.cpp`, 547 lines, GPL-3): an ImGui deko3d backend.
- **switch-examples deko_examples**: compute, MSAA and deferred examples. deko3d is zlib, a clean base.
- **ImGui:**
  - RT64's inspector calls `ImGui_ImplVulkan` directly, not through plume (`gui/rt64_inspector.cpp:80-242`). Compile it out on Switch.
  - recompui's RmlUi goes **through plume** with two trivial shaders: one push constant, one texture and sampler (`ui_renderer.cpp:199-262`). Both already compile with the prototype.

## 6. Phased plan

| Phase | Work | Files | Estimate |
|---|---|---|---|
| a. Skeleton and clear screen | `plume_deko3d.{h,cpp}`: interface, device, queue, cmdlist, memblock suballocator, swapchain, fences. NRO that clears the screen. CMake `PLUME_DEKO3D`, devkitPro `deko3d` portlib | `src/contrib/plume/plume_deko3d.*`, `plume/CMakeLists.txt`, `switch/patches/plume-switch.patch`, `host/CMakeLists.txt` | 1-2 weeks |
| b. Rest of the backend and RT64 shaders through uam | Buffers, textures, views, samplers, sets, pipelines, draws, compute, copies, queries. `spirv_cross_dksh` tool, reflection table, `build_shader_dksh_impl`, host uam (meson) in `switch/build.sh`, padding of the shared structs, uam div/mod fix, `RenderShaderFormat::DKSH`, blob macros, `CreateDeko3DInterface` in `rt64_application.cpp:160-180`, ubershaders forced on | `src/tools/spirv_cross_dksh/`, `CMakeLists.txt:105-160`, `src/shared/rt64_rdp_params.h`, `rt64_gpu_tile.h`, `rt64_raster_params.h`, `rt64_shader_library.cpp`, `rt64_raster_shader.cpp`, `rt64-switch.patch` | 4-6 weeks |
| c. Texture binding | The bindless rewrite in the tool, a contiguous range for the boundless set, the global sampler table. A hardware test of divergent handles; option B if it fails | `plume_deko3d.cpp`, `spirv_cross_dksh` | 3-5 days (+1 week for option B) |
| d. recompui | `GET_SHADER_BLOB` DKSH branch, build rules for InterfaceVS/PS | `recompfrontend-switch.patch` | 2-3 days |
| e. Performance | Specialised shaders: offline list plus build-time DKSH, then runtime uam (the ARMSX2 pattern) with an SD cache. Collapse the queues, cut barriers and tiled-cache flushes, `dkCmdBufBeginCaptureCmds` for static state, CPU profiling | `rt64_raster_shader*.cpp`, new `rt64_dksh_cache.*` | 2-4 weeks |

## Open questions

- I have not measured whether deko3d is faster than NVK on this workload.
  - What it should win: CPU and submit overhead. NVK's Switch WSI makes syscalls on every submit and fence.
  - What is unclear: GPU time, because uam's 2019 nouveau codegen with dual-issue competes with NAK.
  - Suggestion: profile the NVK build on hardware before committing to the 2-3 months above.
- Not yet tested: divergent bindless indices on GM20B; `rgba32f`-declared storage images against RGBA8 targets; whether texel buffers need Image memblocks.
