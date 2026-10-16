# Prototype: Vulkan-GLSL (spirv-cross --vulkan-semantics) -> uam-compatible GLSL.
import re, sys
src = open(sys.argv[1]).read()
ubo = ssbo = tex = img = samp_id = 0
arrays = {}
textures = {}  # name -> (prefix, dim)
res = []
for l in src.split('\n'):
    if l.startswith('spirv_instruction') or re.match(r'#extension GL_EXT_(nonuniform_qualifier|scalar_block_layout|samplerless_texture_functions|spirv_intrinsics)', l):
        continue
    if l.startswith('#version'):
        res.append('#version 460\n#extension GL_ARB_bindless_texture : require\n#define spvNMin min\n#define spvNMax max\n#define spvNClamp clamp'); continue
    m = re.match(r'layout\((?:set = \d+, binding = \d+|push_constant), (?:std140|std430|scalar)\) uniform (\w+)', l)
    if m:
        res.append('layout(binding = %d, std140) uniform %s' % (ubo, m.group(1))); ubo += 1; continue
    m = re.match(r'layout\(set = \d+, binding = \d+, (?:std140|std430|scalar)\) ((?:readonly |writeonly )?)buffer (\w+)', l)
    if m:
        res.append('layout(binding = %d, std430) %sbuffer %s' % (ssbo, m.group(1), m.group(2))); ssbo += 1; continue
    m = re.match(r'layout\(set = \d+, binding = \d+, (\w+)\) uniform (.*image\w+ \w+;)', l)
    if m:
        res.append('layout(binding = %d, %s) uniform %s' % (img, m.group(1), m.group(2))); img += 1; continue
    m = re.match(r'layout\(set = \d+, binding = \d+\) uniform sampler (\w+);', l)
    if m:
        res.append('const uint %s = %du << 20;' % (m.group(1), samp_id)); samp_id += 1; continue
    m = re.match(r'layout\(set = \d+, binding = \d+\) uniform (u?)texture[12]D (\w+)\[8192\];', l)
    if m:
        arrays[m.group(2)] = 'usampler1D' if m.group(1) else 'sampler2D'; continue
    m = re.match(r'layout\(set = \d+, binding = \d+\) uniform ([ui]?)(sampler|texture)(\w+) (\w+);', l)
    if m:
        textures[m.group(4)] = (m.group(1), m.group(3)); continue
    res.append(l)
s = '\n'.join(res)
s = re.sub(r'nonuniformEXT\(([^()]*)\)', r'\1', s)
s = s.replace('gl_VertexIndex', 'gl_VertexID').replace('gl_InstanceIndex', 'gl_InstanceID')
for a, t in arrays.items():
    s = re.sub(r'sampler2D\(%s\[([^\]]+)\], (\w+)\)' % a, r'sampler2D(uvec2(uint(\1) | \2, 0u))', s)
    s = re.sub(r'texelFetch\(%s\[([^\]]+)\]' % a, r'texelFetch(%s(uvec2(uint(\1), 0u))' % t, s)
decls = []
def combined(m):
    global tex
    t, smp = m.group(2), m.group(3)
    name = t + '_' + smp
    if name not in textures:
        p, d = textures[t]
        textures[name] = (p, d); decls.append('layout(binding = %d) uniform %ssampler%s %s;' % (tex, p, d, name)); tex += 1
    return name
s = re.sub(r'([ui]?sampler\w+)\((\w+), (\w+)\)', lambda m: combined(m) if m.group(2) in textures else m.group(0), s)
for name, (p, d) in list(textures.items()):
    if re.search(r'\b%s\b' % name, s) and not any(dd.endswith(' %s;' % name) for dd in decls):
        decls.append('layout(binding = %d) uniform %ssampler%s %s;' % (tex, p, d, name)); tex += 1
s = s.replace('#define spvNClamp clamp', '#define spvNClamp clamp\n' + '\n'.join(decls), 1)
sys.stdout.write(s)
sys.stderr.write('ubo=%d ssbo=%d tex=%d img=%d samplers=%d\n' % (ubo, ssbo, tex, img, samp_id))
