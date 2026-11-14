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

#define TAB1_FILES_ENGINE 11

// files 11 all 3686efa8024a728eba3b86d1e5f106a98b7ce61742b290830958bb76143f86d5
// sha256 bcebc7ce4497dea1f23c100c75973532ba4ccbc61001834ec86eaf62e3339404 engine/tab_colour.h
// sha256 799c35bc110f0f2907d8ddddaacaab248eeeafcc0b0e36a1cbf4f22ca1c04faf engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 17815c34c37a5bc78fcc88e2c378f5273b89a6c2ff459f6c1d6840b74ec5ba2d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 792ebd08135ba7a67f8731be1a3dc072d3139c16b424a01da5c7eaa865907a34 engine/tab_linear.h
// sha256 8532f2d50c820883b1d4d6c7d4a84a22cf46bf3ac877c7827d5225aa0a9ee89c engine/tab_model.h
// sha256 87c29969b75e82204c14d725823497693764c9b4541586e238e8dcb8d6ef749c engine/tab_state.h
// sha256 a3a1c6dfb48395707a622c3071f88d9e6f84b8ad77c8c518794c4970e509c33a engine/tab_ui.h
// sha256 d805e6d93170b2aea423d652dad8cc72b49e18ffbdf16d3bc1518b762142b33f engine_glue.h
// sha256 677765100361a29e1b7424029d51d9bd723097a234b364caac436cac64020765 ota_lock.h

#endif  // TAB_FILES_H
