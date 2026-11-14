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

#define TAB1_FILES_ENGINE 15

// files 15 all d5ab78d7a18e8dc0f06950c40668562ddf16c15fb53287e952ae49b33a0b1787
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 1808e9f08bd1d97aa030a258ec13b3e225e6a3554d712777ab03af4d2d3bb4b4 engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 2016b33a15da1f67ad98e3ed405242d7c3a071747248428c94e1685dadd14d99 engine/tab_model.h
// sha256 e27fb88ef362010ca965cdbb046771b7783e3393adf8070e931033d97e5b6c3c engine/tab_state.h
// sha256 62e29b69a3854e321053ca830fda45f45fab344b023d603ff00300d19e2a8e2a engine/tab_ui.h
// sha256 5ad4d0fabf757fbef394af85ef5ad57bc664d5f1495fc185d5753facee6617a5 engine_glue.h
// sha256 f6a29f649342de1439476e22cf8293db44af34a2734549bd31e476736b696ac2 ota_lock.h

#endif  // TAB_FILES_H
