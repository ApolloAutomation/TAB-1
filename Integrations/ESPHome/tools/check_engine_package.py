#!/usr/bin/env python3
"""Checks the TAB-1 dashboard engine package against the tablet contract.

Static checks (always run, no ESPHome needed):
  - the eight actions in Engine.yaml match contract.json: names, argument
    names and types, response modes;
  - the two events match contract.json and send `data:` (never a template);
  - every action chain holds only `lambda` and `api.respond`: no delay,
    wait_until or other step that would make ESPHome copy the arguments;
  - fonts: the ids, sizes, bits a pixel and glyph lists equal contract.json
    `fonts` and icons.json up to level 2 (the 14 fonts), the generated block
    is up to date, the start-up script hands each font to the right place of
    tab::Fonts, and the metrics table of engine_tests/host/harness.h has one
    row per font in that order with no digits font's line higher than
    contract.json fonts.row_px;
  - both entry files: api and wifi reboot_timeout 0s, the same device as
    TAB-1.yaml, an open hotspot with the set-up page, a 15-minute set-up
    window that tells the waiting screen when it closes, and the update lock
    (ota_lock.h) wired in: on_boot priority 700 before the engine at 100,
    on_loop and safe_mode on_safe_mode;
  - the production file ships no secret at all (no !secret in it or in the
    files it includes), has api encryption without a key, `ota: esphome`
    with an empty password and no encryption, Apollo's update channel over
    checked HTTPS, dashboard_import and no web page;
  - the bench file has `!secret tab1_api_key`, encrypted updates only and the
    web page without firmware upload; apart from those and the update
    channel it equals the production file;
  - ota_lock.h and engine_glue.h hold the lock and erase rules the design
    names (design 10.8, 10.9): each restart in its own case, the #error that
    stops a build without the update password check (unless it requires
    encryption with a key from the YAML), the erase feeding the watchdog,
    erasing the partition once and leaving the erase mark (one byte, after
    the partition erase, before the restart); no factory reset or safe mode
    button anywhere; the glue hands the hotspot state and name to the
    waiting screen, and the host tests use the hotspot name of the entry
    files;
  - the files version: engine/tab_files.h, engine_glue.h and ota_lock.h
    carry one number and Engine.yaml checks each with an #if and #error
    (also a copy from before the numbers, which has none), so an old copy in
    the ESPHome config folder stops the build with an error naming it;
    tab_files.h records the SHA-256 of every file the number covers under a
    `// files N all <sha256>` line, and the check fails when one differs at
    the same number, or when the `all` hash is not the one of the lines (two
    branches merged at one number): raise the number, then run
    --record-files;
  - the restarts in ota_lock.h poll(): esphome::App.safe_reboot() runs first
    in its case, not commented out, not under #if 0 or a condition, before
    any break or return (with a self-test of that rule);
  - the release: .github/workflows/build.yml publishes TAB-1-engine.yaml
    through the shared workflow with a pinned ESPHome version equal to
    min_version in both engine files, .github/release-drafter.yml is there
    (the shared workflow needs it), and ci.yml builds the engine file on
    that pinned version only, still builds TAB-1.yaml, and runs this
    script's static mode;
  - Engine.yaml includes exactly `engine` and `engine_glue.h`; engine/ holds
    only .h files; the Montserrat font and its licence are there;
  - the screen's way up: Engine.yaml takes `lvgl: rotation:` from the
    substitution `screen_rotation`, both entry files set it to "90", and
    fast_display.h has its fast path for 90 and for 270 (a kernel for each,
    both used by the one flush, anything else left to ESPHome's own flush)
    with the host test of both in engine_tests/run.sh; that script also
    builds the whole of fast_display.h against the stand-in headers in
    engine_tests/host/fast_display_sim (one for each header it includes)
    and tests flush_cb, setup and set_enabled (tests/test_flush_cb.cpp);
  - engine_tests/tools/fetch_lvgl.sh pins LVGL 9.5.0 by commit and refuses
    a source or a copy of another commit (run on stand-in sources);
  - engine_tests/run.sh builds everything with -ffp-contract=off, and builds
    and runs the frozen sources of engine 0.1.0 (engine_tests/frozen/0.1.0)
    optimised and with sanitizers before the engine, whose host test
    v1_pixels_unchanged compares their frames byte for byte;
  - docs/engine.md quotes each waiting screen of engine/tab_state.h with its
    exact headline and step (none alone or cut short, no old wording; the
    erased one with its second line and the tablet's name) and each Wi-Fi
    note whole, no line of it starts with ">" outside code blocks (Markdown
    would turn it into a block quote), and the glue's "Home Assistant
    connected" is a connection that follows the states;
  - the waiting screens and Wi-Fi notes of engine/tab_state.h are the
    contract's screens.json word for word (engine_tests/contract), and so
    are CONTRACT.md section 11 and the hotspot name of both entry files;
  - with --peer-contract, the contract folder is the integration's, file
    for file;
  - self-tests: the YAML reader on a sample (and against PyYAML on every
    file read, where PyYAML is installed), the files rule (a merge of two
    branches at one number among its cases), the docs check, the contract
    check of the waiting screens, the contract comparison (--peer-contract),
    and the --compile warning gate.

The static checks need Python 3.12 or later and nothing else: no PyYAML (the
script reads the YAML itself), no network, no ESPHome, no compiler. This is
the mode CI runs (no flags).

Optional steps:
  --peer-contract DIR
             compares the contract folder with the integration's copy
             (TAB-Dashboard tests/contract, or a checkout of it), file for
             file, as the integration's own test compares it with this one.
             CI checks the integration out and passes it here. Without the
             flag only the self-test of the comparison runs.
  --host     builds and runs host tests of the flash queue in engine_glue.h
             and of the lock decisions in ota_lock.h (C++17 compiler; with
             address, undefined and thread sanitizers where the compiler has
             them); runs the preprocessor over ota_lock.h for the update
             defines of each variant and of builds it must stop; builds the
             headers with an older or pre-version ota_lock.h, engine_glue.h
             or engine/ in their place, which must stop with an error naming
             the copy to delete.
  --config   copies this folder to the work folder and runs `esphome config`
             on both engine files, a simulated ESPHome Builder adoption,
             TAB-1.yaml and TAB-1-showcase.yaml. The engine files and the
             adoption must come out with LVGL rotation 90, the two protected
             files with 270, and an adoption that sets `screen_rotation` to
             "270" in its own file with 270.
  --compile  also runs `esphome compile` for the P4 on three variants:
             production, bench and the adoption (a device file in a subfolder
             that pulls TAB-1-engine.yaml in as a package with its own key,
             as ESPHome Builder writes it). Each has its own ESPHome data
             folder in --work, seeded from the work copy's build cache.
             Checks the generated src/ folder (engine headers, engine_glue.h
             and ota_lock.h are there, nothing from engine_tests/, string
             arguments are views, the defines and hooks of each variant; what
             the lock checks read in the YAML, in the generated code: the
             LVGL rotation of `screen_rotation`, no
             set_auth_password, the 15-minute window, set_verify_ssl(true)
             and the certificate bundle, USE_SAFE_MODE_CALLBACK, the
             files-version checks with their numbers), builds the adoption
             once more with a pre-version ota_lock.h and once with a
             pre-version engine_glue.h beside the device file (each must
             stop with the error naming it), reads
             every NVS namespace the firmware opens and compares it with the
             erase list, compares the font metrics with the host harness and
             the host's lv_conf.h with the tablet's (LV_HOST_ONLY apart), the
             firmware's LVGL commit with the host tests' pin, and
             reports firmware size and RAM and flash use. Any compiler
             warning from engine/, engine_glue.h, ota_lock.h or the lambdas
             of Engine.yaml and the entry files fails (main.cpp, where all
             of them build, is built again each time to show them).

ESPHome writes .esphome build folders, so --config and --compile never run
in this folder: they work on a copy in --work (default: a new temporary
folder). The copy gets a secrets.yaml with random throwaway secrets.

--offline points every download of the ESPHome runs at a closed local port
and copies the download caches (touch firmware, fonts, libraries) from this
folder's .esphome into the work copy, so nothing is fetched from the
internet. ESP-IDF components that are not in ESPHome's local component
mirror must then come from a folder named in IDF_COMPONENT_LOCAL_STORAGE_URL,
or from the work copy's build cache (dependencies.lock, managed_components).

Usage (-B: Python writes no __pycache__ into tools/):
  python3 -B tools/check_engine_package.py [--contract DIR] [--engine-dir DIR]
      [--peer-contract DIR] [--host] [--config] [--compile] [--work DIR] [--offline]
      [--esphome PATH] [--report FILE]
  python3 -B tools/check_engine_package.py --record-files
      (after raising the files version: writes the SHA-256 lines)

Exit code 0 only when every selected check passes.
"""

import argparse
import base64
import copy
import datetime
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

if sys.version_info < (3, 12):  # pragma: no cover
    sys.exit("check_engine_package.py needs Python 3.12 or later")

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
# Importing gen_engine_fonts must not leave tools/__pycache__ in the repo,
# also when the script is run without -B.
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE))
import gen_engine_fonts as gen  # noqa: E402

ENGINE = "Engine.yaml"
PROD = "TAB-1-engine.yaml"
BENCH = "TAB-1-engine-bench.yaml"
TAB1 = "TAB-1.yaml"
SHOWCASE = "TAB-1-showcase.yaml"
INCLUDES = ["engine", "engine_glue.h"]
ALLOWED_ACTION_STEPS = {"lambda", "api.respond"}
EXPECTED_EVENT_SCRIPTS = {"esphome.tab1_hello": "tab_send_hello", "esphome.tab1_input": "tab_send_input"}
SECRET_NAME = "tab1_api_key"
KEY_SLOT = 88491486  # ESPHome's NOISE_PSK_PREF_HASH; ota_lock.h keep_key() writes there
ADOPT = "adopt/tab1-adopted.yaml"  # written into the work copy only
ADOPT_NAME = "apollo-tab-1-a1b2c3"
ADOPT_SECRETS = ("wifi_ssid", "wifi_password")
ENTRY_INCLUDES = ["fast_display.h", "ota_lock.h"]
FAST = "fast_display.h"
FAST_TEST = "engine_tests/tests/test_fast_display.cpp"
# The whole of fast_display.h on a computer: the test of flush_cb, setup and
# set_enabled, and the folder of stand-ins for the headers it includes.
FLUSH_TEST = "engine_tests/tests/test_flush_cb.cpp"
FAST_SIM = "engine_tests/host/fast_display_sim"
# The screen's way up: one substitution in the entry files, used by
# Engine.yaml. 90 and 270 are the two landscape ways, half a turn apart.
ROTATION_SUB = "screen_rotation"
ROTATION_DEFAULT = "90"
ROTATION_OTHER = "270"
ADOPT_TURNED = "adopt/tab1-adopted-turned.yaml"  # written into the work copy only
LOCK = "ota_lock.h"
GLUE = "engine_glue.h"
MANIFEST_URL = "https://apolloautomation.github.io/TAB-1/firmware/manifest.json"
IMPORT_URL = "github://ApolloAutomation/TAB-1/Integrations/ESPHome/TAB-1-engine.yaml"
SETUP_WINDOW = "15min"
OTA_ID = "ota_main"
# Bench and production differ only in these (the key, the update doors, the
# update channel and the web page).
BENCH_ONLY_DIFFERENCES = ("ota", "web_server", "dashboard_import", "http_request", "update")
FORBIDDEN_BUTTONS = {"factory_reset", "safe_mode"}
# The namespaces engine_glue.h erases (design 10.8).
ERASE_NAMESPACES = {"esphome", "tab1"}
COPY_IGNORE = shutil.ignore_patterns(
    ".esphome", "engine_tests", ".DS_Store", "__pycache__", "secrets.yaml", "*.pyc"
)
OFFLINE_PROXY = "http://127.0.0.1:9"
OFFLINE_CACHE_DIRS = ("gsl3670", "font", "pio_components")


# ---------------------------------------------------------------------------
#  YAML with ESPHome tags (!include, !secret, !lambda) kept as markers
# ---------------------------------------------------------------------------
# The static checks run on a bare Python 3.12 with the standard library only
# (CI runs them so), so the script reads YAML itself. It reads what these
# files use: block mappings and sequences, plain, quoted and block scalars
# (| and >), flow collections, comments, anchors and aliases, and local tags
# such as !include, !secret and !lambda, kept as Tagged markers. Plain
# scalars resolve as PyYAML's SafeLoader resolves them (YAML 1.1: yes, no,
# on and off are booleans, 0x10 is 16, 1:20 is 80). Anything else (merge
# keys, complex keys, !! tags, several documents, tabs as indentation) stops
# with an error naming the file and line, never a silent misreading. The
# static run checks the reader on a sample (yaml_self_test) and, where PyYAML
# happens to be installed, against PyYAML on every file it read.
class Tagged:
    def __init__(self, tag, value):
        self.tag = tag
        self.value = value

    def __eq__(self, other):
        return isinstance(other, Tagged) and (self.tag, self.value) == (other.tag, other.value)

    def __hash__(self):
        return hash((self.tag, repr(self.value)))

    def __repr__(self):
        return f"{self.tag} {self.value!r}"


class YamlError(ValueError):
    pass


_YAML_NULL = {"", "~", "null", "Null", "NULL"}
_YAML_TRUE = {"yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON"}
_YAML_FALSE = {"no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF"}
_YAML_INT = re.compile(r"[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+")
_YAML_FLOAT = re.compile(
    r"[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?|\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?"
    r"|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN)"
)
_YAML_DATE = re.compile(r"([0-9]{4})-([0-9]{2})-([0-9]{2})")
_YAML_TIMESTAMP = re.compile(r"[0-9]{4}-[0-9]{1,2}-[0-9]{1,2}(?:[Tt]|[ \t]+)[0-9]{1,2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]*)?(?:[ \t]*(?:Z|[-+][0-9]{1,2}(?::[0-9]{2})?))?")
_YAML_ESCAPES = {
    "0": "\0", "a": "\x07", "b": "\x08", "t": "\t", "\t": "\t", "n": "\n", "v": "\x0b", "f": "\x0c", "r": "\r",
    "e": "\x1b", " ": " ", '"': '"', "/": "/", "\\": "\\", "N": "\x85", "_": "\xa0", "L": "\u2028", "P": "\u2029",
}
_YAML_HEX_ESCAPES = {"x": 2, "u": 4, "U": 8}


def _sexagesimal(text):
    total = 0
    for part in text.split(":"):
        total = total * 60 + (float(part) if "." in part else int(part))
    return total


def yaml_resolve(text):
    """A plain scalar as PyYAML's SafeLoader resolves it."""
    if text in _YAML_NULL:
        return None
    if text in _YAML_TRUE:
        return True
    if text in _YAML_FALSE:
        return False
    if _YAML_INT.fullmatch(text):
        v = text.replace("_", "")
        sign = -1 if v[0] == "-" else 1
        v = v[1:] if v[0] in "+-" else v
        if v == "0":
            return 0
        if v.startswith("0b"):
            return sign * int(v[2:], 2)
        if v.startswith("0x"):
            return sign * int(v[2:], 16)
        if v[0] == "0":
            return sign * int(v, 8)
        if ":" in v:
            return sign * _sexagesimal(v)
        return sign * int(v)
    if _YAML_FLOAT.fullmatch(text):
        v = text.replace("_", "").lower()
        sign = -1 if v[0] == "-" else 1
        v = v[1:] if v[0] in "+-" else v
        if v == ".inf":
            return sign * math.inf
        if v == ".nan":
            return math.nan
        if ":" in v:
            return sign * float(_sexagesimal(v))
        return sign * float(v)
    m = _YAML_DATE.fullmatch(text)
    if m:
        return datetime.date(int(m.group(1)), int(m.group(2)), int(m.group(3)))
    if _YAML_TIMESTAMP.fullmatch(text):
        raise YamlError(f"timestamp {text!r}: not read here; quote it")
    if text in ("=", "<<"):
        raise YamlError(f"{text!r} (a YAML value or merge key): not read here")
    return text


def _fold_lines(parts):
    """Line folding of quoted and plain scalars: one line break is a space,
    each empty line in between a line break."""
    out = parts[0]
    i = 1
    while i < len(parts):
        empties = 0
        while i < len(parts) - 1 and parts[i] == "":
            empties += 1
            i += 1
        out += "\n" * empties if empties else " "
        out += parts[i]
        i += 1
    return out


class _YamlReader:
    def __init__(self, text, name):
        self.name = name
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        if text.startswith("\ufeff"):
            text = text[1:]
        self.final_newline = text.endswith("\n")
        self.lines = text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()
        self.i = 0
        self.anchors = {}

    # -- lines ---------------------------------------------------------------
    def fail(self, msg, line=None):
        raise YamlError(f"{self.name}:{(self.i if line is None else line) + 1}: {msg}")

    def indent_of(self, line):
        n = len(line) - len(line.lstrip(" "))
        if line[n:n + 1] == "\t" and line.strip():
            self.fail("a tab in the indentation")
        return n

    @staticmethod
    def blank(line):
        s = line.strip()
        return not s or s.startswith("#")

    def skip_blank(self):
        while self.i < len(self.lines) and self.blank(self.lines[self.i]):
            self.i += 1

    @staticmethod
    def cut_comment(text):
        """Plain text up to a comment (a # after a space, or at the start)."""
        m = re.search(r"(^|[ \t])#", text)
        return (text[: m.start()] if m else text).rstrip()

    # -- document ------------------------------------------------------------
    def document(self):
        self.skip_blank()
        if self.i < len(self.lines) and self.lines[self.i].startswith("%"):
            self.fail("YAML directives are not read here")
        if self.i < len(self.lines) and re.match(r"---(\s|$)", self.lines[self.i]):
            if self.lines[self.i][3:].strip() and not self.lines[self.i][3:].strip().startswith("#"):
                self.fail("content on the --- line is not read here")
            self.i += 1
            self.skip_blank()
        value = None
        if self.i < len(self.lines) and not re.match(r"(---|\.\.\.)(\s|$)", self.lines[self.i]):
            value = self.node(self.indent_of(self.lines[self.i]), -1)
        self.skip_blank()
        if self.i < len(self.lines):
            line = self.lines[self.i]
            if re.match(r"\.\.\.(\s|$)", line):
                self.i += 1
                self.skip_blank()
                if self.i == len(self.lines):
                    return value
            if re.match(r"---(\s|$)", self.lines[self.i]):
                self.fail("a second YAML document is not read here")
            self.fail(f"unexpected indentation or content: {self.lines[self.i].strip()[:60]!r}")
        return value

    # -- block nodes ---------------------------------------------------------
    def node(self, ind, parent):
        """The block node whose first line is self.i, at indent ind."""
        content = self.lines[self.i][ind:]
        if content == "-" or content.startswith(("- ", "-\t")):
            return self.sequence(ind)
        if content.startswith("? ") or content == "?":
            self.fail("complex mapping keys (?) are not read here")
        if self.split_key(content) is not None:
            return self.mapping(ind)
        return self.value(content, parent)

    def split_key(self, content):
        """(key, rest of the line) when content starts a mapping entry, else None."""
        if content[:1] in ('"', "'"):
            end = self.quoted_end(content, 0)
            if end is None:
                return None
            after = content[end:].lstrip(" ")
            if after[:1] == ":" and (len(after) == 1 or after[1] in " \t"):
                return self.unquote(content[:end]), after[1:]
            return None
        if not content or content[0] in "[]{}!&*|>%@`#,":
            return None
        m = re.search(r":(?=[ \t]|$)", content)
        if not m:
            return None
        comment = re.search(r"[ \t]#", content)
        if comment and comment.start() < m.start():
            return None
        key = content[: m.start()].rstrip()
        if not key:
            return None
        if key.startswith(("- ", "? ")):
            return None
        if key == "<<":
            self.fail("merge keys (<<) are not read here")
        try:
            return yaml_resolve(key), content[m.end():]
        except YamlError as err:
            self.fail(str(err))

    def mapping(self, ind):
        out = {}
        while True:
            self.skip_blank()
            if self.i >= len(self.lines):
                break
            line = self.lines[self.i]
            n = self.indent_of(line)
            if n < ind:
                break
            if n > ind:
                self.fail(f"unexpected indentation: {line.strip()[:60]!r}")
            content = line[ind:]
            if re.match(r"(---|\.\.\.)(\s|$)", line):
                break
            if content == "-" or content.startswith(("- ", "-\t")):
                break  # a sequence at the mapping's indent: ends it
            split = self.split_key(content)
            if split is None:
                self.fail(f"expected `key: value`: {content.strip()[:60]!r}")
            key, rest = split
            if key == "<<":
                self.fail("merge keys (<<) are not read here")
            try:
                hash(key)
            except TypeError:
                self.fail(f"a key that is not a scalar: {key!r}")
            out[key] = self.value(rest, ind, same_indent_seq=True)
        return out

    def sequence(self, ind):
        out = []
        while True:
            self.skip_blank()
            if self.i >= len(self.lines):
                break
            line = self.lines[self.i]
            n = self.indent_of(line)
            if n < ind:
                break
            if n > ind:
                self.fail(f"unexpected indentation: {line.strip()[:60]!r}")
            content = line[ind:]
            if not (content == "-" or content.startswith(("- ", "-\t"))):
                break
            rest = content[1:]
            body = rest.lstrip(" ")
            if not body or body.startswith("#"):
                out.append(self.value("", ind))
                continue
            col = ind + 1 + (len(rest) - len(body))
            if body == "-" or body.startswith(("- ", "? ")) or (body[0] not in "!&*|>" and self.split_key(body) is not None):
                # "- key: value" or "- - item": a node that starts at col.
                self.lines[self.i] = " " * col + body
                out.append(self.node(col, ind))
            else:
                out.append(self.value(body, ind))
        return out

    def value(self, rest, ind, same_indent_seq=False):
        """The value that starts with `rest` on line self.i (after `key:` or
        `- `), for a parent at indent ind. Moves self.i past it."""
        text = rest.strip(" \t")
        anchor = tag = None
        while text[:1] in ("&", "!"):
            m = re.match(r"([&!])([^\s]*)(?:[ \t]+|$)", text)
            word = m.group(2)
            if m.group(1) == "&":
                if not word:
                    self.fail("an anchor without a name")
                anchor = word
            else:
                if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.\-/]*", word):
                    self.fail(f"tag !{word}: only local tags such as !include are read here")
                tag = "!" + word
            text = text[m.end():]
        if text.startswith("*"):
            if anchor or tag:
                self.fail("an alias with an anchor or a tag")
            m = re.match(r"\*([^\s,\[\]{}]+)\s*(#.*)?$", text)
            if not m or m.group(1) not in self.anchors:
                self.fail(f"unknown alias {text.split()[0]!r}")
            self.i += 1
            return self.anchors[m.group(1)]
        if not text or text.startswith("#"):
            self.i += 1
            self.skip_blank()
            node = None
            if self.i < len(self.lines):
                line = self.lines[self.i]
                n = self.indent_of(line)
                content = line[n:]
                is_seq = content == "-" or content.startswith(("- ", "-\t"))
                if n > ind or (same_indent_seq and n == ind and is_seq):
                    if re.match(r"(---|\.\.\.)(\s|$)", line):
                        pass
                    elif tag and not is_seq and self.split_key(content) is None:
                        node = Tagged(tag, self.raw_value(content, ind))
                        tag = None
                    else:
                        node = self.node(n, ind)
            if tag:
                node = Tagged(tag, "" if node is None else node)
            if anchor:
                self.anchors[anchor] = node
            return node
        raw, plain = self.scalar_or_flow(text, ind)
        if tag:
            node = Tagged(tag, raw)
        elif plain:
            try:
                node = yaml_resolve(raw)
            except YamlError as err:
                self.fail(str(err))
        else:
            node = raw
        if anchor:
            self.anchors[anchor] = node
        return node

    def raw_value(self, content, ind):
        raw, _plain = self.scalar_or_flow(content.strip(), ind)
        return raw

    def scalar_or_flow(self, text, ind):
        """(value, is_plain) for a scalar or flow collection starting at text
        on line self.i; is_plain says whether a plain scalar still needs
        resolving. Moves self.i past it."""
        start = self.i
        if text[:1] in ("|", ">"):
            return self.block_scalar(text, ind), False
        if text[:1] in ("[", "{"):
            src, end = self.gather_flow(text)
            flow = _FlowReader(src, self, start)
            value = flow.node()
            flow.ws()
            if flow.pos != len(flow.src):
                self.fail(f"text after a flow collection: {flow.src[flow.pos:][:40]!r}", start)
            tail = self.lines[self.i][end:] if end is not None else ""
            if tail.strip() and not tail.strip().startswith("#"):
                self.fail(f"text after a flow collection: {tail.strip()[:40]!r}")
            self.i += 1
            return value, False
        if text[:1] in ('"', "'"):
            return self.quoted_value(text, start), False
        if text[:1] in "%@`":
            self.fail(f"a plain scalar may not start with {text[0]!r}")
        first = self.cut_comment(text)
        parts = [first]
        ended = first != text.rstrip()
        self.i += 1
        while not ended and self.i < len(self.lines):
            line = self.lines[self.i]
            if not line.strip():
                parts.append("")
                self.i += 1
                continue
            n = self.indent_of(line)
            content = line[n:]
            if n <= ind or content.startswith("#"):
                break
            if re.search(r":(?=[ \t]|$)", self.cut_comment(content)):
                self.fail(f"`key: value` inside a plain scalar: {content.strip()[:60]!r}")
            cut = self.cut_comment(content)
            parts.append(cut)
            self.i += 1
            if cut != content.rstrip():
                break
        while parts and parts[-1] == "":
            parts.pop()
            self.i -= 1
        while self.i > start + 1 and self.i > 0 and not self.lines[self.i - 1].strip():
            self.i -= 1
        return _fold_lines([p.strip() for p in parts]) if len(parts) > 1 else parts[0], True

    # -- quoted scalars --------------------------------------------------------
    @staticmethod
    def quoted_end(text, at):
        """Index after the closing quote of the quoted scalar at text[at], on
        this text alone, or None."""
        q = text[at]
        j = at + 1
        while j < len(text):
            c = text[j]
            if q == '"' and c == "\\":
                j += 2
                continue
            if c == q:
                if q == "'" and text[j + 1:j + 2] == "'":
                    j += 2
                    continue
                return j + 1
            j += 1
        return None

    def unquote(self, raw):
        """The value of a whole quoted scalar (quotes included), which may hold
        line breaks."""
        q, body = raw[0], raw[1:-1]
        if "\n" in body:
            segs = []
            for seg in body.split("\n"):
                if segs and segs[-1][1]:
                    # After an escaped line break (\ at the end of a line):
                    # no space, and the next line's indentation goes.
                    segs[-1][0] += seg.lstrip(" \t")
                else:
                    segs.append([seg, False])
                tail = re.search(r"\\+$", segs[-1][0])
                if q == '"' and tail and len(tail.group()) % 2:
                    segs[-1] = [segs[-1][0][:-1], True]
                else:
                    segs[-1][1] = False
            segs = [s for s, _ in segs]
            parts = [segs[0].rstrip(" \t")] + [s.strip(" \t") for s in segs[1:-1]] + [segs[-1].lstrip(" \t")] if len(segs) > 1 else segs
            body = _fold_lines(parts)
        if q == "'":
            return body.replace("''", "'")
        out, j = [], 0
        while j < len(body):
            c = body[j]
            if c != "\\":
                out.append(c)
                j += 1
                continue
            e = body[j + 1:j + 2]
            if e in _YAML_ESCAPES:
                out.append(_YAML_ESCAPES[e])
                j += 2
            elif e in _YAML_HEX_ESCAPES:
                n = _YAML_HEX_ESCAPES[e]
                digits = body[j + 2:j + 2 + n]
                if not re.fullmatch(rf"[0-9a-fA-F]{{{n}}}", digits):
                    self.fail(f"bad escape \\{e}{digits}")
                out.append(chr(int(digits, 16)))
                j += 2 + n
            else:
                self.fail(f"unknown escape \\{e}")
        return "".join(out)

    def quoted_value(self, text, start):
        """A quoted scalar that starts at text (line self.i) and may go on over
        the next lines; the rest of its last line must be empty or a comment."""
        src, line_no = text, self.i
        while True:
            end = self.quoted_end(src, 0)
            if end is not None:
                break
            line_no += 1
            if line_no >= len(self.lines):
                self.fail("a quoted scalar that never ends", start)
            src += "\n" + self.lines[line_no]
        tail = src[end:]
        if tail.strip() and not re.match(r"[ \t]+#", tail):
            self.fail(f"text after a quoted scalar: {tail.strip()[:40]!r}", line_no)
        self.i = line_no + 1
        return self.unquote(src[:end])

    # -- block scalars ---------------------------------------------------------
    def block_scalar(self, header, ind):
        m = re.match(r"([|>])([-+]?)(?:[ \t]+#.*)?$", header)
        if not m:
            self.fail(f"block scalar header {header!r}: only |, |-, |+, >, >- and >+ are read here")
        style, chomp = m.group(1), m.group(2)
        self.i += 1
        body, block = [], None
        while self.i < len(self.lines):
            line = self.lines[self.i]
            if not line.strip():
                body.append(line)
                self.i += 1
                continue
            n = len(line) - len(line.lstrip(" "))  # a tab after the indentation is text
            if block is None:
                if n <= ind:
                    break
                block = n
            if n < block:
                break
            body.append(line)
            self.i += 1
        block = block if block is not None else ind + 1
        lines = [line[block:] if len(line) > block else "" for line in body]
        lead = 0
        while lead < len(lines) and lines[lead] == "":
            lead += 1
        trail = 0
        while len(lines) - trail > lead and lines[len(lines) - trail - 1] == "":
            trail += 1
        core = lines[lead:len(lines) - trail]
        if not core:
            return "\n" * len(lines) if chomp == "+" else ""
        # The last line of a file that does not end in a line break gives
        # the scalar no final line break.
        final = "" if (trail == 0 and self.i == len(self.lines) and not self.final_newline) else "\n"
        if style == "|":
            text = "\n" * lead + "\n".join(core)
        else:
            text = "\n" * lead + core[0]
            empties, prev_more = 0, core[0][:1] in (" ", "\t")
            for line in core[1:]:
                if line == "":
                    empties += 1
                    continue
                more = line[:1] in (" ", "\t")
                if empties == 0:
                    text += ("\n" if (more or prev_more) else " ") + line
                else:
                    text += "\n" * (empties + (1 if (more or prev_more) else 0)) + line
                empties, prev_more = 0, more
        if chomp == "-":
            return text
        if chomp == "+":
            return text + final + "\n" * trail
        return text + final

    # -- flow collections ------------------------------------------------------
    def gather_flow(self, text):
        """The flow collection starting at text (line self.i), over as many
        lines as it takes, comments dropped; and where it ends on its last
        line (self.i is left on that line)."""
        depth, out, q = 0, [], None
        src = text
        offset = len(self.lines[self.i].rstrip(" \t")) - len(text)

        def starts_node():
            # A quote opens a quoted scalar only where a node starts: after
            # [ { , : or a tag or anchor; elsewhere (it's) it is plain text.
            s = "".join(out)
            t = s.rstrip(" \t\n")
            if not t or t[-1] in "[{,:":
                return True
            word = re.split(r"[\s\[\]{},]", t)[-1]
            return word[:1] in ("!", "&") and len(t) < len(s)

        while True:
            j = 0
            while j < len(src):
                c = src[j]
                if q:
                    out.append(c)
                    if q == '"' and c == "\\":
                        out.append(src[j + 1:j + 2])
                        j += 2
                        continue
                    if c == q:
                        if q == "'" and src[j + 1:j + 2] == "'":
                            out.append("'")
                            j += 2
                            continue
                        q = None
                    j += 1
                    continue
                if c == "#" and (j == 0 or src[j - 1] in " \t"):
                    break  # a comment to the end of the line
                if c in "\"'" and starts_node():
                    q = c
                elif c in "[{":
                    depth += 1
                elif c in "]}":
                    depth -= 1
                    if depth == 0:
                        out.append(c)
                        return "".join(out), offset + j + 1
                out.append(c)
                j += 1
            self.i += 1
            if self.i >= len(self.lines):
                self.fail("a flow collection that never ends")
            out.append("\n")
            src, offset = self.lines[self.i], 0


