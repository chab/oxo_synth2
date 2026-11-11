// s1ref: offline reference host for the original Synth1 AU (x86_64, runs
// under Rosetta). Black-box only: loads the component through the public
// AudioToolbox API, sets published parameters, plays MIDI, renders WAV.
//
//   s1ref params <out.json>              parameter metadata + quantization probe
//   s1ref classinfo [id=norm ...]        ClassInfo plist (XML) on stdout
//   s1ref get [id=norm ...]              all 99 param values after setting some
//   s1ref batch <jobs.txt>               render jobs (see README.md)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FRAMES 512
#define NUM_PARAMS 99

static double gSampleRate = 44100.0;
static double gTempo = 120.0;
static Float64 gSampleTime = 0.0;
static double gBeat0 = 0.0; /* host song position (beats) at the first rendered sample */

static void die(const char *what, OSStatus err) {
    fprintf(stderr, "error: %s (%d)\n", what, (int)err);
    exit(1);
}

static OSStatus beatAndTempo(void *user, Float64 *beat, Float64 *tempo) {
    (void)user;
    if (beat) *beat = gBeat0 + gSampleTime / gSampleRate * gTempo / 60.0;
    if (tempo) *tempo = gTempo;
    return noErr;
}

static OSStatus musicalTimeLocation(void *user, UInt32 *deltaNext, Float32 *tsNum, UInt32 *tsDen, Float64 *downBeat) {
    (void)user;
    double beat = gBeat0 + gSampleTime / gSampleRate * gTempo / 60.0;
    if (deltaNext) {
        double nextBar = (floor(beat / 4.0) + 1.0) * 4.0;
        *deltaNext = (UInt32)((nextBar - beat) * 60.0 / gTempo * gSampleRate);
    }
    if (tsNum) *tsNum = 4;
    if (tsDen) *tsDen = 4;
    if (downBeat) *downBeat = floor(beat / 4.0) * 4.0;
    return noErr;
}

static OSStatus transportState(void *user, Boolean *playing, Boolean *changed, Float64 *sampleInTrack, Boolean *cycling,
                               Float64 *cycleStart, Float64 *cycleEnd) {
    (void)user;
    if (playing) *playing = true;
    if (changed) *changed = false;
    if (sampleInTrack) *sampleInTrack = gSampleTime + gBeat0 * 60.0 / gTempo * gSampleRate;
    if (cycling) *cycling = false;
    if (cycleStart) *cycleStart = 0;
    if (cycleEnd) *cycleEnd = 0;
    return noErr;
}

static AudioUnit openSynth1(double sampleRate) {
    AudioComponentDescription desc = {kAudioUnitType_MusicDevice, 'Syn1', 'daiC', 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) die("Synth1 component not found", -1);
    AudioUnit au;
    OSStatus err = AudioComponentInstanceNew(comp, &au);
    if (err) die("AudioComponentInstanceNew", err);

    gSampleRate = sampleRate;
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = sampleRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    err = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof fmt);
    if (err) die("set stream format", err);
    UInt32 maxFrames = MAX_FRAMES;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames);

    HostCallbackInfo cb = {0};
    cb.beatAndTempoProc = beatAndTempo;
    cb.musicalTimeLocationProc = musicalTimeLocation;
    cb.transportStateProc = transportState;
    AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);

    err = AudioUnitInitialize(au);
    if (err) die("AudioUnitInitialize", err);
    return au;
}

static void closeSynth1(AudioUnit au) {
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
}

static void setParam(AudioUnit au, int id, double v) {
    OSStatus err = AudioUnitSetParameter(au, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0, (Float32)v, 0);
    if (err) die("AudioUnitSetParameter", err);
}

static double getParam(AudioUnit au, int id) {
    AudioUnitParameterValue v = 0;
    AudioUnitGetParameter(au, (AudioUnitParameterID)id, kAudioUnitScope_Global, 0, &v);
    return v;
}

static void applyAssignments(AudioUnit au, int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        int id;
        double v;
        if (sscanf(argv[i], "%d=%lf", &id, &v) == 2) setParam(au, id, v);
    }
}

