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

#define TAB1_FILES_ENGINE 19

// files 19 all 84073399f4f04893ab4d2838c86b860041a539513a75c3d253b938ecebaf873c
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 6f58b6215c8259b57b99c4aba776cc2b4065bbe12815b04f734487312b52ae7b engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 78098e580cfcde269826463b0fd2c395a7f405076bba9ae52f6e9c22f66b28fe engine/tab_model.h
// sha256 9a09e5a49429e2d1c4113a757b377aefcdd55a5bf982eb6cf8283cc692fdfc28 engine/tab_state.h
// sha256 0ea078ef3d7ee910d9f72759a61c5e3e20edc6f806ef7cd26186703786392f17 engine/tab_ui.h
// sha256 d52e399295026bb59b0138ec6d739367831e0cefc2ee4d5a96d2770bd4cadbdc engine_glue.h
// sha256 3e13dce6191d666c75c76fe0aa070d2a0645c8fad6915e94642b9deb9d38fce4 ota_lock.h

#endif  // TAB_FILES_H
