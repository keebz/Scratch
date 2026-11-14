// Scratch - a DJ scratch instrument for the MPC (Live III target) by Keebz
//
// Signal flow:
//
//   touch strip --> platter position --> deck read head --+
//   (Bend / CC1 / "Scrub" Q-Link)       (varispeed, loops)|--> cut gate --> EQ A --+
//                                                                                   +--> master --> out
//   pads (notes) ------------------------> one-shot shots ---------------> EQ B --+
//
//   * Deck sample = Content/Samples/NN.wav (NN = 01..16, loaded once at start-up).
//     "Deck Sample" picks which one is on the record (0 = empty).
//   * Touching the strip grabs the record: finger up = forward, finger down = reverse,
//     finger still = record held (silent). Release = platter spins back up to loop speed.
//   * The cut pad (default note 39) gates the deck like a crossfader. Velocity sets how
//     hard/fast it opens when "Cut Curve" > 0. "Hamster" reverses it (open by default, pad cuts).
//   * Stickers are visual cue points: toggling "Sticker" adds one at the platter's current
//     angle, or removes the nearest one. "Go Sticker" jumps the platter to sticker N.
//   * Pads other than the cut pad (Shot Base + 0..15) fire the same numbered samples as shots.
//
// Realtime: process() does no allocation, locking or file I/O. All samples are loaded in
// the constructor.

#include "mpc_plugin.h"
#include "dsp/biquad.h"
#include "dsp/wav.h"
#include "strip_tracker.h"
#include <algorithm>
#include <string>
#include <vector>
#ifndef _WIN32
#include <dirent.h>
#endif
#ifndef SCRATCH_BUILD
#define SCRATCH_BUILD 0            // build number, set by tools/make_zips.py
#endif
#include <thread>
#include <chrono>
#include <atomic>

#include <cmath>
#include <cstdio>
#include <vector>
#ifdef SCRATCH_DEBUG
#include <atomic>
#include <thread>
#include <chrono>
#endif

namespace {

enum ParamIndex
{
    kMaster = 0,                          // master volume, dB (parameter 0 must be harmless)
    kAGain, kAHigh, kAMid, kALow,         // channel A (deck) gain + 3-band DJ EQ
    kBGain, kBHigh, kBMid, kBLow,         // channel B (shots)
    kStripSrc,                            // Bend / Mod Wheel / Scrub (Q-Link) carrier of the strip
    kScrub,                               // 0..1, strip position when the strip drives a Q-Link
    kThrow,                               // % of the record covered by one full strip pull
    kStripRel,                            // ms with no strip data before the record is let go (hold-still time)
    kCutNote,                             // MIDI note of the cut pad
    kCutLearn,                            // while on, the next pad hit becomes the cut pad
    kCutCurve,                            // 0 = instant full-volume cut, 100 = slow velocity curve
    kHamster,                             // reverses the cut (open by default, pad closes)
    kDeckSample,                          // 0 = none, 1..16
    kPlatter,                             // 33 1/3, 45, Stop
    kSticker,                             // tap: add/remove a sticker at the current angle
    kGoSticker,                           // 0 = none, 1..8: jump to that sticker
    kShotBase,                            // MIDI note of the first shot pad
    kSmooth,                              // extra smoothing of the head position (x0.2 ms), see strip_tracker.h
    kLead,                                // 0..100 %: look-ahead along the head speed; more = less latency, more ripple
    // --- appended for the touch-screen UI (see skins/): display-only values the plugin writes itself ---
    kPlatterAng,                          // 0..1: record rotation (sample position as a fraction of one turn)
    kCutPos,                              // 0..1: crossfader / cut gate position
    kLevelA, kLevelB,                     // 0..1: channel output levels (meters)
    kLoadMode,                            // pad mode: 0 OneShot (play), 1 Scratch (send to the record), 2 Load (step the pad to the next sample)
    kPad1, kPad2, kPad3, kPad4, kPad5, kPad6, kPad7, kPad8,   // on-screen pads (momentary)
    kStk1, kStk2, kStk3, kStk4, kStk5, kStk6, kStk7, kStk8,   // 0..1: where each sticker is on the record (display)
    kStripZone,                           // Whole / Bottom half / Top half: which part of the strip scrubs (the strip's rest value sits in the middle)
    kHamNote,                             // MIDI note of the pad that toggles the hamster switch (pad 12 = 47)
    kPadSmp1, kPadSmp2, kPadSmp3, kPadSmp4, kPadSmp5, kPadSmp6, kPadSmp7, kPadSmp8,   // sample (library number, 0 = empty) on each hardware pad 1-8
    kBrowse,                              // selected line of the sample list (the data encoder turns this)
    kRow1, kRow2, kRow3, kRow4, kRow5, kRow6, kRow7, kRow8, kRow9,   // display: library number shown on each list line (0 = blank)
    kBrowseHi,                            // display: which list line is highlighted (0..8, as 0..1)
    kPreview,                             // on: the selected sample plays (channel B) while you scroll
    kArm,                                 // on = LOAD armed: the next hardware pad hit stores the selected sample
    kStatus,                              // display text: 0 blank, 1 press a pad, 2..9 stored on pad n
    kSlot1, kSlot2, kSlot3, kSlot4, kSlot5, kSlot6, kSlot7, kSlot8,   // display: look of each pad slot (0 empty, 1 loaded, 2 playing, 3 flashing)
    kBLoop,                               // library number looping on channel B (0 = none)
    kSendB,                               // button: send the selected sample to channel B (again: clear it)
    kStopB,                               // button: stop the channel B loop
    kScroll,                              // the list's encoder / drag target: any change moves the selection one line (see updateBrowser)
    kLoopSnap,                            // on: when the sequence loops (or restarts) the record snaps back to where it was when the take began
    kNumParams
};

constexpr int kNumSamples  = 32;
constexpr int kListRows    = 9;      // Row parameters (identity); the screen shows the first kVisRows
constexpr int kVisRows     = 5;
constexpr int kMaxStickers = 8;
constexpr int kNumShots    = 8;
constexpr double kMaxSampleSeconds = 6.0;
constexpr double kThrowSec = 0.8;     // Throw 100% = this much audio under one full strip pull
constexpr double kRevSec   = 1.8;     // one turn of the platter at 33 1/3 rpm, whatever the sample length

struct Sample
{
    std::vector<float> l, r;
    double rate = 44100.0;
    size_t size() const { return l.size(); }
};

struct Shot
{
    int    sample = -1;
    double pos = 0.0;
    float  gain = 0.0f;
    bool   active = false;
};

struct Eq3
{
    mpc::dsp::StereoBiquad lo, mid, hi;
    float lastLo = 1e9f, lastMid = 1e9f, lastHi = 1e9f;

    static float dbOrKill(float db) { return db <= -25.0f ? -60.0f : db; }

