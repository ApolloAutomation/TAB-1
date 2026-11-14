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

#define TAB1_FILES_ENGINE 10

// files 10 all 5554df6a4ce87676e6066bf38888d7e18ff80665e839f650d2bcacb4344891cf
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 59307bc582428611d9ddf1a6086bcaa079bd4c00a39610128ad6b043fffe9021 engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 b116456eeb7985fdb871c8ea1300de10678e09a97544dd947e21d3087112d090 engine/tab_model.h
// sha256 87c29969b75e82204c14d725823497693764c9b4541586e238e8dcb8d6ef749c engine/tab_state.h
// sha256 4362c86e399ccb224789f0739bbe568528946adcaf47efbaa0b55c53055b9bf8 engine/tab_ui.h
// sha256 5d427db4241b1ecef7779160d161ed77e0fe96279fc53524980f1506934d938a engine_glue.h
// sha256 320a3788435b7b5d9681791d3511275e275446ef6d499f1527ce82dcf30d33f3 ota_lock.h

#endif  // TAB_FILES_H
