"""Apply reviewed opt-in remaster hooks to newly generated private US game code."""
from pathlib import Path

def apply_remaster(folder):
    files = {p: p.read_text() for p in Path(folder).glob('funcs_*.c')}
    def function(name, edits):
        signature = 'RECOMP_FUNC void ' + name + '('
        matches = [p for p, text in files.items() if signature in text]
        if len(matches) != 1: raise ValueError('Remaster function identity differs: ' + name)
        path = matches[0]
        text = files[path]
        start = text.index(signature)
        end = text.find('RECOMP_FUNC void ', start + len(signature))
        if end < 0: end = len(text)
        body = text[start:end]
        for old, new in edits:
            if body.count(old) != 1: raise ValueError('Remaster instruction anchor differs: ' + name)
            body = body.replace(old, new)
        files[path] = text[:start] + body + text[end:]
        return path
    initial = function('camera_target_track', [
        ('    ctx->f2.u32l = MEM_W(ctx->r29, 0X48);\n',
         '    ctx->f2.u32l = rush_remaster_jet_gain_bits(\n'
         '        (uint32_t)MEM_W(ctx->r29, 0X48), (uint32_t)MEM_W(0X50, ctx->r29));\n')])
    update = function('camera_clip_planes', [
        ('    int c1cs = 0;\n', '    int c1cs = 0;\n    uint32_t rush_remaster_update_sound = UINT32_MAX;\n'),
        ('    // 0x800BF300: lw          $a1, 0x40($v0)\n',
         '    rush_remaster_update_sound = (uint32_t)MEM_W(0X34, ctx->r2);\n'
         '    // 0x800BF300: lw          $a1, 0x40($v0)\n'),
        ('    ctx->f10.u32l = MEM_W(ctx->r29, 0X28);\n',
         '    ctx->f10.u32l = rush_remaster_jet_gain_bits(\n'
         '        (uint32_t)MEM_W(ctx->r29, 0X28), rush_remaster_update_sound);\n')])
    menu = function('overlay_a_func_80390418', [
        ('    uint64_t hi = 0, lo = 0, result = 0;\n', '''    uint64_t hi = 0, lo = 0, result = 0;
    /* Original US ALL TRACKS row 15; guest menu thread only, opt-in. */
    static int rush_remaster_track_access = -1;
    if (rush_remaster_track_access < 0) {
        const char* option = getenv("RUSH_REMASTER_ALL_TRACKS");
        rush_remaster_track_access = option && option[0] == '1' && option[1] == '\\0';
    }
    if (rush_remaster_track_access) {
        MEM_B(0, (int32_t)0x801164A1u) = 1;
        MEM_B(0, (int32_t)(0x8011648Cu + 15)) = 1;
        MEM_H(0, (int32_t)(0x801164A4u + 2 * 15)) = 1;
    }
''')])
    for path in {initial, update}:
        files[path] = '#include "../tools/jet_sound_gain.h"\n' + files[path]
    files[menu] = '#include <stdlib.h>\n' + files[menu]
    # All identities/instruction anchors are checked before any generated writes.
    for path in {initial, update, menu}: path.write_text(files[path])
