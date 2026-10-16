// CONKER_HOST_ONLY: an empty stand-in for N64Recomp's RecompiledFuncs/recomp_overlays.inl, so
// overlays.cpp compiles without the recompiled game. Never linked into a game.
static SectionTableEntry section_table[] = {
    { 0, 0, 0, nullptr, 0, nullptr, 0, 0 },
};
const size_t num_sections = 1;
static int overlay_sections_by_index[] = {
    -1,
};