    void update(double sr, float loDb, float midDb, float hiDb)
    {
        using mpc::dsp::BiquadCoeffs;
        if (loDb != lastLo)   { lastLo = loDb;   lo.setCoeffs(BiquadCoeffs::lowShelf(sr, 250.0f, 0.707f, dbOrKill(loDb))); }
        if (midDb != lastMid) { lastMid = midDb; mid.setCoeffs(BiquadCoeffs::peak(sr, 1000.0f, 0.7f, dbOrKill(midDb))); }
        if (hiDb != lastHi)   { lastHi = hiDb;   hi.setCoeffs(BiquadCoeffs::highShelf(sr, 3500.0f, 0.707f, dbOrKill(hiDb))); }
    }
    void process(float& l, float& r)
    {
        // Tiny alternating offset keeps the filter states out of denormal range.
        flip = -flip; l += flip; r += flip;
        lo.process(l, r); mid.process(l, r); hi.process(l, r);
    }
    float flip = 1e-15f;
    void clear() { lo.reset(); mid.reset(); hi.reset(); lastLo = lastMid = lastHi = 1e9f; }
};

float dbToGain(float dB) { return dB <= -55.0f ? 0.0f : std::pow(10.0f, dB / 20.0f); }

#ifdef SCRATCH_DEBUG
// Debug build only: the audio thread pushes small records into a lock-free ring; a worker thread
// writes them to <plugin folder>/scratch_log.txt. Nothing here runs in the release build.
struct DbgRec { uint64_t t; char kind; int a, b; double c; };
struct DbgLog
{
    static constexpr uint32_t kN = 8192;
    DbgRec ring[kN];
    std::atomic<uint32_t> head{0}, tail{0};
    std::atomic<bool> run{false};
    std::thread th;
    void push(uint64_t t, char k, int a, int b, double c = 0)
    {
        const uint32_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= kN) return;     // full: drop
        ring[h % kN] = { t, k, a, b, c };
        head.store(h + 1, std::memory_order_release);
    }
    void start(const std::string& path, double sr)
    {
        run = true;
        th = std::thread([this, path, sr] {
            FILE* f = std::fopen(path.c_str(), "w");
            if (!f) return;
            std::fprintf(f, "# Scratch debug log, build %d. time_s kind a b c\n# M=midi(status,d1|d2) G=grab R=release(reason) S=state(rate,pos) P=param\n", SCRATCH_BUILD);
            while (run.load() || tail.load() != head.load())
            {
                bool any = false;
                while (tail.load() != head.load(std::memory_order_acquire))
                {
                    const DbgRec& r = ring[tail.load() % kN];
                    std::fprintf(f, "%.4f %c %d %d %.4f\n", (double)r.t / sr, r.kind, r.a, r.b, r.c);
                    tail.store(tail.load() + 1, std::memory_order_release);
                    any = true;
                }
                if (any) std::fflush(f);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::fclose(f);
        });
    }
    void stop() { run = false; if (th.joinable()) th.join(); }
};
#endif

#ifdef SCRATCH_DEBUG
#define DBG(...) dbg_.push(clock_, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

} // namespace

inline mpc::Param onOff(const char* id, const char* name) { return mpc::choiceParam(id, name, {"Off", "On"}, 0); }   // choice, not bool: the MPC screen did not write bool parameters

// Mixer knobs run -1..+1 with 12 o'clock = 0 dB: left end = silent (gain, -60 dB) / kill (EQ, -26 dB), right end = +12 dB.
inline float knobDb(float pos, float negDb) { return pos < 0.0f ? pos * negDb : pos * 12.0f; }