class _FlowReader:
    """[...] and {...}, nested, with quoted and plain scalars, tags, anchors
    and aliases."""

    def __init__(self, src, reader, line):
        self.src, self.pos, self.r, self.line = src, 0, reader, line

    def fail(self, msg):
        self.r.fail(msg, self.line + self.src.count("\n", 0, self.pos))

    def ws(self):
        while self.pos < len(self.src) and self.src[self.pos] in " \t\n":
            self.pos += 1

    def node(self):
        self.ws()
        anchor = tag = None
        while self.src[self.pos:self.pos + 1] in ("&", "!"):
            m = re.compile(r"([&!])([^\s,\[\]{}]*)").match(self.src, self.pos)
            if m.group(1) == "&":
                anchor = m.group(2)
            else:
                if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.\-/]*", m.group(2)):
                    self.fail(f"tag !{m.group(2)}: only local tags are read here")
                tag = "!" + m.group(2)
            self.pos = m.end()
            self.ws()
        c = self.src[self.pos:self.pos + 1]
        plain = False
        if c == "*":
            m = re.compile(r"\*([^\s,\[\]{}]+)").match(self.src, self.pos)
            if not m or m.group(1) not in self.r.anchors:
                self.fail("unknown alias")
            self.pos = m.end()
            return self.r.anchors[m.group(1)]
        if c == "[":
            value = self.seq()
        elif c == "{":
            value = self.map()
        elif c in ('"', "'"):
            end = _YamlReader.quoted_end(self.src, self.pos)
            if end is None:
                self.fail("a quoted scalar that never ends")
            value = self.r.unquote(self.src[self.pos:end])
            self.pos = end
        else:
            value, plain = self.plain(), True
        if tag:
            value = Tagged(tag, value)
        elif plain:
            try:
                value = yaml_resolve(value)
            except YamlError as err:
                self.fail(str(err))
        if anchor:
            self.r.anchors[anchor] = value
        return value

    def plain(self):
        start = self.pos
        while self.pos < len(self.src):
            c = self.src[self.pos]
            if c in ",[]{}":
                break
            if c == ":" and self.src[self.pos + 1:self.pos + 2] in ("", " ", "\t", "\n", ",", "]", "}"):
                break
            if c == "#" and self.pos > start and self.src[self.pos - 1] in " \t\n":
                self.fail("a comment inside a flow collection")
            self.pos += 1
        text = self.src[start:self.pos]
        return " ".join(part.strip() for part in text.split("\n") if part.strip()) if "\n" in text else text.strip()

    def seq(self):
        self.pos += 1
        out = []
        while True:
            self.ws()
            if self.src[self.pos:self.pos + 1] == "]":
                self.pos += 1
                return out
            item = self.node()
            self.ws()
            if self.src[self.pos:self.pos + 1] == ":":
                self.fail("`key: value` inside a flow sequence is not read here")
            out.append(item)
            self.ws()
            c = self.src[self.pos:self.pos + 1]
            if c == ",":
                self.pos += 1
            elif c != "]":
                self.fail(f"expected , or ] in a flow sequence, not {c!r}")

    def map(self):
        self.pos += 1
        out = {}
        while True:
            self.ws()
            if self.src[self.pos:self.pos + 1] == "}":
                self.pos += 1
                return out
            key = self.node()
            self.ws()
            value = None
            if self.src[self.pos:self.pos + 1] == ":":
                self.pos += 1
                self.ws()
                if self.src[self.pos:self.pos + 1] not in (",", "}"):
                    value = self.node()
            try:
                out[key] = value
            except TypeError:
                self.fail(f"a key that is not a scalar: {key!r}")
            self.ws()
            c = self.src[self.pos:self.pos + 1]
            if c == ",":
                self.pos += 1
            elif c != "}":
                self.fail(f"expected , or }} in a flow mapping, not {c!r}")


def parse_yaml(text, name="<yaml>"):
    return _YamlReader(text, name).document()


_READ_FILES = []


def load_yaml(path):
    """A YAML file as PyYAML's SafeLoader would read it, with local tags kept
    as Tagged markers (standard library only)."""
    path = Path(path)
    _READ_FILES.append(path)
    return parse_yaml(path.read_text(encoding="utf-8"), str(path))


# The reader on a sample of what it must read, with the values PyYAML gives
# (checked in every static run, also without PyYAML).
YAML_SAMPLE = """\
# comment
top:
  plain: hello world  # a comment
  url: https://example.com/a#b
  colon: a:b
  quoted: "a \\"b\\" \\u00e9 \\U000F0004 \\\\ # not a comment"
  single: 'it''s # not a comment'
  ints: [0, 7, -3, 0x1F, 0o7, 010, 1_000, 1:20]
  floats: [1.5, .5, -2., 1.0e+3]
  bools: [yes, No, on, OFF, true, False]
  nulls: [~, null]
  strings: [2026.10.2, 15min, 0s, 1e3, 09, "1", 270°]
  substituted: ${screen_rotation}
  empty:
  flow: { text: " ", font: tab_text_16, nested: [a, {b: c}], none: }
  multi: [a,
    b, c]
  literal: |-
    line 1
      line 2

    line 4
  clip: |
    kept
  folded: >-
    one
    two

    three
  wrapped: a long
    plain scalar
  tagged: !lambda return x;
  tagged_block: !lambda |-
    return y;
  include: !include Core.yaml
  anchored: &glyphs
    - "a"
    - "b"
  alias: *glyphs
on: push
list:
- k: v
  k2: v2
- - nested
- !secret key
-
  deep: 1
"""
YAML_SAMPLE_WANT = {
    "top": {
        "plain": "hello world",
        "url": "https://example.com/a#b",
        "colon": "a:b",
        "quoted": 'a "b" \u00e9 \U000F0004 \\ # not a comment',
        "single": "it's # not a comment",
        "ints": [0, 7, -3, 31, "0o7", 8, 1000, 80],
        "floats": [1.5, 0.5, -2.0, 1000.0],
        "bools": [True, False, True, False, True, False],
        "nulls": [None, None],
        "strings": ["2026.10.2", "15min", "0s", "1e3", "09", "1", "270°"],
        "substituted": "${screen_rotation}",
        "empty": None,
        "flow": {"text": " ", "font": "tab_text_16", "nested": ["a", {"b": "c"}], "none": None},
        "multi": ["a", "b", "c"],
        "literal": "line 1\n  line 2\n\nline 4",
        "clip": "kept\n",
        "folded": "one two\nthree",
        "wrapped": "a long plain scalar",
        "tagged": Tagged("!lambda", "return x;"),
        "tagged_block": Tagged("!lambda", "return y;"),
        "include": Tagged("!include", "Core.yaml"),
        "anchored": ["a", "b"],
        "alias": ["a", "b"],
    },
    True: "push",
    "list": [{"k": "v", "k2": "v2"}, ["nested"], Tagged("!secret", "key"), {"deep": 1}],
}
# Each must stop with an error, not be misread.
YAML_REFUSED = (
    ("merge key", "a: &x {b: 1}\nc:\n  <<: *x\n"),
    ("complex key", "? a\n: b\n"),
    ("!! tag", "a: !!str 1\n"),
    ("second document", "a: 1\n---\nb: 2\n"),
    ("tab indentation", "a:\n\tb: 1\n"),
    ("bad indentation", "a:\n    b: 1\n  c: 2\n"),
    ("unknown alias", "a: *nope\n"),
    ("unclosed flow", "a: [1, 2\n"),
    ("text after a quoted scalar", 'a: "b" c\n'),
)


def yaml_self_test(rep):
    try:
        got = parse_yaml(YAML_SAMPLE, "sample")
        problem = "" if got == YAML_SAMPLE_WANT else f"got {got!r}"
    except YamlError as err:
        problem = str(err)
    if not problem and got["top"]["anchored"] is not got["top"]["alias"]:
        problem = "an alias is not the anchored node"
    misread = []
    for what, text in YAML_REFUSED:
        try:
            parse_yaml(text, what)
            misread.append(what)
        except YamlError:
            pass
    rep.check(
        f"self-test: the YAML reader (standard library) reads the sample as PyYAML does and refuses what it does not read ({len(YAML_REFUSED)} cases)",
        not problem and not misread,
        problem or (f"read instead of refused: {misread}" if misread else ""),
    )


def yaml_cross_check(rep):
    """Where PyYAML is installed, every file the static checks read must read
    the same with it (not needed, and skipped, on a bare Python)."""
    try:
        import yaml as pyyaml  # noqa: PLC0415 - optional, only to compare
    except ImportError:
        print("note  yaml: PyYAML is not installed; the reader was checked on its sample only", flush=True)
        return

    class Loader(pyyaml.SafeLoader):
        pass

    def tagged(loader, suffix, node):
        if isinstance(node, pyyaml.ScalarNode):
            return Tagged("!" + suffix, loader.construct_scalar(node))
        if isinstance(node, pyyaml.SequenceNode):
            return Tagged("!" + suffix, loader.construct_sequence(node, deep=True))
        return Tagged("!" + suffix, loader.construct_mapping(node, deep=True))

    Loader.add_multi_constructor("!", tagged)
    files = sorted(set(_READ_FILES))
    differ = []
    for path in files:
        mine = parse_yaml(path.read_text(encoding="utf-8"), str(path))
        with open(path, encoding="utf-8") as f:
            theirs = pyyaml.load(f, Loader=Loader)  # noqa: S506 - SafeLoader subclass
        if mine != theirs:
            differ.append(str(path.relative_to(ROOT.parent.parent)) if _inside(path, ROOT.parent.parent) else str(path))
    sample = pyyaml.load(YAML_SAMPLE, Loader=Loader)  # noqa: S506
    rep.check(
        f"yaml: the reader reads the {len(files)} files of the static checks and its sample as PyYAML {pyyaml.__version__} does",
        not differ and sample == YAML_SAMPLE_WANT,
        "; ".join(([f"read differently: {differ}"] if differ else []) + ([] if sample == YAML_SAMPLE_WANT else ["the sample reads differently"])),
    )


# ---------------------------------------------------------------------------
#  Results
# ---------------------------------------------------------------------------
class Report:
    def __init__(self):
        self.items = []
        self.data = {}

    def check(self, name, ok, detail=""):
        self.items.append({"check": name, "ok": bool(ok), "detail": detail})
        line = f"{'PASS' if ok else 'FAIL'}  {name}"
        if detail and not ok:
            line += f"\n      {detail}"
        elif detail:
            line += f"  ({detail})"
        print(line, flush=True)
        return ok

    @property
    def ok(self):
        return all(item["ok"] for item in self.items)


def _as_list(value):
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def _step_names(steps):
    """Action names of a `then:` list."""
    names = []
    for step in _as_list(steps):
        if isinstance(step, dict):
            names += list(step.keys())
        elif isinstance(step, str):
            names.append(step)
    return names


def _flatten_glyphs(glyphs):
    chars = []
    for item in _as_list(glyphs):
        chars += list(str(item))
    return chars


def _scripts(cfg):
    return {s.get("id"): s for s in _as_list(cfg.get("script")) if isinstance(s, dict)}


# ---------------------------------------------------------------------------
#  Static checks
# ---------------------------------------------------------------------------
def check_actions(engine, contract, rep):
    actions = _as_list((engine.get("api") or {}).get("actions"))
    got = {}
    for a in actions:
        name = a.get("action") or a.get("service")
        variables = a.get("variables") or {}
        args = {}
        for arg, spec in variables.items():
            args[arg] = spec.get("type") if isinstance(spec, dict) else spec
        got[name] = {"args": args, "response": a.get("supports_response"), "then": a.get("then")}
    want = contract["actions"]
    rep.check(
        "actions: the eight action names match contract.json",
        set(got) == set(want),
        f"missing {sorted(set(want) - set(got))}, extra {sorted(set(got) - set(want))}",
    )
    for name in sorted(want):
        if name not in got:
            continue
        w_args = list(want[name]["args"].items())
        g_args = list(got[name]["args"].items())
        rep.check(f"actions: {name} arguments (names, types, order)", g_args == w_args, f"got {g_args}, want {w_args}")
        rep.check(
            f"actions: {name} supports_response is {want[name]['response']}",
            got[name]["response"] == want[name]["response"],
            f"got {got[name]['response']!r} (must be written out, not left to auto-detection)",
        )
        steps = _step_names(got[name]["then"])
        bad = [s for s in steps if s not in ALLOWED_ACTION_STEPS]
        rep.check(
            f"actions: {name} chain is synchronous (only lambda and api.respond)",
            not bad and steps,
            f"steps {steps}; not allowed: {bad}",
        )
        responds = [s for s in _as_list(got[name]["then"]) if isinstance(s, dict) and "api.respond" in s]
        if want[name]["response"] == "none":
            rep.check(f"actions: {name} sends no response", not responds, f"{len(responds)} api.respond steps")
        elif want[name]["response"] == "only":
            ok = len(responds) == 1 and isinstance(responds[0]["api.respond"], dict) and "data" in responds[0]["api.respond"]
            rep.check(f"actions: {name} responds once, with data", ok)
        else:
            ok = (
                len(responds) == 1
                and isinstance(responds[0]["api.respond"], dict)
                and "data" not in responds[0]["api.respond"]
                and {"success", "error_message"} <= set(responds[0]["api.respond"])
            )
            rep.check(f"actions: {name} responds once, with success and error_message", ok)


def check_events(engine, contract, rep):
    found = {}
    for script_id, script in _scripts(engine).items():
        for step in _as_list(script.get("then")):
            if isinstance(step, dict) and "homeassistant.event" in step:
                ev = step["homeassistant.event"]
                found[ev.get("event")] = (script_id, ev)
    want = contract["events"]
    rep.check(
        "events: the two event names match contract.json",
        set(found) == set(want),
        f"got {sorted(found)}, want {sorted(want)}",
    )
    for name in sorted(want):
        if name not in found:
            continue
        script_id, ev = found[name]
        rep.check(
            f"events: {name} is sent by script {EXPECTED_EVENT_SCRIPTS[name]}",
            script_id == EXPECTED_EVENT_SCRIPTS[name],
            f"found in {script_id}",
        )
        keys = sorted((ev.get("data") or {}).keys())
        rep.check(f"events: {name} data keys {sorted(want[name])}", keys == sorted(want[name]), f"got {keys}")
        rep.check(
            f"events: {name} uses data, not data_template or variables",
            not ev.get("data_template") and not ev.get("variables"),
        )
        params = list((_scripts(engine)[script_id].get("parameters") or {}).keys())
        rep.check(f"events: {script_id} parameters are {sorted(want[name])}", sorted(params) == sorted(want[name]), f"got {params}")


HARNESS = Path("engine_tests") / "host" / "harness.h"
FONT_FAMILY = {"tab_text_": "text", "tab_digits_": "digits", "tab_icon_": "icons"}


def harness_metrics(text):
    """[(family, px, line_height, base_line)] of the DEVICE_METRICS table."""
    return [(fam, int(px), int(lh), int(bl)) for fam, px, lh, bl in METRIC_RE.findall(text)]


def harness_metric_problems(text, contract):
    """What is wrong with the harness table against contract.json `fonts`:
    one row per font of Engine.yaml, in its order (--compile compares the
    numbers with a build), and no digits font whose line is higher than its
    fonts.row_px (the lowest rectangle HA gives a label of that size)."""
    problems = []
    rows = harness_metrics(text)
    want = []
    for fid, (_file, px) in gen.font_ids(contract).items():
        family = next(fam for prefix, fam in FONT_FAMILY.items() if fid.startswith(prefix))
        want.append((family, px))
    got = [(fam, px) for fam, px, _lh, _bl in rows]
    if got != want:
        problems.append(f"the table has {got}, Engine.yaml's fonts are {want}")
    row_px = contract["fonts"].get("row_px") or {}
    for fam, px, lh, _bl in rows:
        if fam != "digits":
            continue
        limit = row_px.get(str(px))
        if not isinstance(limit, int):
            problems.append(f"fonts.row_px has no row for the {px} px digits")
        elif lh > limit:
            problems.append(f"the {px} px digits font's line is {lh} px, higher than its fonts.row_px {limit}")
    return problems


def check_fonts(engine, contract, icons, contract_dir, rep):
    fonts = {f.get("id"): f for f in _as_list(engine.get("font")) if isinstance(f, dict)}
    expected = gen.font_ids(contract)
    tab_ids = {fid for fid in fonts if str(fid).startswith("tab_")}
    rep.check(
        f"fonts: exactly the contract sizes ({len(expected)} fonts: text, digits, icons)",
        tab_ids == set(expected) and len(expected) == 14,
        f"missing {sorted(set(expected) - tab_ids)}, extra {sorted(tab_ids - set(expected))}, {len(expected)} in the contract",
    )
    level = gen.ICON_LEVEL
    rep.check(
        f"fonts: the firmware embeds icon level {level}, the newest of icons.json",
        level == icons.get("version"),
        f"icons.json version {icons.get('version')!r}",
    )
    text_set = {chr(c) for c in gen.text_codepoints(contract)}
    digit_set = set(gen.digit_glyphs(contract))
    embedded = {chr(cp) for _name, cp in gen.icon_entries(icons, level)}
    all_up_to = {chr(cp) for cp, lvl in icons["icons"].values() if lvl <= level}
    for fid, (file, size) in expected.items():
        f = fonts.get(fid)
        if f is None:
            continue
        bpp = gen.bpp_of(fid)
        rep.check(
            f"fonts: {fid} is {file} at {size} px, {bpp} bpp",
            f.get("file") == file and f.get("size") == size and f.get("bpp") == bpp,
            f"got file={f.get('file')!r} size={f.get('size')!r} bpp={f.get('bpp')!r}",
        )
        chars = _flatten_glyphs(f.get("glyphs"))
        if fid.startswith("tab_text_"):
            want, what = text_set, "the text set"
        elif fid.startswith("tab_digits_"):
            want, what = digit_set, f"only {gen.digit_glyphs(contract)!r}"
        else:
            want, what = embedded, f"every icons.json icon up to level {level}"
        rep.check(
            f"fonts: {fid} glyphs are {what}",
            set(chars) == want and len(chars) == len(set(chars)),
            f"{len(set(chars))} glyphs, {len(chars) - len(set(chars))} duplicates, "
            f"missing {len(want - set(chars))}, extra {len(set(chars) - want)}",
        )
    rep.check(f"fonts: icon glyph list equals icons.json up to level {level}", embedded == all_up_to and len(embedded) == len(icons["icons"]))
    for key in ("fallback", "placeholder"):
        cp = icons["icons"][icons[key]][0]
        rep.check(f"fonts: the {key} icon {icons[key]} is embedded", chr(cp) in embedded)
    try:
        fresh = gen.is_up_to_date(str(contract_dir), str(ROOT / ENGINE))
    except ValueError as err:
        fresh = False
        rep.check("fonts: generated block is up to date", False, str(err))
    else:
        rep.check("fonts: generated block is up to date (tools/gen_engine_fonts.py --check)", fresh)

    start = _scripts(engine).get("tab_engine_start")
    body = ""
    if start:
        for step in _as_list(start.get("then")):
            if isinstance(step, dict) and "lambda" in step:
                body += str(step["lambda"])
    maps = {}
    for kind in ("text", "digits", "icon"):
        maps[kind] = dict(re.findall(rf"fonts\.{kind}\[(\d+)\]\s*=\s*id\((\w+)\)->get_lv_font\(\)", body))
    for kind, key, prefix in (("text", "text_px", "tab_text_"), ("digits", "digits_px", "tab_digits_"), ("icon", "icon_px", "tab_icon_")):
        sizes = contract["fonts"][key]
        want = {str(i): f"{prefix}{px}" for i, px in enumerate(sizes)}
        rep.check(
            f"fonts: tab::Fonts.{kind}[] gets {', '.join(str(px) for px in sizes)} px in order",
            maps[kind] == want,
            f"got {maps[kind]}",
        )
    rep.check(
        "fonts: tab_engine_start hands every font by its place (no tab::Fonts.digits without a place)",
        not re.search(r"fonts\.digits\s*=", body),
    )
    rep.check("start: tab_engine_start calls tab_glue::start", "tab_glue::start(" in body)

    harness = _read_or_empty(ROOT / HARNESS)
    problems = harness_metric_problems(harness, contract)
    rep.check(
        f"fonts: {HARNESS} has the metrics of the {len(expected)} fonts in Engine.yaml's order, and no digits font's line is higher than contract.json fonts.row_px",
        bool(harness) and not problems,
        "; ".join(problems) if problems else ", ".join(f"{px}: {lh}" for fam, px, lh, _bl in harness_metrics(harness) if fam == "digits"),
    )
    # That rule, on what it must catch.
    rows = re.findall(r'\{"digits",\s*\d+,\s*\d+,\s*\d+\}', harness)
    cases = []
    if rows:
        big = re.sub(r"(\{\"digits\",\s*96,\s*)\d+", lambda m: m.group(1) + "121", harness)
        cases.append(("a 96 px line of 121", big, contract, "higher than its fonts.row_px"))
        no_row = json.loads(json.dumps(contract))
        no_row["fonts"]["row_px"].pop("192", None)
        cases.append(("no row for 192", harness, no_row, "no row for the 192 px digits"))
        cases.append(("a row missing", harness.replace(rows[-1], ""), contract, "Engine.yaml's fonts are"))
        cases.append(("two rows swapped", harness.replace(rows[0], "@@").replace(rows[1], rows[0]).replace("@@", rows[1]), contract, "Engine.yaml's fonts are"))
    missed = [name for name, text, c, want in cases if not any(want in p for p in harness_metric_problems(text, c))]
    rep.check(
        f"self-test: the harness metrics rule fails a digits line above fonts.row_px, a size without a row, and a table row missing or out of order ({len(cases)} cases)",
        len(cases) == 4 and not missed,
        f"not caught: {missed}",
    )


def _walk(value):
    """Every value in a loaded YAML tree, Tagged markers included."""
    yield value
    if isinstance(value, Tagged):
        yield from _walk(value.value)
    elif isinstance(value, dict):
        for k, v in value.items():
            yield from _walk(k)
            yield from _walk(v)
    elif isinstance(value, list):
        for v in value:
            yield from _walk(v)


def _secrets_in(cfg):
    return [v.value for v in _walk(cfg) if isinstance(v, Tagged) and v.tag == "!secret"]


def _included_files(cfg):
    """The files an entry pulls in through `packages: x: !include file`."""
    out = []
    for value in (cfg.get("packages") or {}).values():
        if isinstance(value, Tagged) and value.tag == "!include" and isinstance(value.value, str):
            out.append(value.value)
    return out


def _lambdas(steps):
    return [str(step["lambda"]) for step in _as_list(steps) if isinstance(step, dict) and "lambda" in step]


