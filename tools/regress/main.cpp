// regress: engine regression check. Renders every .sy1 given (files or
// folders, recursive) through fixed scenarios and compares with a baseline.
// Scenarios: phrase (44.1 kHz, 512-frame blocks); sweeps (knobs swept one
// after the other, switches / selectors stepped, over a held chord and
// staccato notes: what a still-knob or silence shortcut can break);
// sweeps96k (96 kHz, 64-frame blocks); rand48k (the phrase after 3 s of
// silence, 48 kHz, random 1..1100-frame blocks like a host's).
//   make -C tools/regress
//   build/regress/regress-<arch> save  <baseline dir> <patches...>   (before a change)
//   build/regress/regress-<arch> check <baseline dir> <patches...>   (after)
// check: "identical" (bit-exact) or the difference's level: residual RMS
// relative to the patch's RMS, its peak (dBFS), the worst 50 ms window
// (relative to that window, floor -120 dBFS). Exit 1 if a patch differs,
// unless every residual is under --tolerance <dB> (e.g. -100). "from":
// where the difference first exceeds -100 dBFS (sweeps: the param swept
// then). --phrase: the phrase only (the quick check).
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
#include <random>
#include <thread>

using namespace oxo::s2;
namespace fs = std::filesystem;

static Job Patch(const PatchValues &values, double sampleRate) {
    Job job;
    job.preroll = 0, job.sampleRate = sampleRate;
    for (int k = 0; k < kParamCount; ++k) job.natives.emplace_back(k, values[k]);
    return job;
}

static void Note(Job &job, double on, double off, int key, int vel) {
    job.events.push_back({on, 0x90, key, vel});
    job.events.push_back({off, 0x80, key, 0});
}

// The phrase: a held chord with the mod wheel, bend and pressure moving,
// staccato notes stealing / reusing voice slots, a legato line (mono /
// portamento patches), then the tails. t0: silence before it.
static Job Phrase(const PatchValues &values, double sampleRate, double t0 = 0) {
    Job job = Patch(values, sampleRate);
    job.length = t0 + 6.0;
    for (int key : {48, 55, 60, 64}) Note(job, t0, t0 + 1.6, key, 100);
    for (int k = 0; k <= 20; ++k) job.events.push_back({t0 + 0.4 + k * 0.05, 0xB0, 1, k * 127 / 20});
    job.events.push_back({t0 + 1.5, 0xB0, 1, 0});
    job.events.push_back({t0 + 1.0, 0xE0, 12000 & 0x7F, 12000 >> 7});
    job.events.push_back({t0 + 1.4, 0xE0, 0, 64});
    job.events.push_back({t0 + 1.0, 0xD0, 64, 0});
    job.events.push_back({t0 + 1.5, 0xD0, 0, 0});
    for (int k = 0; k < 12; ++k) Note(job, t0 + 2.0 + k * 0.1, t0 + 2.06 + k * 0.1, 60 + k * 5 % 17, 40 + k * 7);
    const int line[] = {52, 55, 59, 62};
    for (int k = 0; k < 4; ++k) Note(job, t0 + 3.4 + k * 0.3, t0 + 3.72 + k * 0.3, line[k], 90);
    return job;
}

// The sweeps: one window per param. A knob ramps patch value -> max -> min
// (every 4 ms), jumps to the middle, back to the patch value, then holds
// still; a switch / selector steps through its values (every 20 ms), then
// back.
static constexpr int kSwept[] = {
    kFilterFreq, kFilterRes, kFilterAmount, kFilterKbdTrack, kFilterSat, kOscMix, kOsc1Detune, kUnisonDetune,
    kLfo1Depth, kLfo1Speed, kLfo2Depth, kLfo2Speed, kOscPulseWidth, kOsc1FM, kOsc2Pitch, kOsc2Fine,
    kOscFineTune, kModEnvAmount, kSubGain, kEffectCtrl1, kEffectCtrl2, kEffectLevel, kChorusDepth,
    kChorusRate, kChorusLevel, kDelayDryWet, kDelayFeedback, kEqTone, kEqLevel, kAmpGain, kPan,
    kFilterSustain, kAmpSustain,
    // switches / selectors (stepped)
    kLfo1On, kLfo2On, kEffectOn, kChorusOn, kDelayOn, kModEnvOn, kOsc2Sync, kOsc2Ring, kFilterType,
    kEffectType, kOsc1Shape, kOsc2Shape, kLfo1Dest, kLfo1Type, kLfo2Dest, kUnisonOn, kSubShape};
