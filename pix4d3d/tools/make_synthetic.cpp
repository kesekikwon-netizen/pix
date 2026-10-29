// 합성 3MX 만들기: asec-make-synthetic <출력폴더> [격자간격m] [--no-bushes]
#include <cstdio>
#include <cstdlib>
#include <string>
#include "synth.hpp"
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: asec-make-synthetic <dir> [leafSpacing]\n"); return 2; }
    asec::synth::Params p;
    p.dir = asec::fs::u8path(argv[1]);
    p.bushes = true;
    p.srs = "EPSG:5186+5193";  // 복합: KGD2002 / Central Belt 2010 + KVD1964 height (참고: 5711 은 AHD height, 호주)
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-bushes") p.bushes = false; else p.leafSpacing = std::atof(argv[i]);
    }
    asec::fs::path f; std::string err;
    if (!asec::synth::write(p, &f, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("%s\n", f.u8string().c_str());
    return 0;
}
