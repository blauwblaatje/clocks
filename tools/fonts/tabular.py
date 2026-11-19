"""Tabular digits for the clock fonts (Rogier, 2026-11-19: "a virtual box per
digit, centred in it", so a running clock does not jump when a 1 turns 0).

Rewrites lv_font_conv output in place: every digit gets the advance width of
the widest digit with its ink centred in that box, and kerning
is switched off (it shifts digit pairs too). Other glyphs (: . -) keep their
widths. Running it twice changes nothing.

    python3 tools/fonts/tabular.py firmware/lib/jtui/fonts/jt_sb*.c firmware/lib/jtfonts466/jt466_sb*.c
"""
import re, sys

DSC = re.compile(r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
                 r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")


def fix(path):
    s = open(path).read()
    start = int(re.search(r"\.range_start = (\d+)", s).group(1))
    gstart = int(re.search(r"\.glyph_id_start = (\d+)", s).group(1))
    ofs = [int(x) for x in re.search(r"glyph_id_ofs_list_0\[\] = \{([^}]*)\}", s).group(1).replace("\n", " ").split(",") if x.strip()]
    digit_ids = {gstart + ofs[cp - start] for cp in range(ord("0"), ord("9") + 1)}
    a = s.index("glyph_dsc[] = {")
    b = s.index("};", a)
    table = s[a:b]
    glyphs = [m for m in DSC.finditer(table)]  # id 0 is the reserved first entry
    widest = max(int(glyphs[i].group(2)) for i in digit_ids)
    out, last = [], 0
    for i, m in enumerate(glyphs):
        out.append(table[last:m.start()])
        bi, adv, bw, bh, ox, oy = (int(x) for x in m.groups())
        if i in digit_ids:
            ox = round((widest / 16 - bw) / 2)  # the ink centred in the box; adv_w is in 1/16 px
            adv = widest
        out.append(f"{{.bitmap_index = {bi}, .adv_w = {adv}, .box_w = {bw}, .box_h = {bh}, .ofs_x = {ox}, .ofs_y = {oy}}}")
        last = m.end()
    out.append(table[last:])
    s = s[:a] + "".join(out) + s[b:]
    s = re.sub(r"\.kern_dsc = &kern_classes,", ".kern_dsc = NULL, /* tabular digits: no kerning (tools/fonts/tabular.py) */", s)
    s = re.sub(r"\.kern_classes = 1,", ".kern_classes = 0,", s)
    open(path, "w").write(s)
    print(f"{path}: digits {widest / 16:.1f} px wide")


for p in sys.argv[1:]:
    fix(p)