def strip_cpp_comments(code, blank_strings=False):
    """C++ code without its comments: each becomes a space and keeps its line
    breaks. String and character literals stay whole, or are blanked (same
    length, quotes kept) with blank_strings, so words in them do not count."""
    out = []
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if code.startswith("//", i):
            j = i + 2
            while j < n and not (code[j] == "\n" and code[j - 1] != "\\"):  # a \ at the line end carries it on
                if code[j] == "\n":
                    out.append("\n")
                j += 1
            out.append(" ")
            i = j
        elif code.startswith("/*", i):
            j = code.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * code.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and code[j] not in (c, "\n"):
                j += 2 if code[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * max(0, j - i - 2) + (c if j - i >= 2 else "") if blank_strings else code[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def drop_dead_code(code):
    """`#if 0` (or `#if false`) blocks become empty lines, up to their #else,
    #elif or #endif. Run it on code without comments."""
    out, stack = [], []
    for line in code.split("\n"):
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        word, cond = (m.group(1), m.group(2).strip()) if m else ("", "")
        if word in ("if", "ifdef", "ifndef"):
            if "dead" in stack:
                stack.append("inner")
            elif word == "if" and cond in ("0", "false"):
                stack.append("dead")
            else:
                stack.append("open")
                out.append(line)
                continue
            out.append("")
        elif word in ("elif", "else") and stack and stack[-1] == "dead":
            if word == "elif":  # the #elif part is live under its own condition
                stack[-1] = "open"
                out.append(re.sub(r"#\s*elif", "#if", line, count=1))
            else:  # the #else part is live; its #endif goes with the #if 0
                stack[-1] = "live"
                out.append("")
        elif word == "endif" and stack:
            out.append(line if stack.pop() == "open" else "")
        else:
            out.append("" if "dead" in stack else line)
    return "\n".join(out)


def live_cpp(code):
    """C++ code as the compiler sees its statements: no comments, no `#if 0`
    blocks, string contents blanked."""
    return drop_dead_code(strip_cpp_comments(code, blank_strings=True))


def _function(code, head):
    """From `head` up to the closing brace at the start of a line."""
    at = code.find(head)
    if at < 0:
        return ""
    body = code[at:]
    return body[: body.find("\n}\n") + 2]


def case_body(code, label):
    """The statements of `case <label>:` in live C++ code, up to the next
    case or default label or the end of the switch, at the same depth."""
    m = re.search(rf"\bcase\s+{re.escape(label)}\s*:", code)
    if not m:
        return ""
    body = code[m.end():]
    depth = 0
    for t in re.finditer(r"[{}]|\bcase\b|\bdefault\s*:", body):
        if t.group(0) == "{":
            depth += 1
        elif t.group(0) == "}":
            if depth == 0:
                return body[: t.start()]
            depth -= 1
        elif depth == 0:
            return body[: t.start()]
    return body


REBOOT_CALL = "esphome::App.safe_reboot();"
_REBOOT_RE = re.compile(r"(?<![\w:.])esphome::App\.safe_reboot\(\)\s*;")
_LEAVE_RE = re.compile(r"\b(break|return|continue|goto|throw|co_return)\b")
_BRANCH_RE = re.compile(r"\b(if|else|for|while|do|switch)\b|[{}?]|^\s*#\s*\w+", re.M)


def restart_case_problem(func, label):
    """'' when `case <label>:` in func runs esphome::App.safe_reboot() as a
    plain statement of the case, before anything that leaves the case
    (break, return, ...) and not under a condition, loop or #if. Otherwise
    what is wrong. Comments, `#if 0` blocks and strings do not count."""
    code = live_cpp(func)
    labels = re.findall(rf"\bcase\s+{re.escape(label)}\s*:", code)
    if len(labels) != 1:
        return f"case {label} found {len(labels)} times outside comments"
    at = re.search(rf"\bcase\s+{re.escape(label)}\s*:", code).start()
    switch = code.rfind("switch", 0, at)
    before = code[max(switch, 0): at]
    if len(re.findall(r"^\s*#\s*if", before, re.M)) != len(re.findall(r"^\s*#\s*endif", before, re.M)):
        return f"case {label} is under an #if inside the switch: not in every build"
    body = case_body(code, label)
    call = _REBOOT_RE.search(body)
    if not call:
        return f"case {label} has no {REBOOT_CALL} outside comments and strings"
    prefix = body[: call.start()]
    leave = _LEAVE_RE.search(prefix)
    if leave:
        return f"case {label} can leave ({leave.group(1)}) before {REBOOT_CALL}"
    branch = _BRANCH_RE.search(prefix)
    if branch:
        return f"{REBOOT_CALL} in case {label} is under `{branch.group(0).strip()}`: it may not run"
    if prefix.strip() and not prefix.rstrip().endswith(";"):
        return f"{REBOOT_CALL} in case {label} is not a statement of its own"
    return ""


# Synthetic switches the restart rule must pass or fail (the self-test):
# (what, statements of case RESTART_CLEARED, passes).
RESTART_SELF_TESTS = (
    ("as shipped", 'ESP_LOGW(TAG, "restarting");\n      esphome::App.safe_reboot();\n      break;', True),
    ("commented out", "// esphome::App.safe_reboot();\n      break;", False),
    ("in a block comment", "/* esphome::App.safe_reboot(); */\n      break;", False),
    ("in a comment carried on by a \\", "// note \\\n      esphome::App.safe_reboot();\n      break;", False),
    ("under #if 0", "#if 0\n      esphome::App.safe_reboot();\n#endif\n      break;", False),
    ("in the #else of #if 0 (built)", "#if 0\n      foo();\n#else\n      esphome::App.safe_reboot();\n#endif\n      break;", True),
    ("break before", "break;\n      esphome::App.safe_reboot();", False),
    ("return before", "return;\n      esphome::App.safe_reboot();\n      break;", False),
    ("return under a condition before", "if (in.now_ms == 0) return;\n      esphome::App.safe_reboot();\n      break;", False),
    ("under a condition", "if (in.network)\n        esphome::App.safe_reboot();\n      break;", False),
    ("under #ifdef", "#ifdef USE_X\n      esphome::App.safe_reboot();\n#endif\n      break;", False),
    ("only in a string", 'ESP_LOGW(TAG, "esphome::App.safe_reboot();");\n      break;', False),
    ("only in the next case", "break;\n    case core::OTHER:\n      esphome::App.safe_reboot();\n      break;", False),
    ("the case label commented out", None, False),
)


def _restart_poll(cleared):
    head = "    case core::RESTART_CLEARED:\n" if cleared is not None else "    // case core::RESTART_CLEARED: esphome::App.safe_reboot(); break;\n"
    return (
        "inline void poll(esphome::ESPHomeOTAComponent *ota) {\n"
        "  switch (core::step(state(), in)) {\n"
        "    case core::KEY_CLEARED:\n      break;\n"
        f"{head}      {cleared or ''}\n"
        '    case core::RESTART_OFFLINE:\n      ESP_LOGW(TAG, "offline");\n      esphome::App.safe_reboot();\n      break;\n'
        "    default:\n      break;\n  }\n}\n"
    )


def check_restart_self_test(rep):
    wrong = []
    for what, cleared, passes in RESTART_SELF_TESTS:
        poll = _restart_poll(cleared)
        if (restart_case_problem(poll, "core::RESTART_CLEARED") == "") != passes or restart_case_problem(poll, "core::RESTART_OFFLINE"):
            wrong.append(what)
    rep.check(
        f"self-test: the restart rule passes the shipped case and fails a commented-out, skipped or conditional restart ({len(RESTART_SELF_TESTS)} cases)",
        not wrong,
        f"wrong for: {wrong}" if wrong else "",
    )


# The version of the package's C++ files (design 10.9): each file carries its
# own number, and Engine.yaml, which always comes from the package, checks
# them, so a copy in the ESPHome config folder that is older or foreign stops
# the build.
FILES_VERSION = {
    "engine/tab_files.h": "TAB1_FILES_ENGINE",
    GLUE: "TAB1_FILES_GLUE",
    LOCK: "TAB1_FILES_LOCK",
}
# Engine.yaml checks each number with an #if block, not a static_assert: a
# copy from before the numbers has no macro at all, and a static_assert on it
# would only say "not declared". (macro, number, message)
FILES_ASSERT_RE = re.compile(r'^[ \t]*#if !defined\((TAB1_FILES_\w+)\) \|\| \1 != (\d+)\n[ \t]*#error "([^"\n]*)"\n[ \t]*#endif$', re.M)
# The error line a compiler prints for one of them (GCC `error: #error "..."`,
# clang `error: "..."`), as opposed to the source line it may show below.
FILES_ERROR_LINE = r"error: (?:#error )?\"?{name} is not this package's copy: delete the old {name}"
# The SHA-256 of each file the number covers, recorded in tab_files.h for one
# number: a file that differs at the same number needs a higher number. The
# `// files N all <sha256>` line above them holds the SHA-256 of all those
# lines: two branches that both raise the number to N change that one line
# differently, so git stops their merge with a conflict there (one line per
# file alone would merge cleanly, and N would stand for two sets of files),
# and a merge resolved by hand fails the check until the number is raised again.
FILES_LIST = "engine/tab_files.h"
FILES_RECORD_RE = re.compile(r"^// files (\d+)(?: all ([0-9a-f]{64}))?$", re.M)
FILES_SHA_RE = re.compile(r"^// sha256 ([0-9a-f]{64}) (\S+)$", re.M)
FILES_BLOCK_RE = re.compile(r"^// files \d+(?: all [0-9a-f]{64})?\n(?:// (?:sha256 [^\n]*|\(none recorded yet\))\n)*", re.M)
RECORD_COMMAND = "python3 -B tools/check_engine_package.py --record-files"


def files_versions(root=None):
    """{macro: number or None} as the files define them."""
    root = root or ROOT
    out = {}
    for rel, macro in FILES_VERSION.items():
        path = root / rel
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        m = re.search(rf"^#define {macro} (\d+)$", text, re.M)
        out[macro] = int(m.group(1)) if m else None
    return out


def files_asserts(engine):
    """{macro: (number, message)} from the #if checks in tab_engine_start."""
    start = _scripts(engine).get("tab_engine_start") or {}
    body = "".join(_lambdas(start.get("then")))
    return {m.group(1): (int(m.group(2)), m.group(3)) for m in FILES_ASSERT_RE.finditer(body)}


def versioned_files(root=None):
    """The files the files version covers, relative to root: everything in
    engine/ but tab_files.h (which holds the number and the list), then
    engine_glue.h and ota_lock.h."""
    root = root or ROOT
    engine = sorted(p.relative_to(root).as_posix() for p in (root / "engine").rglob("*") if p.is_file() and p.name != "tab_files.h")
    return engine + [GLUE, LOCK]


def file_sha(path):
    """SHA-256 of a text file, with Windows line ends read as \\n."""
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def files_all(shas):
    """The SHA-256 of the per-file lines, sorted by file: the `all` hash."""
    lines = "".join(f"sha256 {shas[n]} {n}\n" for n in sorted(shas))
    return hashlib.sha256(lines.encode("utf-8")).hexdigest()


def files_recorded(text):
    """(number the SHA-256 lines are for or None, {file: sha256}, the `all`
    hash on that line or None)."""
    m = FILES_RECORD_RE.search(text)
    return (int(m.group(1)) if m else None), {name: sha for sha, name in FILES_SHA_RE.findall(text)}, (m.group(2) if m else None)


def files_hash_problem(number, recorded_number, recorded, current, all_hash):
    """'' when the files are as recorded for this number, else what to do."""
    if number is None:
        return f"no TAB1_FILES_ENGINE number in {FILES_LIST}"
    if recorded_number is None:
        return f"no `// files N` line in {FILES_LIST}"
    if recorded_number > number:
        return f"the SHA-256 lines in {FILES_LIST} are for files version {recorded_number}, above the number {number}: the number only goes up"
    if recorded_number < number:
        return f"the files version is {number} but the SHA-256 lines in {FILES_LIST} are for {recorded_number}: run {RECORD_COMMAND}"
    if all_hash != files_all(recorded):
        what = "has no `all` hash" if all_hash is None else "has an `all` hash that is not the SHA-256 of the lines below it"
        return (
            f"the `// files {number}` line in {FILES_LIST} {what} (two changes met at files version {number}, as in a merge, "
            f"or the lines were edited by hand): raise it to {number + 1} in {FILES_LIST}, {GLUE}, {LOCK} "
            f"and the three checks in {ENGINE}, then run {RECORD_COMMAND}"
        )
    changed = sorted(n for n in current if n in recorded and recorded[n] != current[n])
    added = sorted(set(current) - set(recorded))
    gone = sorted(set(recorded) - set(current))
    if not (changed or added or gone):
        return ""
    what = "; ".join(x for x in (
        f"changed: {', '.join(changed)}" if changed else "",
        f"new: {', '.join(added)}" if added else "",
        f"gone: {', '.join(gone)}" if gone else "",
    ) if x)
    return (
        f"{what}, but the files version is still {number}: raise it to {number + 1} in {FILES_LIST}, {GLUE}, {LOCK} "
        f"and the three checks in {ENGINE}, then run {RECORD_COMMAND}"
    )


def files_hash_state(root=None):
    root = root or ROOT
    path = root / FILES_LIST
    text = path.read_text(encoding="utf-8") if path.is_file() else ""
    number = files_versions(root)["TAB1_FILES_ENGINE"]
    recorded_number, recorded, all_hash = files_recorded(text)
    current = {n: file_sha(root / n) for n in versioned_files(root)}
    return number, recorded_number, recorded, current, all_hash


def record_files():
    """--record-files: write the SHA-256 lines and their `all` hash into
    tab_files.h for the current number. Refuses unless the number was raised
    since the last record (or nothing changed); also when the lines at this
    number do not add up to their `all` hash (two changes met there)."""
    have = files_versions()
    asserts = files_asserts(load_yaml(ROOT / ENGINE))
    numbers = set(have.values()) | {n for n, _ in asserts.values()}
    if None in have.values() or set(asserts) != set(FILES_VERSION.values()) or len(numbers) != 1:
        return f"--record-files: the files version numbers differ (files {have}, {ENGINE} {dict((k, v[0]) for k, v in asserts.items())}); make them one number first"
    number, recorded_number, recorded, current, all_hash = files_hash_state()
    if recorded_number is not None and number < recorded_number:
        return f"--record-files: the SHA-256 lines are for files version {recorded_number}, above {number}: the number only goes up"
    if recorded_number == number:
        problem = files_hash_problem(number, recorded_number, recorded, current, all_hash)
        if not problem:
            print(f"Nothing to record: the {len(current)} files are as recorded for files version {number}.")
            return ""
        return f"--record-files refused: {problem.replace(f', then run {RECORD_COMMAND}', '')} first"
    path = ROOT / FILES_LIST
    text = path.read_text(encoding="utf-8")
    if not FILES_BLOCK_RE.search(text):
        return f"--record-files: no `// files N` block in {FILES_LIST}"
    block = f"// files {number} all {files_all(current)}\n" + "".join(f"// sha256 {current[n]} {n}\n" for n in sorted(current))
    path.write_text(FILES_BLOCK_RE.sub(lambda _m: block, text, count=1), encoding="utf-8")
    print(f"Recorded the SHA-256 of {len(current)} files for files version {number} in {FILES_LIST}.")
    return ""


# The hash rule on made-up states (the self-test): (what, number, recorded
# number, recorded, current, all hash, passes). `True` as the all hash: the
# right one for the recorded lines.
_SHA_A, _SHA_B = "a" * 64, "b" * 64
# Two branches from files version 2 each change one file and raise to 3; git
# merges their per-file lines, and the conflict on the `all` line is resolved
# by taking one side.
_BRANCH_X = {"engine/x.h": _SHA_B, "engine/y.h": _SHA_A}
_MERGED = {"engine/x.h": _SHA_B, "engine/y.h": _SHA_B}
FILES_SELF_TESTS = (
    ("as recorded", 2, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_A}, True, True),
    ("a file changed at the same number", 2, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_B}, True, False),
    ("a new file at the same number", 2, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_A, "engine/y.h": _SHA_A}, True, False),
    ("a file gone at the same number", 2, 2, {"engine/x.h": _SHA_A, "engine/y.h": _SHA_A}, {"engine/x.h": _SHA_A}, True, False),
    ("number raised, lines not recorded", 3, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_B}, True, False),
    ("number lowered", 1, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_A}, True, False),
    ("nothing recorded", 2, None, {}, {"engine/x.h": _SHA_A}, None, False),
    ("two branches raised to one number, merged", 3, 3, _MERGED, _MERGED, files_all(_BRANCH_X), False),
    ("no all hash at the same number", 2, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_A}, None, False),
)


def _self_test_all(recorded, all_hash):
    return files_all(recorded) if all_hash is True else all_hash


def check_files_version(engine, rep):
    have = files_versions()
    asserts = files_asserts(engine)
    numbers = set(have.values()) | {n for n, _ in asserts.values()}
    rep.check(
        "files: engine/, engine_glue.h and ota_lock.h carry one files version, and Engine.yaml checks each (#if !defined || !=, #error)",
        None not in have.values() and set(asserts) == set(FILES_VERSION.values()) and len(numbers) == 1,
        f"files {have}, Engine.yaml {dict((k, v[0]) for k, v in asserts.items())}",
    )
    rep.check(
        "files: each #error names the copy and says to delete it from the ESPHome config folder",
        len(asserts) == 3
        and all(
            "not this package's copy" in msg and "config folder" in msg and re.search(rf"delete the old {re.escape(name.rstrip('/'))}", msg)
            for name, (_n, msg) in ((n, asserts.get(m, (0, ""))) for n, m in (("engine/", "TAB1_FILES_ENGINE"), (GLUE, "TAB1_FILES_GLUE"), (LOCK, "TAB1_FILES_LOCK")))
        ),
    )
    number, recorded_number, recorded, current, all_hash = files_hash_state()
    problem = files_hash_problem(number, recorded_number, recorded, current, all_hash)
    rep.check(
        f"files: the {len(current)} files the number covers are as recorded for files version {number} in {FILES_LIST}, "
        "with the `all` hash of their lines (a change, or two changes merged at one number, needs a higher number)",
        not problem,
        problem,
    )
    wrong = [
        w for w, n, rn, rec, cur, ah, passes in FILES_SELF_TESTS
        if (files_hash_problem(n, rn, rec, cur, _self_test_all(rec, ah)) == "") != passes
    ]
    raise_msg = files_hash_problem(2, 2, {"engine/x.h": _SHA_A}, {"engine/x.h": _SHA_B}, files_all({"engine/x.h": _SHA_A}))
    merge_msg = files_hash_problem(3, 3, _MERGED, _MERGED, files_all(_BRANCH_X))
    rep.check(
        "self-test: the files rule fails a change, a new or a gone file at the same number, and two branches merged at one "
        f"number (which --record-files refuses too), and says to raise it ({len(FILES_SELF_TESTS)} cases)",
        not wrong and "raise it to 3" in raise_msg and "raise it to 4" in merge_msg,
        f"wrong for: {wrong}" if wrong else "",
    )
    tab_engine = (ROOT / "engine" / "tab_engine.h").read_text(encoding="utf-8") if (ROOT / "engine" / "tab_engine.h").is_file() else ""
    glue = (ROOT / GLUE).read_text(encoding="utf-8") if (ROOT / GLUE).is_file() else ""
    lock = (ROOT / LOCK).read_text(encoding="utf-8") if (ROOT / LOCK).is_file() else ""
    cross = "TAB1_FILES_ENGINE != TAB1_FILES_{}\n#error"
    rep.check(
        "files: tab_engine.h includes tab_files.h; engine_glue.h and ota_lock.h stop a build whose engine/ has another number",
        '#include "tab_files.h"' in tab_engine and cross.format("GLUE") in glue and cross.format("LOCK") in lock,
    )


def _bench_view(cfg):
    """The parts that must be equal in bench and production."""
    out = copy.deepcopy(cfg)
    enc = (out.get("api") or {}).get("encryption")
    if isinstance(enc, dict):
        enc.pop("key", None)
        if not enc:
            out["api"]["encryption"] = None
    for key in BENCH_ONLY_DIFFERENCES:
        out.pop(key, None)
    if isinstance(out.get("wifi"), dict):
        out["wifi"].pop("on_connect", None)  # starts the update check
    return out


def check_lock_hooks(name, cfg, rep):
    """ota_lock.h is wired in (design 10.8)."""
    esp = cfg.get("esphome") or {}
    boot = [b for b in _as_list(esp.get("on_boot")) if isinstance(b, dict)]
    prio = [float(b.get("priority", 600)) for b in boot]
    locks = [i for i, b in enumerate(boot) if any(f"tab_lock::boot(id({OTA_ID}))" in code for code in _lambdas(b.get("then")))]
    engine = [i for i, b in enumerate(boot) if any(isinstance(s, dict) and s.get("script.execute") == "tab_engine_start" for s in _as_list(b.get("then")))]
    rep.check(
        f"lock: {name} on_boot priority 700 calls tab_lock::boot(id({OTA_ID})) and nothing else",
        len(locks) == 1 and prio[locks[0]] == 700.0 and _step_names(boot[locks[0]].get("then")) == ["lambda"],
        f"on_boot priorities {prio}",
    )
    rep.check(
        f"lock: {name} the lock runs before the engine (priority 700 before 100)",
        len(locks) == 1 and len(engine) == 1 and prio[locks[0]] > prio[engine[0]] and prio[engine[0]] == 100.0,
        f"on_boot priorities {prio}",
    )
    loop = _lambdas(esp.get("on_loop"))
    rep.check(f"lock: {name} on_loop calls tab_lock::poll(id({OTA_ID}))", any(f"tab_lock::poll(id({OTA_ID}))" in code for code in loop), f"got {loop}")
    safe = _lambdas((cfg.get("safe_mode") or {}).get("on_safe_mode"))
    rep.check(
        f"lock: {name} safe_mode on_safe_mode calls tab_lock::safe_mode(id({OTA_ID}))",
        any(f"tab_lock::safe_mode(id({OTA_ID}))" in code for code in safe),
        f"got {safe}",
    )
    esphome_otas = [o for o in _as_list(cfg.get("ota")) if isinstance(o, dict) and o.get("platform") == "esphome"]
    rep.check(f"lock: {name} has one `ota: esphome` with id {OTA_ID}", len(esphome_otas) == 1 and esphome_otas[0].get("id") == OTA_ID, f"got {esphome_otas!r}")


def check_setup(name, cfg, rep):
    """Open hotspot, set-up page and the 15-minute set-up window."""
    wifi = cfg.get("wifi") or {}
    ap = wifi.get("ap")
    rep.check(
        f"setup: {name} hotspot is open (no password) and keeps ESPHome's 90 s return",
        isinstance(ap, dict) and "password" not in ap and "ap_timeout" not in ap and "ap_timeout" not in wifi,
        f"got {ap!r}",
    )
    rep.check(f"setup: {name} has the set-up page (captive_portal)", "captive_portal" in cfg)
    prov = cfg.get("provisioning")
    window = prov.get("timeout") if isinstance(prov, dict) else None
    on_timeout = _lambdas(prov.get("on_timeout")) if isinstance(prov, dict) else []
    rep.check(f"setup: {name} set-up window is {SETUP_WINDOW} (provisioning timeout)", window == SETUP_WINDOW, f"got {prov!r}")
    rep.check(
        f"setup: {name} a closed window tells the waiting screen (tab_glue::setup_window_closed)",
        any("tab_glue::setup_window_closed()" in code for code in on_timeout),
        f"got {on_timeout}",
    )
    rep.check(f"setup: {name} never turns the hotspot's return off (no set_ap_timeout)", "set_ap_timeout" not in repr(cfg))


def check_buttons(name, cfg, rep):
    found = []
    for domain in ("button", "switch"):
        for item in _as_list(cfg.get(domain)):
            if isinstance(item, dict) and item.get("platform") in FORBIDDEN_BUTTONS:
                found.append(f"{domain}: {item.get('platform')}")
    rep.check(f"entry: {name} has no factory reset or safe mode button", not found, f"found {found}")


def check_entries(prod, bench, tab1, rep):
    for name, cfg in ((PROD, prod), (BENCH, bench)):
        api = cfg.get("api") or {}
        wifi = cfg.get("wifi") or {}
        rep.check(f"entry: {name} api reboot_timeout is 0s", api.get("reboot_timeout") == "0s", f"got {api.get('reboot_timeout')!r}")
        rep.check(f"entry: {name} wifi reboot_timeout is 0s", wifi.get("reboot_timeout") == "0s", f"got {wifi.get('reboot_timeout')!r}")
        rep.check(
            f"entry: {name} pulls in Engine.yaml, Core.yaml and a board",
            (cfg.get("packages") or {}).get("engine") == Tagged("!include", ENGINE)
            and (cfg.get("packages") or {}).get("core") == Tagged("!include", "Core.yaml")
            and isinstance((cfg.get("packages") or {}).get("board"), Tagged),
        )
        esp = cfg.get("esphome") or {}
        rep.check(f"entry: {name} includes exactly {ENTRY_INCLUDES}", esp.get("includes") == ENTRY_INCLUDES, f"got {esp.get('includes')}")
        rep.check(f"entry: {name} loop_interval is 2ms", esp.get("loop_interval") == "2ms")
        boot_steps = []
        for item in _as_list(esp.get("on_boot")):
            if isinstance(item, dict):
                boot_steps += _as_list(item.get("then"))
        order = []
        for step in boot_steps:
            if isinstance(step, dict) and "fast_display::setup" in str(step.get("lambda", "")):
                order.append("fast_display")
            if isinstance(step, dict) and step.get("script.execute") == "tab_engine_start":
                order.append("engine")
        rep.check(f"entry: {name} on_boot runs fast_display::setup, then the engine", order == ["fast_display", "engine"], f"got {order}")
        subs, subs1 = cfg.get("substitutions") or {}, tab1.get("substitutions") or {}
        esp1 = tab1.get("esphome") or {}
        same = (
            subs.get("name") == subs1.get("name")
            and subs.get("friendly_name") == subs1.get("friendly_name")
            and esp.get("name") == esp1.get("name")
            and esp.get("friendly_name") == esp1.get("friendly_name")
            and esp.get("name_add_mac_suffix") == esp1.get("name_add_mac_suffix")
            and (esp.get("project") or {}).get("name") == (esp1.get("project") or {}).get("name")
        )
        rep.check(f"entry: {name} is the same device and project as {TAB1}", same)
        turn = subs.get(ROTATION_SUB)
        rep.check(
            f'entry: {name} sets the substitution {ROTATION_SUB} to "{ROTATION_DEFAULT}" ("{ROTATION_OTHER}" is the other way up)',
            isinstance(turn, (str, int)) and not isinstance(turn, bool) and str(turn).removesuffix("°") == ROTATION_DEFAULT,
            f"got {turn!r}",
        )
        check_lock_hooks(name, cfg, rep)
        check_setup(name, cfg, rep)
        check_buttons(name, cfg, rep)
        for inc in _included_files(cfg):
            path = ROOT / inc
            if path.is_file():
                check_buttons(f"{name} -> {inc}", load_yaml(path), rep)

    # Production: no shared secret anywhere, the update lock, the update channel.
    secrets = _secrets_in(prod)
    for inc in _included_files(prod):
        path = ROOT / inc
        if path.is_file():
            secrets += [f"{inc}: {s}" for s in _secrets_in(load_yaml(path))]
        else:
            secrets.append(f"{inc}: missing file")
    rep.check(f"entry: {PROD} ships no secret (no !secret in it or the files it includes)", not secrets, f"found {secrets}")
    p_enc = (prod.get("api") or {}).get("encryption", "missing")
    rep.check(
        f"entry: {PROD} has api encryption without a key",
        p_enc is None or (isinstance(p_enc, dict) and "key" not in p_enc),
        f"got {p_enc!r}",
    )
    otas = [o for o in _as_list(prod.get("ota")) if isinstance(o, dict)]
    platforms = sorted(o.get("platform") for o in otas)
    rep.check(f"entry: {PROD} ota platforms are esphome and http_request", platforms == ["esphome", "http_request"], f"got {platforms}")
    esp_ota = next((o for o in otas if o.get("platform") == "esphome"), {})
    rep.check(
        f"entry: {PROD} `ota: esphome` has password \"\" (the lock sets it) and no encryption",
        esp_ota.get("password") == "" and "encryption" not in esp_ota,
        f"got {esp_ota!r}",
    )
    http = prod.get("http_request")
    rep.check(f"entry: {PROD} http_request verify_ssl is true", isinstance(http, dict) and http.get("verify_ssl") is True, f"got {http!r}")
    updates = [u for u in _as_list(prod.get("update")) if isinstance(u, dict)]
    rep.check(
        f"entry: {PROD} update: http_request from {MANIFEST_URL}",
        len(updates) == 1 and updates[0].get("platform") == "http_request" and updates[0].get("source") == MANIFEST_URL,
        f"got {updates!r}",
    )
    on_connect = _as_list((prod.get("wifi") or {}).get("on_connect"))
    rep.check(
        f"entry: {PROD} checks for an update on each Wi-Fi connect",
        updates and any(isinstance(s, dict) and s.get("component.update") == updates[0].get("id") for s in on_connect),
        f"got {on_connect!r}",
    )
    imp = prod.get("dashboard_import")
    rep.check(
        f"entry: {PROD} dashboard_import points at itself, as a package",
        isinstance(imp, dict) and imp.get("package_import_url") == IMPORT_URL and imp.get("import_full_config") is False,
        f"got {imp!r}",
    )
    rep.check(f"entry: {PROD} has no web page (no web_server)", "web_server" not in prod)

    # Bench: the fixed key, encrypted updates only, the web page without firmware upload.
    b_key = ((bench.get("api") or {}).get("encryption") or {}).get("key")
    rep.check(f"entry: {BENCH} has encryption key !secret {SECRET_NAME}", b_key == Tagged("!secret", SECRET_NAME), f"got {b_key!r}")
    rep.check(f"entry: {BENCH} has no other secret", _secrets_in(bench) == [SECRET_NAME], f"got {_secrets_in(bench)}")
    b_otas = [o for o in _as_list(bench.get("ota")) if isinstance(o, dict)]
    ok = len(b_otas) == 1 and b_otas[0].get("platform") == "esphome" and "encryption" in b_otas[0] and "password" not in b_otas[0] and not (b_otas[0].get("encryption") or {})
    rep.check(f"entry: {BENCH} network update is encrypted with the API key, nothing else", ok, f"got {b_otas!r}")
    web = bench.get("web_server")
    rep.check(
        f"entry: {BENCH} web page takes no firmware (web_server ota: false)",
        isinstance(web, dict) and web.get("ota") is False and "auth" not in web,
        f"got {web!r}",
    )
    rep.check(f"entry: {BENCH} has no update channel or dashboard_import", not any(k in bench for k in ("update", "http_request", "dashboard_import")))
    rep.check(f"entry: {BENCH} equals {PROD} apart from the key, ota, web page and update channel", _bench_view(prod) == _bench_view(bench))