class Scratch : public mpc::Plugin
{
public:
    ~Scratch() override
    {
        notifyRun_ = false;
        if (notifyTh_.joinable()) notifyTh_.join();
#ifdef SCRATCH_DEBUG
        dbg_.stop();
#endif
    }
    Scratch() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam ("master",   "Master",      -40.0f, 6.0f,  -3.0f, "dB", 1));
        addParam(mpc::floatParam ("a_gain", "A Gain", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("a_high", "A High", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("a_mid", "A Mid", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("a_low", "A Low", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("b_gain", "B Gain", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("b_high", "B High", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("b_mid", "B Mid", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::floatParam ("b_low", "B Low", -1.0f, 1.0f, 0.0f, "", 2));      // knob position, 12 o'clock = 0 dB (see knobDb)
        addParam(mpc::choiceParam("strip_src","Strip Src",   {"Bend", "ModWheel", "Scrub"}, 0));
        addParam(mpc::floatParam ("scrub",    "Scrub",        0.0f, 1.0f,    0.5f, "",   3));
        addParam(mpc::floatParam ("throw",    "Throw",       10.0f, 400.0f, 120.0f, "%",  0));
        addParam(mpc::logParam   ("strip_rel","Strip Rel",   50.0f, 4000.0f, 100.0f, "ms", 0));
        addParam(mpc::intParam   ("cut_note", "Cut Note",     0, 127, 39));
        addParam(onOff("cut_learn", "Cut Learn"));
        addParam(mpc::floatParam ("cut_curve","Cut Curve",    0.0f, 100.0f,  0.0f, "%",  0));
        addParam(onOff("hamster", "Hamster"));
        addParam(mpc::intParam   ("deck",     "Deck Sample",  0, kNumSamples, 1));
        addParam(mpc::choiceParam("platter",  "Platter",     {"33 1/3", "45", "Stop"}, 0));
        addParam(onOff("sticker", "Sticker"));
        addParam(mpc::intParam   ("go_sticker","Go Sticker",  0, kMaxStickers, 0));
        addParam(mpc::intParam   ("shot_base","Shot Base",    0, 111, 36));
        addParam(mpc::logParam   ("smooth",   "Smooth",       2.0f, 300.0f,  300.0f, "ms", 0));
        addParam(mpc::floatParam ("lead",     "Lead",         0.0f, 300.0f,  300.0f, "%",  0));
        addParam(mpc::floatParam ("platter_angle", "Rec Angle",   0.0f, 1.0f,    0.0f, "",   3));
        addParam(mpc::floatParam ("cut_pos",  "Cut Pos",      0.0f, 1.0f,    0.0f, "",   2));
        addParam(mpc::floatParam ("level_a",  "Level A",      0.0f, 1.0f,    0.0f, "",   2));
        addParam(mpc::floatParam ("level_b",  "Level B",      0.0f, 1.0f,    0.0f, "",   2));
        addParam(mpc::choiceParam("load_mode","Pad Mode",    {"OneShot", "Scratch", "Load"}, 0));
        for (int i = 0; i < 8; ++i)
        {
            char id[16], nm[16];
            std::snprintf(id, sizeof id, "pad%d", i + 1); std::snprintf(nm, sizeof nm, "Pad %d", i + 1);
            addParam(onOff(id, nm));
        }
        for (int i = 0; i < 8; ++i)
        {
            char id[16], nm[16];
            std::snprintf(id, sizeof id, "stk%d", i + 1); std::snprintf(nm, sizeof nm, "Stk %d", i + 1);
            addParam(mpc::floatParam(id, nm, 0.0f, 1.0f, 0.0f, "", 3));
        }
        addParam(mpc::choiceParam("strip_zone", "Strip Zone", {"Whole", "Bottom", "Top"}, 1));
        addParam(mpc::intParam   ("ham_note", "Ham Note",     0, 127, 47));
        for (int i = 0; i < 8; ++i)
        {
            char id[16], nm[16];
            std::snprintf(id, sizeof id, "pad_smp%d", i + 1); std::snprintf(nm, sizeof nm, "Pad %d Smp", i + 1);
            addParam(mpc::intParam(id, nm, 0, kNumSamples, i + 1));
        }
        addParam(mpc::intParam   ("browse",   "Browse",       0, kNumSamples - 1, 0));
        for (int i = 0; i < kListRows; ++i)
        {
            char id[16], nm[16];
            std::snprintf(id, sizeof id, "row%d", i + 1); std::snprintf(nm, sizeof nm, "Row %d", i + 1);
            addParam(mpc::intParam(id, nm, 0, kNumSamples, 0));
        }
        addParam(mpc::floatParam ("browse_hi", "Browse Hi",   0.0f, 1.0f, 0.0f, "", 2));
        addParam(onOff("preview", "Preview"));
        addParam(onOff("arm", "Load"));
        addParam(mpc::intParam   ("status",   "Status",       0, 9, 0));
        for (int i = 0; i < 8; ++i)
        {
            char id[16], nm[16];
            std::snprintf(id, sizeof id, "slot%d", i + 1); std::snprintf(nm, sizeof nm, "Slot %d", i + 1);
            addParam(mpc::floatParam(id, nm, 0.0f, 1.0f, 0.0f, "", 2));
        }
        addParam(mpc::intParam   ("b_loop",   "B Loop",       0, kNumSamples, 0));
        addParam(onOff("send_b", "Send B"));
        addParam(onOff("stop_b", "Stop B"));
        addParam(mpc::floatParam ("scroll",   "Scroll",       0.0f, 1.0f, 0.5f, "", 3));
        addParam(mpc::choiceParam("loop_snap", "Loop Snap",   {"Off", "On"}, 1));

        //                    master A: gain hi mid lo   B: gain hi mid lo  src scrub throw rel note learn curve ham deck plat stk go  base
        addProgram("Init", {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 120, 100, 39, 0, 0, 0, 1, 0, 0, 0, 36, 300, 300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 47, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1});
        addProgram("Battle", {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 120, 100, 39, 0, 0, 0, 1, 0, 0, 0, 36, 300, 300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 47, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1});
        addProgram("Smooth", {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 120, 100, 39, 0, 70, 0, 1, 0, 0, 0, 36, 300, 300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 47, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1});
        addProgram("Hamster", {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 120, 100, 39, 0, 0, 1, 1, 0, 0, 0, 36, 300, 300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 47, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1});

        loadLibrary();
    }

    // The sample library is every .wav in Content/Samples, in name order (a leading "01 " only sets the order and is not
    // shown), up to kNumSamples. Pads point into it (Pad n Smp); put more files on the card and they show up.
    void loadLibrary()
    {
        const std::string dir = mpc::contentPath("Samples");
        std::vector<std::string> files;
#ifndef _WIN32
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                std::string f = e->d_name;
                std::string low = f; for (char& c : low) c = (char)std::tolower((unsigned char)c);
                if (low.size() > 4 && low.compare(low.size() - 4, 4, ".wav") == 0 && f[0] != '.') files.push_back(f);
            }
            closedir(d);
        }
#endif
        std::sort(files.begin(), files.end());
        if ((int)files.size() > kNumSamples) files.resize(kNumSamples);
        for (int i = 0; i < (int)files.size(); ++i)
        {
            loadSample(i, dir + "/" + files[i]);
            std::string n = files[i].substr(0, files[i].size() - 4);
            size_t k = 0; while (k < n.size() && std::isdigit((unsigned char)n[k])) ++k;
            while (k < n.size() && (n[k] == ' ' || n[k] == '_' || n[k] == '-')) ++k;
            n = k < n.size() ? n.substr(k) : n;
            for (char& c : n) c = (char)std::toupper((unsigned char)c);
            if (n.size() > 9) n.resize(9);
            names_[i] = n;
        }
        libCount_ = (int)files.size();
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[24];
        if (index >= kAGain && index <= kBLow)
        {
            const bool gain = index == kAGain || index == kBGain;
            const float db = knobDb(plain, gain ? 60.0f : 26.0f);
            if (db <= (gain ? -55.0f : -25.0f)) { out = gain ? "OFF" : "KILL"; return true; }
            std::snprintf(b, sizeof b, "%+.1f dB", db); out = b; return true;
        }
        if (index >= kPadSmp1 && index <= kPadSmp8)
        {
            const int v = (int)std::lround(plain);
            out = (v >= 1 && v <= libCount_) ? names_[v - 1] : "EMPTY";
            return true;
        }
        if (index >= kRow1 && index <= kRow9)
        {
            const int v = (int)std::lround(plain);
            out = (v >= 1 && v <= libCount_) ? names_[v - 1] : "";
            return true;
        }
        if (index == kBLoop)
        {
            const int v = (int)std::lround(plain);
            out = (v >= 1 && v <= libCount_) ? names_[v - 1] : "NONE";
            return true;
        }
        if (index == kStatus)
        {
            const int v = (int)std::lround(plain);
            if (v == 1) out = "PRESS A PAD  (A01 - A08)";
            else if (v >= 2) { std::snprintf(b, sizeof b, "STORED ON PAD %d", v - 1); out = b; }
            else out = "";
            return true;
        }
        return false;
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = sampleRate;
        trk_.prepare(sampleRate);
        startNotifier();
        set(kArm, 0.0f); set(kPreview, 0.0f); set(kStatus, 0.0f); set(kSendB, 0.0f);   // transient controls start off (not in reset(): that runs when a project is loaded)
#ifdef SCRATCH_DEBUG
        if (!dbgStarted_) { dbgStarted_ = true; dbg_.start(mpc::pluginFolder() + "/scratch_log.txt", sampleRate); }
#endif
        reset();
    }

    void reset() override
    {
        pos_ = 0.0; speed_ = 0.0; rate_ = 0.0; gateGain_ = 1.0f; gateTarget_ = 1.0f;
        prevPlaying_ = prevRecording_ = false;     // (the loop anchor is project state: it is not cleared here)
        touched_ = false; armedCut_ = false; pendingRest_ = false; lastHamster_ = getBool(kHamster); cutHeld_ = false;
        heldPad_ = -1; lastBrowse_ = -1; lastPreview_ = false; lastArm_ = false; winStart_ = 0; statusAge_ = 0;
        bSmp_ = getInt(kBLoop); bPos_ = 0.0;
        smoothedGain_ = dbToGain(get(kMaster));
        eqA_.clear(); eqB_.clear();
        for (Shot& s : shots_) s = Shot();
        lastDeck_ = getInt(kDeckSample);
        lastSticker_ = getBool(kSticker);
        lastGo_ = getInt(kGoSticker);
        lastScrub_ = get(kScrub);
        for (int i = 0; i < 8; ++i) lastPad_[i] = getBool(kPad1 + i);
        peakA_ = peakB_ = levelA_ = levelB_ = 0.0f;
        gateTarget_ = gateGain_ = getBool(kHamster) ? 1.0f : 0.0f;
        sinceStrip_ = 1 << 30;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        eqA_.update(sr_, knobDb(get(kALow), 26.0f), knobDb(get(kAMid), 26.0f), knobDb(get(kAHigh), 26.0f));
        eqB_.update(sr_, knobDb(get(kBLow), 26.0f), knobDb(get(kBMid), 26.0f), knobDb(get(kBHigh), 26.0f));
        const float gainA  = dbToGain(knobDb(get(kAGain), 60.0f));
        const float gainB  = dbToGain(knobDb(get(kBGain), 60.0f));
        const float target = dbToGain(get(kMaster));
        const bool  hamster = getBool(kHamster);
        if (hamster != lastHamster_) { lastHamster_ = hamster; if (!cutHeld_) setGate(hamster ? 1.0f : 0.0f, 5.0f); }
        const int   src = getInt(kStripSrc);
        const double throwFrac = get(kThrow) * 0.01;
        const int   relSamples = (int)(get(kStripRel) * 0.001 * sr_);
        const double platterSpeed[3] = { 1.0, 1.35, 0.0 };
        const double motorTarget = platterSpeed[mpc::clamp(getInt(kPlatter), 0, 2)];
        const double motorCoef = 1.0 - std::exp(-1.0 / (0.04 * sr_));     // ~40 ms: a released record picks up quickly
        trk_.setParams(get(kLead) * 0.01, get(kSmooth) * 0.2);   // see strip_tracker.h
        const double ampCoef = 1.0 - std::exp(-1.0 / (0.002 * sr_));       // ~2 ms volume smoothing (no clicks)

#ifdef SCRATCH_DEBUG
        {   // log every change of a control parameter (pads, mode, platter, hamster, sticker, deck): did the screen's write arrive?
            static const int ctl[] = { kHamster, kPlatter, kSticker, kDeckSample, kLoadMode, kPadSmp1, kPad1, kPad2, kPad3, kPad4, kPad5, kPad6, kPad7, kPad8 };
            for (int k = 0; k < (int)(sizeof ctl / sizeof ctl[0]); ++k)
            {
                const float v = getNormalised(ctl[k]);
                if (v != dbgCtl_[k]) { dbgCtl_[k] = v; DBG('P', ctl[k], (int)(v * 1000), v); }
            }
        }
#endif
        updateBrowser();

        // Loop snap: remember where the record was when the take began, put it back whenever the sequence passes that point
        const int snapAt = loopSnapBlock(ctx.transport, n);

        // Parameter-driven actions
        const int deckNow = getInt(kDeckSample);
        if (deckNow != lastDeck_)
        {
            cueDeck(deckNow);
            if (!prevPlaying_) anchorValid_ = false;     // a new record chosen while stopped: the next start begins a new take
        }

        const bool stickNow = getBool(kSticker);
        if (stickNow != lastSticker_) { lastSticker_ = stickNow; toggleSticker(); }

        const int goNow = getInt(kGoSticker);
        if (goNow != lastGo_)
        {
            lastGo_ = goNow;
            const Sample* s = deckSample();
            if (s && goNow > 0 && goNow <= stickerCount_[deckNow - 1])
            {
                pos_ = stickers_[deckNow - 1][goNow - 1] * (double)s->size();
                touched_ = false;
            }
        }

        if (src == 2)   // Scrub Q-Link carrier
        {
            const float sc = get(kScrub);
            if (sc != lastScrub_) { lastScrub_ = sc; stripMoved(sc, throwFrac); }
        }

        armedCut_ = getBool(kCutLearn);
        const int cutNote = learnedNote_ >= 0 ? learnedNote_ : getInt(kCutNote);
        const int shotBase = getInt(kShotBase);

        int midiIndex = 0;
        for (int i = 0; i < n; ++i)
        {
            if (i == snapAt) snapToAnchor();
            while (midiIndex < ctx.numMidiEvents && ctx.midi[midiIndex].sampleOffset <= i)
                handleMidi(ctx.midi[midiIndex++], src, throwFrac, hamster, cutNote, shotBase);

            // ---- deck ----
            ++sinceStrip_; if (sinceSnap_ < (1 << 30)) ++sinceSnap_;
#ifdef SCRATCH_DEBUG
            ++clock_;
            if (touched_ && (clock_ & 2047) == 0) DBG('S', (int)(targetPos_ - pos_), 0, rate_);
#endif
            const Sample* s = deckSample();
            float aL = 0.0f, aR = 0.0f;
            if (pendingRest_ && ++pendAge_ > (int)(0.030 * sr_)) { pendingRest_ = false; releaseRecord(2); }     // the lift
            // Off-centre the lift announces itself (above), and a held finger only reports now and then (gaps up to
            // 0.6 s measured), so wait a second. A lift from the dead centre (-1) is silent, so it can only be told by a short silence: Strip Rel / 3.
            if (touched_ && sinceStrip_ > (!zoned_ && std::fabs(lastStrip_ - kRestV) < 1e-5f ? relSamples / 3 : (int)(1.0 * sr_))) releaseRecord(3);
            if (s)
            {
                const double len = (double)s->size();
                const double fileStep = s->rate / sr_;       // frames per output sample at 1x
                double v;
                if (touched_)
                {
                    v = trk_.next() - pos_;                                  // strip tracker: see strip_tracker.h
                    const double vMax = 24.0 * fileStep;                     // safety: never faster than 24x
                    v = v > vMax ? vMax : (v < -vMax ? -vMax : v);
                    pos_ += v; turnSec_ += v / s->rate;
                    rate_ = v / fileStep;                     // x normal speed, signed
                }
                else
                {
                    speed_ += (motorTarget - speed_) * motorCoef;
                    rate_ = speed_;
                    pos_ += speed_ * fileStep; turnSec_ += speed_ * fileStep / s->rate;
                }
                // Wrap the head around the loop, and move the finger anchors by the same amount so the
                // head never sees a target a whole record away (that made a loud buzz).
                if (pos_ >= len || pos_ < 0.0)
                {
                    const double wrapped = pos_ - std::floor(pos_ / len) * len;
                    const double shift = wrapped - pos_;
                    pos_ = wrapped; targetPos_ += shift; grabPos_ += shift; trk_.shift(shift);
                }

                // 4-point cubic (Catmull-Rom) read: much cleaner than linear at slow and fast speeds
                const size_t n0 = s->size(), i0 = (size_t)pos_;
                const size_t im = i0 == 0 ? n0 - 1 : i0 - 1, i1 = i0 + 1 < n0 ? i0 + 1 : i0 + 1 - n0, i2 = i1 + 1 < n0 ? i1 + 1 : i1 + 1 - n0;
                const float fr = (float)(pos_ - (double)i0);
                auto cubic = [fr](float y0, float y1, float y2, float y3) {
                    return y1 + 0.5f * fr * (y2 - y0 + fr * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + fr * (3.0f * (y1 - y2) + y3 - y0)));
                };
                aL = cubic(s->l[im], s->l[i0], s->l[i1], s->l[i2]);
                aR = cubic(s->r[im], s->r[i0], s->r[i1], s->r[i2]);

                // Volume follows speed only very close to standstill (where the sound is sub-audio anyway).
                const float mv = (float)std::fabs(rate_);
                const float x = mv >= 0.03f ? 1.0f : mv * (1.0f / 0.03f);
                const float g = touched_ ? x : 1.0f;         // a released record plays at full volume at once
                ampG_ += (g - ampG_) * (float)ampCoef;
                aL *= ampG_; aR *= ampG_;
            }

            // cut gate (crossfader): linear slew toward target
            if (gateGain_ < gateTarget_)      { gateGain_ += gateStepUp_;   if (gateGain_ > gateTarget_) gateGain_ = gateTarget_; }
            else if (gateGain_ > gateTarget_) { gateGain_ -= gateStepDown_; if (gateGain_ < gateTarget_) gateGain_ = gateTarget_; }
            aL *= gateGain_ * gainA; aR *= gateGain_ * gainA;
            eqA_.process(aL, aR);
            peakA_ = std::fmax(peakA_, std::fmax(std::fabs(aL), std::fabs(aR)));

            // ---- shots ----
            float bL = 0.0f, bR = 0.0f;
            for (Shot& sh : shots_)
            {
                if (!sh.active) continue;
                const Sample& sm = samples_[sh.sample];
                const size_t i0 = (size_t)sh.pos;
                if (i0 + 1 >= sm.size()) { sh.active = false; continue; }
                const float fr = (float)(sh.pos - (double)i0);
                bL += (sm.l[i0] + fr * (sm.l[i0 + 1] - sm.l[i0])) * sh.gain;
                bR += (sm.r[i0] + fr * (sm.r[i0 + 1] - sm.r[i0])) * sh.gain;
                sh.pos += sm.rate / sr_;
            }
            if (bSmp_ >= 1 && bSmp_ <= libCount_)                        // channel B loop: wraps seamlessly, read across the join
            {
                const Sample& sm = samples_[bSmp_ - 1];
                const size_t n0 = sm.size();
                if (n0 > 1)
                {
                    const size_t i0 = (size_t)bPos_, im = i0 == 0 ? n0 - 1 : i0 - 1, i1 = i0 + 1 < n0 ? i0 + 1 : i0 + 1 - n0, i2 = i1 + 1 < n0 ? i1 + 1 : i1 + 1 - n0;
                    const float fr = (float)(bPos_ - (double)i0);
                    auto cub = [fr](float y0, float y1, float y2, float y3) {
                        return y1 + 0.5f * fr * (y2 - y0 + fr * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + fr * (3.0f * (y1 - y2) + y3 - y0)));
                    };
                    bL += cub(sm.l[im], sm.l[i0], sm.l[i1], sm.l[i2]);
                    bR += cub(sm.r[im], sm.r[i0], sm.r[i1], sm.r[i2]);
                    bPos_ += sm.rate / sr_;
                    if (bPos_ >= (double)n0) bPos_ -= (double)n0;
                }
            }
            bL *= gainB; bR *= gainB;
            eqB_.process(bL, bR);
            peakB_ = std::fmax(peakB_, std::fmax(std::fabs(bL), std::fabs(bR)));

            smoothedGain_ += 0.002f * (target - smoothedGain_);
            // 0.5 = headroom so deck + shots at full level do not clip
            const float mL = (aL + bL) * smoothedGain_ * 0.5f;
            const float mR = (aR + bR) * smoothedGain_ * 0.5f;
            outL[i] = mL;
            if (outR) outR[i] = mR;
        }
        (void)hamster;
        updateDisplay(n);
    }

    // Stickers persist with the project: [count u8, positions float x count] per sample.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        out.push_back((uint8_t)(learnedNote_ < 0 ? 255 : learnedNote_));
        for (int s = 0; s < kNumSamples; ++s)
        {
            out.push_back((uint8_t)stickerCount_[s]);
            for (int k = 0; k < stickerCount_[s]; ++k)
            {
                for (float f : { stickers_[s][k], stickerTurn_[s][k] })
                {
                    uint32_t b; std::memcpy(&b, &f, 4);
                    for (int j = 0; j < 4; ++j) out.push_back((uint8_t)(b >> (8 * j)));
                }
            }
        }
        out.push_back(anchorValid_ ? 1 : 0); out.push_back((uint8_t)anchorDeck_);     // loop anchor
        for (double v : { anchorPpq_, anchorPos_, anchorTurn_, anchorRate_ })
        {
            uint64_t b; std::memcpy(&b, &v, 8);
            for (int j = 0; j < 8; ++j) out.push_back((uint8_t)(b >> (8 * j)));
        }
    }

    void loadExtraState(const uint8_t* d, size_t size) override
    {
        scrollResync_ = true;
        size_t p = 0;
        if (p >= size) return;
        learnedNote_ = d[p] == 255 ? -1 : d[p]; ++p;
        for (int s = 0; s < kNumSamples && p < size; ++s)
        {
            int c = d[p++];
            if (c > kMaxStickers || p + (size_t)c * 8 > size) return;
            stickerCount_[s] = c;
            for (int k = 0; k < c; ++k, p += 8)
            {
                uint32_t b = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24);
                uint32_t t = d[p + 4] | (d[p + 5] << 8) | (d[p + 6] << 16) | ((uint32_t)d[p + 7] << 24);
                std::memcpy(&stickers_[s][k], &b, 4); std::memcpy(&stickerTurn_[s][k], &t, 4);
            }
        }
        if (p + 34 <= size)
        {
            anchorValid_ = d[p] != 0; anchorDeck_ = d[p + 1]; p += 2;
            double* dst[4] = { &anchorPpq_, &anchorPos_, &anchorTurn_, &anchorRate_ };
            for (int k = 0; k < 4; ++k, p += 8)
            {
                uint64_t b = 0; for (int j = 0; j < 8; ++j) b |= (uint64_t)d[p + j] << (8 * j);
                std::memcpy(dst[k], &b, 8);
            }
        }
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Scratch";
        info.vendor       = "Keebz";
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);
        info.version      = 1000;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    static constexpr char kUid[5] = "KpBT";

    void loadSample(int slot, const std::string& path)
    {
        dsp::WavData w;
        if (!dsp::loadWav(path, w)) return;
        Sample& s = samples_[slot];
        s.rate = w.sampleRate;
        size_t frames = w.numFrames();
        const size_t cap = (size_t)(kMaxSampleSeconds * w.sampleRate);
        if (frames > cap) frames = cap;
        s.l.assign(w.channels[0].begin(), w.channels[0].begin() + frames);
        const std::vector<float>& rch = w.numChannels() > 1 ? w.channels[1] : w.channels[0];
        s.r.assign(rch.begin(), rch.begin() + frames);
    }

    const Sample* deckSample() const
    {
        const int d = lastDeck_;
        if (d < 1 || d > kNumSamples || samples_[d - 1].size() < 2) return nullptr;
        return &samples_[d - 1];
    }

    // Strip carrier delivered a new position 0..1; the tracker (strip_tracker.h) turns the stream of positions
    // into a smooth head path.
    void stripMoved(float v, double throwFrac, bool zoned = false)
    {
        zoned_ = zoned;
        const Sample* s = deckSample();
        if (!s) return;
        if (!touched_)
        {
            touched_ = true;
            grabStrip_ = v; lastStrip_ = v;
            grabPos_ = pos_;
            targetPos_ = pos_;
            trk_.reset(pos_);
            DBG('G', (int)(v * 16383), 0, pos_);
        }
        else
        {
            // The strip reports an exact rest value (-1) when the finger lifts, one message after the last real
            // position (measured: 84 lifts in the log, always followed by >100 ms of silence). The same value is
            // sent while a finger sits on the centre dead band, but then more arrive at the normal ~13 ms pace.
            // So a rest value after real movement is held back: if another message follows quickly it was a
            // touch at the centre and is used as an ordinary position, if not it was a lift.
            const bool rest = !zoned && std::fabs(v - kRestV) < 1e-5f, lastRest = std::fabs(lastStrip_ - kRestV) < 1e-5f;
            if (pendingRest_) pendingRest_ = false;                  // something followed: it was the centre
            else if (rest && !lastRest && std::fabs(lastStrip_ - kRestV) > kLiftMin) { pendingRest_ = true; pendAge_ = 0; return; }
            if (!rest && std::fabs(v - lastStrip_) > kMaxStripJump)
            {
                grabStrip_ = v; grabPos_ = pos_;                     // implausible jump: a new touch elsewhere
                trk_.reset(pos_);
            }
        }
        lastStrip_ = v;
        sinceStrip_ = 0;
        targetPos_ = grabPos_ + (double)(v - grabStrip_) * throwFrac * kThrowSec * s->rate;   // Throw 100% = 0.8 s of audio per full strip pull, whatever the sample length
        trk_.message(targetPos_);
    }

    // ---- loop snap ---------------------------------------------------------------------------------------------
    // The sequencer records the strip and the pads, but not where the record was. So the plugin keeps one "anchor": the
    // record's state at the song position where the take started (or playback first started). When the transport
    // starts at that position or loops past it, the record goes back to the anchor, so the recorded scratches land on
    // the same part of the sample every pass. Returns the frame of this block where the snap happens (-1: none).
    int loopSnapBlock(const mpc::Transport& t, int n)
    {
        const bool on = getBool(kLoopSnap);
        if (!on) anchorValid_ = false;
        const bool ok = on && t.valid && t.ppqValid && t.playing && t.bpm > 0.0;
        if (!ok) { prevPlaying_ = false; prevRecording_ = t.recording; return -1; }

        const double ppq0 = t.ppqPos, dppq = (double)n / sr_ * t.bpm / 60.0, ppq1 = ppq0 + dppq;
        const bool jump = !prevPlaying_ || std::fabs(ppq0 - prevPpqEnd_) > 0.1;      // transport (re)started, looped or was moved
        const double eps = 0.05;                                                     // a loop may land a little after its start
        const bool hit = anchorValid_ && (jump ? (anchorPpq_ >= ppq0 - eps && anchorPpq_ < ppq1)
                                               : (anchorPpq_ >= ppq0 && anchorPpq_ < ppq1));
        int at = -1;
        if (hit)
        {
            at = mpc::clamp((int)((anchorPpq_ - ppq0) / dppq * n), 0, n - 1);
            DBG('N', 1, (int)(ppq0 * 1000), ppq0);
        }
        else if (!prevPlaying_ || (t.recording && !prevRecording_))
        {
            anchorValid_ = true; anchorPpq_ = ppq0; anchorDeck_ = lastDeck_; anchorPos_ = pos_; anchorTurn_ = turnSec_;
            anchorRate_ = touched_ ? rate_ : speed_;
            DBG('N', 0, (int)(ppq0 * 1000), pos_);
        }
        prevPlaying_ = true; prevRecording_ = t.recording; prevPpqEnd_ = ppq1;
        return at;
    }

    void snapToAnchor()
    {
        if (anchorDeck_ >= 1 && anchorDeck_ <= kNumSamples && samples_[anchorDeck_ - 1].size() >= 2)
        {
            if (getInt(kDeckSample) != anchorDeck_) set(kDeckSample, (float)anchorDeck_);
            lastDeck_ = anchorDeck_;
        }
        pos_ = targetPos_ = grabPos_ = anchorPos_; turnSec_ = anchorTurn_; speed_ = anchorRate_;
        touched_ = false; pendingRest_ = false;
        trk_.reset(pos_);
    }

    // why: 1 bend at centre, 2 bend jumped to centre, 3 timeout
    void releaseRecord(int why)
    {
        pendingRest_ = false;
        if (!touched_) return;
        DBG('R', why, 0, rate_);
        touched_ = false;
        speed_ = rate_;      // spin back up from the current velocity
    }

    // One sticker per record: tapping puts it at the needle (12 o'clock, where it then rides round with the record);
    // tapping again takes it off.
    void toggleSticker()
    {
        const Sample* s = deckSample();
        if (!s) return;
        const int slot = lastDeck_ - 1;
        if (stickerCount_[slot] > 0) stickerCount_[slot] = 0;
        else
        {
            double t = turnSec_ / kRevSec; t -= std::floor(t);
            stickers_[slot][0] = (float)(pos_ / (double)s->size()); stickerTurn_[slot][0] = (float)t; stickerCount_[slot] = 1;
        }
    }

    void setGate(float level, float rampMs)
    {
        gateTarget_ = level;
        const float step = 1.0f / std::fmax(1.0f, rampMs * 0.001f * (float)sr_);
        if (level > gateGain_) gateStepUp_ = step; else gateStepDown_ = step;
    }

    // The record's start point for a sample: its cue sticker if it has one, otherwise the start.
    double cuePos(int smp) const
    {
        if (smp < 1 || smp > kNumSamples || samples_[smp - 1].size() < 2) return 0.0;
        return stickerCount_[smp - 1] > 0 ? (double)stickers_[smp - 1][0] * (double)samples_[smp - 1].size() : 0.0;
    }

    // Put a library sample on the record and start it from its cue.
    void cueDeck(int smp)
    {
        lastDeck_ = smp;
        pos_ = cuePos(smp); speed_ = 0.0; touched_ = false; pendingRest_ = false;
        trk_.reset(pos_);
    }

    // The sample list: keeps the selected line on screen, writes the display parameters, starts / stops the preview, and
    // runs the LOAD button (armed: the next hardware pad hit stores the selected sample).
    void updateBrowser()
    {
        const int cnt = std::max(1, libCount_);
        // The encoder (and a drag) turn kScroll. How far one notch moves a parameter is the host's business, so only the
        // direction of the change is used: one line per change. Near either end the parameter is put back to the middle.
        const float sc = getNormalised(kScroll);
        if (scrollResync_.exchange(false)) lastScroll_ = sc;            // after a project / preset load: whatever it holds is not a notch
        if (sc != lastScroll_)
        {
            const int dir = sc > lastScroll_ ? 1 : -1;
            set(kBrowse, (float)mpc::clamp(getInt(kBrowse) + dir, 0, cnt - 1));
            if (sc < 0.2f || sc > 0.8f) { setNormalised(kScroll, 0.5f); lastScroll_ = 0.5f; scrollSync_ = true; }
            else lastScroll_ = sc;
        }
        int br = mpc::clamp(getInt(kBrowse), 0, cnt - 1);
        if (br != getInt(kBrowse)) set(kBrowse, (float)br);
        // the window is a pure function of the selection (the selected line sits in the middle except near the ends)
        winStart_ = mpc::clamp(br - kVisRows / 2, 0, std::max(0, libCount_ - kVisRows));
        for (int k = 0; k < kListRows; ++k)
        {
            const int item = winStart_ + k;
            const int want = (k < kVisRows && item < libCount_) ? item + 1 : 0;
            if (getInt(kRow1 + k) != want) set(kRow1 + k, (float)want);
        }
        const float hi = (float)(br - winStart_) / (float)(kVisRows - 1);
        if (std::fabs(getNormalised(kBrowseHi) - hi) > 1e-4f) setNormalised(kBrowseHi, hi);

        const bool prev = getBool(kPreview);
        if (br != lastBrowse_ || prev != lastPreview_)
        {
            lastBrowse_ = br; lastPreview_ = prev;
            if (prev && br < libCount_) startPreview(br); else stopPreview();
        }
        if (getBool(kSendB))                                           // SEND TO B: loop the selected sample on channel B; again = clear
        {
            set(kBLoop, getInt(kBLoop) == br + 1 ? 0.0f : (float)(br + 1));
            set(kSendB, 0.0f);
        }
        if (getBool(kStopB)) { set(kBLoop, 0.0f); set(kStopB, 0.0f); }
        if (getInt(kBLoop) != bSmp_) { bSmp_ = getInt(kBLoop); bPos_ = 0.0; }
        const bool arm = getBool(kArm);
        if (arm != lastArm_)
        {
            lastArm_ = arm;
            set(kStatus, arm ? 1.0f : 0.0f);
        }
    }

    void startPreview(int idx)
    {
        if (idx < 0 || idx >= kNumSamples || samples_[idx].size() <= 1) return;
        Shot& sh = shots_[0];
        sh.sample = idx; sh.pos = 0.0; sh.gain = 0.9f; sh.active = true;
    }
    void stopPreview() { shots_[0].active = false; }

    // A hardware pad (1-8) was hit.
    void padHit(int pad, float velocity, bool hamster)
    {
        if (getBool(kArm))
        {
            if (libCount_ > 0) set(kPadSmp1 + pad, (float)(mpc::clamp(getInt(kBrowse), 0, libCount_ - 1) + 1));
            set(kArm, 0.0f); lastArm_ = false;
            set(kStatus, (float)(pad + 2));
            statusAge_ = 0;
            return;
        }
        const int smp = getInt(kPadSmp1 + pad);
        if (smp < 1 || smp > libCount_ || samples_[smp - 1].size() < 2) return;
        if (smp != getInt(kDeckSample)) { set(kDeckSample, (float)smp); cueDeck(smp); }   // a different sample: cue it
        heldPad_ = pad;
        cutHeld_ = true;
        const float curve = get(kCutCurve) * 0.01f;                                       // the pad is also the crossfader
        const float level = (1.0f - curve) + curve * std::pow(velocity, 1.5f);
        const float ramp = 0.5f + curve * (60.0f * (1.0f - velocity) + 5.0f);
        setGate(hamster ? 1.0f - level : level, ramp);
    }

    void fireShot(int idx, float vel)
    {
        if (idx < 0 || idx >= kNumSamples || samples_[idx].size() <= 1) return;
        Shot* free = &shots_[0];
        for (Shot& sh : shots_) { if (!sh.active) { free = &sh; break; } }
        free->sample = idx; free->pos = 0.0; free->gain = vel; free->active = true;
    }

    // The MPC screen only redraws a parameter when the plugin tells it the value moved (writing the value is not
    // enough), so the record angle, crossfader, meters and stickers need this. The audio thread only stores the
    // values; this thread tells the host about the ones that changed, ~30 times a second, off the audio thread.
    void startNotifier()
    {
        if (notifyRun_) return;
        notifyRun_ = true;
        notifyTh_ = std::thread([this] {
            float sent[kNumParams] = {};
            while (notifyRun_)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                for (int i = kPlatterAng; i < kNumParams; ++i)
                {
                    // display values, plus the controls the plugin itself can change (hamster pad, Load stepping a pad's sample)
                    const bool show = (i >= kPlatterAng && i <= kLevelB) || (i >= kStk1 && i <= kStk1 + 7) || i >= kPadSmp1;
                    if (!show) continue;
                    const float v = getNormalised(i);
                    if (std::fabs(v - sent[i]) < 0.002f) continue;
                    sent[i] = v;
                    notifyHostParameterChanged(i);
                }
                if (scrollSync_.exchange(false)) notifyHostParameterChanged(kScroll);
                const float h = getNormalised(kHamster);
                if (h != sent[kHamster]) { sent[kHamster] = h; notifyHostParameterChanged(kHamster); }
            }
        });
    }

    // Write the display-only values the touch-screen skin shows (record angle, crossfader, meters, stickers).
    // Called once per block; the MPC UI reads these parameters like any other.
    void updateDisplay(int frames)
    {
        levelA_ = std::fmax(peakA_, levelA_ * std::pow(0.92f, frames / 128.0f));
        levelB_ = std::fmax(peakB_, levelB_ * std::pow(0.92f, frames / 128.0f));
        peakA_ = peakB_ = 0.0f;
        if (++displayTick_ % 4 != 0) return;                       // ~85 updates per second is plenty
        const Sample* s = deckSample();
        double turn = turnSec_ / kRevSec; turn -= std::floor(turn);
        // The label strip has 72 frames: parameter value i/71 shows frame i.
        setNormalised(kPlatterAng, (float)(std::floor(turn * 72.0) / 71.0));
        // Crossfader cap: closed at one end, open at the centre; the hamster switch puts the closed end on the other side.
        setNormalised(kCutPos, getBool(kHamster) ? 1.0f - 0.5f * gateGain_ : 0.5f * gateGain_);
        setNormalised(kLevelA, std::fmin(1.0f, levelA_ * 1.6f));
        setNormalised(kLevelB, std::fmin(1.0f, levelB_ * 1.6f));
        // Stickers are stuck to the record: a sticker placed at turn s sits under the needle (12 o'clock) when
        // the record is at s, so on screen (clockwise from the needle) it is at turn - s. The sticker strip has 37 frames: frame 0 blank,
        // frame j = angle (j-1)/36 of a turn; parameter value j/36.
        const int slot = lastDeck_ - 1;
        for (int k = 0; k < kMaxStickers; ++k)
        {
            float v = 0.0f;
            if (s && slot >= 0 && k < stickerCount_[slot])
            {
                double f = turn - (double)stickerTurn_[slot][k]; f -= std::floor(f);   // the sticker rides with the platter (clockwise)
                v = (float)((1 + ((int)std::lround(f * 36.0) % 36)) / 36.0);
            }
            setNormalised(kStk1 + k, v);
        }
        // pad slots: 0 empty, 1 loaded, 2 playing (its pad is held), 3 flashing (LOAD armed: pick me)
        flashClock_ += frames * 4;                                 // this runs every 4th block
        const bool armed = getBool(kArm), flash = ((long)(flashClock_ / (0.35 * sr_)) & 1) != 0;   // ~1.4 flashes a second
        for (int i = 0; i < 8; ++i)
        {
            const bool has = getInt(kPadSmp1 + i) >= 1;
            int st = has ? 1 : 0;
            if (heldPad_ == i && has) st = 2;
            if (armed && flash) st = 3;
            const float want = (float)st / 3.0f;
            if (std::fabs(getNormalised(kSlot1 + i) - want) > 1e-4f) setNormalised(kSlot1 + i, want);
        }
        statusAge_ += frames * 4;                                  // this runs every 4th block
        if (statusAge_ > (int)(2.5 * sr_) && getInt(kStatus) >= 2) set(kStatus, 0.0f);
    }

    void handleMidi(const mpc::MidiEvent& e, int src, double throwFrac, bool hamster, int cutNote, int shotBase)
    {
        DBG('M', e.data[0], e.data[1] | (e.data[2] << 8), (double)sinceStrip_);
        if (e.isPitchBend() && src == 0)
        {
            const int b = e.pitchBend();
            const int zone = getInt(kStripZone);
            if (zone == 0) stripMoved((float)(b + 8192) / 16383.0f, throwFrac);   // whole strip; the rest value is a position here
            else
            {
                // One half of the strip scrubs and the other half is ignored, so the strip's rest value (-1, the
                // middle) sits at the END of the scrubbing zone, where it can only mean "lifted": see stripMoved.
                if (b == -1) { if (touched_ && !pendingRest_) { pendingRest_ = true; pendAge_ = 0; } }
                else
                {
                    const float u = zone == 1 ? (b < -1 ? (float)(b + 8192) / 8191.0f : 1.0f)       // Bottom half: 0 at the bottom end, 1 at the middle
                                              : (b >= 0 ? (float)b / 8191.0f : 0.0f);               // Top half: 0 at the middle, 1 at the top end
                    stripMoved(u, throwFrac * 0.5, true);                                          // same feel per millimetre as the whole strip
                }
            }
        }
        else if (e.isController() && e.controller() == 1 && src == 1)
        {
            stripMoved((float)e.controllerValue() / 127.0f, throwFrac);
        }
        else if (e.isNoteOn())
        {
            const int note = e.note();
            if (note == getInt(kHamNote)) { set(kHamster, getBool(kHamster) ? 0.0f : 1.0f); return; }   // pad 12: toggles the hamster switch
            const int pad = note - shotBase;
            if (pad >= 0 && pad < 8) padHit(pad, e.velocity() / 127.0f, hamster);
        }
        else if (e.isNoteOff())
        {
            const int pad = e.note() - shotBase;
            if (pad >= 0 && pad < 8 && pad == heldPad_)
            {
                heldPad_ = -1; cutHeld_ = false;
                setGate(hamster ? 1.0f : 0.0f, 0.5f + get(kCutCurve) * 0.01f * 25.0f);
            }
        }
        else if (e.isAllNotesOff())
        {
            for (Shot& sh : shots_) sh.active = false;
        }
    }