static void jsonString(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
        else fputc(*s, f);
    }
    fputc('"', f);
}

static void cfToC(CFStringRef s, char *buf, size_t n) {
    buf[0] = 0;
    if (s) CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
}

static int cmdParams(const char *outPath) {
    AudioUnit au = openSynth1(44100.0);
    FILE *f = fopen(outPath, "w");
    if (!f) die("open output", -1);
    double defaults[NUM_PARAMS];
    for (int id = 0; id < NUM_PARAMS; id++) defaults[id] = getParam(au, id);

    fprintf(f, "[\n");
    for (int id = 0; id < NUM_PARAMS; id++) {
        AudioUnitParameterInfo info;
        UInt32 size = sizeof info;
        memset(&info, 0, sizeof info);
        AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, (AudioUnitElement)id, &info, &size);
        char name[256];
        if (info.cfNameString) cfToC(info.cfNameString, name, sizeof name);
        else snprintf(name, sizeof name, "%s", info.name);

        fprintf(f, "  {\"id\": %d, \"name\": ", id);
        jsonString(f, name);
        fprintf(f, ", \"unit\": %u, \"min\": %g, \"max\": %g, \"default\": %g, \"flags\": %u, \"initial\": %.9g",
                (unsigned)info.unit, info.minValue, info.maxValue, info.defaultValue, (unsigned)info.flags, defaults[id]);

        CFArrayRef valueStrings = NULL;
        size = sizeof valueStrings;
        if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global, (AudioUnitElement)id,
                                 &valueStrings, &size) == noErr && valueStrings) {
            fprintf(f, ", \"valueStrings\": [");
            for (CFIndex i = 0; i < CFArrayGetCount(valueStrings); i++) {
                char buf[256];
                cfToC((CFStringRef)CFArrayGetValueAtIndex(valueStrings, i), buf, sizeof buf);
                fprintf(f, "%s", i ? ", " : "");
                jsonString(f, buf);
            }
            fprintf(f, "]");
            CFRelease(valueStrings);
        }

        // Quantization probe: sweep the normalized range finely, record every
        // distinct read-back value and the set range that produced it.
        enum { STEPS = 4096 };
        double lastBack = NAN;
        int first = 1;
        fprintf(f, ", \"steps\": [");
        for (int k = 0; k <= STEPS; k++) {
            double x = info.minValue + (info.maxValue - info.minValue) * k / STEPS;
            setParam(au, id, x);
            double back = getParam(au, id);
            if (k == 0 || back != lastBack) {
                char str[256] = "";
                AudioUnitParameterStringFromValue sfv = {(AudioUnitParameterID)id, NULL, NULL};
                AudioUnitParameterValue bv = (AudioUnitParameterValue)back;
                sfv.inValue = &bv;
                size = sizeof sfv;
                if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterStringFromValue, kAudioUnitScope_Global, 0, &sfv,
                                         &size) == noErr && sfv.outString) {
                    cfToC(sfv.outString, str, sizeof str);
                    CFRelease(sfv.outString);
                }
                fprintf(f, "%s{\"from\": %.9g, \"value\": %.9g, \"str\": ", first ? "" : ", ", x, back);
                jsonString(f, str);
                fprintf(f, "}");
                first = 0;
                lastBack = back;
            }
        }
        fprintf(f, "]");
        setParam(au, id, defaults[id]);
        fprintf(f, "}%s\n", id + 1 < NUM_PARAMS ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
    closeSynth1(au);
    return 0;
}

static int cmdClassInfo(int argc, char **argv) {
    AudioUnit au = openSynth1(44100.0);
    applyAssignments(au, argc, argv);
    CFPropertyListRef plist = NULL;
    UInt32 size = sizeof plist;
    OSStatus err = AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size);
    if (err || !plist) die("get ClassInfo", err);
    CFDataRef data = CFPropertyListCreateData(NULL, plist, kCFPropertyListXMLFormat_v1_0, 0, NULL);
    fwrite(CFDataGetBytePtr(data), 1, (size_t)CFDataGetLength(data), stdout);
    CFRelease(data);
    CFRelease(plist);
    closeSynth1(au);
    return 0;
}