def check_lock_header(rep):
    """The rules design 10.8 names, in ota_lock.h and engine_glue.h."""
    lock = (ROOT / LOCK).read_text(encoding="utf-8") if (ROOT / LOCK).is_file() else ""
    rep.check(f"lock: {LOCK} is there", bool(lock))
    key_saved = lock[lock.find("inline bool key_saved()"):]
    key_saved = key_saved[: key_saved.find("\n}\n") + 2]
    rep.check(
        f"lock: {LOCK} key_saved() is also true with the key in the YAML (#elif defined(USE_OTA_ENCRYPTION))",
        "USE_OTA_ENCRYPTION_PROVISIONED" in key_saved and "#elif defined(USE_OTA_ENCRYPTION)" in key_saved and "return true;" in key_saved,
    )
    safe = lock[lock.find("inline void safe_mode("):]
    rep.check(f"lock: {LOCK} locks in safe mode when a key exists", "key_saved()" in safe[:400] and "lock(ota" in safe[:400])
    rep.check(
        f"lock: {LOCK} answers 403 to POST /update while locked",
        'request->send(403' in lock and '== "/update"' in lock and "HTTP_POST" in lock and "add_handler_without_auth(new UpdateGate())" in lock,
    )
    # Each restart must be in its own case: the RESTART_OFFLINE one must not
    # stand in for a missing RESTART_CLEARED one, or the other way round. The
    # restart must run: not commented out or under #if 0, and before any
    # break or return of its case, not under a condition (self-tested below).
    live_lock = live_cpp(lock)
    poll = _function(live_lock, "inline void poll(")
    for label, what in (
        ("core::RESTART_CLEARED", "restarts once about 2 s after the key is cleared"),
        ("core::RESTART_OFFLINE", "restarts when the network is back after the window closed offline"),
    ):
        problem = restart_case_problem(poll, label)
        if label == "core::RESTART_CLEARED" and "CLEARED_REBOOT_MS = 2000" not in live_lock:
            problem = problem or "no CLEARED_REBOOT_MS = 2000"
        rep.check(
            f"lock: {LOCK} {what} ({REBOOT_CALL} first in case {label.split('::')[1]}, before any break or return)",
            not problem,
            f"{problem}; case body {' '.join(case_body(poll, label).split())!r}" if problem else "",
        )
    check_restart_self_test(rep)
    rep.check(
        f"lock: {LOCK} password is hex(SHA-256(key || random || counter)), random alone without a key",
        "sha256_update(sha, key, 32)" in lock and "sha256_update(sha, random, 32)" in lock and "esphome::random_bytes(" in lock,
    )
    rep.check(
        f"lock: {LOCK} never locks with a made-up password (no key and no random bytes: logs and tries again)",
        "if (made == core::MADE_NONE)" in lock and "return false;" in lock[lock.find("if (made == core::MADE_NONE)"):][:400],
    )
    rep.check(f"lock: {LOCK} wipes the key, the random bytes and the password", all(f"wipe({b}, sizeof {b})" in lock for b in ("key", "rnd", "pw")))
    # A build with the key in its YAML (adopted in ESPHome Builder, bench)
    # must leave that key in the settings: Apollo's release installed over it
    # reads its key only from there, and would otherwise start with no key.
    keep = lock[lock.find("inline void keep_key()"):]
    keep = keep[: keep.find("\n}\n") + 2]
    boot = lock[lock.find("inline void boot("):]
    boot = boot[: boot.find("\n}\n") + 2]
    rep.check(
        f"lock: {LOCK} keeps a key from the YAML in the settings (keep_key, called at boot), written only when it differs",
        "defined(USE_API_NOISE_PSK_FROM_YAML)" in keep
        and "make_preference<esphome::api::SavedNoisePsk>(KEY_SLOT, true)" in keep
        and "core::stored_key_stale(" in keep
        and "pref.save(&yaml) && esphome::global_preferences->sync()" in keep
        and keep.find("core::stored_key_stale(") < keep.find("pref.save(&yaml)")
        and "keep_key();" in boot
        and f"KEY_SLOT = {KEY_SLOT}UL;" in lock,
    )
    rep.check(f"lock: {LOCK} sets the password only where the build has one (USE_OTA_PASSWORD)", "#ifdef USE_OTA_PASSWORD" in lock and lock.count("set_auth_password(") == 1)
    # A build without the password check must not compile, unless it requires
    # updates encrypted with a key from the YAML (design 10.9). The host test
    # runs the preprocessor on every combination.
    guard = re.search(r"#if !defined\(USE_OTA_PASSWORD\)(.*?)\n#error \"([^\n]*)\"\n#endif", lock, re.S)
    cond = re.sub(r"\s+", " ", guard.group(1).replace("\\\n", " ")) if guard else ""
    rep.check(
        f"lock: {LOCK} stops a build that has no update password check and does not require encryption with a YAML key (#error)",
        bool(guard)
        and "defined(USE_OTA_ENCRYPTION_REQUIRED) && defined(USE_OTA_ENCRYPTION) && !defined(USE_OTA_ENCRYPTION_PROVISIONED)" in cond
        and "password" in guard.group(2),
        f"guard {cond!r}",
    )
    lock_fn = lock[lock.find("inline bool lock("):]
    lock_fn = lock_fn[: lock_fn.find("\n}\n") + 2]
    no_pw = lock_fn[lock_fn.find("#else"): lock_fn.find("#endif", lock_fn.find("#else"))] if "#else" in lock_fn else ""
    rep.check(
        f"lock: {LOCK} without a password check says updates need the OTA encryption key, not that a password locks them",
        "Updates need the OTA encryption key" in no_pw and "Updates locked to the API key" not in no_pw,
        f"#else branch {no_pw.strip()[:200]!r}",
    )
    glue = (ROOT / GLUE).read_text(encoding="utf-8") if (ROOT / GLUE).is_file() else ""
    erase = glue[glue.find("inline void erase_all()"):]
    erase = erase[: erase.find("\n}\n") + 2]
    names = re.search(r"ERASE_NAMESPACES\[\]\s*=\s*\{([^}]*)\}", glue)
    listed = set()
    if names:
        for tok in (t.strip() for t in names.group(1).split(",")):
            if tok.startswith('"'):
                listed.add(tok.strip('"'))
            else:
                m = re.search(rf"\b{re.escape(tok)}\s*=\s*\"([^\"]+)\"", glue)
                listed.add(m.group(1) if m else f"?{tok}")
    rep.check(f"erase: {GLUE} erases the namespaces {sorted(ERASE_NAMESPACES)}", listed == ERASE_NAMESPACES, f"got {sorted(listed)}")
    rep.check(
        f"erase: {GLUE} erase_all wipes the whole NVS partition once (global_preferences->reset()), then restarts without saving",
        "global_preferences->reset()" in erase
        and "nvs_flash_erase()" not in erase  # reset() already erased it; a second erase only costs time
        and "esp_restart()" in erase
        and erase.find("global_preferences->reset()") < erase.find("esp_restart()")
        and "safe_reboot" not in erase.replace("No App.safe_reboot()", ""),
    )
    # erase_all blocks the main loop: up to 1 s waiting for the flash writer,
    # two namespace erases and a partition erase, against a 5 s watchdog.
    wait_loop = re.search(r"for \(int i = 0; i < \d+ && cache::busy\(\); i\+\+\) \{(.*?)\n  \}", erase, re.S)
    ns_loop = re.search(r"for \(const char \*ns : ERASE_NAMESPACES\) \{(.*?)\n  \}", erase, re.S)
    reset_at = erase.find("global_preferences->reset()")
    before_reset = erase[: erase.rfind("\n", 0, reset_at)].rstrip().splitlines()[-1:] if reset_at >= 0 else []
    feed = "esphome::App.feed_wdt();"
    rep.check(
        f"erase: {GLUE} erase_all feeds the watchdog while it waits, before each namespace erase and before the partition erase",
        bool(wait_loop and feed in wait_loop.group(1) and ns_loop and feed in ns_loop.group(1) and before_reset and feed in before_reset[0]),
        f"wait loop {bool(wait_loop)}, namespace loop {bool(ns_loop)}, line before reset() {before_reset}",
    )
    rep.check(f"erase: {GLUE} hands uptime_ms and erase_all to the engine", "platform.uptime_ms = detail::uptime_ms;" in glue and "platform.erase_all = detail::erase_all;" in glue)
    # The erase mark (design 10.11): after the partition erase, one byte that
    # makes the waiting screen send the owner to Repairs, never to add a
    # device by the tablet's name. Written after reset() (or it is erased with
    # the rest) and before the restart; nothing secret, cleared once a key comes.
    mark_write = _function(glue, "inline bool erased_mark_write(")
    mark_clear = _function(glue, "inline void erased_clear(")
    write_at = erase.find("erased_mark_write()")
    rep.check(
        f"erase: {GLUE} erase_all leaves the erase mark (one byte, \"tab1\" \"erased\") after reset() and before the restart, "
        "and hands it and erased_clear to the engine",
        reset_at >= 0
        and reset_at < write_at < erase.find("esp_restart()")
        and 'KEY_ERASED = "erased";' in glue
        and "nvs_flash_init()" in mark_write
        and "nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h)" in mark_write
        and "nvs_set_u8(h, KEY_ERASED, 1)" in mark_write
        and "nvs_set_blob" not in mark_write
        and "nvs_set_str" not in mark_write
        and "nvs_erase_key(h, KEY_ERASED)" in mark_clear
        and "platform.erased = detail::erased_mark();" in glue
        and "platform.erased_clear = detail::erased_clear;" in glue,
        f"reset() at {reset_at}, mark at {write_at}, restart at {erase.find('esp_restart()')}",
    )
    # The no-Wi-Fi note after a closed window on a tablet with a key goes when
    # the network is back; the engine learns that through network_up.
    net = glue[glue.find("inline bool network_up()"):]
    net = net[: net.find("\n}\n") + 2]
    rep.check(
        f"setup: {GLUE} hands network_up (esphome::network::is_connected) to the engine",
        "platform.network_up = detail::network_up;" in glue and "return esphome::network::is_connected();" in net,
    )
    # The waiting screen names the hotspot to join; it reads whether it is on
    # and its name from the Wi-Fi component (design 10.9).
    hot = glue[glue.find("inline bool hotspot_on()"):]
    hot = hot[: hot.find("\n}\n") + 2]
    hot_name = glue[glue.find("inline const char *hotspot_name()"):]
    hot_name = hot_name[: hot_name.find("\n}\n") + 2]
    rep.check(
        f"setup: {GLUE} hands hotspot_on (is_ap_active) and the hotspot's name (get_ap().get_ssid()) to the engine",
        "platform.hotspot_on = detail::hotspot_on;" in glue
        and "platform.hotspot = detail::hotspot_name();" in glue
        and "is_ap_active()" in hot
        and "get_ssid()" in hot_name,
    )


def check_hotspot_name(prod, bench, rep):
    """The host tests' hotspot name is the one both entry files set."""
    fake = ROOT / "engine_tests" / "host" / "fake_platform.h"
    text = fake.read_text(encoding="utf-8") if fake.is_file() else ""
    m = re.search(r'const char \*hotspot_name = "([^"]*)";', text)
    ssids = {name: ((cfg.get("wifi") or {}).get("ap") or {}).get("ssid") for name, cfg in ((PROD, prod), (BENCH, bench))}
    rep.check(
        "setup: the host tests show the hotspot name both entry files set (wifi: ap: ssid:)",
        m is not None and all(s == m.group(1) for s in ssids.values()),
        f"fake {m.group(1) if m else None!r}, entry files {ssids}",
    )


def c_string(literal):
    """The text of a C string literal's body (UTF-8 written as \\xNN)."""
    raw = re.sub(r'\\(x[0-9A-Fa-f]{2}|["\\n])', lambda m: {'"': '"', "\\": "\\", "n": "\n"}.get(m.group(1)) or chr(int(m.group(1)[1:], 16)), literal)
    return raw.encode("latin-1").decode("utf-8")


def c_strings(text):
    """The text of one or more adjacent C string literals ("a" "b")."""
    return "".join(c_string(b) for b in re.findall(r'"((?:[^"\\]|\\.)*)"', text))


# The literals of a HintText: two or three, the third may be nullptr.
_LITS = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
_HINT_RE = re.compile(rf"static const HintText (\w+) = \{{\s*{_LITS},\s*{_LITS}(?:,\s*(nullptr|{_LITS}))?\s*\}};")
# enum Hint (tab_state.h) as screens.json names it (CONTRACT.md 11).
SCREEN_STATES = {
    "HINT_HOTSPOT": "hotspot", "HINT_CONNECTING": "connecting", "HINT_ADD": "add", "HINT_ERASED": "erased",
    "HINT_STARTING": "starting", "HINT_TURN_ON": "turn_on", "HINT_WAIT_HA": "wait_ha", "HINT_LOADING": "loading",
    "HINT_PICK": "pick", "HINT_CLOSED": "closed", "HINT_NO_WIFI": "no_wifi",
}
# The tablet's name in docs/engine.md quotes of the erased step: the entry
# files' friendly_name and the last six characters of an example MAC.
DOCS_MAC_SUFFIX = "12ee0e"


def hint_texts(state_h):
    """engine/tab_state.h's waiting-screen words as written: {HINT_x:
    (headline, step, second line or None)}, and its string constants
    (WAIT_*, NOTE_*)."""
    hints = {}
    for m in _HINT_RE.finditer(state_h):
        name, title, step, third = m.group(1), m.group(2), m.group(3), m.group(4)
        hints[name] = (c_strings(title), c_strings(step), None if third in (None, "nullptr") else c_strings(third))
    consts = {}
    for m in re.finditer(rf"static const char \*const (\w+) =\s*{_LITS};", state_h):
        consts[m.group(1)] = c_strings(m.group(2))
    return hints, consts


def screen_templates(hints, consts):
    """The waiting screens and Wi-Fi notes as tab_state.h builds them, with
    {hotspot} and {name} where the platform puts the hotspot's and the
    tablet's name: {state: {headline, step, [step2, step_long_name]}},
    {note: text}."""
    before, after = consts.get("WAIT_HOTSPOT_BEFORE"), consts.get("WAIT_HOTSPOT_AFTER")
    off_on, prefix = consts.get("WAIT_OFF_ON_BEFORE"), consts.get("NOTE_PREFIX")
    e_before, e_after = consts.get("WAIT_ERASED_BEFORE"), consts.get("WAIT_ERASED_AFTER")
    waiting = {}
    for name, (title, step, more) in hints.items():
        state = SCREEN_STATES.get(name, name)
        entry = {"headline": title, "step": step}
        if name == "HINT_HOTSPOT" and before and after:
            entry["step"] = before + "{hotspot}" + after
        elif name == "HINT_NO_WIFI" and off_on and after:
            entry["step"] = off_on + "{hotspot}" + after
        elif name == "HINT_ERASED" and e_before and e_after:
            entry["step"] = e_before + "{name}" + e_after
            entry["step_long_name"] = step
        if more:
            entry["step2"] = more
        waiting[state] = entry
    notes = {}
    if prefix and before and after and off_on:
        notes = {"hotspot": prefix + before + "{hotspot}" + after, "off_on": prefix + off_on + "{hotspot}" + after}
    if consts.get("NOTE_CONNECTING"):
        notes["connecting"] = consts["NOTE_CONNECTING"]
    return waiting, notes


def fill_screen(text, ssid, name):
    return text.replace("{hotspot}", ssid or "{hotspot}").replace("{name}", name or "{name}")


def waiting_texts(state_h, ssid, name=None):
    """(pairs, notes, alternates) for the docs: the waiting screen's
    (headline, step) or (headline, step, second line) per state from
    engine/tab_state.h, with the entry files' hotspot name and, in the erased
    step, the tablet's name; the Wi-Fi notes over a dashboard; and the
    (headline, step) a quote may use instead (the erased step for a name too
    long for its line)."""
    waiting, notes = screen_templates(*hint_texts(state_h))
    pairs, alternates = {}, []
    for state, e in waiting.items():
        pair = (e["headline"], fill_screen(e["step"], ssid, name))
        pairs[state] = pair + ((e["step2"],) if e.get("step2") else ())
        if e.get("step_long_name"):
            alternates.append((e["headline"], e["step_long_name"]))
    return pairs, {fill_screen(n, ssid, name) for n in notes.values()}, alternates


