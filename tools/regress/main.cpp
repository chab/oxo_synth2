// regress: engine regression check. Renders every .sy1 given (files or
// folders, recursive) through a fixed phrase and compares with a baseline.
//   make -C tools/regress
//   build/regress/regress-<arch> save  <baseline dir> <patches...>   (before a change)
//   build/regress/regress-<arch> check <baseline dir> <patches...>   (after)
// check: "identical" (bit-exact) or the difference's level: residual RMS
// relative to the patch's RMS, its peak (dBFS), the worst 50 ms window
// (relative to that window, floor -120 dBFS). Exit 1 if a patch differs,
// unless every residual is under --tolerance <dB> (e.g. -100).
// Baselines are per architecture (FMA): build the tool with -arch x86_64
// for a Rosetta baseline (make ARCH=x86_64). Paths are made absolute; an
// unreadable patch or a missing baseline file is an error.
#include "../../EngineCore/Jobs.hpp"
#include "../../EngineCore/Sy1Codec.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

using namespace oxo::s2;
namespace fs = std::filesystem;

// The phrase: a held chord with the mod wheel, bend and pressure moving,
// staccato notes stealing / reusing voice slots, a legato line (mono /
// portamento patches), then the tails.
static Job Phrase(const PatchValues &values) {
    Job job;
    job.length = 6.0, job.preroll = 0;
    for (int k = 0; k < kParamCount; ++k) job.natives.emplace_back(k, values[k]);
    auto note = [&](double on, double off, int key, int vel) {
        job.events.push_back({on, 0x90, key, vel});
        job.events.push_back({off, 0x80, key, 0});
    };
    for (int key : {48, 55, 60, 64}) note(0.0, 1.6, key, 100);
    for (int k = 0; k <= 20; ++k) job.events.push_back({0.4 + k * 0.05, 0xB0, 1, k * 127 / 20});
    job.events.push_back({1.5, 0xB0, 1, 0});
    job.events.push_back({1.0, 0xE0, 12000 & 0x7F, 12000 >> 7});
    job.events.push_back({1.4, 0xE0, 0, 64});
    job.events.push_back({1.0, 0xD0, 64, 0});
    job.events.push_back({1.5, 0xD0, 0, 0});
    for (int k = 0; k < 12; ++k) note(2.0 + k * 0.1, 2.06 + k * 0.1, 60 + k * 5 % 17, 40 + k * 7);
    const int line[] = {52, 55, 59, 62};
    for (int k = 0; k < 4; ++k) note(3.4 + k * 0.3, 3.72 + k * 0.3, line[k], 90);
    return job;
}

static bool ReadText(const fs::path &p, std::string &text) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    text.assign(std::istreambuf_iterator<char>(in), {});
    return bool(in) || in.eof();
}

static std::string Key(const fs::path &p) {
    std::string k = p.string();
    for (char &c : k)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    return k + ".f32";
}

struct Result {
    std::string name, error;
    bool identical = false;
    double residual = 0, peak = 0, window = 0;
};

static double Db(double x) { return 20 * std::log10(std::max(x, 1e-30)); }

