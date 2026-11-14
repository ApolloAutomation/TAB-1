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

#define TAB1_FILES_ENGINE 7

// files 7 all fb1f1ee56580feb025cf76066431eda37a164dc8c174ab5624ea06f980e9cd89
// sha256 5c2123b9f2bf2a0aeee0682a86167153145b9c14620f0babc3e5f0a1ecd4d6dd engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 6a6dc2e86e6dd02d9981c427e2d404c7693547722ff28992a866f7ff1c1b8ef4 engine/tab_model.h
// sha256 37c96d4c3a2734c7ffa84304be410f2778750290a3716296ef257a569fb1a6a0 engine/tab_state.h
// sha256 a3071a7ac7b16f8e936d34160290c3a178f25dc361cbed019eb0c2b2e8841963 engine/tab_ui.h
// sha256 936d87050a88005a47b99b4cd2482b40c038455d79dc1b01dc1520bd601df0a8 engine_glue.h
// sha256 9329e6181449d59be7659fd7604b441e1ae919e8602f908e70ae0efff4227dc7 ota_lock.h

#endif  // TAB_FILES_H