def contract_md_screens(text):
    """CONTRACT.md section 11's table: {state: (headline, step)}."""
    sec = re.search(r"^## 11\. What the tablet's screen says\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    rows = {}
    for line in (sec.group(1) if sec else "").splitlines():
        m = re.fullmatch(r"\| `(\w+)` \| (.+?) \| (.+?) \|", line.strip())
        if m:
            rows[m.group(1)] = (m.group(2), m.group(3))
    return rows


def screens_problems(state_h, screens, ssids, contract_md):
    """Where engine/tab_state.h and the contract's screens.json (and the
    table in CONTRACT.md section 11) do not hold the same words: each state's
    headline, step (with {hotspot} or {name} where the tablet puts them),
    second line and step without the name, each Wi-Fi note, the hotspot's
    name (both entry files' `wifi: ap: ssid:`), and no state or note on one
    side only."""
    problems = []
    hints, consts = hint_texts(state_h)
    waiting, notes = screen_templates(hints, consts)
    if len(hints) != WAITING_STATES:
        problems.append(f"read {len(hints)} of {WAITING_STATES} HintText in tab_state.h")
    if not isinstance(screens, dict):
        return problems + ["screens.json is not a JSON object"]
    if any(s != screens.get("hotspot") for s in ssids.values()):
        problems.append(f"hotspot {screens.get('hotspot')!r}, entry files {ssids}")
    got = screens.get("waiting") if isinstance(screens.get("waiting"), dict) else {}
    for state in sorted(set(waiting) | set(got)):
        want, have = waiting.get(state), got.get(state)
        if want is None or not isinstance(have, dict):
            problems.append(f"waiting state {state!r} only in {'screens.json' if want is None else 'tab_state.h'}")
            continue
        for key in ("headline", "step", "step2", "step_long_name"):
            if want.get(key) != have.get(key):
                problems.append(f"{state}.{key}: tab_state.h {want.get(key)!r}, screens.json {have.get(key)!r}")
        if not have.get("when"):
            problems.append(f"{state}: no `when`")
    got_notes = screens.get("notes") if isinstance(screens.get("notes"), dict) else {}
    for note in sorted(set(notes) | set(got_notes)):
        have = got_notes.get(note)
        text = have.get("text") if isinstance(have, dict) else None
        if notes.get(note) != text:
            problems.append(f"note {note}: tab_state.h {notes.get(note)!r}, screens.json {text!r}")
    rows = contract_md_screens(contract_md)
    for state, e in got.items():
        if not isinstance(e, dict):
            continue
        step = e.get("step", "") + (f"; second line: {e['step2']}" if e.get("step2") else "")
        if rows.get(state) != (e.get("headline"), step):
            problems.append(f"CONTRACT.md section 11, {state}: {rows.get(state)!r}, screens.json {(e.get('headline'), step)!r}")
    problems += [f"CONTRACT.md section 11 names {s!r}, screens.json does not" for s in rows if s not in got]
    return problems


def check_screens_contract(prod, bench, contract_dir, rep):
    """The tablet's words are the contract's (CONTRACT.md 11, round 11)."""
    state_h = (ROOT / "engine" / "tab_state.h").read_text(encoding="utf-8")
    path, md = Path(contract_dir) / "screens.json", Path(contract_dir) / "CONTRACT.md"
    try:
        screens = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as err:
        screens = None
        problems = [f"cannot read {path}: {err}"]
    else:
        ssids = {name: ((cfg.get("wifi") or {}).get("ap") or {}).get("ssid") for name, cfg in ((PROD, prod), (BENCH, bench))}
        problems = screens_problems(state_h, screens, ssids, md.read_text(encoding="utf-8") if md.is_file() else "")
    # Self-test: one changed word on either side, a state or note missing, a
    # stale CONTRACT.md row, each fails.
    wrong = []
    if isinstance(screens, dict) and not problems:
        good_md = md.read_text(encoding="utf-8")
        ssids = {PROD: screens.get("hotspot"), BENCH: screens.get("hotspot")}
        def broken(edit, text=state_h, contract_md=good_md):
            s = copy.deepcopy(screens)
            edit(s)
            return bool(screens_problems(text, s, ssids, contract_md))
        cases = {
            "a headline": lambda s: s["waiting"]["erased"].update(headline="Add this tablet in Home Assistant"),
            "a step": lambda s: s["waiting"]["add"].update(step=s["waiting"]["add"]["step"] + " now"),
            "the second line": lambda s: s["waiting"]["erased"].pop("step2"),
            "a state": lambda s: s["waiting"].pop("pick"),
            "a note": lambda s: s["notes"]["connecting"].update(text="No Wi-Fi"),
            "the hotspot": lambda s: s.update(hotspot="Other"),
        }
        wrong = [k for k, edit in cases.items() if not broken(edit)]
        # the firmware's erased step without "only" (screens.json unchanged)
        no_only = re.sub(r"(WAIT_ERASED_BEFORE =\s*\"[^\"]*)confirm only ", r"\1confirm ", state_h, count=1)
        if no_only == state_h or not broken(lambda s: None, text=no_only):
            wrong.append("the firmware's erased step")
        if not broken(lambda s: None, contract_md=good_md.replace("| `pick` | Nothing to show yet |", "| `pick` | Nothing yet |")):
            wrong.append("a CONTRACT.md row")
    rep.check(
        "contract: engine/tab_state.h's waiting screens and Wi-Fi notes are the contract's screens.json word for word (headline, step with {hotspot} or {name}, second line, step without the name), and so is CONTRACT.md section 11 and the hotspot name of both entry files",
        not problems,
        "; ".join(problems[:6]) + (f" (+{len(problems) - 6} more)" if len(problems) > 6 else ""),
    )
    rep.check(
        "self-test: the contract check fails a changed headline, step, second line, note or hotspot name in screens.json, a state missing there, the firmware's old erased step, or a stale CONTRACT.md row (8 cases)",
        isinstance(screens, dict) and not problems and not wrong,
        f"not caught: {wrong}" if wrong else ("skipped: the contract check fails" if problems else ""),
    )


# ---------------------------------------------------------------------------
# The contract is one folder in two repositories (cards spec B.9, G.2 E11):
# this one and the integration's tests/contract in TAB-Dashboard. Both read
# its fixtures as the law, so the two must be the same file for file. The
# rule is the integration's own comparison (tests/frontend/
# tablet-screens.test.mjs): every file under the folder, except its
# top-level ruff.toml (the integration's lint settings) and __pycache__.


def contract_files(folder):
    """The files of a contract folder that must be the same in both
    repositories, as relative POSIX paths."""
    folder = Path(folder)
    out = []
    for path in folder.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(folder)
        if rel.as_posix() == "ruff.toml" or "__pycache__" in rel.parts:
            continue
        out.append(rel.as_posix())
    return sorted(out)


def peer_contract_dir(peer):
    """The contract folder in what --peer-contract names: the folder itself,
    or a TAB-Dashboard checkout (its tests/contract)."""
    peer = Path(peer)
    if (peer / "contract.json").is_file():
        return peer
    return peer / "tests" / "contract"


def contract_peer_problems(ours, theirs):
    """Where two contract folders differ: a file only in one, or other bytes."""
    ours, theirs = Path(ours), Path(theirs)
    if not (theirs / "contract.json").is_file():
        return [f"no contract.json in {theirs}"]
    mine, other = contract_files(ours), contract_files(theirs)
    problems = [f"only here: {f}" for f in sorted(set(mine) - set(other))]
    problems += [f"only in {theirs}: {f}" for f in sorted(set(other) - set(mine))]
    for f in sorted(set(mine) & set(other)):
        if (ours / f).read_bytes() != (theirs / f).read_bytes():
            problems.append(f"differs: {f}")
    return problems


def check_contract_peer(contract_dir, peer, rep):
    """With --peer-contract, this contract folder against the integration's.
    Always: a self-test of the comparison on copies of this folder."""
    if peer is not None:
        theirs = peer_contract_dir(peer)
        problems = contract_peer_problems(contract_dir, theirs)
        rep.check(
            f"contract: {contract_dir} is the same as the integration's copy in {theirs}, file for file (ruff.toml and __pycache__ apart)",
            not problems,
            "; ".join(problems[:8]) + (f" (+{len(problems) - 8} more)" if len(problems) > 8 else ""),
        )
    wrong = []
    with tempfile.TemporaryDirectory(prefix="tab1-contract-self-test-") as tmp:
        base = Path(tmp) / "theirs"
        shutil.copytree(contract_dir, base, ignore=shutil.ignore_patterns("__pycache__"))
        same = not contract_peer_problems(contract_dir, base)
        fixture = sorted((base / "fixtures").glob("*.json"))[0]
        edits = {
            "a byte of a fixture": lambda d: (d / "fixtures" / fixture.name).write_bytes((d / "fixtures" / fixture.name).read_bytes() + b"\n"),
            "CONTRACT.md gone": lambda d: (d / "CONTRACT.md").unlink(),
            "an extra fixture": lambda d: (d / "fixtures" / "extra.json").write_text("{}", encoding="utf-8"),
            "a reference file changed": lambda d: (d / "reference" / "tabproto.py").write_text("", encoding="utf-8"),
            "no contract.json": lambda d: (d / "contract.json").unlink(),
        }
        for name, edit in edits.items():
            case = Path(tmp) / f"case{len(wrong)}-{abs(hash(name))}"
            shutil.copytree(base, case)
            edit(case)
            if not contract_peer_problems(contract_dir, case):
                wrong.append(name)
        # What the integration's copy may hold besides: its lint settings and caches.
        extra = Path(tmp) / "extra"
        shutil.copytree(base, extra)
        (extra / "ruff.toml").write_text("line-length = 120\n", encoding="utf-8")
        (extra / "reference" / "__pycache__").mkdir()
        (extra / "reference" / "__pycache__" / "tabproto.cpython-314.pyc").write_bytes(b"x")
        if contract_peer_problems(contract_dir, extra):
            wrong.append("ruff.toml or __pycache__ counted")
        # A TAB-Dashboard checkout is found by its tests/contract.
        repo = Path(tmp) / "repo"
        shutil.copytree(base, repo / "tests" / "contract")
        if peer_contract_dir(repo) != repo / "tests" / "contract" or contract_peer_problems(contract_dir, peer_contract_dir(repo)):
            wrong.append("a checkout's tests/contract")
    rep.check(
        f"self-test: the contract comparison passes an identical copy, also with ruff.toml, __pycache__ or as a checkout's tests/contract, and fails a changed fixture, a missing or extra file, a changed reference file and a folder without contract.json ({len(edits) + 3} cases)",
        same and not wrong,
        "an identical copy fails" if not same else (f"not caught: {wrong}" if wrong else ""),
    )


# Waiting-screen words the firmware no longer shows (rounds 9 to 11 changed
# them). A quote cut short (the start of a waiting text) fails on its own.
RETIRED_WAITING_TEXTS = (
    "pick a dashboard for this tablet",
    "from a phone to set up Wi-Fi",
    "On Wi-Fi, not turned on yet",
    "to give this tablet a dashboard",
    "Turn the tablet off and on to set up Wi-Fi",
    "in Settings > System > Repairs and confirm it",
)
# The waiting screen's states (enum Hint) and the Wi-Fi notes over a dashboard.
WAITING_STATES, WIFI_NOTES = 11, 3
QUOTE_RE = re.compile(r'"([^"\n]+)"|“([^”\n]+)”')


def doc_blocks(text):
    """docs text as blocks: each table row, list item or paragraph, with its
    lines joined (a quote may wrap)."""
    blocks, cur = [], []
    for line in text.split("\n") + [""]:
        s = line.strip()
        starts = s.startswith(("|", "- ", "* ", "#", "```")) or re.match(r"\d+\. ", s)
        if not s or starts:
            if cur:
                blocks.append(" ".join(cur))
            cur = []
            if s.startswith("|") or s.startswith("#"):
                blocks.append(s)
                continue
        if s:
            cur.append(s)
    return [re.sub(r"\s+", " ", b) for b in blocks]


def docs_line_problems(doc):
    """Lines of docs/engine.md that Markdown would read as a block quote: a
    line that starts with ">" (also a wrapped line of a list item or
    paragraph, where it splits the text), outside code blocks. The file has
    no block quotes on purpose."""
    problems, fence = [], False
    for n, line in enumerate(doc.split("\n"), 1):
        s = line.strip()
        if s.startswith("```"):
            fence = not fence
            continue
        if not fence and s.startswith(">"):
            problems.append(f"line {n} starts with '>' (Markdown makes it a block quote): {s[:60]!r}")
    return problems


def docs_waiting_problems(doc, pairs, notes, alternates=()):
    """Where docs/engine.md does not quote the waiting screen exactly: a
    headline without its step (and its second line, for the state that has
    one) or a step without its headline (in one table row, list item or
    paragraph; a table cell may hold two lines split by <br>), a quote that
    holds a waiting text but is not one, or is the start of one, a retired
    wording, a state missing from the table or from the hardware checklist,
    or a Wi-Fi note never quoted whole. pairs: (headline, step) or
    (headline, step, second line) per state; alternates: (headline, step)
    that may stand for that headline's step."""
    problems, seen_row, seen_item = [], set(), set()
    triples = [tuple(v) + (None,) * (3 - len(v)) for v in pairs.values()]
    by_title = {t: s for t, s, _ in triples}
    second = {t: m for t, _, m in triples if m}
    by_step = {s: t for t, s, _ in triples}
    by_step.update({m: t for t, _, m in triples if m})
    by_step.update({s: t for t, s in alternates})
    texts = set(by_title) | set(by_step)
    checklist = re.search(r"^## Hardware checklist\n(.*?)(?=^## |\Z)", doc, re.S | re.M)
    checklist_items = {b for b in doc_blocks(checklist.group(1)) if b.startswith(("- ", "* "))} if checklist else set()
    for block in doc_blocks(doc):
        seen = seen_row if block.startswith("|") else seen_item if block in checklist_items else set()
        if block.startswith("|"):
            quotes = [q.strip() for c in block.strip("|").split("|") for q in c.split("<br>")]
            # a table cell may add a note in brackets after the step
            quotes = [next((s for s in by_step if q.startswith(s + " (")), q) for q in quotes]
        else:
            quotes = [a or b for a, b in QUOTE_RE.findall(block)]
        quotes = [q for q in quotes if q not in notes]
        for q in quotes:
            if q in by_title:
                has_step = by_title[q] in quotes or any(t == q and s in quotes for t, s in alternates)
                if not has_step:
                    problems.append(f"headline {q!r} without its step {by_title[q]!r}: {block[:90]!r}")
                if q in second and second[q] not in quotes:
                    problems.append(f"headline {q!r} without its second line {second[q]!r}: {block[:90]!r}")
                if has_step and (q not in second or second[q] in quotes):
                    seen.add(q)
            if q in by_step and by_step[q] not in quotes:
                problems.append(f"step {q!r} without its headline {by_step[q]!r}: {block[:90]!r}")
            if q not in texts and any(t in q for t in texts if len(t) > 8):
                problems.append(f"not the exact words: {q!r}")
            elif q not in texts and len(q) > 8 and any(t.startswith(q) for t in texts):
                problems.append(f"cut short: {q!r}")
    flat = re.sub(r"\s+", " ", doc)
    problems += [f"retired wording {r!r}" for r in RETIRED_WAITING_TEXTS if r in flat]
    problems += [f"Wi-Fi note not quoted whole: {n!r}" for n in sorted(notes) if n not in flat]
    # The table in "Setting up a new tablet" and the hardware checklist (a
    # list) each name every state.
    problems += [f"not in a table row with its step: {t!r}" for t in by_title if t not in seen_row]
    problems += [f"not in the hardware checklist with its step: {t!r}" for t in by_title if t not in seen_item]
    return problems


# (what, docs text, passes)
DOCS_QUOTE_SELF_TESTS = (
    ("a table row with both", "| On Wi-Fi | Ready to turn on | Open TAB-1 in Home Assistant and turn this tablet on |\n", True),
    ("a paragraph with both, wrapped", 'it says "Ready to turn on" and "Open TAB-1 in Home Assistant\nand turn this tablet on".\n', True),
    ("the step alone", 'it says "Open TAB-1 in Home Assistant and turn this tablet on".\n', False),
    ("the headline alone", '"Ready to turn on" shows.\n', False),
    ("an old step", '"Ready to turn on" and "Open TAB-1 in Home Assistant and turn this tablet on now".\n', False),
    ("a step cut short", '"Ready to turn on" and "Open TAB-1 in Home Assistant and turn".\n', False),
    ("all three lines", '"Confirm it" with "Go to Repairs" and "Or add it".\n', True),
    ("a table row with both lines", "| Erased | Confirm it | Go to Repairs<br>Or add it |\n", True),
    ("without the second line", '"Confirm it" with "Go to Repairs".\n', False),
    ("the second line alone", 'it says "Or add it".\n', False),
    ("the step without the name", '"Confirm it" with "Go to Repairs for the name above" and "Or add it".\n', True),
)
# (what, docs text, passes)
DOCS_LINE_SELF_TESTS = (
    ("a wrapped list item", "- it says \"Find it in Settings > System\n  > Repairs\"\n", False),
    ("a block quote", "> a note\n", False),
    ("'>' inside a line", "- Settings > System > Repairs\n", True),
    ("'>' in a code block", "```\n> prompt\n```\n", True),
)


def check_docs_waiting(prod, rep):
    """docs/engine.md quotes every waiting screen exactly (T3, round 9)."""
    state_h = (ROOT / "engine" / "tab_state.h").read_text(encoding="utf-8")
    ssid = ((prod.get("wifi") or {}).get("ap") or {}).get("ssid")
    friendly = str(((prod.get("substitutions") or {}).get("friendly_name")) or "")
    pairs, notes, alternates = waiting_texts(state_h, ssid, f"{friendly} {DOCS_MAC_SUFFIX}" if friendly else None)
    sample = {
        "HINT_TURN_ON": ("Ready to turn on", "Open TAB-1 in Home Assistant and turn this tablet on"),
        "HINT_ERASED": ("Confirm it", "Go to Repairs", "Or add it"),
    }
    sample_alt = [("Confirm it", "Go to Repairs for the name above")]
    coverage = ("not in a table row", "not in the hardware checklist")  # a one-line sample has neither
    wrong = [w for w, doc, passes in DOCS_QUOTE_SELF_TESTS if (not [p for p in docs_waiting_problems(doc, sample, set(), sample_alt) if not p.startswith(coverage)]) != passes]
    wrong += [w for w, doc, passes in DOCS_LINE_SELF_TESTS if (not docs_line_problems(doc)) != passes]
    rep.check(
        f"self-test: the docs check fails a headline, step or second line quoted alone, cut short, words that are not the tablet's, or a line Markdown reads as a block quote ({len(DOCS_QUOTE_SELF_TESTS) + len(DOCS_LINE_SELF_TESTS)} cases)",
        not wrong,
        f"wrong: {wrong}" if wrong else "",
    )
    doc = (ROOT / "docs" / "engine.md").read_text(encoding="utf-8")
    problems = (
        docs_waiting_problems(doc, pairs, notes, alternates)
        if len(pairs) == WAITING_STATES and len(notes) == WIFI_NOTES and friendly
        else [f"read {len(pairs)} of {WAITING_STATES} waiting texts and {len(notes)} of {WIFI_NOTES} Wi-Fi notes from tab_state.h, friendly_name {friendly!r}"]
    )
    rep.check(
        f"docs: docs/engine.md quotes each of the {len(pairs)} waiting screens with its exact headline and step (the erased one with its second line, and the tablet's name as \"{friendly} {DOCS_MAC_SUFFIX}\"), in its table and its hardware checklist, never one alone, and nothing else as one; and each of the {len(notes)} Wi-Fi notes whole",
        not problems,
        "; ".join(problems[:6]) + (f" (+{len(problems) - 6} more)" if len(problems) > 6 else ""),
    )
    lines = docs_line_problems(doc)
    rep.check(
        "docs: no line of docs/engine.md starts with '>' outside code blocks (Markdown would split the text into a block quote)",
        not lines,
        "; ".join(lines[:4]),
    )
    glue = live_cpp((ROOT / GLUE).read_text(encoding="utf-8"))
    body = _function(glue, "inline bool ha_connected()")
    rep.check(
        "glue: the waiting screen's \"Home Assistant connected\" is a connection that follows the states (is_connected_with_state_subscription), not Home Assistant's look while it discovers",
        bool(body) and "is_connected_with_state_subscription()" in body and "is_connected()" not in body.replace("is_connected_with_state_subscription()", "")
        and "platform.ha_connected = detail::ha_connected;" in glue,
        body.strip()[:200] if body else "no ha_connected() in engine_glue.h",
    )


def check_release(prod, bench, rep):
    """The release workflow that publishes TAB-1-engine.yaml (design 10.8)."""
    gh = ROOT.parent.parent / ".github"
    build_path, ci_path, drafter_path = gh / "workflows" / "build.yml", gh / "workflows" / "ci.yml", gh / "release-drafter.yml"
    build = load_yaml(build_path) if build_path.is_file() else {}
    job = ((build.get("jobs") or {}).get("build-and-publish") or {}) if isinstance(build, dict) else {}
    uses = str(job.get("uses", ""))
    args = job.get("with") or {}
    rep.check(
        "release: .github/workflows/build.yml publishes TAB-1-engine.yaml to Pages through the shared workflow, pinned to a commit",
        re.fullmatch(r"ApolloAutomation/Workflows/\.github/workflows/build\.yml@[0-9a-f]{40}", uses) is not None
        and str(args.get("yaml-files", "")).split() == [f"Integrations/ESPHome/{PROD}"]
        and args.get("firmware-names") == "engine:firmware"
        # The release tag comes from this file's version; it must be the
        # version the manifest and the tablet carry.
        and args.get("core-yaml-path") == f"Integrations/ESPHome/{PROD}"
        and args.get("publish-to-pages") is True,
        f"uses {uses!r}, with {args!r}",
    )
    # The shared workflow runs release-drafter, which stops without this file:
    # no release, so firmware/manifest.json is never published.
    drafter = load_yaml(drafter_path) if drafter_path.is_file() else None
    rep.check(
        "release: .github/release-drafter.yml is there (the shared workflow's release-drafter needs it)",
        isinstance(drafter, dict) and "template" in drafter and "tag-template" in drafter,
        f"{drafter_path} {'unreadable' if drafter_path.is_file() else 'missing'}" if not isinstance(drafter, dict) else "",
    )
    # The lock relies on ESPHome internals checked against one version: the
    # release builds with exactly that one, and min_version says the same.
    pinned = str(args.get("esphome-version", ""))
    mins = {name: str((cfg.get("esphome") or {}).get("min_version", "")) for name, cfg in ((PROD, prod), (BENCH, bench))}
    rep.check(
        "release: build.yml builds with a pinned ESPHome version (not stable, beta or dev), equal to min_version in both engine files",
        re.fullmatch(r"\d{4}\.\d{1,2}\.\d+", pinned) is not None and all(v == pinned for v in mins.values()),
        f"esphome-version {pinned!r}, min_version {mins}",
    )
    # ci.yml calls the shared ESPHome CI once per job: TAB-1.yaml on its
    # default versions, TAB-1-engine.yaml on the pinned version only (the
    # lock is checked against that one), and a job runs this script's static
    # mode so a changed engine file needs a higher files number.
    ci = load_yaml(ci_path) if ci_path.is_file() else {}
    ci = ci if isinstance(ci, dict) else {}
    # PyYAML reads the key `on` as True.
    triggers = ci.get(True, ci.get("on"))
    on_prs = "pull_request" in (triggers if isinstance(triggers, (dict, list)) else [triggers])
    jobs = ci.get("jobs") if isinstance(ci.get("jobs"), dict) else {}
    builds = {}  # file -> one version list per job that builds it (None: the shared default)
    static_run = False
    switched_off = []  # jobs or steps that could be skipped or allowed to fail
    for job_id, job in jobs.items():
        if not isinstance(job, dict):
            continue
        job_off = "if" in job or "continue-on-error" in job
        if re.fullmatch(r"ApolloAutomation/Workflows/\.github/workflows/esphome-ci\.yml@[0-9a-f]{40}", str(job.get("uses", ""))):
            with_ = job.get("with") if isinstance(job.get("with"), dict) else {}
            versions = str(with_.get("esphome-versions", "")).split() or None
            for name in str(with_.get("yaml-files", "")).split():
                builds.setdefault(name, []).append(versions)
                if job_off:
                    switched_off.append(job_id)
        for step in job.get("steps") or []:
            if (
                isinstance(step, dict)
                and str(step.get("working-directory", "")) == "Integrations/ESPHome"
                and str(step.get("run", "")).strip() == "python3 -B tools/check_engine_package.py"
            ):
                static_run = True
                if job_off or "if" in step or "continue-on-error" in step:
                    switched_off.append(f"{job_id} (static step)")
    rep.check(
        "release: ci.yml runs on pull requests, builds TAB-1-engine.yaml in one job on the pinned ESPHome version only, builds TAB-1.yaml on the shared default versions, and runs this script's static mode, none of them skippable",
        on_prs
        and builds.get(f"Integrations/ESPHome/{PROD}") == [[pinned]]
        and builds.get(f"Integrations/ESPHome/{TAB1}") == [None]
        and static_run
        and not switched_off,
        f"pull_request trigger: {on_prs}, builds {builds!r}, static mode run: {static_run}, skippable: {switched_off}",
    )


def check_package(engine, rep):
    esp = engine.get("esphome") or {}
    rep.check(f"package: {ENGINE} includes exactly {INCLUDES}", esp.get("includes") == INCLUDES, f"got {esp.get('includes')}")
    lv = engine.get("lvgl") or {}
    rep.check(
        f"package: lvgl rotation is ${{{ROTATION_SUB}}} (the entry file's substitution) and buffer_size 12%",
        lv.get("rotation") == f"${{{ROTATION_SUB}}}" and lv.get("buffer_size") == "12%",
        f"got rotation {lv.get('rotation')!r}, buffer_size {lv.get('buffer_size')!r}",
    )
    rep.check("package: lvgl draws on dsi_display with touchscreen_id", lv.get("displays") == ["dsi_display"])


def fast_display_problems(text, test_text, run_sh):
    """What keeps fast_display.h from having its fast path for both landscape
    rotations, with the host test of both. An empty list when nothing does."""
    code = live_cpp(text)
    problems = []
    for angle in (ROTATION_DEFAULT, ROTATION_OTHER):
        if not re.search(rf"\bstatic inline void rot{angle}_rgb565\(", code):
            problems.append(f"no kernel rot{angle}_rgb565")
    area = _function(code, "static inline void rotate_area(")
    for angle in (ROTATION_DEFAULT, ROTATION_OTHER):
        if f"rot{angle}_rgb565(" not in area:
            problems.append(f"rotate_area does not use rot{angle}_rgb565")
    fast = _function(code, "static inline bool fast_rotation(")
    for angle in (ROTATION_DEFAULT, ROTATION_OTHER):
        if f"DISPLAY_ROTATION_{angle}_DEGREES" not in fast:
            problems.append(f"fast_rotation() does not take {angle} degrees")
    if re.search(r"DISPLAY_ROTATION_(?:0|180)_DEGREES", fast):
        problems.append("fast_rotation() takes a rotation that has no kernel")
    flush = _function(code, "static void flush_cb(")
    if "if (!fast_rotation(" not in flush or "s_stock(d, area, px);" not in flush:
        problems.append("flush_cb does not leave other rotations to the stock flush")
    if "rotate_area(" not in flush or f"DISPLAY_ROTATION_{ROTATION_DEFAULT}_DEGREES" not in flush:
        problems.append("flush_cb does not rotate through rotate_area for both")
    setup = _function(code, "static bool setup(")
    if "!fast_rotation(comp->get_rotation())" not in setup:
        problems.append("setup() does not ask fast_rotation()")
    if "#ifndef FAST_DISPLAY_HOST_TEST" not in text:
        problems.append("no FAST_DISPLAY_HOST_TEST guard around the ESPHome part")
    if "#define FAST_DISPLAY_HOST_TEST" not in test_text or f'#include "{FAST}"' not in test_text:
        problems.append(f"{FAST_TEST} does not build {FAST} with FAST_DISPLAY_HOST_TEST")
    for name in (f"rot{ROTATION_DEFAULT}_rgb565(", f"rot{ROTATION_OTHER}_rgb565(", "rotate_area("):
        if name not in test_text:
            problems.append(f"{FAST_TEST} does not test {name.rstrip('(')}")
    if "tests/test_fast_display.cpp" not in run_sh or "test_fast_display_o2" not in run_sh or "test_fast_display_asan" not in run_sh:
        problems.append("engine_tests/run.sh does not build and run the screen rotation test twice (optimised, sanitizers)")
    return problems


def flush_test_problems(text, test_text, run_sh, stand_ins):
    """What keeps the host tests from building the whole of fast_display.h
    (not only the rotation) and testing flush_cb, setup and set_enabled.
    `stand_ins` are the files in the stand-in folder, relative to it. An
    empty list when nothing does."""
    problems = []
    if f'#include "{FAST}"' not in test_text:
        problems.append(f"{FLUSH_TEST} does not include {FAST}")
    if re.search(r"^[ \t]*#[ \t]*define[ \t]+FAST_DISPLAY_HOST_TEST\b", test_text, re.M):
        problems.append(f"{FLUSH_TEST} defines FAST_DISPLAY_HOST_TEST, which leaves flush_cb and setup out of the build")
    test_code = live_cpp(test_text)
    for name in ("fast_display::flush_cb", "fast_display::setup(", "fast_display::set_enabled("):
        if name not in test_code:
            problems.append(f"{FLUSH_TEST} does not use {name.rstrip('(')}")
    # Every header fast_display.h includes by name needs its stand-in, or the
    # test stops building when one is added.
    for inc in re.findall(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', text, re.M):
        if inc not in stand_ins:
            problems.append(f"{FAST_SIM} has no stand-in for {inc}")
    script = "\n".join(line for line in run_sh.splitlines() if not line.lstrip().startswith("#"))
    built = "tests/test_flush_cb.cpp" in script and "host/fast_display_sim" in script
    # Each run counts: a failure sets the script's exit status.
    runs = [name for name in ("o2", "asan") if re.search(rf'"\$WORK/test_flush_cb_{name}"[^\n]*\|\| status=1', script)]
    if not built or runs != ["o2", "asan"]:
        problems.append("engine_tests/run.sh does not build and run the screen flush test twice (optimised, sanitizers)")
    return problems


def _read_or_empty(path):
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def check_display(rep):
    text = _read_or_empty(ROOT / FAST)
    rep.check(f"display: {FAST} is there", bool(text))
    problems = fast_display_problems(text, _read_or_empty(ROOT / FAST_TEST), _read_or_empty(ROOT / "engine_tests" / "run.sh"))
    rep.check(
        f"display: {FAST} has the fast path for {ROTATION_DEFAULT} and {ROTATION_OTHER} degrees (a kernel each, one flush, the stock flush for anything else) and engine_tests/run.sh tests both",
        not problems,
        "; ".join(problems),
    )
    # The rule itself, on what it must catch: the fast path for 270 only (as
    # the file was), a flush without the fallback, a kernel gone.
    only_270 = text.replace(f"rotation == esphome::display::DISPLAY_ROTATION_{ROTATION_DEFAULT}_DEGREES ||", "")
    no_fallback = text.replace("s_stock(d, area, px);", "")
    cases = (
        ("270 only", only_270, f"fast_rotation() does not take {ROTATION_DEFAULT} degrees"),
        ("no fallback", no_fallback, "flush_cb does not leave other rotations to the stock flush"),
        ("no 90 kernel", text.replace(f"rot{ROTATION_DEFAULT}_rgb565", "rot_other"), f"no kernel rot{ROTATION_DEFAULT}_rgb565"),
    )
    missed = [name for name, changed, want in cases if changed == text or want not in fast_display_problems(changed, "", "")]
    rep.check(f"self-test: the {FAST} rule catches a fast path for 270 only, a missing fallback and a missing kernel ({len(cases)} cases)", not missed, f"not caught: {missed}")
    # flush_cb and setup() are below the FAST_DISPLAY_HOST_TEST guard, so the
    # rotation test does not build them: a second test builds the whole file.
    flush_test = _read_or_empty(ROOT / FLUSH_TEST)
    run_sh = _read_or_empty(ROOT / "engine_tests" / "run.sh")
    sim = ROOT / FAST_SIM
    stand_ins = {path.relative_to(sim).as_posix() for path in sim.rglob("*.h")} if sim.is_dir() else set()
    problems = flush_test_problems(text, flush_test, run_sh, stand_ins)
    rep.check(
        f"display: engine_tests/run.sh builds the whole of {FAST} against the stand-in headers in {FAST_SIM} and tests flush_cb, setup and set_enabled ({FLUSH_TEST}), optimised and with sanitizers",
        not problems,
        "; ".join(problems),
    )
    # That rule, on what it must catch.
    run_line = '"$WORK/test_flush_cb_asan" "$@" || status=1'
    cases = (
        ("only the rotation is built", text, "#define FAST_DISPLAY_HOST_TEST 1\n" + flush_test, run_sh, stand_ins, "defines FAST_DISPLAY_HOST_TEST"),
        ("setup() is not tested", text, flush_test.replace("fast_display::setup(", "setup("), run_sh, stand_ins, "does not use fast_display::setup"),
        ("flush_cb only in a comment", text, flush_test.replace("fast_display::flush_cb", "flush") + "// fast_display::flush_cb\n", run_sh, stand_ins, "does not use fast_display::flush_cb"),
        ("a header without a stand-in", text + '\n#include "esp_timer.h"\n', flush_test, run_sh, stand_ins, "no stand-in for esp_timer.h"),
        ("no stand-in folder", text, flush_test, run_sh, set(), "no stand-in for lvgl.h"),
        ("the sanitizer run is commented out", text, flush_test, run_sh.replace(run_line, "# " + run_line), stand_ins, "does not build and run the screen flush test twice"),
        ("a failed run does not count", text, flush_test, run_sh.replace(run_line, run_line.replace(" || status=1", " || true")), stand_ins, "does not build and run the screen flush test twice"),
        ("the test is not built", text, flush_test, run_sh.replace("tests/test_flush_cb.cpp", "tests/other.cpp"), stand_ins, "does not build and run the screen flush test twice"),
    )
    missed = [
        name
        for name, fast, test, script, sims, want in cases
        if (fast, test, script, sims) == (text, flush_test, run_sh, stand_ins) or not any(want in problem for problem in flush_test_problems(fast, test, script, sims))
    ]
    rep.check(
        f"self-test: the screen flush test rule catches a test that builds only the rotation or leaves out setup() or flush_cb, a header without a stand-in, and a run.sh that does not build it, run it or count its result ({len(cases)} cases)",
        not missed,
        f"not caught: {missed}",
    )


FROZEN = Path("engine_tests") / "frozen" / "0.1.0"
FROZEN_FILES = tuple(f"engine/{n}" for n in (
    "tab_core.h", "tab_crypto.h", "tab_engine.h", "tab_files.h", "tab_json.h", "tab_model.h", "tab_state.h", "tab_ui.h",
)) + ("host/harness.h", "host/fake_platform.h")


def host_test_problems(run_sh, frozen_present):
    """What engine_tests/run.sh lacks for v1_pixels_unchanged and for doubles
    as the tablet works them out (cards spec B.1, B.9): -ffp-contract=off in
    COMMON (LVGL and every test), the frozen engine 0.1.0 built before host/
    and run optimised and with sanitizers before the engine, each run
    counted, and the engine told where its frames are."""
    problems = []
    script = "\n".join(line for line in run_sh.splitlines() if not line.lstrip().startswith("#"))
    common = re.search(r'^COMMON="([^"]*)"', script, re.M)
    if not common or "-ffp-contract=off" not in common.group(1).split():
        problems.append("COMMON in engine_tests/run.sh lacks -ffp-contract=off")
    built = re.search(r'-I"\$T/frozen/0\.1\.0/host" -I"\$T/frozen/0\.1\.0/engine" \$COMMON "\$T/tests/test_frozen\.cpp"', script)
    if not built:
        problems.append("engine_tests/run.sh does not build tests/test_frozen.cpp against frozen/0.1.0 (its host/ and engine/ before host/)")
    for name in ("o2", "asan"):
        if not re.search(rf'^build_frozen {name} ', script, re.M):
            problems.append(f"engine_tests/run.sh does not build test_frozen_{name}")
        if not re.search(rf'"\$WORK/test_frozen_{name}"[^\n]*"\$WORK/frozen_frames_{name}" \|\| status=1', script):
            problems.append(f"engine_tests/run.sh does not run test_frozen_{name} into frozen_frames_{name}, or its failure does not count")
        run_engine = script.find(f'"$WORK/test_engine_{name}"')
        run_frozen = script.find(f'"$WORK/test_frozen_{name}"')
        if run_engine < 0 or run_frozen < 0 or run_frozen > run_engine:
            problems.append(f"engine_tests/run.sh does not run test_frozen_{name} before test_engine_{name}")
        if not re.search(rf'TAB_FROZEN_FRAMES="\$WORK/frozen_frames_{name}"[^\n]*(\\\n[^\n]*)?"\$WORK/test_engine_{name}"', script):
            problems.append(f"engine_tests/run.sh does not hand frozen_frames_{name} to test_engine_{name} (TAB_FROZEN_FRAMES)")
    missing = [f for f, there in frozen_present.items() if not there]
    if missing:
        problems.append(f"{FROZEN} lacks {missing}")
    return problems


# The engine answers protocol 2 only once the whole of version 2 is drawn
# (cards spec B.8): until then tab_state.h keeps TAB_PROTO_HIGH at 1, no
# shipped YAML raises it, and only the host tests build with 2.
PROTO_DEFAULT_RE = re.compile(r"^#ifndef TAB_PROTO_HIGH\n#define TAB_PROTO_HIGH (\d+)\n#endif$", re.M)
PROTO_FLAG_RE = re.compile(r"-DTAB_PROTO_HIGH(?:=(\S+))?")


def proto_high_problems(state_h, yamls, run_sh):
    problems = []
    live = live_cpp(state_h)
    found = PROTO_DEFAULT_RE.findall(live)
    if found != ["1"]:
        problems.append(f"tab_state.h does not default TAB_PROTO_HIGH to 1 (found {found})")
    elif re.findall(r"#\s*(?:define|undef)\s+TAB_PROTO_HIGH\b", live) != ["#define TAB_PROTO_HIGH"]:
        problems.append("tab_state.h sets TAB_PROTO_HIGH more than once")
    for name, text in sorted(yamls.items()):
        if "TAB_PROTO_HIGH" in text:
            problems.append(f"{name} names TAB_PROTO_HIGH")
    flags = [m for line in run_sh.splitlines() if not line.lstrip().startswith("#") for m in PROTO_FLAG_RE.findall(line)]
    if flags != ["2"]:
        problems.append(f"engine_tests/run.sh does not build the engine tests with -DTAB_PROTO_HIGH=2 exactly once (found {flags})")
    return problems


def check_proto_high(rep):
    state_h = _read_or_empty(ROOT / "engine" / "tab_state.h")
    yamls = {p.name: _read_or_empty(p) for p in sorted(ROOT.glob("*.yaml"))}
    run_sh = _read_or_empty(ROOT / "engine_tests" / "run.sh")
    problems = proto_high_problems(state_h, yamls, run_sh)
    rep.check(
        "protocol: the shipped engine speaks up to protocol 1 (TAB_PROTO_HIGH defaults to 1 in tab_state.h, no YAML raises it), the host tests build with -DTAB_PROTO_HIGH=2",
        not problems,
        "; ".join(problems),
    )
    block = "#ifndef TAB_PROTO_HIGH\n#define TAB_PROTO_HIGH 1\n#endif"
    flag = "-DTAB_PROTO_HIGH=2"
    cases = (
        ("as shipped", state_h, yamls, run_sh, True),
        ("default 2", state_h.replace(block, block.replace("HIGH 1", "HIGH 2")), yamls, run_sh, False),
        ("default commented out", state_h.replace(block, "// " + block.replace("\n", "\n// ")), yamls, run_sh, False),
        ("raised after the default", state_h + "\n#undef TAB_PROTO_HIGH\n#define TAB_PROTO_HIGH 2\n", yamls, run_sh, False),
        ("a YAML build flag", state_h, {**yamls, "TAB-1-engine.yaml": yamls.get("TAB-1-engine.yaml", "") + "\n  build_flags:\n    - -DTAB_PROTO_HIGH=2\n"}, run_sh, False),
        ("no flag in run.sh", state_h, yamls, run_sh.replace(flag, ""), False),
        ("the flag only in a comment", state_h, yamls, run_sh.replace(flag, "") + "\n# " + flag + "\n", False),
        ("the flag set to 1", state_h, yamls, run_sh.replace(flag, "-DTAB_PROTO_HIGH=1"), False),
    )
    wrong = [what for what, h, y, r, passes in cases if (not proto_high_problems(h, y, r)) != passes]
    rep.check(
        f"self-test: the protocol rule passes the shipped files and fails a raised default, a YAML flag or a host build without 2 ({len(cases)} cases)",
        not wrong,
        f"wrong for: {wrong}" if wrong else "",
    )


def check_host_tests(rep):
    run_sh = _read_or_empty(ROOT / "engine_tests" / "run.sh")
    present = {f: (ROOT / FROZEN / f).is_file() for f in FROZEN_FILES}
    problems = host_test_problems(run_sh, present)
    rep.check(
        f"host tests: engine_tests/run.sh builds with -ffp-contract=off, and builds and runs the frozen engine 0.1.0 ({FROZEN}) optimised and with sanitizers before the engine, whose v1_pixels_unchanged compares their frames",
        not problems,
        "; ".join(problems),
    )
    frozen_run = '"$WORK/test_frozen_asan" "$T/contract" "$TEXT_TTF" "$ICON_TTF" "$WORK/frozen_frames_asan" || status=1'
    cases = (
        ("no -ffp-contract=off", run_sh.replace(" -ffp-contract=off", ""), present, "lacks -ffp-contract=off"),
        ("the flag only in a comment", run_sh.replace(" -ffp-contract=off", "") + "\n# -ffp-contract=off\n", present, "lacks -ffp-contract=off"),
        ("frozen built against engine/", run_sh.replace('-I"$T/frozen/0.1.0/engine" ', ""), present, "does not build tests/test_frozen.cpp"),
        ("the sanitizer run does not count", run_sh.replace(frozen_run, frozen_run.replace(" || status=1", " || true")), present, "does not run test_frozen_asan"),
        ("no frames for the engine", run_sh.replace('TAB_FROZEN_FRAMES="$WORK/frozen_frames_o2" ', ""), present, "does not hand frozen_frames_o2"),
        ("a frozen file gone", run_sh, dict(present, **{"engine/tab_ui.h": False}), "lacks ['engine/tab_ui.h']"),
    )
    missed = [name for name, text, there, want in cases if not any(want in p for p in host_test_problems(text, there))]
    rep.check(
        f"self-test: the host test rule fails a run.sh without -ffp-contract=off, a frozen program built against engine/, a frozen run that does not count or whose frames the engine does not get, and a frozen file gone ({len(cases)} cases)",
        not missed,
        f"not caught: {missed}",
    )


# LVGL for the host tests, pinned by commit (cards spec B.9, G.2 E11):
# engine_tests/tools/fetch_lvgl.sh names LVGL_COMMIT and refuses a source or
# a copy that names another one. --compile compares the pin with the LVGL the
# firmware build got (its idf_component.yml names the commit).
FETCH_LVGL = Path("engine_tests") / "tools" / "fetch_lvgl.sh"
LVGL_PIN_RE = re.compile(r"^LVGL_COMMIT=([0-9a-f]{40})$", re.M)


def lvgl_pin():
    m = LVGL_PIN_RE.search(_read_or_empty(ROOT / FETCH_LVGL))
    return m.group(1) if m else ""


def lvgl_fetch_problems(script):
    """Runs a fetch_lvgl.sh text against stand-in sources in a temporary
    folder (sh and awk; git where installed): what it does wrong."""
    pin = LVGL_PIN_RE.search(script)
    if not pin:
        return ["no LVGL_COMMIT=<40 hex> line"]
    pin = pin.group(1)
    sh = shutil.which("sh")
    if not sh:
        return ["no sh to run it"]
    problems = []
    with tempfile.TemporaryDirectory(prefix="tab1-lvgl-pin-") as tmp:
        tmp = Path(tmp)
        tools = tmp / "engine_tests" / "tools"
        tools.mkdir(parents=True)
        (tools / "fetch_lvgl.sh").write_text(script, encoding="utf-8")

        def source(name, commit_yml=None):
            d = tmp / name
            (d / "src").mkdir(parents=True)
            (d / "src" / "a.c").write_text("int a;\n", encoding="utf-8")
            (d / "lvgl.h").write_text("", encoding="utf-8")
            (d / "lv_version.h").write_text("#define LVGL_VERSION_MAJOR 9\n#define LVGL_VERSION_MINOR 5\n#define LVGL_VERSION_PATCH 0\n", encoding="utf-8")
            if commit_yml:
                (d / "idf_component.yml").write_text(f"repository_info:\n  commit_sha: {commit_yml}\n  path: .\nversion: 9.5.0\n", encoding="utf-8")
            return d

        def run(src, dest):
            env = dict(os.environ, TAB_LVGL_SRC=str(src) if src else "")
            r = subprocess.run([sh, str(tools / "fetch_lvgl.sh"), str(dest)], env=env, capture_output=True, text=True, timeout=60)
            return r.returncode

        good = source("good", pin)
        if run(good, tmp / "c1") != 0 or _read_or_empty(tmp / "c1" / ".lvgl_commit").strip() != pin:
            problems.append("the pinned commit is not copied with its commit recorded")
        elif run(None, tmp / "c1") != 0:
            problems.append("its own copy is refused")
        else:
            (tmp / "c1" / ".lvgl_commit").write_text("0" * 40 + "\n", encoding="utf-8")
            if run(None, tmp / "c1") == 0:
                problems.append("a copy of another commit is taken")
        if run(source("other", "1" * 40), tmp / "c2") == 0:
            problems.append("a source naming another commit is taken")
        if run(source("none"), tmp / "c3") != 0:
            problems.append("a source naming no commit is refused")
        git = shutil.which("git")
        if git:
            repo = source("checkout")
            genv = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.invalid", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
            ok = all(
                subprocess.run([git, "-C", str(repo), *a], env=genv, capture_output=True, timeout=60).returncode == 0
                for a in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "x"])
            )
            if ok and run(repo, tmp / "c4") == 0:
                problems.append("a git checkout at another commit is taken")
    return problems