// ---- native state chunk -------------------------------------------------------
// ClassInfo "data" = 'Synth1 VST Chunk Data' blob; little-endian int32 words,
// parameter k stored as the pair (1, nativeValue) at words 142 + 2k.

#define CHUNK_PARAM_WORD 142

static void renderBlock(AudioUnit au, float *l, float *r, UInt32 frames);

static CFPropertyListRef copyClassInfo(AudioUnit au) {
    CFPropertyListRef plist = NULL;
    UInt32 size = sizeof plist;
    OSStatus err = AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size);
    if (err || !plist) die("get ClassInfo", err);
    return plist;
}

static void readNative(AudioUnit au, int native[NUM_PARAMS]) {
    CFPropertyListRef plist = copyClassInfo(au);
    CFDataRef data = CFDictionaryGetValue((CFDictionaryRef)plist, CFSTR("data"));
    const int32_t *w = (const int32_t *)CFDataGetBytePtr(data);
    if (CFDataGetLength(data) < (CHUNK_PARAM_WORD + 2 * NUM_PARAMS) * 4) die("chunk too short", -1);
    for (int k = 0; k < NUM_PARAMS; k++) native[k] = w[CHUNK_PARAM_WORD + 2 * k + 1];
    CFRelease(plist);
}

#define CHUNK_VERSION_WORD 140

static void writeNative(AudioUnit au, int version, const int *ids, const int *values, int n) {
    CFPropertyListRef plist = copyClassInfo(au);
    CFMutableDictionaryRef dict = CFDictionaryCreateMutableCopy(NULL, 0, (CFDictionaryRef)plist);
    CFMutableDataRef data = CFDataCreateMutableCopy(NULL, 0, CFDictionaryGetValue(dict, CFSTR("data")));
    int32_t *w = (int32_t *)CFDataGetMutableBytePtr(data);
    if (version > 0) w[CHUNK_VERSION_WORD] = version;
    for (int i = 0; i < n; i++) {
        w[CHUNK_PARAM_WORD + 2 * ids[i]] = 1;
        w[CHUNK_PARAM_WORD + 2 * ids[i] + 1] = values[i];
    }
    CFDictionarySetValue(dict, CFSTR("data"), data);
    CFPropertyListRef out = dict;
    OSStatus err = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &out, sizeof out);
    if (err) die("set ClassInfo", err);
    CFRelease(data);
    CFRelease(dict);
    CFRelease(plist);
}

// Raw chunk words (state outside the parameter pairs).
static void writeRaw(AudioUnit au, const int *words, const int *values, int n) {
    CFPropertyListRef plist = copyClassInfo(au);
    CFMutableDictionaryRef dict = CFDictionaryCreateMutableCopy(NULL, 0, (CFDictionaryRef)plist);
    CFMutableDataRef data = CFDataCreateMutableCopy(NULL, 0, CFDictionaryGetValue(dict, CFSTR("data")));
    int32_t *w = (int32_t *)CFDataGetMutableBytePtr(data);
    const int count = (int)(CFDataGetLength(data) / 4);
    for (int i = 0; i < n; i++)
        if (words[i] >= 0 && words[i] < count) w[words[i]] = values[i];
    CFDictionarySetValue(dict, CFSTR("data"), data);
    CFPropertyListRef out = dict;
    OSStatus err = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &out, sizeof out);
    if (err) die("set ClassInfo", err);
    CFRelease(data);
    CFRelease(dict);
    CFRelease(plist);
}

// For every parameter and every published step: the native value it maps to.
static int cmdNativeMap(void) {
    AudioUnit au = openSynth1(44100.0);
    int native[NUM_PARAMS];
    for (int id = 0; id < NUM_PARAMS; id++) {
        double def = getParam(au, id);
        double last = NAN;
        int lastNative = INT32_MIN;
        printf("%d", id);
        enum { STEPS = 4096 };
        int limit = (id >= 86 && id <= 89) ? 0 : STEPS; // continuous ids: probed separately
        for (int k = 0; k <= limit; k++) {
            setParam(au, id, (double)k / STEPS);
            double back = getParam(au, id);
            if (back == last) continue;
            last = back;
            readNative(au, native);
            if (native[id] != lastNative) printf(" %.6f:%d", back, native[id]);
            lastNative = native[id];
        }
        printf("\n");
        setParam(au, id, def);
    }
    closeSynth1(au);
    return 0;
}