#ifdef SCRATCH_DEBUG
    DbgLog   dbg_;
    bool     dbgStarted_ = false;
    float    dbgCtl_[20] = {};
    uint64_t clock_ = 0;
#endif
    Sample   samples_[kNumSamples];
    std::string names_[kNumSamples];
    int      libCount_ = 0;
    Shot     shots_[kNumShots];
    float    stickers_[kNumSamples][kMaxStickers] = {};      // cue point: fraction of the sample
    float    stickerTurn_[kNumSamples][kMaxStickers] = {};   // where on the platter it was stuck (fraction of a turn)
    int      stickerCount_[kNumSamples] = {};
    Eq3      eqA_, eqB_;

    double   sr_ = 44100.0;
    double   pos_ = 0.0, speed_ = 0.0, rate_ = 0.0;
    StripTracker trk_;
    double   grabPos_ = 0.0, targetPos_ = 0.0;
    float    grabStrip_ = 0.0f, lastStrip_ = 0.5f;
    static constexpr float kMaxStripJump = 0.14f, kLiftMin = 0.009f;   // of the strip range
    static constexpr float kRestV = 8191.0f / 16383.0f;              // the strip's rest value (-1)
    bool pendingRest_ = false, zoned_ = false; int pendAge_ = 0;
    bool     touched_ = false, armedCut_ = false;
    int      sinceStrip_ = 1 << 30, sinceSnap_ = 1 << 30;
    std::thread notifyTh_;
    std::atomic<bool> notifyRun_{false};
    float    ampG_ = 1.0f;
    bool     lastPad_[8] = {};
    bool     lastHamster_ = false, cutHeld_ = false, lastArm_ = false, lastPreview_ = false;
    int      heldPad_ = -1, winStart_ = 0, lastBrowse_ = -1, statusAge_ = 0, bSmp_ = 0;
    bool     anchorValid_ = false, prevPlaying_ = false, prevRecording_ = false;   // loop snap (see loopSnapBlock)
    int      anchorDeck_ = 0;
    double   anchorPpq_ = 0.0, anchorPos_ = 0.0, anchorTurn_ = 0.0, anchorRate_ = 0.0, prevPpqEnd_ = 0.0;
    double   turnSec_ = 0.0;                // platter angle: seconds of record travelled (unwrapped), 1.8 s per turn
    double   bPos_ = 0.0;                   // channel B loop position (frames of its sample)
    float    peakA_ = 0.0f, peakB_ = 0.0f, levelA_ = 0.0f, levelB_ = 0.0f;
    double   flashClock_ = 0.0;
    float    lastScroll_ = 0.5f;
    std::atomic<bool> scrollSync_{false}, scrollResync_{false};
    unsigned displayTick_ = 0;     // smoothed deck volume (speed fade near standstill)
    int      learnedNote_ = -1;
    int      lastDeck_ = 1, lastGo_ = 0;
    bool     lastSticker_ = false;
    float    lastScrub_ = 0.5f;
    float    gateGain_ = 0.0f, gateTarget_ = 0.0f, gateStepUp_ = 1.0f, gateStepDown_ = 1.0f;
    float    smoothedGain_ = 0.5f;
};

MPC_PLUGIN_ENTRY(Scratch)