static constexpr int kSweptKnobs = 33;
static constexpr double kSweepStart = 0.05, kSweepWindow = 0.16;
static constexpr double kSweepEnd = kSweepStart + kSweepWindow * std::size(kSwept);

static Job Sweeps(const PatchValues &values, double sampleRate) {
    Job job = Patch(values, sampleRate);
    double t = kSweepStart;
    for (size_t w = 0; w < std::size(kSwept); ++w, t += kSweepWindow) {
        const int p = kSwept[w], v0 = values[size_t(p)], lo = kParams[size_t(p)].min, hi = kParams[size_t(p)].max;
        if (w < kSweptKnobs) {
            for (int s = 0; s <= 20; ++s) {
                const double x = s / 20.0;
                const double v = x < 0.5 ? v0 + (hi - v0) * x * 2 : hi + (lo - hi) * (x - 0.5) * 2;
                job.events.push_back({t + s * 0.004, 0, p, int(std::lround(v))});
            }
            job.events.push_back({t + 0.10, 0, p, (lo + hi) / 2});
            job.events.push_back({t + 0.12, 0, p, v0});
        } else {
            for (int v = lo; v <= hi; ++v) job.events.push_back({t + (v - lo) * 0.02, 0, p, v});
            job.events.push_back({t + 0.14, 0, p, v0});
        }
    }
    job.length = kSweepEnd + 2.5;
    for (int key : {45, 57, 64, 69}) Note(job, 0.0, kSweepEnd, key, 96);
    for (double s = 0.3; s < kSweepEnd; s += 0.37) Note(job, s, s + 0.09, 72 + int(s * 10) % 12, 50 + int(s * 30) % 70);
    return job;
}

// RenderJob with a block size of its own: fixed, or random 1..1100 (seeded)
static void Render(const Job &job, int block, std::vector<float> &out) {
    Synth synth;
    synth.SetSampleRate(job.sampleRate);
    for (auto [id, v] : job.natives) synth.SetParam(id, v);
    synth.SetVolume(job.volume);
    double samplePos = 0;
    auto transport = [&] { synth.SetTransport(job.tempo, job.beat0 + samplePos / job.sampleRate * job.tempo / 60.0, true); };
    std::vector<JobEvent> events = job.events;
    std::stable_sort(events.begin(), events.end(), [](auto &a, auto &b) { return a.time < b.time; });
    const size_t total = size_t(job.length * job.sampleRate);
    std::vector<float> left(total), right(total);
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> random(1, 1100);
    for (size_t pos = 0, ev = 0; pos < total;) {
        const size_t start = pos, end = std::min(total, pos + size_t(block ? block : random(rng)));
        synth.BeginBlock(int(end - start));
        while (ev < events.size()) {
            size_t at = size_t(std::llround(events[ev].time * job.sampleRate));
            if (at >= end) break;
            const int status = events[ev].status & 0xF0;
            if (status == 0x90 || status == 0x80) at = start + size_t(NoteEventOffset(int(at - start)));
            if ((at = std::max(at, pos)) > pos) {
                transport();
                synth.Render(left.data() + pos, right.data() + pos, int(at - pos));
                samplePos += double(at - pos), pos = at;
            }
            Dispatch(synth, events[ev++]);
        }
        transport();
        synth.Render(left.data() + pos, right.data() + pos, int(end - pos));
        samplePos += double(end - pos), pos = end;
    }
    out.resize(total * 2);
    for (size_t k = 0; k < total; ++k) out[2 * k] = left[k], out[2 * k + 1] = right[k];
}

struct Scenario {
    const char *name; // baseline file suffix ("" = the phrase's, as before the scenarios)
    double sampleRate;
    int block; // 0: random
    bool sweeps;
    double t0; // silence before the phrase
};
static constexpr Scenario kScenarios[] = {
    {"", 44100, 512, false, 0}, {".sweeps", 44100, 512, true, 0}, {".sweeps96k", 96000, 64, true, 0},
    {".rand48k", 48000, 0, false, 3.0}};

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
    return k;
}

struct Result {
    std::string name, error;
    bool identical = false;
    double residual = 0, peak = 0, window = 0, from = 0; // from: seconds
    int swept = -1; // sweeps: the param swept at `from`
};