static Result Compare(const std::vector<float> &a, const std::vector<float> &b) {
    Result r;
    if (a.size() != b.size()) return r.error = "length differs", r;
    if (std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return std::bit_cast<uint32_t>(x) == std::bit_cast<uint32_t>(y); }))
        return r.identical = true, r;
    double se = 0, sr = 0, peak = 0, worst = -1e9;
    const size_t win = 2 * 44100 / 20;
    for (size_t w = 0; w < a.size(); w += win) {
        double we = 0, wr = 0;
        for (size_t i = w; i < std::min(a.size(), w + win); ++i) {
            const double d = double(a[i]) - b[i];
            we += d * d, wr += double(a[i]) * a[i];
            peak = std::max(peak, std::fabs(d));
        }
        se += we, sr += wr;
        if (we > 0) worst = std::max(worst, Db(std::sqrt(we)) - std::max(Db(std::sqrt(wr)), Db(std::sqrt(double(win))) - 120));
    }
    r.residual = Db(std::sqrt(se)) - Db(std::sqrt(sr)), r.peak = Db(peak), r.window = worst;
    return r;
}

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    double tolerance = 1;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--tolerance") tolerance = std::atof(args[i + 1].c_str()), args.erase(args.begin() + i, args.begin() + i + 2);
    if (args.size() < 3 || (args[0] != "save" && args[0] != "check")) {
        std::fprintf(stderr, "usage: regress save|check <baseline dir> <patch.sy1|folder>... [--tolerance dB]\n");
        return 2;
    }
    const bool save = args[0] == "save";
    const fs::path base = fs::absolute(args[1]);
    std::vector<fs::path> patches;
    for (size_t i = 2; i < args.size(); ++i) {
        const fs::path p = fs::absolute(args[i]);
        if (fs::is_directory(p)) {
            for (const auto &e : fs::recursive_directory_iterator(p))
                if (e.is_regular_file() && e.path().extension() == ".sy1") patches.push_back(e.path());
        } else if (fs::is_regular_file(p)) patches.push_back(p);
        else return std::fprintf(stderr, "not found: %s\n", p.c_str()), 1;
    }
    std::sort(patches.begin(), patches.end());
    if (patches.empty()) return std::fprintf(stderr, "no .sy1 found\n"), 1;
    if (save) fs::create_directories(base);

    std::vector<Result> results(patches.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        std::vector<float> l, r, mine, theirs;
        for (size_t i; (i = next++) < patches.size();) {
            Result &res = results[i];
            res.name = patches[i].string();
            std::string text;
            if (!ReadText(patches[i], text)) { res.error = "unreadable patch"; continue; }
            RenderJob(Phrase(ParseSy1(text).values), l, r);
            mine.resize(l.size() * 2);
            for (size_t k = 0; k < l.size(); ++k) mine[2 * k] = l[k], mine[2 * k + 1] = r[k];
            const fs::path file = base / Key(patches[i]);
            if (save) {
                std::ofstream out(file, std::ios::binary);
                out.write(reinterpret_cast<const char *>(mine.data()), std::streamsize(mine.size() * 4));
                if (!out) res.error = "cannot write baseline";
                continue;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) { res.error = "no baseline"; continue; }
            theirs.assign(size_t(fs::file_size(file) / 4), 0.0f);
            in.read(reinterpret_cast<char *>(theirs.data()), std::streamsize(theirs.size() * 4));
            const std::string name = res.name;
            res = Compare(theirs, mine);
            res.name = name;
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(worker);
    for (auto &t : pool) t.join();

    int errors = 0, identical = 0, over = 0;
    for (const Result &r : results)
        if (!r.error.empty()) ++errors, std::fprintf(stderr, "ERROR %s: %s\n", r.name.c_str(), r.error.c_str());
    if (save) {
        std::printf("saved %zu baselines in %s\n", patches.size() - errors, base.c_str());
        return errors ? 1 : 0;
    }
    std::vector<const Result *> differ;
    for (const Result &r : results)
        if (r.error.empty()) r.identical ? void(++identical) : differ.push_back(&r);
    std::sort(differ.begin(), differ.end(), [](auto *a, auto *b) { return a->residual > b->residual; });
    for (const Result *r : differ) {
        over += r->residual > tolerance;
        std::printf("  residual %7.1f dB  peak %7.1f dBFS  worst 50 ms %7.1f dB  %s\n", r->residual, r->peak,
                    r->window, r->name.c_str());
    }
    std::printf("%zu patches: %d identical, %zu differ (%d over tolerance), %d errors\n", patches.size(), identical,
                differ.size(), tolerance < 1 ? over : int(differ.size()), errors);
    return errors || (tolerance < 1 ? over : !differ.empty()) ? 1 : 0;
}
