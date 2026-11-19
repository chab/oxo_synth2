#pragma once

// Offline render jobs, same text format as tools/s1ref (see its README):
// the reference corpus manifests replay through our engine unchanged.

#include "Synth.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace oxo::s2 {

struct JobEvent {
    double time;
    int status, d1, d2;
};

struct Job {
    std::string out;
    double sampleRate = 44100, length = 3.0, preroll = 0.25, tempo = 120, beat0 = 0;
    std::vector<std::pair<int, int>> natives;
    int volume = kVolumeParam.def; // `w 651 v`: the original's vol chunk word
    std::vector<JobEvent> events;
};

// Parses every job of a batch; unknown lines are an error (returns false).
inline bool ParseJobs(const std::string &text, std::vector<Job> &jobs, std::string &error) {
    std::istringstream in(text);
    std::string line;
    Job job;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string cmd;
        if (!(ls >> cmd) || cmd[0] == '#') continue;
        if (cmd == "job") {
            job = Job{};
            std::getline(ls >> std::ws, job.out);
        } else if (cmd == "sr") ls >> job.sampleRate;
        else if (cmd == "len") ls >> job.length;
        else if (cmd == "preroll") ls >> job.preroll;
        else if (cmd == "tempo") ls >> job.tempo;
        else if (cmd == "beat0") ls >> job.beat0;
        else if (cmd == "n") {
            int id, v;
            if (ls >> id >> v) job.natives.emplace_back(id, v);
        } else if (cmd == "w") {
            int word, v;
            if (ls >> word >> v && word == 651) job.volume = v; // other raw words: no meaning for us
        } else if (cmd == "p") {
            // normalized AU values are the original's encoding; corpora use `n`
        } else if (cmd == "note") {
            double on, off;
            int key, vel;
            if (ls >> on >> off >> key >> vel) {
                job.events.push_back({on, 0x90, key, vel});
                job.events.push_back({off, 0x80, key, 0});
            }
        } else if (cmd == "cc") {
            double t;
            int num, val;
            if (ls >> t >> num >> val) job.events.push_back({t, 0xB0, num, val});
        } else if (cmd == "bend") {
            double t;
            int v;
            if (ls >> t >> v) job.events.push_back({t, 0xE0, v & 0x7F, (v >> 7) & 0x7F});
        } else if (cmd == "pt") { // AU parameter at a time: native by the original's step layout
            double t, n;
            int id;
            if (ls >> t >> id >> n && id >= 0 && id < kParamCount) job.events.push_back({t, 0, id, FromNormalized(id, n)});
        } else if (cmd == "press") {
            double t;
            int v;
            if (ls >> t >> v) job.events.push_back({t, 0xD0, v, 0});
        } else if (cmd == "end") {
            jobs.push_back(job);
        } else {
            error = "unknown job line: " + line;
            return false;
        }
    }
    return true;
}

inline void Dispatch(Synth &synth, const JobEvent &e) {
    switch (e.status) {
    case 0x90: synth.NoteOn(e.d1, e.d2); break;
    case 0x80: synth.NoteOff(e.d1); break;
    case 0xB0: synth.ControlChange(e.d1, e.d2); break;
    case 0xE0: synth.PitchBend(e.d1 | (e.d2 << 7)); break;
    case 0xD0: synth.ChannelPressure(e.d1); break;
    case 0: synth.SetParam(e.d1, e.d2); break; // `pt`
    }
}

// Renders like s1ref: fresh engine, natives applied, silent pre-roll dropped,
// events at exact sample offsets (notes on the original's 16-sample grid,
// NoteEventOffset), fixed 512-frame blocks. renderSeconds: the time spent
// after the pre-roll (what s1ref's S1REF_TIME measures).
inline void RenderJob(const Job &job, std::vector<float> &left, std::vector<float> &right,
                      double *renderSeconds = nullptr) {
    constexpr int kBlock = 512;
    Synth synth;
    synth.SetSampleRate(job.sampleRate);
    for (auto [id, v] : job.natives) synth.SetParam(id, v);
    synth.SetVolume(job.volume);
    double samplePos = 0;
    auto transport = [&] { synth.SetTransport(job.tempo, job.beat0 + samplePos / job.sampleRate * job.tempo / 60.0, true); };

    std::vector<float> scratchL(kBlock), scratchR(kBlock);
    for (size_t pre = size_t(job.preroll * job.sampleRate); pre > 0;) {
        int n = int(std::min<size_t>(pre, kBlock));
        transport();
        synth.BeginBlock(n);
        synth.Render(scratchL.data(), scratchR.data(), n);
        samplePos += n;
        pre -= size_t(n);
    }

    std::vector<JobEvent> events = job.events;
    std::stable_sort(events.begin(), events.end(), [](auto &a, auto &b) { return a.time < b.time; });
    const size_t total = size_t(job.length * job.sampleRate);
    left.assign(total, 0.0f);
    right.assign(total, 0.0f);
    size_t pos = 0, ev = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (pos < total) {
        const size_t block = pos, end = std::min(total, pos + kBlock);
        synth.BeginBlock(int(end - block));
        while (ev < events.size()) {
            size_t at = size_t(std::llround(events[ev].time * job.sampleRate));
            if (at >= end) break;
            const int status = events[ev].status & 0xF0;
            if (status == 0x90 || status == 0x80) at = block + size_t(NoteEventOffset(int(at - block)));
            at = std::max(at, pos);
            if (at > pos) {
                transport();
                synth.Render(left.data() + pos, right.data() + pos, int(at - pos));
                samplePos += double(at - pos);
                pos = at;
            }
            Dispatch(synth, events[ev++]);
        }
        transport();
        synth.Render(left.data() + pos, right.data() + pos, int(end - pos));
        samplePos += double(end - pos);
        pos = end;
    }
    if (renderSeconds) *renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

inline bool WriteWavFloat(const std::string &path, const std::vector<float> &l, const std::vector<float> &r,
                          double sr) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t frames = uint32_t(l.size()), dataBytes = frames * 8, riff = 36 + dataBytes, fmtSize = 16;
    const uint32_t rate = uint32_t(sr), byteRate = rate * 8;
    const uint16_t tag = 3, chans = 2, block = 8, bits = 32;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtSize, 4, 1, f);
    std::fwrite(&tag, 2, 1, f);
    std::fwrite(&chans, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&block, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    std::vector<float> interleaved(size_t(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        interleaved[2 * i] = l[i];
        interleaved[2 * i + 1] = r[i];
    }
    std::fwrite(interleaved.data(), 4, interleaved.size(), f);
    return std::fclose(f) == 0;
}

} // namespace oxo::s2