// Native values -> what the AU reports back as normalized parameters.
static int cmdFromNative(int argc, char **argv) {
    AudioUnit au = openSynth1(44100.0);
    int ids[NUM_PARAMS], vals[NUM_PARAMS], n = 0, version = 0;
    for (int i = 0; i < argc && n < NUM_PARAMS; i++) {
        if (sscanf(argv[i], "ver=%d", &version) == 1) continue;
        if (sscanf(argv[i], "%d=%d", &ids[n], &vals[n]) == 2) n++;
    }
    writeNative(au, version, ids, vals, n);
    int native[NUM_PARAMS];
    readNative(au, native);
    for (int id = 0; id < NUM_PARAMS; id++) printf("%d %.9g %d\n", id, getParam(au, id), native[id]);
    closeSynth1(au);
    return 0;
}

static int cmdPresets(int argc, char **argv) {
    AudioUnit au = openSynth1(44100.0);
    CFArrayRef presets = NULL;
    UInt32 size = sizeof presets;
    OSStatus err = AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &size);
    printf("factory presets: err=%d count=%ld\n", (int)err, presets ? (long)CFArrayGetCount(presets) : -1L);
    for (CFIndex i = 0; presets && i < CFArrayGetCount(presets) && i < 8; i++) {
        const AUPreset *p = CFArrayGetValueAtIndex(presets, i);
        char name[256];
        cfToC(p->presetName, name, sizeof name);
        printf("  %d %s\n", (int)p->presetNumber, name);
    }
    int native[NUM_PARAMS];
    for (int i = 0; i < argc; i++) {
        int prog = atoi(argv[i]);
        MusicDeviceMIDIEvent(au, 0xC0, (UInt32)prog, 0, 0);
        float l[MAX_FRAMES], r[MAX_FRAMES];
        gSampleTime = 0;
        renderBlock(au, l, r, MAX_FRAMES);
        readNative(au, native);
        printf("pc %d:", prog);
        for (int id = 0; id < NUM_PARAMS; id++) printf(" %d", native[id]);
        printf("\n");
    }
    closeSynth1(au);
    return 0;
}

static int cmdGet(int argc, char **argv) {
    AudioUnit au = openSynth1(44100.0);
    applyAssignments(au, argc, argv);
    for (int id = 0; id < NUM_PARAMS; id++) printf("%d %.9g\n", id, getParam(au, id));
    closeSynth1(au);
    return 0;
}

// ---- rendering --------------------------------------------------------------

typedef struct {
    double time;
    UInt32 status, d1, d2; // status 0 = AU parameter d1 = value (timed `pt`)
    double value;
    int seq; // insertion order: equal times keep the job's order (qsort is not stable)
} Event;

typedef struct {
    char out[1024];
    double sampleRate, length, preroll, tempo, beat0;
    int nParams;
    int paramId[512];
    double paramVal[512];
    int nNative;
    int nativeId[NUM_PARAMS * 2];
    int nativeVal[NUM_PARAMS * 2];
    int nRaw; // raw chunk words (non-parameter state, e.g. word 651)
    int rawWord[64];
    int rawVal[64];
    int nEvents;
    Event events[4096];
} Job;

static int eventCmp(const void *a, const void *b) {
    const Event *ea = a, *eb = b;
    if (ea->time != eb->time) return ea->time < eb->time ? -1 : 1;
    return ea->seq - eb->seq;
}

static void writeWav(const char *path, float *const ch[2], size_t frames, double sr) {
    FILE *f = fopen(path, "wb");
    if (!f) die("open wav", -1);
    uint32_t dataBytes = (uint32_t)(frames * 2 * 4);
    uint32_t riffSize = 36 + dataBytes;
    uint16_t fmtTag = 3, chans = 2, block = 8, bits = 32;
    uint32_t fmtSize = 16, rate = (uint32_t)sr, byteRate = rate * 8;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmtTag, 2, 1, f);
    fwrite(&chans, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&block, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
    for (size_t i = 0; i < frames; i++) {
        fwrite(&ch[0][i], 4, 1, f);
        fwrite(&ch[1][i], 4, 1, f);
    }
    fclose(f);
}

