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

#define TAB1_FILES_ENGINE 16

// files 16 all 62ffda30563ed8b93a89c4012d680b685fa700752f8993f55c007dcf6166540f
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 416abbefc09fb8336498a2d9825d7d7561b1eccd97aef7f196746a2762692dee engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 78098e580cfcde269826463b0fd2c395a7f405076bba9ae52f6e9c22f66b28fe engine/tab_model.h
// sha256 747e70a2198af8d7323bad7a21bd0fa8dc70bcf09baaa2068ea34e3d9a2e1735 engine/tab_state.h
// sha256 f9baa31d53d91c1356a0089c26cabecd16b02462a7250c56e998b73c5427216c engine/tab_ui.h
// sha256 8b7a9bfedce8f80686d8474cb8d37760755368da3b5c979f26bb548492d833e9 engine_glue.h
// sha256 83345b2c02af74409050792bef66b7816250ea9f9de21b8ca1e903b3795426be ota_lock.h

#endif  // TAB_FILES_H
