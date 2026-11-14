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

#define TAB1_FILES_ENGINE 8

// files 8 all 55ca3f9773caefb49e80b67415e102560408015f5c1ed23dec89cb07e4885eb5
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 66aba681c545c7390b01c4a1b1f7ff9617adbc5273de8b8958c59412d3dcde5c engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 ed3812581ba81401b375a6e33bbdead909a5af86c34e525e1f5d6545d4af4d11 engine/tab_model.h
// sha256 e83aabf2acb1054c9bb35da65632b3585cd8f53b3b331019a412fda9b601a136 engine/tab_state.h
// sha256 ef48a2b8f1e6752b3b8e0340dcc3cbc218bf09520dc3af04722c606488656c21 engine/tab_ui.h
// sha256 f39464ce8aefad4871957319d898cb72efdd077cf92315f9d29fdde4646d92bf engine_glue.h
// sha256 7216bb6ff7438cddd3675d660b7bf1e807b82b825423014b81bf50585315ee80 ota_lock.h

#endif  // TAB_FILES_H