def check_lvgl_pin(rep):
    script = _read_or_empty(ROOT / FETCH_LVGL)
    problems = lvgl_fetch_problems(script)
    rep.check(
        f"host tests: {FETCH_LVGL.as_posix()} pins LVGL 9.5.0 by commit ({lvgl_pin() or 'none'}): it copies a source of that commit and records it, refuses a source or a copy of another commit (named in idf_component.yml or as a git checkout's HEAD), and takes one that names none",
        not problems,
        "; ".join(problems),
    )
    unpinned = re.sub(r"(?m)^LVGL_COMMIT=.*\n", "", script, count=1)
    no_check = script.replace('if [ -n "$c" ] && [ "$c" != "$LVGL_COMMIT" ]; then', 'if false; then', 1)
    cases = {"no pin": unpinned, "the source check gone": no_check}
    missed = [name for name, text in cases.items() if text == script or not lvgl_fetch_problems(text)]
    rep.check(
        f"self-test: the LVGL pin rule fails a fetch_lvgl.sh without the pin or without the source check ({len(cases)} cases)",
        not problems and not missed,
        f"not caught: {missed}" if missed else ("skipped: the pin rule fails" if problems else ""),
    )


def check_lvgl_commit(build_dir, rep, label):
    yml = _read_or_empty(build_dir / "managed_components" / "lvgl__lvgl" / "idf_component.yml")
    m = re.search(r"^\s*commit_sha:\s*([0-9a-f]{40})\s*$", yml, re.M)
    got = m.group(1) if m else ""
    rep.check(
        f"compile {label}: the firmware's LVGL is the commit {FETCH_LVGL.as_posix()} pins for the host tests",
        bool(got) and got == lvgl_pin(),
        f"build {got or 'names no commit'}, pin {lvgl_pin() or 'none'}",
    )


def check_files(engine_dir, rep, substitute):
    ttf = ROOT / "fonts" / "Montserrat-Medium.ttf"
    ofl = ROOT / "fonts" / "Montserrat-OFL.txt"
    rep.check("files: fonts/Montserrat-Medium.ttf is a TrueType font", ttf.is_file() and ttf.read_bytes()[:4] == b"\x00\x01\x00\x00")
    text = ofl.read_text(encoding="utf-8") if ofl.is_file() else ""
    rep.check("files: fonts/Montserrat-OFL.txt is the SIL Open Font License for Montserrat", "SIL OPEN FONT LICENSE" in text and "Montserrat" in text)
    rep.check("files: engine_glue.h is there", (ROOT / "engine_glue.h").is_file())
    label = f"files: engine/ holds only .h files{' (substitute: ' + str(engine_dir) + ')' if substitute else ''}"
    if not engine_dir.is_dir():
        rep.check(label, False, f"{engine_dir} does not exist yet")
        return
    files = [p for p in engine_dir.rglob("*") if p.is_file()]
    bad = [str(p.relative_to(engine_dir)) for p in files if p.suffix != ".h"]
    detail = f"not headers: {bad}" if bad else ("empty" if not files else f"{len(files)} headers")
    rep.check(label, files and not bad, detail)
    rep.check("files: engine/tab_engine.h is there", (engine_dir / "tab_engine.h").is_file())


# ---------------------------------------------------------------------------
#  Host test of the flash queue
# ---------------------------------------------------------------------------
HOST_TEST = r"""
#define TAB_GLUE_HOST_TEST 1
#include "engine_glue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace tab_glue;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
      g_failed++;                                                       \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);               \
    }                                                                   \
  } while (0)

static std::mutex g_queue_lock;        // the queue's critical section
static std::mutex g_m;                 // fake flash
static std::condition_variable g_cv;
static bool g_gate_closed = false;     // a write or erase waits while closed
static bool g_in_op = false;
static std::vector<std::string> g_log; // "save:<bytes>" or "erase"
static std::string g_flash;
static bool g_stored = false;
static std::atomic<int> g_allocs{0}, g_frees{0};
static std::atomic<bool> g_alloc_fail{false}, g_spawn_fail{false}, g_write_fail{false};
static std::mutex g_threads_m;
static std::vector<std::thread> g_threads;

static void *fake_alloc(size_t n) {
  if (g_alloc_fail) return nullptr;
  g_allocs++;
  return std::malloc(n);
}
static void fake_release(void *p) {
  g_frees++;
  std::free(p);
}
static void fake_lock() { g_queue_lock.lock(); }
static void fake_unlock() { g_queue_lock.unlock(); }
static bool fake_spawn() {
  if (g_spawn_fail) return false;
  std::lock_guard<std::mutex> l(g_threads_m);
  g_threads.emplace_back([] { cache::worker_run(); });
  return true;
}
static bool fake_write(const uint8_t *d, size_t n) {
  std::unique_lock<std::mutex> l(g_m);
  g_in_op = true;
  g_cv.notify_all();
  g_cv.wait(l, [] { return !g_gate_closed; });
  if (g_write_fail) {
    g_log.push_back("failed:" + std::string(reinterpret_cast<const char *>(d), n));
    g_in_op = false;
    g_cv.notify_all();
    return false;
  }
  g_flash.assign(reinterpret_cast<const char *>(d), n);
  g_stored = true;
  g_log.push_back("save:" + g_flash);
  g_in_op = false;
  g_cv.notify_all();
  return true;
}
static bool fake_erase() {
  std::unique_lock<std::mutex> l(g_m);
  g_in_op = true;
  g_cv.notify_all();
  g_cv.wait(l, [] { return !g_gate_closed; });
  g_flash.clear();
  g_stored = false;
  g_log.push_back("erase");
  g_in_op = false;
  g_cv.notify_all();
  return true;
}

static void close_gate() { std::lock_guard<std::mutex> l(g_m); g_gate_closed = true; }
static void open_gate() { std::lock_guard<std::mutex> l(g_m); g_gate_closed = false; g_cv.notify_all(); }
static bool wait_in_op() {
  std::unique_lock<std::mutex> l(g_m);
  return g_cv.wait_for(l, std::chrono::seconds(5), [] { return g_in_op; });
}
static void wait_idle() {
  for (int i = 0; i < 5000 && cache::busy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::vector<std::thread> done;
  {
    std::lock_guard<std::mutex> l(g_threads_m);
    done.swap(g_threads);
  }
  for (auto &t : done) t.join();
}
static std::vector<std::string> log_since(size_t from) {
  std::lock_guard<std::mutex> l(g_m);
  return std::vector<std::string>(g_log.begin() + from, g_log.end());
}
static size_t log_size() { std::lock_guard<std::mutex> l(g_m); return g_log.size(); }
static bool save_text(const std::string &s) { return cache::save(reinterpret_cast<const uint8_t *>(s.data()), s.size()); }

int main() {
  cache::set_backend(cache::Backend{fake_alloc, fake_release, fake_lock, fake_unlock, fake_spawn, fake_write, fake_erase});

  // 1. A save copies before it returns; the caller may reuse its buffer at once.
  {
    std::string buf = "layout-A";
    CHECK(cache::save(reinterpret_cast<const uint8_t *>(buf.data()), buf.size()));
    buf[0] = 'X';
    wait_idle();
    CHECK(g_flash == "layout-A");
    CHECK(!cache::busy());
  }
  // 2. An erase during a running save is done after it.
  {
    size_t from = log_size();
    close_gate();
    CHECK(save_text("B"));
    CHECK(wait_in_op());
    CHECK(cache::erase());
    CHECK(cache::busy());
    open_gate();
    wait_idle();
    CHECK((log_since(from) == std::vector<std::string>{"save:B", "erase"}));
    CHECK(!g_stored);
  }
  // 3. A save while another save is being written is refused, and nothing is
  //    copied for it; a save queued behind it (erase in between) is refused too.
  {
    size_t from = log_size();
    close_gate();
    CHECK(save_text("C"));
    CHECK(wait_in_op());
    int allocs = g_allocs;
    CHECK(!save_text("D"));
    CHECK(g_allocs == allocs);
    CHECK(cache::erase());
    CHECK(!save_text("E"));
    open_gate();
    wait_idle();
    CHECK((log_since(from) == std::vector<std::string>{"save:C", "erase"}));
    CHECK(!g_stored);
  }
  // 4. During a running erase, saves are queued and a newer save replaces
  //    the queued one; a newer erase replaces a queued save.
  {
    size_t from = log_size();
    close_gate();
    CHECK(cache::erase());
    CHECK(wait_in_op());
    CHECK(save_text("F"));
    CHECK(save_text("G"));
    open_gate();
    wait_idle();
    CHECK((log_since(from) == std::vector<std::string>{"erase", "save:G"}));
    CHECK(g_flash == "G");
    from = log_size();
    close_gate();
    CHECK(cache::erase());
    CHECK(wait_in_op());
    CHECK(save_text("H"));
    CHECK(cache::erase());
    open_gate();
    wait_idle();
    CHECK((log_since(from) == std::vector<std::string>{"erase", "erase"}));
    CHECK(!g_stored);
    // An erase during a running save, then a newer erase: one erase after it.
    from = log_size();
    close_gate();
    CHECK(save_text("I"));
    CHECK(wait_in_op());
    CHECK(cache::erase());
    CHECK(cache::erase());
    open_gate();
    wait_idle();
    CHECK((log_since(from) == std::vector<std::string>{"save:I", "erase"}));
  }
  // 5. No memory for the copy: false, nothing queued.
  {
    g_alloc_fail = true;
    CHECK(!save_text("J"));
    CHECK(!cache::busy());
    g_alloc_fail = false;
  }
  // 6. No worker could start: false, the copy is freed, the next save works.
  {
    g_spawn_fail = true;
    CHECK(!save_text("K"));
    CHECK(!cache::busy());
    CHECK(!cache::erase());
    g_spawn_fail = false;
    CHECK(save_text("L"));
    wait_idle();
    CHECK(g_flash == "L");
  }
  // 7. Too big to store: the stored copy is erased instead, nothing is copied.
  {
    size_t from = log_size();
    int allocs = g_allocs;
    std::vector<uint8_t> big(cache::MAX_STORED_BYTES + 1, 7);
    CHECK(cache::save(big.data(), big.size()));
    wait_idle();
    CHECK(g_allocs == allocs);
    CHECK((log_since(from) == std::vector<std::string>{"erase"}));
    CHECK(!g_stored);
    std::vector<uint8_t> max(cache::MAX_STORED_BYTES, 9);
    CHECK(cache::save(max.data(), max.size()));
    wait_idle();
    CHECK(g_flash.size() == cache::MAX_STORED_BYTES);
  }
  // 8. Reports: one fresh report after work, then none.
  {
    cache::Kind kind;
    bool ok;
    size_t n;
    CHECK(save_text("report"));
    wait_idle();
    CHECK(cache::take_report(&kind, &ok, &n));
    CHECK(kind == cache::SAVE && ok && n == 6);
    CHECK(!cache::take_report(&kind, &ok, &n));
  }
  // 9. Many operations from the main thread while the worker runs: the last
  //    accepted one always wins, refusals happen only while a save runs, and
  //    every copy is freed.
  {
    std::string last;
    int accepted = 0, refused = 0;
    for (int i = 0; i < 300; i++) {
      if (i % 7 == 3) {
        CHECK(cache::erase());
        last = "";
      } else {
        std::string v = "v" + std::to_string(i);
        if (save_text(v)) {
          last = v;
          accepted++;
        } else {
          refused++;
        }
      }
    }
    wait_idle();
    CHECK(g_flash == last);
    CHECK(g_stored == !last.empty());
    CHECK(accepted > 0);
    std::printf("stress: %d saves accepted, %d refused while a save ran\n", accepted, refused);
    // With nothing running, a save is always accepted.
    CHECK(save_text("final"));
    wait_idle();
    CHECK(g_flash == "final");
  }
  // 10. A save whose write fails is reported to the engine once; a later save
  //     that is written, or an erase, reports nothing.
  {
    CHECK(!cache::take_save_failed());
    g_write_fail = true;
    CHECK(save_text("full"));
    wait_idle();
    g_write_fail = false;
    CHECK(cache::take_save_failed());
    CHECK(!cache::take_save_failed());
    g_write_fail = true;
    CHECK(save_text("full again"));
    wait_idle();
    g_write_fail = false;
    CHECK(save_text("fits"));
    wait_idle();
    CHECK(!cache::take_save_failed());
    CHECK(cache::erase());
    wait_idle();
    CHECK(!cache::take_save_failed());
    CHECK(g_flash.empty() && !g_stored);
  }
  CHECK(g_allocs == g_frees);
  std::printf("glue host test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
"""


def lock_host_test():
    """C++ host test of ota_lock.h (tab_lock::core), with the expected
    passwords computed here by hashlib, independently of the engine's SHA-256."""
    key = bytes(range(32))
    rnd = bytes((0xA0 + i) & 0xFF for i in range(32))
    counter = 0x0123456789ABCDEF
    le = counter.to_bytes(8, "little")
    want_both = hashlib.sha256(key + rnd + le).hexdigest()
    want_key = hashlib.sha256(key + le).hexdigest()
    want_rnd = rnd.hex()
    return (
        LOCK_HOST_TEST.replace("@WANT_BOTH@", want_both)
        .replace("@WANT_KEY@", want_key)
        .replace("@WANT_RND@", want_rnd)
    )


LOCK_HOST_TEST = r"""
#define TAB_LOCK_HOST_TEST 1
#include "ota_lock.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace tab_lock::core;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
      g_failed++;                                                       \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);               \
    }                                                                   \
  } while (0)

static Inputs in(bool key, bool window, bool provisioned, bool closed, bool setup, bool net, uint32_t now) {
  Inputs i{};
  i.key_active = key;
  i.has_window = window;
  i.provisioned = provisioned;
  i.closed = closed;
  i.setup_complete = setup;
  i.network = net;
  i.now_ms = now;
  return i;
}

int main() {
  uint8_t key[32], rnd[32];
  for (int i = 0; i < 32; i++) {
    key[i] = (uint8_t) i;
    rnd[i] = (uint8_t) (0xA0 + i);
  }
  const uint64_t counter = 0x0123456789ABCDEFull;
  char pw[65];
  // 1. The password: hex(SHA-256(key || random || counter LE)).
  CHECK(make_password(key, rnd, counter, pw) == MADE_KEY_AND_RANDOM);
  CHECK(std::string(pw) == "@WANT_BOTH@");
  // 2. No key: the random bytes alone, as hex.
  CHECK(make_password(nullptr, rnd, counter, pw) == MADE_RANDOM);
  CHECK(std::string(pw) == "@WANT_RND@");
  // 3. No random bytes: from the key and the counter, never a fixed value.
  CHECK(make_password(key, nullptr, counter, pw) == MADE_KEY);
  CHECK(std::string(pw) == "@WANT_KEY@");
  char pw2[65];
  CHECK(make_password(key, nullptr, counter + 1, pw2) == MADE_KEY && std::strcmp(pw, pw2) != 0);
  // 4. Neither: nothing to lock with, and the buffer holds no password.
  std::memset(pw, 'x', sizeof pw);
  CHECK(make_password(nullptr, nullptr, counter, pw) == MADE_NONE);
  bool zero = true;
  for (char c : pw) zero = zero && c == 0;
  CHECK(zero);
  // 5. A different counter or random value gives a different password.
  CHECK(make_password(key, rnd, counter ^ 1, pw2) == MADE_KEY_AND_RANDOM);
  CHECK(std::string(pw2) != "@WANT_BOTH@" && std::strlen(pw2) == 64);

  // 6. Unprovisioned, window open: nothing, also before setup completes.
  {
    State s{};
    CHECK(step(s, in(false, true, false, false, true, true, 10)) == NONE);
    CHECK(step(s, in(false, true, true, false, false, true, 20)) == NONE);  // setup not done yet
    CHECK(step(s, in(false, true, true, false, true, true, 30)) == LOCK_KEY);  // provisioned
  }
  // 7. The window closes without a key: lock.
  {
    State s{};
    CHECK(step(s, in(false, true, false, true, true, false, 10)) == LOCK_CLOSED);
    s.locked = true;
    CHECK(step(s, in(false, true, false, true, true, true, 20)) == NONE);  // no key: no restart
  }
  // 8. A key is active (provisioned before, or in the YAML): lock, also without a window.
  {
    State s{};
    CHECK(step(s, in(true, false, false, false, false, false, 10)) == LOCK_KEY);
    s.locked = true;
    CHECK(step(s, in(true, false, false, false, true, true, 20)) == NONE);
  }
  // 9. The key is cleared: one log, the restart 2 s later, then nothing.
  {
    State s{};
    CHECK(step(s, in(true, true, true, false, true, true, 1000)) == LOCK_KEY);
    s.locked = true;
    CHECK(step(s, in(true, true, true, false, true, true, 1100)) == NONE);
    CHECK(step(s, in(false, true, false, false, true, true, 5000)) == KEY_CLEARED);
    CHECK(step(s, in(false, true, false, false, true, true, 5001)) == NONE);
    CHECK(step(s, in(false, true, false, false, true, true, 6999)) == NONE);
    CHECK(step(s, in(false, true, false, false, true, true, 7000)) == RESTART_CLEARED);
    CHECK(step(s, in(false, true, false, false, true, true, 9000)) == NONE);
    CHECK(step(s, in(true, true, true, false, true, true, 9500)) == NONE);
  }
  // 10. A key that comes back within the 2 s still restarts.
  {
    State s{};
    s.locked = true;
    CHECK(step(s, in(true, true, true, false, true, true, 100)) == NONE);
    CHECK(step(s, in(false, true, false, false, true, true, 200)) == KEY_CLEARED);
    CHECK(step(s, in(true, true, true, false, true, true, 300)) == NONE);
    CHECK(step(s, in(true, true, true, false, true, true, 2200)) == RESTART_CLEARED);
  }
  // 11. Across the millisecond counter wrap.
  {
    State s{};
    s.locked = true;
    CHECK(step(s, in(true, true, true, false, true, true, 0xFFFFF000u)) == NONE);
    CHECK(step(s, in(false, true, false, false, true, true, 0xFFFFFF00u)) == KEY_CLEARED);
    CHECK(step(s, in(false, true, false, false, true, true, 0x00000100u)) == NONE);   // 512 ms later
    CHECK(step(s, in(false, true, false, false, true, true, 0x00000700u)) == RESTART_CLEARED);  // 2048 ms
  }
  // 12. A lock that could not be set (no password): asked again next pass.
  {
    State s{};
    CHECK(step(s, in(true, false, false, false, true, true, 10)) == LOCK_KEY);
    CHECK(step(s, in(true, false, false, false, true, true, 12)) == LOCK_KEY);
  }
  // 13. Window closed while a tablet with a key was offline: one restart once
  //     the network is back.
  {
    State s{};
    s.locked = true;
    CHECK(step(s, in(true, true, false, true, true, false, 10)) == NONE);
    CHECK(step(s, in(true, true, false, true, true, true, 20)) == RESTART_OFFLINE);
    CHECK(step(s, in(true, true, false, true, true, true, 30)) == NONE);
  }
  // 14. A key from the YAML is written to the settings when none is stored
  //     or another one is; never when it is the same, and never an empty key.
  {
    uint8_t other[32], zero[32] = {0};
    std::memcpy(other, key, 32);
    CHECK(!stored_key_stale(key, other));
    other[31] ^= 1;
    CHECK(stored_key_stale(key, other));
    other[31] ^= 1;
    other[0] ^= 0x80;
    CHECK(stored_key_stale(key, other));
    CHECK(stored_key_stale(key, nullptr));
    CHECK(stored_key_stale(key, zero));
    CHECK(!stored_key_stale(zero, nullptr));
    CHECK(!stored_key_stale(zero, key));
    CHECK(!stored_key_stale(nullptr, key));
  }
  std::printf("lock host test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
"""


