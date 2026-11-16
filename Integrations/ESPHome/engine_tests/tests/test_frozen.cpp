// The second test program: the frozen sources of engine 0.1.0
// (engine_tests/frozen/0.1.0, copied once from dd3e379, never shipped) draw
// the frames of frames.h. run.sh builds it with the same compiler, flags,
// LVGL and fonts as test_engine, runs it first, and test_engine's
// v1_pixels_unchanged compares its own frames with these byte for byte.
//
//   test_frozen <contract dir> <text ttf> <icon ttf> <frames dir>
//
// Writes <frames dir>/<name>.raw for each frame and <frames dir>/frames.txt
// with the names in order. Exit code 0 only when the whole run worked.
//
// Built with -I frozen/0.1.0/host and -I frozen/0.1.0/engine before host/,
// so harness.h, fake_platform.h and the engine are those of 0.1.0.
#include "frames.h"
#include "host_fonts.h"

#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr, "usage: %s <contract dir> <text ttf> <icon ttf> <frames dir>\n", argv[0]);
    return 2;
  }
  const std::string cdir = argv[1], dir = argv[4];
  const std::string mk = "rm -rf '" + dir + "' && mkdir -p '" + dir + "'";
  if (system(mk.c_str()) != 0) return 2;

  hh::init();
  hh::g_each_ms = fake::pump;
  size_t tn = 0, in = 0;
  uint8_t *text = hh::load_file(argv[2], &tn);
  uint8_t *icon = hh::load_file(argv[3], &in);
  // The fonts of engine 0.1.0: six text sizes, one digits size, four icon
  // sizes, each with the contract's characters, as test_engine's fonts.
  const tj::Value contract = tj::load(cdir + "/contract.json");
  const std::vector<uint32_t> text_ranges = hf::text_ranges(contract), digit_ranges = hf::digit_ranges(contract);
  if (text_ranges.empty() || digit_ranges.empty()) {
    fprintf(stderr, "contract.json has no fonts.text_glyphs or fonts.digits_glyphs\n");
    return 2;
  }
  tab::Fonts fonts;
  const int text_px[6] = {14, 15, 16, 18, 20, 34}, icon_px[4] = {24, 28, 48, 72};
  for (int i = 0; i < 6; i++) fonts.text[i] = hh::limited_font(hh::ttf_font(text, tn, text_px[i], "text"), text_ranges);
  fonts.digits = hh::limited_font(hh::ttf_font(text, tn, 48, "digits"), digit_ranges);
  for (int i = 0; i < 4; i++) fonts.icon[i] = hh::ttf_font(icon, in, icon_px[i], "icons");

  std::string names;
  int count = 0;
  bool written = true;
  const std::string err = frames::run(cdir, fonts, [&](const std::string &name) {
    written = frames::write(dir + "/" + name + ".raw") && written;
    names += name + "\n";
    count++;
  });
  FILE *f = fopen((dir + "/frames.txt").c_str(), "w");
  if (f) {
    fputs(names.c_str(), f);
    fclose(f);
  }
  if (!err.empty() || !written || !f || count == 0) {
    fprintf(stderr, "engine 0.1.0: %s\n", !err.empty() ? err.c_str() : "no frame, or a frame could not be written");
    return 1;
  }
  printf("engine 0.1.0 drew %d frames into %s\n", count, dir.c_str());
  return 0;
}