static void renderBlock(AudioUnit au, float *l, float *r, UInt32 frames) {
    struct {
        AudioBufferList list;
        AudioBuffer extra;
    } buf;
    buf.list.mNumberBuffers = 2;
    buf.list.mBuffers[0] = (AudioBuffer){1, frames * 4, l};
    buf.list.mBuffers[1] = (AudioBuffer){1, frames * 4, r};
    AudioTimeStamp ts = {0};
    ts.mSampleTime = gSampleTime;
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    AudioUnitRenderActionFlags flags = 0;
    OSStatus err = AudioUnitRender(au, &flags, &ts, 0, frames, &buf.list);
    if (err) die("AudioUnitRender", err);
    gSampleTime += frames;
}

static void runJob(Job *job) {
    gTempo = job->tempo;
    gBeat0 = job->beat0;
    gSampleTime = 0;
    AudioUnit au = openSynth1(job->sampleRate);
    if (job->nNative) writeNative(au, 0, job->nativeId, job->nativeVal, job->nNative);
    if (job->nRaw) writeRaw(au, job->rawWord, job->rawVal, job->nRaw);
    for (int i = 0; i < job->nParams; i++) setParam(au, job->paramId[i], job->paramVal[i]);

    // Pre-roll lets any parameter smoothing settle; it is rendered and dropped.
    size_t preFrames = (size_t)(job->preroll * job->sampleRate);
    float scratchL[MAX_FRAMES], scratchR[MAX_FRAMES];
    while (preFrames > 0) {
        UInt32 n = preFrames > MAX_FRAMES ? MAX_FRAMES : (UInt32)preFrames;
        renderBlock(au, scratchL, scratchR, n);
        preFrames -= n;
    }
    Float64 origin = gSampleTime;

    size_t total = (size_t)(job->length * job->sampleRate);
    float *out[2] = {calloc(total, 4), calloc(total, 4)};
    qsort(job->events, (size_t)job->nEvents, sizeof(Event), eventCmp);
    int ev = 0;
    size_t pos = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (pos < total) {
        UInt32 n = total - pos > MAX_FRAMES ? MAX_FRAMES : (UInt32)(total - pos);
        while (ev < job->nEvents) {
            size_t at = (size_t)llround(job->events[ev].time * job->sampleRate);
            if (at >= pos + n) break;
            if (job->events[ev].status == 0) { // parameter: the block ends at it, set before the next
                if (at > pos) {
                    n = (UInt32)(at - pos);
                    break;
                }
                setParam(au, (int)job->events[ev].d1, job->events[ev].value);
                ev++;
                continue;
            }
            UInt32 offset = at > pos ? (UInt32)(at - pos) : 0;
            MusicDeviceMIDIEvent(au, job->events[ev].status, job->events[ev].d1, job->events[ev].d2, offset);
            ev++;
        }
        renderBlock(au, out[0] + pos, out[1] + pos, n);
        pos += n;
    }
    (void)origin;
    if (getenv("S1REF_TIME")) { // render speed (the pre-roll excluded)
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double cpu = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        fprintf(stderr, "  %-40s x%.1f realtime\n", job->out, job->length / cpu);
    }
    writeWav(job->out, out, total, job->sampleRate);
    free(out[0]);
    free(out[1]);
    closeSynth1(au);
}

static void addEvent(Job *job, double t, UInt32 status, UInt32 d1, UInt32 d2) {
    if (job->nEvents >= (int)(sizeof job->events / sizeof job->events[0])) die("too many events", -1);
    job->events[job->nEvents] = (Event){t, status, d1, d2, 0.0, job->nEvents};
    job->nEvents++;
}

static void resetJob(Job *job) {
    memset(job, 0, sizeof *job);
    job->sampleRate = 44100;
    job->length = 3.0;
    job->preroll = 0.25;
    job->tempo = 120;
    job->beat0 = 0;
}

