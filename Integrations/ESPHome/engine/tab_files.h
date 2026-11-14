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

#define TAB1_FILES_ENGINE 6

// files 6 all 40bf4c07132f0190265911a5b49627ffc25a2867dd2f221216025cd82f423f87
// sha256 b072b84fab051357ff5368a38e64cc3b8ee02c4d2f409b5fa8604efc66f934a2 engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 5983d068c32103bbb174d954d6e633e987bd5a15725532ed28a1f73aaa61cce8 engine/tab_model.h
// sha256 328a87ea5fea51fcbf5d1412d1aeb1dccf230bfbc0318e31220b1fafafbe89c4 engine/tab_state.h
// sha256 49dad08fff835b409b13ee7d549c8373fb115074cd2a50049c9c0cf11e7a130e engine/tab_ui.h
// sha256 32ef04f95e4bfe65f3a2f57804b84fb272ab660d21c2d5dfc4d060b584b35e2d engine_glue.h
// sha256 e294206369394eed1faf0dcde0d26abdb337cfdb6808eccf88b5947f2fefdac2 ota_lock.h

#endif  // TAB_FILES_H
