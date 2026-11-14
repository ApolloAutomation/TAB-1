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

#define TAB1_FILES_ENGINE 14

// files 14 all 4079cf815f4d8788ff793fb28c7aea6cf6e1030caec101cb9436fe2f228d33cb
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 007c7ffb5aa5cfbb11c577c1c684c96f1efc9b39ca61c8a56854fd05eb10fced engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 8b5855e26360e771b0c54f375345fb2a30813e08f0441a27121a446ffdcd8ff7 engine/tab_model.h
// sha256 247defe68b034fa9b58cdc79e854bfd4f207844631f46c7ee0ab31a10059d901 engine/tab_state.h
// sha256 d724ae73422a9589bbcb3bb2f1147135206c7371cc968d4429237891419472c8 engine/tab_ui.h
// sha256 bb16ecda08f4c791720b4801e6ebc70b79714307143b659c5346c7fd086df57c engine_glue.h
// sha256 c6ca66ddfd5685f31ec8e7c152ec55601b06e7eebda003040f651d258c5b70c5 ota_lock.h

#endif  // TAB_FILES_H
