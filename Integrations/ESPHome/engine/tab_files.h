// The version of the engine's files in this folder.
//
// ESPHome looks for `includes:` in the ESPHome config folder before the
// package's own folder, so an old or foreign engine/ folder there would be
// built instead of this one without a word. Engine.yaml (which always comes
// from the package) stops such a build with an #error on this number, and so
// do engine_glue.h and ota_lock.h with their own numbers (TAB1_FILES_GLUE,
// TAB1_FILES_LOCK). Plain C preprocessor only: ota_lock.h includes it without
// LVGL.
//
// Raise all four numbers together (here, engine_glue.h, ota_lock.h and the
// three checks in Engine.yaml) whenever any file below changes, then run
//   python3 -B tools/check_engine_package.py --record-files
// to write the new SHA-256 lines. tools/check_engine_package.py fails while
// the numbers differ, while a file's SHA-256 differs from its line below at
// the same number, and while the `all` hash on the `// files N` line is not
// the SHA-256 of the lines below it. Two branches that both raise the number
// meet in a git conflict on that line: then raise the number once more and
// record again, never pick one side.
#ifndef TAB_FILES_H
#define TAB_FILES_H

#define TAB1_FILES_ENGINE 12

// files 12 all 355417519e4e534e19486ce510356086bbea138a0cbedf1f0362deba5e61d1b9
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 52fbc561c269485c2ac9870b1b450cf4e5f23b5e3c0daaffde09fd77187aa28c engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 2792cad4c535d25a383077694c359a7535b4fe346e6cecb4d3eb2fb0a2b614ea engine/tab_model.h
// sha256 87c29969b75e82204c14d725823497693764c9b4541586e238e8dcb8d6ef749c engine/tab_state.h
// sha256 2307981b392b95172b859aff4c8eecc7db71ed50baab8c7a7340fa1100bbba02 engine/tab_ui.h
// sha256 157a6f11b794638340f1614bf813e0b6289c18615a554bc1dd0c50a1b0dc62e6 engine_glue.h
// sha256 7462e08b7a7ea06afe44d69d289007783bbffcff5ed9b8c6beb43197461f6ca6 ota_lock.h

#endif  // TAB_FILES_H
