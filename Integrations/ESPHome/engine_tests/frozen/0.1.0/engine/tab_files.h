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

#define TAB1_FILES_ENGINE 5

// files 5 all 46a70df2216d5a68487c7fdde57ddbe59fb578edf1958eb8093a3987fe5b03c1
// sha256 b072b84fab051357ff5368a38e64cc3b8ee02c4d2f409b5fa8604efc66f934a2 engine/tab_core.h
// sha256 87451f63db1e59609f764fc7f754f8a12b269e7ce92627a4cf9bba0c6b982b5b engine/tab_crypto.h
// sha256 8e75e477da5d4e187dd5bde1865fae6f88e5428ab24df16a5a08c568a42cfa4d engine/tab_engine.h
// sha256 17100ec9f3bd1b52ab2d34d3db096b476c85edff4d97cd4a299cfed8d2440a22 engine/tab_json.h
// sha256 5983d068c32103bbb174d954d6e633e987bd5a15725532ed28a1f73aaa61cce8 engine/tab_model.h
// sha256 328a87ea5fea51fcbf5d1412d1aeb1dccf230bfbc0318e31220b1fafafbe89c4 engine/tab_state.h
// sha256 183b941ed76e2e4f500bf58e024c268a19212b51707e343c7749db1f27121294 engine/tab_ui.h
// sha256 e565fd2ca5183ca76b5336395943f9f0b6a4fe0576ff461112ad6b09079a8e46 engine_glue.h
// sha256 22ebbc325db41b5032396124d386633f389ad9b3bcacf705822b918c4717d333 ota_lock.h

#endif  // TAB_FILES_H