static int cmdBatch(const char *path) {
    FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;
    if (!f) die("open jobs", -1);
    static Job job;
    resetJob(&job);
    char line[2048];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        char cmd[32];
        if (sscanf(p, "%31s", cmd) != 1) continue;
        char *rest = p + strlen(cmd);
        double a, b, c, d;
        int id;
        if (!strcmp(cmd, "job")) {
            resetJob(&job);
            sscanf(rest, " %1023[^\n]", job.out);
        } else if (!strcmp(cmd, "sr")) {
            sscanf(rest, "%lf", &job.sampleRate);
        } else if (!strcmp(cmd, "len")) {
            sscanf(rest, "%lf", &job.length);
        } else if (!strcmp(cmd, "preroll")) {
            sscanf(rest, "%lf", &job.preroll);
        } else if (!strcmp(cmd, "tempo")) {
            sscanf(rest, "%lf", &job.tempo);
        } else if (!strcmp(cmd, "beat0")) {
            sscanf(rest, "%lf", &job.beat0);
        } else if (!strcmp(cmd, "p")) {
            if (sscanf(rest, "%d %lf", &id, &a) == 2) {
                job.paramId[job.nParams] = id;
                job.paramVal[job.nParams++] = a;
            }
        } else if (!strcmp(cmd, "n")) {
            int v;
            if (sscanf(rest, "%d %d", &id, &v) == 2 && id >= 0 && id < NUM_PARAMS && job.nNative < NUM_PARAMS * 2) {
                job.nativeId[job.nNative] = id;
                job.nativeVal[job.nNative++] = v;
            }
        } else if (!strcmp(cmd, "w")) {
            int v;
            if (sscanf(rest, "%d %d", &id, &v) == 2 && job.nRaw < 64) {
                job.rawWord[job.nRaw] = id;
                job.rawVal[job.nRaw++] = v;
            }
        } else if (!strcmp(cmd, "note")) {
            if (sscanf(rest, "%lf %lf %lf %lf", &a, &b, &c, &d) == 4) {
                addEvent(&job, a, 0x90, (UInt32)c, (UInt32)d);
                addEvent(&job, b, 0x80, (UInt32)c, 0);
            }
        } else if (!strcmp(cmd, "cc")) {
            if (sscanf(rest, "%lf %lf %lf", &a, &b, &c) == 3) addEvent(&job, a, 0xB0, (UInt32)b, (UInt32)c);
        } else if (!strcmp(cmd, "bend")) {
            if (sscanf(rest, "%lf %lf", &a, &b) == 2) {
                UInt32 v = (UInt32)b;
                addEvent(&job, a, 0xE0, v & 0x7F, (v >> 7) & 0x7F);
            }
        } else if (!strcmp(cmd, "pt")) {
            if (sscanf(rest, "%lf %d %lf", &a, &id, &b) == 3) {
                addEvent(&job, a, 0, (UInt32)id, 0);
                job.events[job.nEvents - 1].value = b;
            }
        } else if (!strcmp(cmd, "press")) {
            if (sscanf(rest, "%lf %lf", &a, &b) == 2) addEvent(&job, a, 0xD0, (UInt32)b, 0);
        } else if (!strcmp(cmd, "end")) {
            runJob(&job);
            count++;
            if (count % 50 == 0) fprintf(stderr, "%d jobs\n", count);
        } else {
            fprintf(stderr, "unknown job line: %s", line);
            return 1;
        }
    }
    fprintf(stderr, "rendered %d jobs\n", count);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "params")) return cmdParams(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "classinfo")) return cmdClassInfo(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "get")) return cmdGet(argc - 2, argv + 2);
    if (argc >= 3 && !strcmp(argv[1], "batch")) return cmdBatch(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "nativemap")) return cmdNativeMap();
    if (argc >= 2 && !strcmp(argv[1], "fromnative")) return cmdFromNative(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "presets")) return cmdPresets(argc - 2, argv + 2);
    fprintf(stderr, "usage: s1ref params <out.json> | classinfo [id=v..] | get [id=v..] | batch <jobs.txt|->\n");
    return 2;
}