static double Db(double x) { return 20 * std::log10(std::max(x, 1e-30)); }

static Result Compare(const std::vector<float> &a, const std::vector<float> &b, double sampleRate) {
    Result r;
    if (a.size() != b.size()) return r.error = "length differs", r;
    if (std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return std::bit_cast<uint32_t>(x) == std::bit_cast<uint32_t>(y); }))
        return r.identical = true, r;
    double se = 0, sr = 0, peak = 0, worst = -1e9;
    const size_t win = 2 * size_t(sampleRate / 20);
    r.from = -1;
    for (size_t w = 0; w < a.size(); w += win) {
        double we = 0, wr = 0;
        for (size_t i = w; i < std::min(a.size(), w + win); ++i) {
            const double d = double(a[i]) - b[i];
            we += d * d, wr += double(a[i]) * a[i];
            peak = std::max(peak, std::fabs(d));
            if (r.from < 0 && std::fabs(d) > 1e-5) r.from = double(i / 2) / sampleRate;
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
    size_t scenarios = std::size(kScenarios);
    for (size_t i = 0; i < args.size();)
        if (args[i] == "--tolerance" && i + 1 < args.size()) tolerance = std::atof(args[i + 1].c_str()), args.erase(args.begin() + i, args.begin() + i + 2);
        else if (args[i] == "--phrase") scenarios = 1, args.erase(args.begin() + i);
        else ++i;
    if (args.size() < 3 || (args[0] != "save" && args[0] != "check")) {
        std::fprintf(stderr, "usage: regress save|check <baseline dir> <patch.sy1|folder>... [--tolerance dB] [--phrase]\n");
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

    std::vector<Result> results(patches.size() * scenarios);
    std::atomic<size_t> next{0};
    auto worker = [&] {
        std::vector<float> mine, theirs;
        for (size_t j; (j = next++) < results.size();) {
            const fs::path &patch = patches[j / scenarios];
            const Scenario &sc = kScenarios[j % scenarios];
            Result &res = results[j];
            res.name = patch.string() + (*sc.name ? " [" + std::string(sc.name + 1) + "]" : "");
            std::string text;
            if (!ReadText(patch, text)) { res.error = "unreadable patch"; continue; }
            const PatchValues values = ParseSy1(text).values;
            Render(sc.sweeps ? Sweeps(values, sc.sampleRate) : Phrase(values, sc.sampleRate, sc.t0), sc.block, mine);
            const fs::path file = base / (Key(patch) + sc.name + ".f32");
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
            res = Compare(theirs, mine, sc.sampleRate);
            res.name = name;
            const double w = (res.from - kSweepStart) / kSweepWindow;
            if (sc.sweeps && w >= 0 && w < double(std::size(kSwept))) res.swept = kSwept[size_t(w)];
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(worker);
    for (auto &t : pool) t.join();

    int errors = 0, identical = 0, over = 0;
    for (const Result &r : results)
        if (!r.error.empty()) ++errors, std::fprintf(stderr, "ERROR %s: %s\n", r.name.c_str(), r.error.c_str());
    if (save) {
        std::printf("saved %zu baselines in %s\n", results.size() - errors, base.c_str());
        return errors ? 1 : 0;
    }
    std::vector<const Result *> differ;
    for (const Result &r : results)
        if (r.error.empty()) r.identical ? void(++identical) : differ.push_back(&r);
    std::sort(differ.begin(), differ.end(), [](auto *a, auto *b) { return a->residual > b->residual; });
    for (const Result *r : differ) {
        over += r->residual > tolerance;
        std::printf("  residual %7.1f dB  peak %7.1f dBFS  worst 50 ms %7.1f dB  %s", r->residual, r->peak, r->window,
                    r->name.c_str());
        if (r->from >= 0) std::printf("  from %.3f s", r->from);
        if (r->swept >= 0) std::printf(" (%s)", kParams[size_t(r->swept)].key);
        std::printf("\n");
    }
    std::printf("%zu patches x %zu scenarios: %d identical, %zu differ (%d over tolerance), %d errors\n", patches.size(),
                scenarios, identical,
                differ.size(), tolerance < 1 ? over : int(differ.size()), errors);
    return errors || (tolerance < 1 ? over : !differ.empty()) ? 1 : 0;
}