def run_host_test(work, rep):
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        rep.check("host: C++ compiler found", False, "no c++, clang++ or g++ on PATH")
        return
    tests = (
        ("glue", "flash queue test", HOST_TEST, True),
        ("lock", "update lock test", lock_host_test(), False),
    )
    for short, what, code, threads in tests:
        src = work / f"{short}_host_test.cpp"
        src.write_text(code, encoding="utf-8")
        base = [cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread", f"-I{ROOT}"]
        variants = [("asan+ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]), ("plain", [])]
        if threads:
            variants.insert(1, ("tsan", ["-fsanitize=thread"]))
        built_any = False
        for label, flags in variants:
            exe = work / f"{short}_host_test_{label.replace('+', '_')}"
            build = subprocess.run([*base, *flags, str(src), "-o", str(exe)], capture_output=True, text=True)
            if build.returncode != 0:
                if flags:
                    print(f"note  host: {what} {label} build not available here, skipped")
                    continue
                rep.check(f"host: {what} builds ({label})", False, build.stderr.strip()[-2000:])
                break
            built_any = True
            env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1", ASAN_OPTIONS="halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120, env=env)
            out = (run.stdout + run.stderr).strip()
            rep.check(f"host: {what} passes ({label})", run.returncode == 0, out[-2000:] if run.returncode else out.splitlines()[-1])
        if not built_any:
            rep.check(f"host: {what} builds", False)
    run_guard_test(cxx, work, rep)
    run_files_test(cxx, work, rep)


# Every include of the package's C++ files that is not a standard header;
# the host tests below give each an empty stand-in, as they only run the
# preprocessor over them.
STUB_INCLUDE_RE = re.compile(r'^\s*#include\s+[<"]((?:esphome/|esp_|freertos/|nvs|lvgl)[^>"]*)[>"]', re.M)
GUARD_MESSAGE = "ota_lock.h: the `ota: - platform: esphome` block needs"
# The update defines of each case, and whether ota_lock.h must build. The
# first three are the real variants (VARIANT_DEFINES); the rest would leave
# plaintext updates open or are not what the package ships.
GUARD_CASES = (
    ("prod", {"USE_OTA_PASSWORD", "USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_PROVISIONED"}, True),
    ("bench", {"USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_REQUIRED", "USE_API_NOISE_PSK_FROM_YAML"}, True),
    ("adopt", {"USE_OTA_PASSWORD", "USE_OTA_ENCRYPTION", "USE_API_NOISE_PSK_FROM_YAML"}, True),
    ("prod without `password: \"\"`", {"USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_PROVISIONED"}, False),
    ("required encryption with a runtime key", {"USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_PROVISIONED", "USE_OTA_ENCRYPTION_REQUIRED"}, False),
    ("encryption with a YAML key, not required", {"USE_OTA_ENCRYPTION", "USE_API_NOISE_PSK_FROM_YAML"}, False),
    ("no password, no encryption", set(), False),
)


def _write_stubs(stub_dir, sources, defines):
    for text in sources:
        for inc in STUB_INCLUDE_RE.findall(text):
            path = stub_dir / inc
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.write_text("", encoding="utf-8")
    d = stub_dir / "esphome" / "core" / "defines.h"
    d.parent.mkdir(parents=True, exist_ok=True)
    d.write_text("#pragma once\n" + "".join(f"#define {n}\n" for n in sorted(defines)), encoding="utf-8")


def run_guard_test(cxx, work, rep):
    """ota_lock.h stops every build that has no update password check and
    does not require encryption with a key from the YAML (design 10.9)."""
    lock = (ROOT / LOCK).read_text(encoding="utf-8")
    base = work / "guard"
    shutil.rmtree(base, ignore_errors=True)
    results = []
    for i, (what, defines, want_ok) in enumerate(GUARD_CASES):
        stubs = base / f"case{i}"
        _write_stubs(stubs, [lock], defines | {"USE_API", "USE_PROVISIONING", "USE_NETWORK", "USE_WEBSERVER_OTA"})
        tu = stubs / "tu.cpp"
        tu.write_text(f'#include "{LOCK}"\n', encoding="utf-8")
        run = subprocess.run([cxx, "-std=c++17", "-E", f"-I{ROOT}", f"-I{stubs}", str(tu), "-o", os.devnull], capture_output=True, text=True)
        stopped = run.returncode != 0 and GUARD_MESSAGE in run.stderr
        other = run.returncode != 0 and not stopped
        ok = (run.returncode == 0) if want_ok else stopped
        results.append(ok)
        if not ok:
            print(f"      guard case {what}: exit {run.returncode}{' (other error)' if other else ''}\n      {run.stderr.strip()[-600:]}")
    shutil.rmtree(base, ignore_errors=True)
    rep.check(
        f"host: {LOCK} builds for prod, bench and adopt and stops (#error) without `password: \"\"` or required encryption with a YAML key ({len(GUARD_CASES)} cases)",
        all(results),
        ", ".join(f"{c[0]}: {'ok' if r else 'WRONG'}" for c, r in zip(GUARD_CASES, results)),
    )


def _set_number(text, macro, number):
    return re.sub(rf"^#define {macro} \d+$", f"#define {macro} {number}", text, flags=re.M)


def _drop(text, pattern):
    return re.sub(pattern, "", text, flags=re.M | re.S)


# A copy in the ESPHome config folder replaces the package's file in the
# build. Each case swaps one of them for an older or foreign copy.
FILES_CROSS_RE = r"^#if !defined\(TAB1_FILES_ENGINE\) \|\| TAB1_FILES_ENGINE != TAB1_FILES_\w+\n#error [^\n]*\n#endif\n"
FILES_INCLUDE_RE = r'^#if __has_include\("engine/tab_files\.h"\)[^\n]*\n#include "engine/tab_files\.h"\n#endif\n'


def pre_version(text, macro):
    """A header as it was before the files version: no number, no check of
    its own and no tab_files.h."""
    return _drop(_drop(_drop(text, rf"^#define {macro} \d+\n"), FILES_CROSS_RE), FILES_INCLUDE_RE)

# (what, file, change (None: delete), the name Engine.yaml's message gives,
#  whether the other headers' own checks also stop it)
FILES_CASES = (
    ("the package's own files", None, None, None, False),
    ("an older ota_lock.h (another number)", LOCK, lambda t: _set_number(t, "TAB1_FILES_LOCK", 0), "ota_lock.h", True),
    ("an ota_lock.h from before the files version (no number, no check)", LOCK, lambda t: pre_version(t, "TAB1_FILES_LOCK"), "ota_lock.h", False),
    ("an older engine_glue.h (another number)", GLUE, lambda t: _set_number(t, "TAB1_FILES_GLUE", 0), "engine_glue.h", True),
    ("an engine_glue.h from before the files version (no number, no check)", GLUE, lambda t: pre_version(t, "TAB1_FILES_GLUE"), "engine_glue.h", False),
    ("an older engine/ (another number)", "engine/tab_files.h", lambda t: _set_number(t, "TAB1_FILES_ENGINE", 0), "engine/", True),
    ("an older engine/ (no tab_files.h)", "engine/tab_files.h", None, "engine/", True),
)


def run_files_test(cxx, work, rep):
    """An older or foreign copy of ota_lock.h, engine_glue.h or engine/ in
    the ESPHome config folder stops the build with a message naming it."""
    engine = load_yaml(ROOT / ENGINE)
    start = _scripts(engine).get("tab_engine_start") or {}
    asserts = "\n".join(m.group(0) for m in FILES_ASSERT_RE.finditer("".join(_lambdas(start.get("then")))))
    base = work / "files"
    shutil.rmtree(base, ignore_errors=True)
    results = []
    for i, (what, rel, change, names, header_check) in enumerate(FILES_CASES):
        src = base / f"case{i}" / "src"
        shutil.copytree(ROOT / "engine", src / "engine")
        shutil.copy2(ROOT / GLUE, src / GLUE)
        shutil.copy2(ROOT / LOCK, src / LOCK)
        if rel:
            path = src / rel
            if change is None:
                path.unlink()
                te = src / "engine" / "tab_engine.h"
                te.write_text(_drop(te.read_text(encoding="utf-8"), r'^#include "tab_files.h"[^\n]*\n'), encoding="utf-8")
            else:
                path.write_text(change(path.read_text(encoding="utf-8")), encoding="utf-8")
        stubs = base / f"case{i}" / "stubs"
        texts = [p.read_text(encoding="utf-8") for p in [src / GLUE, src / LOCK, *(src / "engine").glob("*.h")]]
        _write_stubs(stubs, texts, {"USE_OTA_PASSWORD", "USE_API", "USE_NETWORK", "USE_WIFI_AP"})
        # 1. The headers' own checks, as main.cpp includes them (preprocessor).
        tu_pp = base / f"case{i}" / "pp.cpp"
        tu_pp.write_text(f'#include "engine/tab_engine.h"\n#include "{GLUE}"\n#include "{LOCK}"\n', encoding="utf-8")
        pp = subprocess.run([cxx, "-std=c++17", "-E", f"-I{src}", f"-I{stubs}", str(tu_pp), "-o", os.devnull], capture_output=True, text=True)
        # 2. Engine.yaml's static_asserts in tab_engine_start (compiler).
        tu_sa = base / f"case{i}" / "sa.cpp"
        tu_sa.write_text(
            "#define TAB_GLUE_HOST_TEST 1\n#define TAB_LOCK_HOST_TEST 1\n"
            '#if __has_include("engine/tab_files.h")\n#include "engine/tab_files.h"\n#endif\n'
            f'#include "{GLUE}"\n#include "{LOCK}"\nint main() {{\n{asserts}\n  return 0;\n}}\n',
            encoding="utf-8",
        )
        sa = subprocess.run([cxx, "-std=c++17", "-fsyntax-only", f"-I{src}", str(tu_sa)], capture_output=True, text=True)
        if rel is None:
            ok = pp.returncode == 0 and sa.returncode == 0 and len(FILES_ASSERT_RE.findall(asserts)) == 3
        else:
            # Engine.yaml stops every such build with its own error line,
            # naming the copy to delete, also when the copy has no number.
            ok = sa.returncode != 0 and re.search(FILES_ERROR_LINE.format(name=re.escape(names)), sa.stderr) is not None
            # The other headers' checks see another number, or none; a
            # foreign header itself has no check of its own.
            if header_check:
                ok = ok and pp.returncode != 0 and "come from different copies" in pp.stderr
        results.append(ok)
        if not ok:
            print(f"      files case {what}: preprocessor exit {pp.returncode}, compiler exit {sa.returncode}\n      {(pp.stderr + sa.stderr).strip()[-800:]}")
    shutil.rmtree(base, ignore_errors=True)
    rep.check(
        f"host: an older or pre-version ota_lock.h, engine_glue.h or engine/ stops the build with an error naming the copy to delete ({len(FILES_CASES)} cases)",
        all(results),
        ", ".join(f"{c[0]}: {'ok' if r else 'WRONG'}" for c, r in zip(FILES_CASES, results)),
    )


# ---------------------------------------------------------------------------
#  ESPHome in a work copy
# ---------------------------------------------------------------------------
def _inside(path, parent):
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def _repo_top():
    top = ROOT
    while top != top.parent:
        if (top / ".git").exists():
            return top
        top = top.parent
    return ROOT


def prepare_copy(work, engine_dir, rep):
    copy_dir = work / "ESPHome"
    copy_dir.mkdir(parents=True, exist_ok=True)
    # Keep the copy's .esphome (build cache) and secrets; replace the rest.
    for entry in copy_dir.iterdir():
        if entry.name in (".esphome", "secrets.yaml"):
            continue
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    shutil.copytree(ROOT, copy_dir, ignore=COPY_IGNORE, dirs_exist_ok=True)
    if engine_dir.resolve() != (ROOT / "engine").resolve():
        target = copy_dir / "engine"
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(engine_dir, target, ignore=COPY_IGNORE)
    secrets = copy_dir / "secrets.yaml"
    have = secrets.read_text(encoding="utf-8") if secrets.exists() else ""
    if not have:
        have = "# Throwaway secrets for the package check. Not used on any tablet.\n"
    for name in (SECRET_NAME, *ADOPT_SECRETS):
        if not re.search(rf"^{name}:", have, re.M):
            have += f'{name}: "{base64.b64encode(os.urandom(32)).decode()}"\n'
    secrets.write_text(have, encoding="utf-8")
    write_adoption(copy_dir)
    return copy_dir


def write_adoption(copy_dir):
    """The device file ESPHome Builder writes when it adopts a tablet
    (dashboard_import, import_full_config: false), with the package pulled in
    from a subfolder so every include must resolve beside TAB-1-engine.yaml,
    as for the remote package. The key is a throwaway made here."""
    path = copy_dir / ADOPT
    path.parent.mkdir(parents=True, exist_ok=True)
    key = base64.b64encode(os.urandom(32)).decode()
    path.write_text(
        "# Simulated ESPHome Builder adoption for the package check. Not used on any tablet.\n"
        "substitutions:\n"
        f"  name: {ADOPT_NAME}\n"
        "  friendly_name: Apollo TAB-1 a1b2c3\n"
        "packages:\n"
        f"  ApolloAutomation.TAB-1: !include ../{PROD}\n"
        "esphome:\n"
        "  name: ${name}\n"
        "  name_add_mac_suffix: false\n"
        "  friendly_name: ${friendly_name}\n"
        "api:\n"
        "  encryption:\n"
        f'    key: "{key}"\n'
        "wifi:\n"
        "  ssid: !secret wifi_ssid\n"
        "  password: !secret wifi_password\n",
        encoding="utf-8",
    )
    # The same tablet the other way up: the person's own file sets the
    # substitution, and it must win over the package's.
    text = path.read_text(encoding="utf-8")
    (copy_dir / ADOPT_TURNED).write_text(
        text.replace("  friendly_name: Apollo TAB-1 a1b2c3\n", f'  friendly_name: Apollo TAB-1 a1b2c3\n  {ROTATION_SUB}: "{ROTATION_OTHER}"\n', 1),
        encoding="utf-8",
    )
    # ESPHome reads !secret from the secrets.yaml beside the device file (the
    # Builder's config folder).
    (path.parent / "secrets.yaml").write_text(
        "# Throwaway secrets for the package check. Not used on any tablet.\n"
        + "".join(f'{name}: "{base64.b64encode(os.urandom(18)).decode()}"\n' for name in ADOPT_SECRETS),
        encoding="utf-8",
    )


def esphome_env(offline):
    env = dict(os.environ)
    env.pop("PYTHONPATH", None)
    if offline:
        for var in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
            env[var] = OFFLINE_PROXY
        env["NO_PROXY"] = ""
        env["no_proxy"] = ""
    return env


def data_dir_for(copy_dir):
    if os.environ.get("ESPHOME_DATA_DIR"):
        return Path(os.environ["ESPHOME_DATA_DIR"])
    return copy_dir / ".esphome"


def seed_offline_caches(copy_dir, dst_root=None):
    dst_root = Path(dst_root) if dst_root else data_dir_for(copy_dir)
    for src_root, names in ((ROOT / ".esphome", OFFLINE_CACHE_DIRS), (data_dir_for(copy_dir), OFFLINE_CACHE_DIRS + (".espressif",))):
        if src_root.resolve() == dst_root.resolve():
            continue
        for name in names:
            src = src_root / name
            dst = dst_root / name
            if src.is_dir() and not dst.exists():
                shutil.copytree(src, dst, symlinks=True)


def run_esphome(esphome, verb, entry, copy_dir, log_path, env, timeout, extra=()):
    with open(log_path, "w", encoding="utf-8") as log:
        try:
            proc = subprocess.run([esphome, verb, *extra, entry], cwd=copy_dir, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=timeout)
            return proc.returncode
        except subprocess.TimeoutExpired:
            log.write(f"\nTIMEOUT after {timeout} s\n")
            return -1


def _tail(path, lines=25):
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n      ".join(text[-lines:])


# The LVGL rotation `esphome config` prints for each file: the engine files
# and the adoption take the substitution's default, an adoption that sets the
# substitution itself gets its own value, the two protected files keep 270.
CONFIG_ROTATION = {PROD: ROTATION_DEFAULT, BENCH: ROTATION_DEFAULT, ADOPT: ROTATION_DEFAULT, ADOPT_TURNED: ROTATION_OTHER, TAB1: "270", SHOWCASE: "270"}
LVGL_ROTATION_RE = re.compile(r"(?:  - |    )rotation: '?(\d+)'?")


def config_rotations(text):
    """The `rotation` of each `lvgl:` entry in what `esphome config` printed
    (a widget's own rotation sits deeper and does not count)."""
    found, inside = [], False
    for line in ANSI_RE.sub("", text).splitlines():
        if line and not line[0].isspace():
            inside = line.rstrip() == "lvgl:"
        elif inside and (m := LVGL_ROTATION_RE.fullmatch(line.rstrip())):
            found.append(m.group(1))
    return found


def run_config(esphome, copy_dir, work, env, rep):
    for entry in (PROD, BENCH, ADOPT, ADOPT_TURNED, TAB1, SHOWCASE):
        log = work / f"config-{Path(entry).name}.log"
        # ESPHome keeps its data beside the device file; the adopted file sits
        # in a subfolder, so point it at the copy's data (downloaded files).
        venv = dict(env, ESPHOME_DATA_DIR=str(data_dir_for(copy_dir))) if entry in (ADOPT, ADOPT_TURNED) else env
        code = run_esphome(esphome, "config", entry, copy_dir, log, venv, 900)
        text = log.read_text(encoding="utf-8", errors="replace")
        ok = code == 0 and "Configuration is valid!" in text
        rep.check(
            f"config: esphome config {entry}",
            ok,
            f"log {log}" if ok else f"exit {code}, log {log}\n      {_tail(log)}",
        )
        if ok:
            got = config_rotations(text)
            rep.check(f"config: {entry} comes out with lvgl rotation {CONFIG_ROTATION[entry]}", got == [CONFIG_ROTATION[entry]], f"got {got}")


SIZE_RE = {
    "ram": re.compile(r"RAM:\s+\[[^\]]*\]\s+([\d.]+)% \(used (\d+) bytes from (\d+) bytes\)"),
    "flash": re.compile(r"Flash:\s+\[[^\]]*\]\s+([\d.]+)% \(used (\d+) bytes from (\d+) bytes\)"),
    "binary": re.compile(r"binary size (0x[0-9a-f]+) bytes\. Smallest app partition is (0x[0-9a-f]+) bytes\. (0x[0-9a-f]+) bytes \((\d+)%\) free"),
    "image": re.compile(r"Total image size: (\d+) bytes"),
}
REGION_RE = re.compile(r"^│ (\S[^│]*?)\s*│\s*(\d+)\s*│\s*([\d.]+)\s*│\s*(\d*)\s*│\s*(\d*)\s*│")


def parse_sizes(log_text):
    out = {}
    m = SIZE_RE["ram"].search(log_text)
    if m:
        out["ram"] = {"percent": float(m.group(1)), "used": int(m.group(2)), "total": int(m.group(3))}
    m = SIZE_RE["flash"].search(log_text)
    if m:
        out["flash"] = {"percent": float(m.group(1)), "used": int(m.group(2)), "total": int(m.group(3))}
    m = SIZE_RE["binary"].search(log_text)
    if m:
        out["app"] = {
            "binary": int(m.group(1), 16),
            "partition": int(m.group(2), 16),
            "free": int(m.group(3), 16),
            "free_percent": int(m.group(4)),
        }
    m = SIZE_RE["image"].search(log_text)
    if m:
        out["image_bytes"] = int(m.group(1))
    regions = {}
    for line in log_text.splitlines():
        r = REGION_RE.match(line.strip())
        if r and not r.group(1).startswith(".") and r.group(1) not in ("Memory Type/Section",):
            regions[r.group(1)] = {
                "used": int(r.group(2)),
                "percent": float(r.group(3)),
                "remain": int(r.group(4)) if r.group(4) else None,
                "total": int(r.group(5)) if r.group(5) else None,
            }
    if regions:
        out["regions"] = regions
    return out


def check_generated_src(build_dir, copy_dir, rep, label):
    src = build_dir / "src"
    if not src.is_dir():
        rep.check(f"compile {label}: generated src/ exists", False, str(src))
        return
    files = sorted(str(p.relative_to(src)) for p in src.rglob("*") if p.is_file())
    engine_src = copy_dir / "engine"
    want_engine = sorted(str(p.relative_to(copy_dir)) for p in engine_src.rglob("*") if p.is_file())
    got_engine = [f for f in files if f.split("/")[0] == "engine"]
    rep.check(f"compile {label}: src/engine/ holds exactly the engine headers", got_engine == want_engine, f"got {got_engine}, want {want_engine}")
    same = all((src / f).read_bytes() == (copy_dir / f).read_bytes() for f in want_engine if (src / f).is_file())
    rep.check(f"compile {label}: engine headers in src/ are byte-identical", same)
    glue_ok = (src / "engine_glue.h").is_file() and (src / "engine_glue.h").read_bytes() == (copy_dir / "engine_glue.h").read_bytes()
    rep.check(f"compile {label}: src/engine_glue.h is there and identical", glue_ok)
    fast = src / "fast_display.h"
    rep.check(f"compile {label}: src/fast_display.h is there and identical", fast.is_file() and fast.read_bytes() == (copy_dir / "fast_display.h").read_bytes())
    from_tests = [f for f in files if "engine_tests" in f.split("/")]
    tests_dir = ROOT / "engine_tests"
    if tests_dir.is_dir():
        test_names = {p.name for p in tests_dir.rglob("*") if p.is_file()}
        from_tests += [f for f in files if f.split("/")[0] == "engine" and Path(f).name in test_names and not f.endswith(".h")]
    rep.check(f"compile {label}: nothing from engine_tests/ in src/", not from_tests, f"found {from_tests}")
    code = [f for f in got_engine if Path(f).suffix in (".c", ".cc", ".cpp", ".cxx", ".S", ".s")]
    rep.check(f"compile {label}: no engine .c/.cpp in src/", not code, f"found {code}")
    main = (src / "main.cpp").read_text(encoding="utf-8", errors="replace") if (src / "main.cpp").is_file() else ""
    rep.check(
        f"compile {label}: main.cpp includes engine/tab_engine.h and engine_glue.h",
        '#include "engine/tab_engine.h"' in main and '#include "engine_glue.h"' in main,
    )
    triggers = re.findall(r"UserServiceTrigger<[^;]*?>", main)
    string_args = [t for t in triggers if "std::string" in t]
    views = [t for t in triggers if "StringRef" in t]
    rep.check(
        f"compile {label}: every action takes its strings as views (StringRef, not std::string)",
        len(triggers) >= 8 and not string_args and len(views) >= 7,
        f"{len(triggers)} action triggers, {len(views)} with StringRef, {len(string_args)} with std::string",
    )


# What each variant's generated defines.h must and must not hold.
VARIANT_DEFINES = {
    # Runtime key from Home Assistant; empty password so the lock can set one.
    "prod": (
        {"USE_OTA_PASSWORD", "USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_PROVISIONED", "USE_PROVISIONING", "USE_WEBSERVER_OTA", "USE_CAPTIVE_PORTAL", "USE_WIFI_AP"},
        {"USE_API_NOISE_PSK_FROM_YAML", "USE_OTA_ENCRYPTION_REQUIRED"},
    ),
    # Fixed key, encrypted updates only.
    "bench": (
        {"USE_OTA_ENCRYPTION", "USE_OTA_ENCRYPTION_REQUIRED", "USE_API_NOISE_PSK_FROM_YAML", "USE_PROVISIONING", "USE_WEBSERVER_OTA", "USE_WIFI_AP"},
        {"USE_OTA_PASSWORD", "USE_OTA_ENCRYPTION_PROVISIONED"},
    ),
    # Adopted in ESPHome Builder: the key in the YAML, the package's empty
    # password; key_saved() takes the USE_OTA_ENCRYPTION branch.
    "adopt": (
        {"USE_OTA_PASSWORD", "USE_OTA_ENCRYPTION", "USE_API_NOISE_PSK_FROM_YAML", "USE_PROVISIONING", "USE_WEBSERVER_OTA", "USE_WIFI_AP"},
        {"USE_OTA_ENCRYPTION_PROVISIONED", "USE_OTA_ENCRYPTION_REQUIRED"},
    ),
}
# Variants with Apollo's update channel (checked HTTPS).
UPDATE_VARIANTS = {"prod", "adopt"}
SETUP_WINDOW_MS = 15 * 60 * 1000
VARIANTS = (
    # label, entry, data folder in --work
    ("prod", PROD, "data-prod"),
    ("bench", BENCH, "data-bench"),
    ("adopt", ADOPT, "data-adopt"),
)
NVS_OPEN_RE = re.compile(r"\bnvs_open(_from_partition)?\s*\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,")
FONT_RE = re.compile(r"font::Font\(\s*\w+,\s*\d+,\s*(-?\d+),\s*(\d+),")
METRIC_RE = re.compile(r'\{"(text|digits|icons)",\s*(\d+),\s*(\d+),\s*(\d+)\}')


def _defines(src):
    path = src / "esphome" / "core" / "defines.h"
    if not path.is_file():
        return None
    return set(re.findall(r"^#define\s+(\w+)", path.read_text(encoding="utf-8", errors="replace"), re.M))


def _resolve_ns(arg, text):
    arg = arg.strip()
    if arg.startswith('"') and arg.endswith('"'):
        return arg[1:-1]
    m = re.search(rf"\b{re.escape(arg)}\b(?:\[\])?\s*=\s*\"([^\"]+)\"", text)
    return m.group(1) if m else None


def check_nvs(build_dir, rep, label):
    """Every NVS namespace the firmware opens is erased by hold-to-erase:
    the two the erase names, or (in an ESP-IDF component) the default NVS
    partition that the erase wipes as a whole."""
    found, unresolved, other_partition, components = {}, [], [], {}
    roots = [("src", build_dir / "src"), ("managed_components", build_dir / "managed_components")]
    for kind, root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix not in (".c", ".cc", ".cpp", ".h", ".hpp") or not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if "nvs_open" not in text:
                continue
            rel = str(path.relative_to(build_dir))
            for m in NVS_OPEN_RE.finditer(text):
                if m.group(1):  # nvs_open_from_partition(partition, namespace, ...)
                    part = _resolve_ns(m.group(2), text)
                    ns_arg = m.group(3)
                    if part not in (None, "nvs") and "NVS_DEFAULT_PART_NAME" not in m.group(2):
                        other_partition.append(f"{rel}: {m.group(2)}")
                else:
                    ns_arg = m.group(2)
                # Skip the declaration in a header (nvs_open(const char *name, ...)).
                if ns_arg.startswith("const ") or ns_arg.startswith("char"):
                    continue
                ns = _resolve_ns(ns_arg, text)
                if kind == "managed_components":
                    components.setdefault(rel.split("/")[1], set()).add(ns or ns_arg)
                    continue
                if ns is None:
                    # engine_glue.h erases each namespace of its list through a parameter.
                    if rel.endswith(GLUE) and ns_arg == "ns" and "erase_namespace" in text:
                        continue
                    unresolved.append(f"{rel}: {ns_arg}")
                    continue
                found.setdefault(ns, set()).add(rel)
    rep.data.setdefault("nvs", {})[label] = {
        "namespaces": {k: sorted(v) for k, v in found.items()},
        "components": {k: sorted(v) for k, v in components.items()},
    }
    rep.check(
        f"compile {label}: every NVS namespace in src/ is on the erase list {sorted(ERASE_NAMESPACES)}",
        set(found) <= ERASE_NAMESPACES and ERASE_NAMESPACES <= set(found) and not unresolved,
        f"found {dict((k, sorted(v)) for k, v in found.items())}; unresolved {unresolved}",
    )
    rep.check(
        f"compile {label}: ESP-IDF components open NVS only on the default partition (wiped as a whole)",
        not other_partition,
        f"components {dict((k, sorted(v)) for k, v in components.items()) or 'none'}" if not other_partition else f"other partitions: {other_partition}",
    )
    prefs = set()
    src = build_dir / "src"
    for path in src.rglob("*"):
        if path.is_file() and path.suffix in (".h", ".cpp"):
            for m in re.finditer(r"make_preference<", path.read_text(encoding="utf-8", errors="replace")):
                prefs.add(str(path.relative_to(src)).split("/")[0] if "/" in str(path.relative_to(src)) else path.name)
    defines = _defines(src) or set()
    rep.check(
        f"compile {label}: ESPHome preferences live in NVS namespace \"esphome\" (no RTC preference storage)",
        "USE_ESP32_RTC_PREFERENCES_STORAGE" not in defines,
        f"preferences used in {sorted(prefs)}",
    )


# Settings of the host's lv_conf.h that differ from the tablet's on purpose:
# one draw unit, the font the host has, the C library allocator (so
# AddressSanitizer sees every block), no LVGL log.
LV_HOST_ONLY = {"LV_DRAW_SW_DRAW_UNIT_CNT", "LV_FONT_DEFAULT", "LV_USE_LOG", "LV_USE_STDLIB_MALLOC"}
LV_DEFINE_RE = re.compile(r"^#define\s+(LV_\w+)\s+(.*?)\s*$", re.M)


def lv_conf_problems(host_text, build_text):
    """Settings both lv_conf.h files name with another value, apart from
    LV_HOST_ONLY: a host test could pass with what the tablet lacks."""
    host = dict(LV_DEFINE_RE.findall(host_text))
    build = dict(LV_DEFINE_RE.findall(build_text))
    if not host or not build:
        return ["an lv_conf.h has no LV_ settings"]
    return [f"{k} is {host[k]} on the host, {build[k]} on the tablet" for k in sorted(host) if k in build and k not in LV_HOST_ONLY and host[k] != build[k]]


def check_lv_conf_self_test(rep):
    host = _read_or_empty(ROOT / "engine_tests" / "host" / "lv_conf.h")
    cases = (
        ("the image widget on", host.replace("#define LV_USE_IMAGE 0", "#define LV_USE_IMAGE 1"), "LV_USE_IMAGE is 0 on the host, 1"),
        ("A8 layers on", host.replace("#define LV_DRAW_SW_SUPPORT_A8 0", "#define LV_DRAW_SW_SUPPORT_A8 1"), "LV_DRAW_SW_SUPPORT_A8 is 0 on the host, 1"),
        ("no settings", "", "no LV_ settings"),
    )
    missed = [name for name, build, want in cases if not any(want in p for p in lv_conf_problems(host, build))]
    rep.check(
        f"self-test: the --compile lv_conf.h rule fails a tablet build with the image widget or A8 layers the host lacks, and an empty file ({len(cases)} cases), and passes the host file against itself",
        bool(host) and not missed and not lv_conf_problems(host, host),
        f"not caught: {missed}",
    )


def check_lv_conf(build_dir, rep, label):
    problems = lv_conf_problems(_read_or_empty(ROOT / "engine_tests" / "host" / "lv_conf.h"), _read_or_empty(build_dir / "src" / "lv_conf.h"))
    rep.check(
        f"compile {label}: engine_tests/host/lv_conf.h has the tablet's LVGL settings (src/lv_conf.h), apart from {sorted(LV_HOST_ONLY)}",
        not problems,
        "; ".join(problems),
    )


def check_font_metrics(build_dir, rep, label):
    main = build_dir / "src" / "main.cpp"
    got = FONT_RE.findall(main.read_text(encoding="utf-8", errors="replace")) if main.is_file() else []
    want = harness_metrics(_read_or_empty(ROOT / HARNESS))
    pairs = [(int(h), int(h) - int(a)) for a, h in got]
    table = [(lh, bl) for _f, _px, lh, bl in want]
    rep.check(
        f"compile {label}: font line_height and base_line equal the host harness table ({HARNESS}, {len(table)} fonts)",
        len(table) == 14 and pairs == table,
        f"build {pairs}, harness {table}",
    )


def check_variant_src(build_dir, copy_dir, rep, label, entry):
    src = build_dir / "src"
    lock_ok = (src / LOCK).is_file() and (src / LOCK).read_bytes() == (copy_dir / LOCK).read_bytes()
    rep.check(f"compile {label}: src/{LOCK} is there and identical (includes resolve from {entry})", lock_ok)
    main = (src / "main.cpp").read_text(encoding="utf-8", errors="replace") if (src / "main.cpp").is_file() else ""
    rep.check(f"compile {label}: main.cpp includes {LOCK} and fast_display.h", f'#include "{LOCK}"' in main and '#include "fast_display.h"' in main)
    turned = re.findall(r"->set_rotation\(display::DISPLAY_ROTATION_(\d+)_DEGREES\)", main)
    rep.check(
        f"compile {label}: main.cpp sets the LVGL rotation to {ROTATION_DEFAULT} degrees ({ROTATION_SUB} reached the generated code)",
        turned == [ROTATION_DEFAULT],
        f"got {turned}",
    )
    for call in ("tab_lock::boot(", "tab_lock::poll(", "tab_lock::safe_mode(", "tab_glue::setup_window_closed()"):
        rep.check(f"compile {label}: main.cpp calls {call.rstrip('(')}", call in main)
    i_lock, i_safe = main.find("tab_lock::safe_mode("), main.find("should_enter_safe_mode(")
    rep.check(
        f"compile {label}: the safe mode lock is set up before the safe mode check (so it runs in safe mode)",
        0 <= i_lock < i_safe,
        f"tab_lock::safe_mode at {i_lock}, should_enter_safe_mode at {i_safe}",
    )
    defines = _defines(src)
    must, never = VARIANT_DEFINES[label]
    if defines is None:
        rep.check(f"compile {label}: defines.h found", False, str(src / "esphome/core/defines.h"))
    else:
        rep.check(
            f"compile {label}: defines {sorted(must)} and none of {sorted(never)}",
            must <= defines and not (never & defines),
            f"missing {sorted(must - defines)}, unwanted {sorted(never & defines)}",
        )
    # ota_lock.h keep_key() writes the YAML key where ESPHome's own firmware
    # reads a key from Home Assistant. Same place, or the copy is useless.
    api_cpp = src / "esphome" / "components" / "api" / "api_server.cpp"
    api_text = api_cpp.read_text(encoding="utf-8", errors="replace") if api_cpp.is_file() else ""
    slot = re.search(r"NOISE_PSK_PREF_HASH\s*=\s*(\d+)UL", api_text)
    rep.check(
        f"compile {label}: ESPHome keeps the API key in settings slot {KEY_SLOT}, the one keep_key() writes",
        bool(slot) and int(slot.group(1)) == KEY_SLOT and "make_preference<SavedNoisePsk>(NOISE_PSK_PREF_HASH, true)" in api_text,
        f"api_server.cpp has {slot.group(1) if slot else 'no NOISE_PSK_PREF_HASH'}",
    )
    has_url = MANIFEST_URL in main
    rep.check(
        f"compile {label}: {'has' if label != 'bench' else 'has no'} the update channel ({MANIFEST_URL})",
        has_url == (label != "bench"),
    )
    # The web page is web_server::WebServer. captive_portal brings in
    # web_server::WebServerOTAComponent (its firmware upload) in every variant;
    # tab_lock's UpdateGate guards that one.
    has_page = re.search(r"\bweb_server::WebServer\b", main) is not None
    rep.check(
        f"compile {label}: {'has the' if label == 'bench' else 'has no'} web page (web_server::WebServer)",
        has_page == (label == "bench"),
    )


def check_generated_lock(build_dir, engine, rep, label):
    """What the static lock checks read in the YAML, in the code ESPHome
    generated from it: the static checks could pass on YAML that ESPHome
    turns into something else."""
    src = build_dir / "src"
    main = (src / "main.cpp").read_text(encoding="utf-8", errors="replace") if (src / "main.cpp").is_file() else ""
    defines = _defines(src) or set()
    sdk_path = build_dir / f"sdkconfig.{build_dir.name}"
    sdk = sdk_path.read_text(encoding="utf-8", errors="replace") if sdk_path.is_file() else ""
    sdk_on = set(re.findall(r"^(CONFIG_\w+)=y$", sdk, re.M))
    # The empty password must not become a fixed password in the firmware:
    # ota_lock.h is the only one that sets it, at runtime.
    # (Line by line: main.cpp has lines of almost 1 MB, font data.)
    calls = [line.strip()[:120] for line in main.splitlines() if "set_auth_password(" in line]
    rep.check(f"compile {label}: main.cpp sets no update password (no set_auth_password; ota_lock.h sets it at runtime)", bool(main) and not calls, f"found {calls[:3]}")
    window = re.findall(r"provisioning\w*->set_timeout\((\d+)\);", main)
    rep.check(
        f"compile {label}: main.cpp sets the set-up window to {SETUP_WINDOW_MS} ms (provisioning set_timeout)",
        window == [str(SETUP_WINDOW_MS)],
        f"got {window}",
    )
    verify = re.findall(r"set_verify_ssl\((\w+)\)", main)
    if label in UPDATE_VARIANTS:
        rep.check(f"compile {label}: main.cpp checks the update server's certificate (set_verify_ssl(true) only)", verify == ["true"], f"got {verify}")
        rep.check(
            f"compile {label}: sdkconfig.{build_dir.name} has the certificate bundle on and no insecure TLS",
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE" in sdk_on and not ({"CONFIG_ESP_TLS_INSECURE", "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY"} & sdk_on),
            f"{sdk_path.name} {'missing' if not sdk else 'bundle ' + ('on' if 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE' in sdk_on else 'off') + ', insecure ' + str(sorted({'CONFIG_ESP_TLS_INSECURE', 'CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY'} & sdk_on))}",
        )
    else:
        rep.check(f"compile {label}: main.cpp makes no HTTPS requests (no set_verify_ssl)", not verify, f"got {verify}")
    rep.check(
        f"compile {label}: defines.h has USE_SAFE_MODE_CALLBACK (safe_mode on_safe_mode, where tab_lock::safe_mode runs)",
        "USE_SAFE_MODE_CALLBACK" in defines,
    )
    # The same (macro, number) pairs as Engine.yaml, not only the same names:
    # a check ESPHome turned into another number would pass the wrong copy.
    asserts = {(macro, number) for macro, (number, _msg) in files_asserts(engine).items()}
    found = [(m.group(1), int(m.group(2))) for m in FILES_ASSERT_RE.finditer(main)]
    rep.check(
        f"compile {label}: main.cpp holds Engine.yaml's three files-version checks with the same numbers",
        len(asserts) == 3 and len(found) == 3 and set(found) == asserts,
        f"Engine.yaml {sorted(asserts)}, main.cpp {sorted(found)}",
    )


# Compiler warnings from the package's own code fail --compile: engine/,
# engine_glue.h, ota_lock.h and the lambdas of Engine.yaml and the entry
# files (ESPHome puts a #line with the YAML file in front of each lambda, so
# GCC names the YAML file and line). Warnings from elsewhere (ESPHome, the
# IDF, the protected TAB-1 files) are listed, not failed. All of the
# package's code builds in main.cpp, so the check deletes main.cpp.obj before
# each compile and makes sure the log shows it built again: a build with
# nothing to do would print no warnings at all.
WARNING_LINE_RE = re.compile(r"(?P<path>[^\s:]+):(?P<line>\d+):(?P<col>\d+): warning: (?P<text>.*)")
OWN_WARNING_FILE_RE = re.compile(
    r"(?:^|/)(?:src/engine/[^/]+\.h|engine/tab_[^/]+\.h|engine_glue\.h|ota_lock\.h"
    r"|Engine\.yaml|TAB-1-engine\.yaml|TAB-1-engine-bench\.yaml|tab1-adopted\.yaml)$"
)
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
MAIN_OBJ = Path("build/esp-idf/src/CMakeFiles/__idf_src.dir/main.cpp.obj")  # under the build folder
MAIN_BUILT = "Building CXX object esp-idf/src/CMakeFiles/__idf_src.dir/main.cpp.obj"


def compile_warnings(log_text):
    """(own, other): the distinct compiler warnings in a build log, as
    `file:line:col: warning: text`, split into the package's and the rest."""
    own, other = [], []
    for line in re.split(r"[\r\n]+", ANSI_RE.sub("", log_text)):
        m = WARNING_LINE_RE.search(line)
        if not m:
            continue
        item = f"{m.group('path')}:{m.group('line')}:{m.group('col')}: warning: {m.group('text').strip()}"
        target = own if OWN_WARNING_FILE_RE.search(m.group("path")) else other
        if item not in target:
            target.append(item)
    return own, other


def warning_gate_problem(log_text):
    """Why a compile log fails the warning gate, or ""."""
    if MAIN_BUILT not in log_text:
        return "main.cpp was not built again, so its warnings cannot be seen"
    own, _other = compile_warnings(log_text)
    return "; ".join(own)


# (what, log, passes). The first is the warning GCC gave in round 9.
WARNING_GATE_SELF_TESTS = (
    ("the round 9 warning in tab_core.h",
     f"[612/1203] {MAIN_BUILT}\r/w/data-prod/build/apollo-tab-1/src/engine/tab_core.h:749:28: warning: enumerated and non-enumerated type in conditional expression [-Wextra]\n", False),
    ("a lambda in Engine.yaml",
     f"{MAIN_BUILT}\n/w/ESPHome/Engine.yaml:412:7: warning: unused variable 'x' [-Wunused-variable]\n", False),
    ("a lambda in the adoption's device file",
     f"{MAIN_BUILT}\n/w/ESPHome/adopt/tab1-adopted.yaml:30:3: warning: unused variable 'y' [-Wunused-variable]\n", False),
    ("ota_lock.h, with colour codes",
     f"{MAIN_BUILT}\n\x1b[01m\x1b[K/w/src/ota_lock.h:88:5:\x1b[m\x1b[K \x1b[01;35m\x1b[Kwarning: \x1b[m\x1b[Kcomparison of integer expressions [-Wsign-compare]\n", False),
    ("engine_glue.h", f"{MAIN_BUILT}\n/w/src/engine_glue.h:12:1: warning: no return statement\n", False),
    ("main.cpp not built again (nothing to do)", "ninja: no work to do.\n", False),
    ("a clean build", f"[612/1203] {MAIN_BUILT}\n[1203/1203] Linking CXX executable apollo-tab-1.elf\n", True),
    ("a warning in ESPHome's own code",
     f"{MAIN_BUILT}\n/w/src/esphome/components/api/api_connection.cpp:10:2: warning: unused parameter [-Wunused-parameter]\n", True),
    ("a warning in the protected Display.yaml",
     f"{MAIN_BUILT}\n/w/ESPHome/Display.yaml:120:9: warning: unused variable 'z'\n", True),
)


def check_warning_gate_self_test(rep):
    wrong = [what for what, log, passes in WARNING_GATE_SELF_TESTS if (warning_gate_problem(log) == "") != passes]
    rep.check(
        f"self-test: a compiler warning from engine/, engine_glue.h, ota_lock.h or a lambda of the package fails --compile, as does a build that did not rebuild main.cpp ({len(WARNING_GATE_SELF_TESTS)} cases)",
        not wrong,
        f"wrong: {wrong}" if wrong else "",
    )


def run_compile(esphome, copy_dir, work, env, rep):
    seed = data_dir_for(copy_dir) / "build" / (load_yaml(copy_dir / PROD).get("substitutions") or {}).get("name", "apollo-tab-1")
    seeded = (seed / "managed_components").is_dir() and (seed / "dependencies.lock").is_file()
    print(f"note  compile: seed build cache {seed}{'' if seeded else ' (none: components come from the network or the local mirror)'}", flush=True)
    for label, entry, folder in VARIANTS:
        data = work / folder
        data.mkdir(parents=True, exist_ok=True)
        venv = dict(env, ESPHOME_DATA_DIR=str(data))
        seed_offline_caches(copy_dir, data)
        cfg = load_yaml(copy_dir / entry)
        name = (cfg.get("substitutions") or {}).get("name") or (load_yaml(copy_dir / PROD).get("substitutions") or {}).get("name")
        build_dir = data / "build" / name
        if seeded and not (build_dir / "managed_components").is_dir():
            # First build in this data folder: ESPHome wipes the build folder
            # once (no storage file yet), so generate first, then bring in the
            # resolved components; the compile then reuses them.
            log = work / f"generate-{label}.log"
            code = run_esphome(esphome, "compile", entry, copy_dir, log, venv, 900, extra=("--only-generate",))
            if not rep.check(f"compile {label}: esphome compile --only-generate {entry}", code == 0, f"exit {code}, log {log}\n      {_tail(log)}" if code else f"log {log}"):
                continue
            shutil.copy2(seed / "dependencies.lock", build_dir / "dependencies.lock")
            shutil.copytree(seed / "managed_components", build_dir / "managed_components", symlinks=True)
        log = work / f"compile-{label}.log"
        (build_dir / MAIN_OBJ).unlink(missing_ok=True)  # main.cpp builds again: its warnings show
        print(f"....  esphome compile {entry} ({label}, log: {log})", flush=True)
        code = run_esphome(esphome, "compile", entry, copy_dir, log, venv, 5400)
        text = log.read_text(encoding="utf-8", errors="replace")
        ok = code == 0 and "Successfully compiled program" in text
        if not rep.check(
            f"compile {label}: esphome compile {entry} for the P4",
            ok,
            f"log {log}" if ok else f"exit {code}, log {log}\n      {_tail(log, 40)}",
        ):
            continue
        own, other = compile_warnings(text)
        problem = warning_gate_problem(text)
        rep.check(
            f"compile {label}: no compiler warning from engine/, engine_glue.h, ota_lock.h or the package's lambdas (main.cpp built again)",
            not problem,
            problem or (f"{len(other)} warning(s) elsewhere, not the package's: " + "; ".join(other[:5]) if other else "no warnings"),
        )
        rep.data.setdefault("warnings", {})[label] = {"own": own, "other": other}
        sizes = parse_sizes(text)
        bins = {}
        for p in sorted((build_dir / "build").glob("*.bin")):
            bins[p.name] = p.stat().st_size
        sizes["files"] = bins
        rep.data.setdefault("sizes", {})[label] = sizes
        detail = []
        if "app" in sizes:
            a = sizes["app"]
            detail.append(f"app {a['binary']} bytes, {a['free_percent']}% of {a['partition']} free")
        if "ram" in sizes:
            detail.append(f"RAM {sizes['ram']['used']}/{sizes['ram']['total']} ({sizes['ram']['percent']}%)")
        if "flash" in sizes:
            detail.append(f"flash {sizes['flash']['used']}/{sizes['flash']['total']} ({sizes['flash']['percent']}%)")
        rep.check(f"compile {label}: firmware size reported", "app" in sizes and "ram" in sizes and "flash" in sizes, "; ".join(detail))
        check_generated_src(build_dir, copy_dir, rep, label)
        check_variant_src(build_dir, copy_dir, rep, label, entry)
        check_generated_lock(build_dir, load_yaml(copy_dir / ENGINE), rep, label)
        check_nvs(build_dir, rep, label)
        check_font_metrics(build_dir, rep, label)
        check_lv_conf(build_dir, rep, label)
        check_lvgl_commit(build_dir, rep, label)
        if label == "adopt":
            run_stale_copies(esphome, copy_dir, work, venv, build_dir, entry, rep)


# Copies from before the files version (no number, no check of their own) in
# the adopted tablet's config folder, where ESPHome looks first. ESPHome runs
# with the relative path of the device file, as ESPHome Builder does.
STALE_COPIES = ((LOCK, "TAB1_FILES_LOCK"), (GLUE, "TAB1_FILES_GLUE"))


def run_stale_copies(esphome, copy_dir, work, venv, build_dir, entry, rep):
    for name, macro in STALE_COPIES:
        stale = copy_dir / Path(entry).parent / name
        stale.write_text(pre_version((copy_dir / name).read_text(encoding="utf-8"), macro), encoding="utf-8")
        log = work / f"compile-stale-{name}.log"
        print(f"....  esphome compile {entry} with a pre-version {name} beside it (log: {log})", flush=True)
        try:
            code = run_esphome(esphome, "compile", entry, copy_dir, log, venv, 3600)
        finally:
            stale.unlink(missing_ok=True)
        text = log.read_text(encoding="utf-8", errors="replace")
        built = build_dir / "src" / name
        used = built.is_file() and f"#define {macro}" not in built.read_text(encoding="utf-8", errors="replace")
        line = re.search(FILES_ERROR_LINE.format(name=re.escape(name)) + r"[^\n]*", text)
        undeclared = re.search(rf"'{macro}' was not declared|undeclared identifier '{macro}'", text)
        rep.check(
            f"compile adopt: {name} from before the files version, beside the device file, stops `esphome compile {entry}` with an error naming it and saying to delete it",
            code != 0 and used and line is not None and not undeclared,
            (line.group(0)[:200] if line else f"exit {code}, old copy in the build: {used}, log {log}\n      {_tail(log, 30)}")
            + (f"; also says {undeclared.group(0)}" if undeclared else ""),
        )


# ---------------------------------------------------------------------------
STATIC_MODE_HELP = """\
With no flags (CI runs `python3 -B tools/check_engine_package.py`) only the
static checks run: Python 3.12 or later with the standard library only (no
PyYAML or other package to install: the script reads the YAML itself), no
network, no ESPHome, no compiler. They include every check that needs no
toolchain: the files-hash guard (a changed engine file at the same files
number fails, and so does a merge of two branches that raised it to the
same number, through the `all` hash; raise the number, then
--record-files), the actions, events
and fonts against the contract, both entry files and the update lock, the
screen's way up (the screen_rotation substitution, the fast path of
fast_display.h for 90 and 270, and its two host tests in
engine_tests/run.sh), the
release workflow, the waiting-screen words in docs/engine.md (and no line
of it starting with ">"), the waiting-screen words against the contract's
screens.json, and the self-tests of the YAML reader, the files rule, the
restart rule, the docs check, the contract check, the contract comparison
and the --compile warning gate. With --peer-contract the contract folder is
also compared with the integration's copy, file for file. Where PyYAML happens to be installed, the YAML of every file read is
also compared with PyYAML's.

--host needs a C++17 compiler; --config and --compile need ESPHome; with
--compile, any compiler warning from engine/, engine_glue.h, ota_lock.h or
the package's lambdas fails the run."""


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        epilog=STATIC_MODE_HELP,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--contract", default=str(ROOT / "engine_tests" / "contract"), help="contract folder (contract.json, icons.json)")
    parser.add_argument(
        "--peer-contract",
        help="the integration's contract folder, or a TAB-Dashboard checkout (its tests/contract): fail unless it is the same as --contract, file for file",
    )
    parser.add_argument("--engine-dir", default=str(ROOT / "engine"), help="engine headers to check and build with (default: engine/)")
    parser.add_argument("--host", action="store_true", help="build and run the flash queue host test")
    parser.add_argument("--config", action="store_true", help="run esphome config in a work copy")
    parser.add_argument("--compile", action="store_true", help="also run esphome compile for production, bench and an adoption")
    parser.add_argument("--work", help="work folder outside this repository (default: a new temporary folder)")
    parser.add_argument("--offline", action="store_true", help="block downloads in ESPHome runs and reuse local caches")
    parser.add_argument("--esphome", help="esphome command (default: esphome on PATH)")
    parser.add_argument("--report", help="write the results as JSON to this file")
    parser.add_argument(
        "--record-files",
        action="store_true",
        help="write the SHA-256 of the files the files version covers, and their `all` hash, into engine/tab_files.h (only after raising the number), then stop",
    )
    args = parser.parse_args()

    if args.record_files:
        problem = record_files()
        if problem:
            sys.exit(problem)
        return

    contract_dir = Path(args.contract)
    if not (contract_dir / "contract.json").is_file():
        sys.exit(f"No contract.json in {contract_dir}. Pass --contract with the contract folder.")
    contract, icons = gen.load_contract(str(contract_dir))
    engine_dir = Path(args.engine_dir)
    substitute = engine_dir.resolve() != (ROOT / "engine").resolve()

    rep = Report()
    print(f"Checking {ROOT} against {contract_dir}", flush=True)
    yaml_self_test(rep)
    try:
        engine = load_yaml(ROOT / ENGINE)
        prod = load_yaml(ROOT / PROD)
        bench = load_yaml(ROOT / BENCH)
        tab1 = load_yaml(ROOT / TAB1)
        check_package(engine, rep)
        check_display(rep)
        check_host_tests(rep)
        check_lvgl_pin(rep)
        check_proto_high(rep)
        check_actions(engine, contract, rep)
        check_events(engine, contract, rep)
        check_fonts(engine, contract, icons, contract_dir, rep)
        check_entries(prod, bench, tab1, rep)
        check_lock_header(rep)
        check_hotspot_name(prod, bench, rep)
        check_docs_waiting(prod, rep)
        check_screens_contract(prod, bench, contract_dir, rep)
        check_contract_peer(contract_dir, args.peer_contract, rep)
        check_files_version(engine, rep)
        check_release(prod, bench, rep)
        check_files(engine_dir, rep, substitute)
        check_warning_gate_self_test(rep)
        check_lv_conf_self_test(rep)
        yaml_cross_check(rep)
    except YamlError as err:
        sys.exit(f"FAIL  cannot read the YAML: {err}")

    work = None
    if args.host or args.config or args.compile:
        work = Path(args.work) if args.work else Path(tempfile.mkdtemp(prefix="tab1-engine-check-"))
        if _inside(work, _repo_top()):
            sys.exit(f"--work must be outside the repository ({_repo_top()}); ESPHome writes build folders there.")
        work.mkdir(parents=True, exist_ok=True)
        print(f"Work folder: {work}", flush=True)

    if args.host:
        run_host_test(work, rep)

    if args.config or args.compile:
        esphome = args.esphome or shutil.which("esphome")
        if not esphome:
            rep.check("esphome: command found", False, "esphome is not on PATH; pass --esphome")
        else:
            copy_dir = prepare_copy(work, engine_dir, rep)
            env = esphome_env(args.offline)
            if args.offline:
                seed_offline_caches(copy_dir)
            run_config(esphome, copy_dir, work, env, rep)
            if args.compile:
                run_compile(esphome, copy_dir, work, env, rep)

    failed = [i for i in rep.items if not i["ok"]]
    print(f"\n{len(rep.items) - len(failed)} passed, {len(failed)} failed", flush=True)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump({"ok": rep.ok, "checks": rep.items, **rep.data}, f, indent=1)
    sys.exit(0 if rep.ok else 1)


if __name__ == "__main__":
    main()
