#!/usr/bin/env python3
"""Writes the font block of Engine.yaml from the tablet contract.

The dashboard engine draws text and icons only at the sizes the contract
names (contract.json `fonts`) and only with the icons in icons.json. This
script turns those two files into the `font:` entries between the BEGIN and
END marker lines in Engine.yaml, so the firmware always carries exactly the
sizes and glyphs the contract asks for:

  tab_text_<px>     Montserrat Medium, one per size in fonts.text_px, with
                    the characters of fonts.text_glyphs (Latin, Latin-1,
                    Latin Extended-A, common punctuation)
  tab_digits_<px>   Montserrat Medium, fonts.digits_px, only fonts.digits_glyphs
  tab_icon_<px>     Material Design Icons, one per size in fonts.icon_px,
                    every icon of icons.json up to ICON_LEVEL

Usage:
  python3 tools/gen_engine_fonts.py [--contract DIR] [--check]
    --contract DIR  folder with contract.json and icons.json
                    (default: engine_tests/contract next to Engine.yaml)
    --check         change nothing; exit 1 if Engine.yaml is out of date

Standard library only. tools/check_engine_package.py imports this file.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ENGINE_YAML = os.path.join(ROOT, "Engine.yaml")
DEFAULT_CONTRACT = os.path.join(ROOT, "engine_tests", "contract")

TEXT_FILE = "fonts/Montserrat-Medium.ttf"
ICON_FILE = "fonts/materialdesignicons-webfont.ttf"
BPP = 4
# The icon set level this firmware embeds and reports as caps.icons.
ICON_LEVEL = 1

BEGIN = "# BEGIN tab fonts"
END = "# END tab fonts"
INDENT = "  "


def load_contract(contract_dir):
    """(contract.json, icons.json) from a contract folder."""
    with open(os.path.join(contract_dir, "contract.json"), encoding="utf-8") as f:
        contract = json.load(f)
    with open(os.path.join(contract_dir, "icons.json"), encoding="utf-8") as f:
        icons = json.load(f)
    return contract, icons


def text_codepoints(contract):
    """The code points of the text sizes: contract.json fonts.text_glyphs.

    The editor's tablet preview draws a box for any other character, as the
    tablet does, so both read the same list.
    """
    return [c for lo, hi in contract["fonts"]["text_glyphs"] for c in range(lo, hi + 1)]


def digit_glyphs(contract):
    return contract["fonts"]["digits_glyphs"]


def icon_entries(icons, level=ICON_LEVEL):
    """[(name, code point)] of every icon up to `level`, sorted by name."""
    return sorted(
        (name, cp) for name, (cp, lvl) in icons["icons"].items() if lvl <= level
    )


def font_ids(contract):
    """{id: (file, size)} of every font the engine gets, in Fonts order."""
    fonts = contract["fonts"]
    out = {}
    for px in fonts["text_px"]:
        out[f"tab_text_{px}"] = (TEXT_FILE, px)
    for px in fonts["digits_px"]:
        out[f"tab_digits_{px}"] = (TEXT_FILE, px)
    for px in fonts["icon_px"]:
        out[f"tab_icon_{px}"] = (ICON_FILE, px)
    return out


def _quote(text):
    """A YAML double-quoted string; everything outside printable ASCII escaped."""
    out = []
    for ch in text:
        c = ord(ch)
        if ch in '"\\':
            out.append("\\" + ch)
        elif 0x20 <= c < 0x7F:
            out.append(ch)
        elif c <= 0xFFFF:
            out.append(f"\\u{c:04X}")
        else:
            out.append(f"\\U{c:08X}")
    return '"' + "".join(out) + '"'


def _font_entry(font_id, file, size, glyph_lines, anchor=None, alias=None):
    lines = [
        f'{INDENT}- file: "{file}"',
        f"{INDENT}  id: {font_id}",
        f"{INDENT}  size: {size}",
        f"{INDENT}  bpp: {BPP}",
    ]
    if alias:
        lines.append(f"{INDENT}  glyphs: *{alias}")
    else:
        head = f"{INDENT}  glyphs:"
        if anchor:
            head += f" &{anchor}"
        lines.append(head)
        lines += [f"{INDENT}    - {g}" for g in glyph_lines]
    return lines


def render_block(contract, icons):
    """The lines that go between the BEGIN and END markers."""
    fonts = contract["fonts"]
    text = "".join(chr(c) for c in text_codepoints(contract))
    text_lines = [_quote(text[i : i + 16]) for i in range(0, len(text), 16)]
    icon_lines = [f"{_quote(chr(cp))}  # {name}" for name, cp in icon_entries(icons)]

    lines = [
        f"{INDENT}# Made by tools/gen_engine_fonts.py from contract.json and",
        f"{INDENT}# icons.json. Do not edit by hand; run the script again instead.",
    ]
    for i, px in enumerate(fonts["text_px"]):
        if i == 0:
            lines += _font_entry(f"tab_text_{px}", TEXT_FILE, px, text_lines, anchor="tab_text_glyphs")
        else:
            lines += _font_entry(f"tab_text_{px}", TEXT_FILE, px, None, alias="tab_text_glyphs")
    for px in fonts["digits_px"]:
        lines += _font_entry(f"tab_digits_{px}", TEXT_FILE, px, [_quote(digit_glyphs(contract))])
    for i, px in enumerate(fonts["icon_px"]):
        if i == 0:
            lines += _font_entry(f"tab_icon_{px}", ICON_FILE, px, icon_lines, anchor="tab_icon_glyphs")
        else:
            lines += _font_entry(f"tab_icon_{px}", ICON_FILE, px, None, alias="tab_icon_glyphs")
    return lines


def replace_block(source, block_lines):
    """Engine.yaml text with the marked block replaced. Raises ValueError."""
    lines = source.split("\n")
    starts = [i for i, line in enumerate(lines) if line.strip().startswith(BEGIN)]
    ends = [i for i, line in enumerate(lines) if line.strip().startswith(END)]
    if len(starts) != 1 or len(ends) != 1 or ends[0] < starts[0]:
        raise ValueError(f"Engine.yaml needs exactly one '{BEGIN}' line and one '{END}' line after it")
    return "\n".join(lines[: starts[0] + 1] + block_lines + lines[ends[0] :])


def is_up_to_date(contract_dir, engine_yaml=ENGINE_YAML):
    """True when Engine.yaml already holds the block this script would write."""
    contract, icons = load_contract(contract_dir)
    with open(engine_yaml, encoding="utf-8") as f:
        source = f.read()
    return replace_block(source, render_block(contract, icons)) == source


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--contract", default=DEFAULT_CONTRACT, help="folder with contract.json and icons.json")
    parser.add_argument("--check", action="store_true", help="only compare, exit 1 if out of date")
    parser.add_argument("--engine-yaml", default=ENGINE_YAML, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if not os.path.isfile(os.path.join(args.contract, "contract.json")):
        sys.exit(f"No contract.json in {args.contract}. Pass --contract with the contract folder.")
    contract, icons = load_contract(args.contract)
    with open(args.engine_yaml, encoding="utf-8") as f:
        source = f.read()
    try:
        updated = replace_block(source, render_block(contract, icons))
    except ValueError as err:
        sys.exit(str(err))

    if args.check:
        if updated != source:
            print("Engine.yaml fonts are out of date. Run: python3 tools/gen_engine_fonts.py")
            sys.exit(1)
        print("Engine.yaml fonts match the contract.")
        return
    if updated == source:
        print("Engine.yaml fonts already match the contract.")
        return
    with open(args.engine_yaml, "w", encoding="utf-8") as f:
        f.write(updated)
    n_icons = len(icon_entries(icons))
    print(f"Wrote {len(font_ids(contract))} fonts to Engine.yaml ({len(text_codepoints(contract))} text glyphs, {n_icons} icons).")


if __name__ == "__main__":
    main()
