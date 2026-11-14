// Desktop test for the Scratch engine: drives process() directly with strip/pad MIDI.
//   g++ -std=c++17 -O1 -w -I framework -I plugins/Scratch/src plugins/Scratch/tests/scratch_test.cpp -o /tmp/scratch_test
// Run from plugins/Scratch after building (uses build/Keebz - VST - Scratch/Content).
//
// The strip timing model comes from a debug log recorded on a real MPC Live III (Pitch Bend mode):
// clean 14-bit values, messages 11.6 / 14.5 ms apart (block-quantised), rest value -1, and NO message
// when the finger lifts.
#define private public
#include "../src/Scratch.cpp"
#undef private
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <complex>
#include <functional>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s: ", #c); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("PASS %s\n", #c); } } while (0)

static Scratch* P;
static std::vector<float> L(128), R(128);

// Render `blocks` 128-frame blocks; events apply at the first block (as the MPC delivers them). Returns RMS.
static double blocksRun(int blocks, std::vector<mpc::MidiEvent> ev = {})
{
    double sum = 0; int cnt = 0;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f);
        float* outs[2] = { L.data(), R.data() };
        mpc::ProcessContext c; c.outputs = outs; c.numOutputs = 2; c.numFrames = 128; c.sampleRate = 44100;
        if (b == 0 && !ev.empty()) { c.midi = ev.data(); c.numMidiEvents = (int)ev.size(); }
        P->process(c);
        for (int i = 0; i < 128; ++i) { sum += L[i] * L[i]; ++cnt; }
    }
    return std::sqrt(sum / std::max(1, cnt));
}
static double run(double ms, std::vector<mpc::MidiEvent> ev = {}) { return blocksRun(std::max(1, (int)std::ceil(ms * 44.1 / 128.0)), ev); }
static mpc::MidiEvent bend(int v) { mpc::MidiEvent e; e.size = 3; e.data[0] = 0xE0; int u = v + 8192; e.data[1] = u & 127; e.data[2] = u >> 7; return e; }

static void setup(float smooth = 80.0f, float throwPct = 100.0f, float relMs = 250.0f)
{
    P->reset(); P->set(kDeckSample, 1.0f); P->set(kSmooth, smooth); P->set(kThrow, throwPct); P->set(kStripRel, relMs);
    P->set(kHamster, 0.0f); P->set(kStripZone, 0.0f);   // the tests below model the whole strip unless a test sets a zone
    blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(20);
}

// A clean pull at `unitsPerMs` (steady strip steps) delivered with the Live III's alternating 4/5-block spacing. Records rate_ per block.
static void pull(double unitsPerMs, int messages, int start, std::vector<double>* rates, int* lastValue = nullptr)
{
    for (int m = 0; m < messages; ++m)
    {
        const int blocks = (m % 2 == 0) ? 4 : 5;                 // delivered 11.6 ms / 14.5 ms apart...
        const int v = start + (int)(unitsPerMs * 13.06 * m);     // ...but the strip itself steps evenly (log: corr(step, gap)=0.06)
        for (int b = 0; b < blocks; ++b)
        {
            blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(v) } : std::vector<mpc::MidiEvent>{});
            if (rates) rates->push_back(P->rate_);
        }
        if (lastValue) *lastValue = v;
    }
}


// ---- sample-accurate evaluation of the real plugin against an emulated strip ---------------------------------
// The finger moves along x(t) (strip units). The strip samples it every `Tphys` seconds (14-bit rounding) and the
// message is delivered at the start of the next 128-frame block, like the MPC. process() is then called one frame at
// a time (events only on block starts) so the head position can be read after every sample.
struct Eval { std::vector<double> head, truth, out; };
static Eval evalMotion(const std::function<double(double)>& x, double seconds, double Tphys, float smooth = 80.0f, float lead = 100.0f, std::vector<float>* sine = nullptr)
{
    setup(smooth); P->set(kLead, lead);
    if (sine) { P->samples_[0].l = *sine; P->samples_[0].r = *sine; P->samples_[0].rate = 44100.0; }
    const double len = (double)P->samples_[0].size(), fpu = 1.0 * len / 16383.0;   // Throw 100%
    P->pos_ = len * 0.5; const double grab = P->pos_;
    Eval e; const int total = (int)(seconds * 44100.0); double nextMsg = 0;
    float oL = 0, oR = 0; float* outs[2] = { &oL, &oR };
    for (int i = 0; i < total; ++i)
    {
        std::vector<mpc::MidiEvent> ev;
        if (i % 128 == 0) { const double tb = i / 44100.0; while (nextMsg <= tb) { ev.push_back(bend((int)std::lround(x(nextMsg)))); nextMsg += Tphys; } }
        mpc::ProcessContext c; c.outputs = outs; c.numOutputs = 2; c.numFrames = 1; c.sampleRate = 44100;
        if (!ev.empty()) { c.midi = ev.data(); c.numMidiEvents = (int)ev.size(); }
        oL = oR = 0; P->process(c);
        e.head.push_back(P->pos_); e.out.push_back(oL);
        e.truth.push_back(grab + (x(i / 44100.0) - x(0.0)) * fpu);
    }
    return e;
}
// share of the head speed's energy above 20 Hz, in %
static double rippleOf(const std::vector<double>& head)
{
    const size_t skip = (size_t)(0.3 * 44100), n = head.size();
    size_t N = 1; while (N < n - 2 * skip) N <<= 1; if (N > n - 2 * skip) N >>= 1;
    std::vector<std::complex<double>> X(N);
    for (size_t i = 0; i < N; ++i) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)); X[i] = (head[skip + i] - head[skip + i - 1]) * w; }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(X[i], X[j]); }
    for (size_t l = 2; l <= N; l <<= 1) { double a = -2 * M_PI / l; std::complex<double> wl(std::cos(a), std::sin(a));
        for (size_t i = 0; i < N; i += l) { std::complex<double> w(1); for (size_t k = 0; k < l / 2; ++k) { auto u = X[i + k], t2 = X[i + k + l / 2] * w; X[i + k] = u + t2; X[i + k + l / 2] = u - t2; w *= wl; } } }
    double hi = 0, all = 0; for (size_t k = 1; k < N / 2; ++k) { double f = k * 44100.0 / N, en = std::norm(X[k]); if (f > 0.5) all += en; if (f > 20.0) hi += en; }
    return 100.0 * std::sqrt(hi / std::fmax(all, 1e-12));
}
static void latencyOf(const Eval& e, double& latencyMs, double& errPct)
{
    const size_t skip = (size_t)(0.5 * 44100), n = e.head.size(); double best = 1e30; int bl = 0;
    for (int lag = 0; lag <= (int)(0.08 * 44100); lag += 4)
    { double s2 = 0; size_t c = 0; for (size_t i = skip; i < n; i += 3) { double d = e.head[i] - e.truth[i - lag]; s2 += d * d; ++c; } s2 /= c; if (s2 < best) { best = s2; bl = lag; } }
    double lo = 1e30, hi = -1e30; for (size_t i = skip; i < n; ++i) { lo = std::fmin(lo, e.truth[i]); hi = std::fmax(hi, e.truth[i]); }
    latencyMs = bl / 44.1; errPct = 100.0 * std::sqrt(best) / std::fmax(1e-9, hi - lo);
}

int main()
{
    setenv("MPC_PLUGIN_FOLDER", getenv("SCRATCH_FOLDER") ? getenv("SCRATCH_FOLDER") : "tests/fixture", 1);
    Scratch plugin; P = &plugin;
    plugin.prepare(44100.0, 128);
    CHECK(plugin.samples_[0].size() > 1000, "sample 01 loaded (%zu frames)", plugin.samples_[0].size());

    // 1. cut pad (note 39)
    double r = run(100);
    CHECK(r < 1e-4, "deck gated closed by default, rms=%g", r);
    run(1, { mpc::MidiEvent::noteOn(0, 36, 127) });
    run(300);                                        // Ahh.wav fades in
    r = run(300);
    CHECK(r > 0.02, "cut pad opens deck, rms=%g", r);
    run(1, { mpc::MidiEvent::noteOff(0, 36) });
    r = run(100);
    CHECK(r < 1e-3, "cut pad release closes deck, rms=%g", r);

    // 2. hamster reverses the cut
    plugin.set(kHamster, 1.0f); plugin.reset(); plugin.set(kHamster, 1.0f);
    plugin.gateGain_ = plugin.gateTarget_ = 1.0f;
    run(300); r = run(300);
    CHECK(r > 0.02, "hamster: open by default, rms=%g", r);
    run(1, { mpc::MidiEvent::noteOn(0, 36, 127) });
    r = run(100);
    CHECK(r < 1e-3, "hamster: pad press cuts, rms=%g", r);
    plugin.set(kHamster, 0.0f); plugin.reset();

    // 3. velocity curve
    plugin.set(kCutCurve, 100.0f);
    run(1, { mpc::MidiEvent::noteOn(0, 36, 20) }); double soft = plugin.gateTarget_;
    plugin.reset();
    run(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); double hard = plugin.gateTarget_;
    CHECK(soft < 0.2 && hard > 0.95, "curve: soft target %.2f, hard target %.2f", soft, hard);
    plugin.set(kCutCurve, 0.0f); plugin.reset();
    run(1, { mpc::MidiEvent::noteOn(0, 36, 20) });
    CHECK(plugin.gateTarget_ > 0.99, "curve 0: any velocity gives full cut");

    setup();
    // 4. scratch directions + stopping + release by silence
    plugin.pos_ = 0.5 * (double)plugin.samples_[0].size(); plugin.trk_.reset(plugin.pos_);   // mid-sample so the pulls below do not wrap round the loop
    double p0 = plugin.pos_;
    int last = 0;
    pull(+30, 12, -3000, nullptr, &last);            // finger up
    double p1 = plugin.pos_;
    CHECK(plugin.touched_, "record grabbed");
    CHECK(p1 > p0, "finger up moves the head forward (%+.0f frames)", p1 - p0);
    pull(-30, 12, last, nullptr, &last);             // finger down
    double p2 = plugin.pos_;
    CHECK(p2 < p1, "finger down moves the head backwards (%+.0f frames)", p2 - p1);
    run(60, { bend(last) }); r = run(100, { bend(last) });
    CHECK(r < 4e-3, "held record is silent, rms=%g", r);
    run(400);
    CHECK(plugin.touched_, "a finger held still off-centre stays held (the strip only reports now and then: gaps up to 0.6 s)");
    run(800);
    CHECK(!plugin.touched_, "record released after a long silence off-centre (4 x Strip Rel)");
    r = run(150);
    CHECK(r > 0.02 && plugin.rate_ > 0.9, "released record is back at full speed and volume (rate %.2fx, rms %.3f)", plugin.rate_, r);

    // 5. WAV loop wrap must not spike the speed
    auto maxRate = [&](double startFrac, int from, double upm) {
        setup(); plugin.pos_ = plugin.samples_[0].size() * startFrac;
        std::vector<double> rates; pull(upm, 40, from, &rates);
        double mx = 0; for (double x : rates) mx = std::fmax(mx, std::fabs(x)); return mx;
    };
    double mUp = maxRate(0.97, -3000, +20), mDn = maxRate(0.02, 3000, -20), mMid = maxRate(0.5, -3000, +20);
    std::printf("max |rate|: wrap fwd %.1fx, wrap rev %.1fx, no wrap %.1fx\n", mUp, mDn, mMid);
    CHECK(mUp < mMid * 1.5 + 1 && mDn < mMid * 1.5 + 1, "loop wrap does not cause a speed spike");

    // 6. smoothness with the real strip timing: a steady pull must give a steady speed
    for (float sm : { 2.0f, 20.0f, 50.0f, 80.0f })
    {
        setup(sm); std::vector<double> rates; pull(+20, 60, -4000, &rates);
        double sum = 0, sum2 = 0; int cnt = 0;
        for (size_t i = 120; i < rates.size(); ++i) { sum += rates[i]; sum2 += rates[i] * rates[i]; ++cnt; }
        double m = sum / cnt, sd = std::sqrt(sum2 / cnt - m * m);
        std::printf("steady pull, Smooth %2.0f: mean %.2fx, wobble %.1f%% of speed\n", sm, m, 100 * sd / std::fabs(m));
        if (sm == 50.0f) CHECK(sd < std::fabs(m) * 0.06, "default Smooth: steady pull wobbles only %.1f%%", 100 * sd / std::fabs(m));
    }

    // 7. lag during a long fast pull (ms of audio the head is behind the latest strip value)
    auto lagMs = [&](float smooth, double upm, double& speedX) {
        setup(smooth); std::vector<double> rates; pull(upm, 36, -7000, &rates);
        const double fileStep = plugin.samples_[0].rate / 44100.0;
        speedX = plugin.rate_;
        return (plugin.targetPos_ - plugin.pos_) / std::fmax(1e-9, std::fabs(plugin.rate_) * fileStep) / 44.1;
    };
    double sA, sB, sC; double lag50 = lagMs(50, 25, sA), lag80 = lagMs(80, 25, sB), lag10 = lagMs(10, 25, sC);
    std::printf("lag in a long pull: Smooth 10 -> %.0f ms (%.1fx), 50 -> %.0f ms (%.1fx), 80 -> %.0f ms (%.1fx)\n", lag10, sC, lag50, sA, lag80, sB);
    CHECK(lag80 < 25, "long pull at Smooth 80: head only %.0f ms behind the finger", lag80);

    // 8. stopping with the finger: no wind-down
    setup(50);
    { int lv; pull(+25, 30, -5000, nullptr, &lv); double cruise = std::fabs(plugin.rate_);
      double ms = 0; bool quiet = false;
      for (int i = 0; i < 100 && !quiet; ++i) { blocksRun(1); ms += 128 / 44.1; if (std::fabs(plugin.rate_) < 0.1 * cruise) quiet = true; }
      std::printf("stop: cruise %.2fx -> below 10%% after %.0f ms with no more messages\n", cruise, ms);
      CHECK(quiet && ms < 50, "record stops about when the finger stops (%.0f ms)", ms); }

    // 9. a finger resting at the centre (rest value -1, irregular messages up to ~190 ms apart) keeps the record
    setup(50);
    { const int gapsMs[] = { 49, 26, 15, 64, 49, 15, 38, 12, 26, 26, 12, 12, 15, 40, 26, 55, 12, 12, 15, 12 };   // gaps up to 66 ms covered 95% of a finger resting on the centre (build 1 log)
      double c0 = plugin.pos_;
      blocksRun(1, { bend(-1) });
      CHECK(plugin.touched_, "touch at the exact centre grabs the record");
      bool held = true;
      for (int g : gapsMs) { run(g, {}); if (!plugin.touched_) held = false; blocksRun(1, { bend(-1) }); }
      CHECK(held, "resting finger at the centre is never mistaken for a lift");
      run(150);
      CHECK(!plugin.touched_, "a lift from the centre (silent) is detected after Strip Rel / 3 (%.0f ms)", plugin.get(kStripRel) / 3);
      double c1 = plugin.pos_; (void)c0; (void)c1;
      for (int v = -1; v >= -1500; v -= 150) run(12, { bend(v) });
      CHECK(plugin.touched_, "finger pulling away from the centre moves the record"); }

    // 10. stickers + state
    setup(); plugin.reset(); plugin.pos_ = plugin.samples_[0].size() * 0.25;
    plugin.set(kSticker, 1.0f); run(1);
    CHECK(plugin.stickerCount_[0] == 1, "sticker added");
    std::vector<uint8_t> st; plugin.saveExtraState(st);
    plugin.stickerCount_[0] = 0; plugin.loadExtraState(st.data(), st.size());
    CHECK(plugin.stickerCount_[0] == 1 && std::fabs(plugin.stickers_[0][0] - 0.25f) < 0.01f, "sticker survives state save/load");
    plugin.set(kSticker, 0.0f); run(1);
    CHECK(plugin.stickerCount_[0] == 0, "tap again removes sticker");
    plugin.stickers_[0][0] = 0.75f; plugin.stickerCount_[0] = 1;
    plugin.set(kGoSticker, 1.0f); run(1);
    CHECK(std::fabs(plugin.pos_ / plugin.samples_[0].size() - 0.75) < 0.01, "Go Sticker jumps platter");

    // 11. warble / latency / tracking of the real plugin on scratch motions (emulated Live III strip), default settings.
    //     For reference the old one-pole 80 ms smoother had: 4.3% / 7.7% ripple (3 / 6 Hz) at 64 / 44 ms latency.
    { struct M { const char* name; std::function<double(double)> x; double secs; double maxRip, maxLat, maxErr; };
      const M ms[] = {
        { "steady pull 0.3x",     [](double t) { return -4000 + 5700 * t; },                    1.4, 4.0, 28.0, 1.0 },      // slow speeds: the strip's 14-bit steps are a larger share of each move (~3% floor)
        { "slow drag",            [](double t) { return -2000 + 2500 * t; },                    1.6, 8.0, 28.0, 1.0 },
        { "scratch 3 Hz +-2500",  [](double t) { return 2500 * std::sin(2 * M_PI * 3 * t); },   2.0, 3.5, 28.0, 1.5 },
        { "scratch 6 Hz +-3000",  [](double t) { return 3000 * std::sin(2 * M_PI * 6 * t); },   2.0, 9.0, 28.0, 3.0 },
      };
      for (const M& m : ms)
      {
          Eval e = evalMotion(m.x, m.secs, 0.01306); double lat, err; latencyOf(e, lat, err); double rip = rippleOf(e.head);
          std::printf("%-22s ripple %.1f%%, latency %.1f ms, error %.2f%%\n", m.name, rip, lat, err);
          CHECK(rip < m.maxRip && lat < m.maxLat && err < m.maxErr, "%s: warble, latency and tracking within limits", m.name);
      } }

    // 12. robust to the strip's real message interval (unknown in advance): steady pull must stay smooth
    for (double T : { 0.0119, 0.0124, 0.01306, 0.0136, 0.0142 })
    {
        Eval e = evalMotion([](double t) { return -4000 + 5700 * t; }, 1.4, T); double rip = rippleOf(e.head);
        CHECK(rip < 4.0, "steady pull with a %.2f ms strip interval: ripple %.2f%%", T * 1000, rip);
    }

    // 13. no dead zone: a finger at rest on the centre (rest value -1, irregular messages) that starts to move must
    //     get the record moving at once, in BOTH directions, equally fast.
    { double onset[2]; int k = 0;
      for (int dir : { +1, -1 })
      {
          setup(); const int gaps[] = { 49, 26, 15, 64, 49, 15, 190, 12, 26, 26, 12, 12, 15, 165, 26, 78 };
          for (int g : gaps) { blocksRun(1, { bend(-1) }); run(g); }
          const double p0 = plugin.pos_; double t = 0; bool moved = false; double half = -1;
          for (int m = 0; m < 12; ++m)                                   // pull away from the centre at 1x
          {
              const int v = -1 + dir * (int)(260 * (m + 1));
              for (int b = 0; b < ((m % 2 == 0) ? 4 : 5); ++b)
              {
                  blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(v) } : std::vector<mpc::MidiEvent>{});
                  t += 128 / 44.1;
                  if (!moved && std::fabs(plugin.pos_ - p0) > 25.0) { moved = true; onset[k] = t; }
              }
          }
          CHECK(moved, "pulling %s from the centre moves the record", dir > 0 ? "up" : "down");
          ++k;
      }
      std::printf("onset from the centre: up %.1f ms, down %.1f ms\n", onset[0], onset[1]);
      CHECK(onset[0] < 45 && onset[1] < 45 && std::fabs(onset[0] - onset[1]) < 12, "centre is not dead: record starts moving within %.0f / %.0f ms, same both ways", onset[0], onset[1]); }

    // 14. audio purity: scratching a pure 1 kHz tone at a steady speed must give a steady pitch (no warble, no grit)
    { std::vector<float> sine(88200); for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * std::sin(2 * M_PI * 1000.0 * i / 44100.0);
      for (double upm : { 4.4, 9.6, 14.0 })                                  // ~0.5x, 1x, 1.5x with this Throw / sample length
      {
          Eval e = evalMotion([upm](double t) { return -4000 + upm * 1000 * t; }, 1.6, 0.01306, 20.0f, 50.0f, &sine);
          std::vector<double> periods; double last = -1; size_t from = (size_t)(0.6 * 44100);
          for (size_t i = from + 1; i < e.out.size(); ++i)
              if (e.out[i - 1] < 0 && e.out[i] >= 0) { double fr = -e.out[i - 1] / (e.out[i] - e.out[i - 1]); double tt = i - 1 + fr; if (last >= 0) periods.push_back(tt - last); last = tt; }
          double m = 0, v2 = 0; for (double p : periods) m += p; m /= periods.size(); for (double p : periods) v2 += (p - m) * (p - m); v2 = std::sqrt(v2 / periods.size());
          const double hz = 44100.0 / m;
          std::printf("1 kHz tone pulled at ~%.2fx: %.0f Hz, pitch wobble %.2f%% (%zu cycles)\n", hz / 1000.0, hz, 100 * v2 / m, periods.size());
          CHECK(periods.size() > 100 && v2 / m < 0.025, "pure tone stays pure at %.0f Hz: pitch wobble %.2f%%", hz, 100 * v2 / m);
      } }

    // 15. replay of the REAL pull recorded on the Live III (values and block gaps from scratch_log.txt)
    { const int val[] = {-1,-142,-428,-849,-1270,-1675,-2062,-2432,-2769,-3089,-3409,-3763,-4134,-4588,-5127,-5784,-6424,-7014,-7569,-8041,-8176,-8192};
      const int gap[] = { 4,5,4,4,4,5,4,5,4,5,4,4,5,4,5,4,4,5,5,4,5 };
      setup(); plugin.set(kThrow, 50.0f); std::vector<double> rs; const double p0 = plugin.pos_; double onsetMs = -1, t = 0;
      for (int i = 0; i < 22; ++i) { int n = (i < 21) ? gap[i] : 12; for (int b = 0; b < n; ++b) { blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(val[i]) } : std::vector<mpc::MidiEvent>{}); t += 128 / 44.1; if (i >= 3 && i < 19) rs.push_back(plugin.rate_); if (onsetMs < 0 && i >= 1 && std::fabs(plugin.pos_ - p0) > 30) onsetMs = t; } }
      double d2 = 0, mean = 0; for (size_t i = 1; i + 1 < rs.size(); ++i) { d2 += std::fabs(rs[i + 1] - 2 * rs[i] + rs[i - 1]); mean += std::fabs(rs[i]); }
      const double rough = 100.0 * (d2 / (rs.size() - 2)) / (mean / (rs.size() - 2));
      std::printf("real recorded pull: roughness %.1f%% of mean speed; head first moves %.1f ms after the finger leaves the centre\n", rough, onsetMs - 4 * 128 / 44.1 );
      CHECK(rough < 5.0, "real recorded pull plays smoothly (roughness %.1f%%)", rough); }

    // 16. touch-screen UI plumbing: the display-only parameters the skin shows
    { setup(); plugin.set(kHamster, 0.0f);

      // display values
      setup(); plugin.reset(); plugin.set(kDeckSample, 1.0f);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(200);
      const double len = (double)plugin.samples_[0].size();
      plugin.pos_ = len * 0.25; plugin.turnSec_ = 0.25 * kRevSec; run(20);          // the platter turns 1.8 s per revolution, whatever the sample length
      const double turn = plugin.pos_ / len; (void)turn;
      const int frame = (int)std::lround(plugin.getNormalised(kPlatterAng) * 71.0);
      std::printf("record angle frame %d of 72 (head at %.3f of a turn)\n", frame, plugin.pos_ / len - std::floor(plugin.pos_ / len));
      CHECK(std::abs(frame - 18) <= 5, "record-angle display follows the head (frame %d, expected ~18)", frame);
      CHECK(std::fabs(plugin.getNormalised(kCutPos) - 0.5f) < 0.02f, "crossfader cap sits at the centre when the cut is open (%.2f)", plugin.getNormalised(kCutPos));
      CHECK(plugin.getNormalised(kLevelA) > 0.02, "channel A meter shows the deck playing (%.2f)", plugin.getNormalised(kLevelA));
      // sticker placed at the needle shows at 12 o'clock (frame 1), and moves clockwise as the record turns
      plugin.stickerCount_[0] = 0;                                              // clean slate
      plugin.pos_ = len * 0.25; plugin.turnSec_ = 0.25 * kRevSec; run(1);
      plugin.set(kSticker, plugin.getBool(kSticker) ? 0.0f : 1.0f); run(20);
      CHECK(plugin.stickerCount_[0] == 1, "tapping the record adds a sticker");
      int j0 = (int)std::lround(plugin.getNormalised(kStk1) * 36.0);
      plugin.turnSec_ = 0.50 * kRevSec; run(20);                                      // a quarter turn later
      int j1 = (int)std::lround(plugin.getNormalised(kStk1) * 36.0);
      std::printf("sticker frame: at the needle %d, after a quarter turn %d (frame 1 = 12 o'clock, +1 per 10 degrees)\n", j0, j1);
      CHECK(j0 >= 1 && j0 <= 2 && std::abs(j1 - (j0 + 9)) <= 2, "sticker rides the record: frame %d -> %d after 90 degrees", j0, j1);
      CHECK(plugin.getNormalised(kStk2) == 0.0f, "unused sticker slots stay blank"); }


    // Strip spring-back: the finger lifts off-centre and the strip reports the centre at once. The record must
    // release and play on at normal speed, not sweep to the centre ("zip").
    for (int dir : { -1, 1 }) {
      setup(); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100);
      double maxRate = 0; int last = -1;
      for (int m = 0; m < 12; ++m) { last = dir * (m * 500); for (int b = 0; b < 4; ++b) { blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(last) } : std::vector<mpc::MidiEvent>{}); } }
      for (int b = 0; b < 40; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(last) } : std::vector<mpc::MidiEvent>{});   // held still
      const double posBefore = plugin.pos_;
      for (int b = 0; b < 120; ++b) {
          blocksRun(1, (b == 0) ? std::vector<mpc::MidiEvent>{ bend(-1) } : std::vector<mpc::MidiEvent>{});
          maxRate = std::max(maxRate, std::fabs(plugin.rate_));
      }
      std::printf("spring-back %s: touched=%d max |rate| %.2f x, moved %.0f frames\n", dir < 0 ? "down" : "up", (int)plugin.touched_, maxRate, std::fabs(plugin.pos_ - posBefore));
      CHECK(!plugin.touched_, "record is released by the spring-back (dir %d)", dir);
      CHECK(maxRate < 2.0, "no zip after the spring-back (dir %d, max %.2fx)", dir, maxRate);
    }


    // Behaviours seen in a real Live III log (build 1): a lift is one rest value (-1) followed by silence; a finger on the
    // centre sends -1 at the normal pace; a held finger reports only now and then (gaps up to 0.6 s).
    {   setup(); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100);
        for (int m = 0; m < 10; ++m) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(2000 + m * 17) } : std::vector<mpc::MidiEvent>{}); }
        for (int k = 0; k < 3; ++k) { run(550, { bend(2150) }); }
        CHECK(plugin.touched_, "a finger held off-centre, reporting every 0.55 s, stays held");
        const double tl = plugin.pos_;
        blocksRun(1, { bend(-1) }); double maxr = 0; int relBlocks = -1;
        for (int b = 0; b < 40; ++b) { blocksRun(1); maxr = std::max(maxr, std::fabs(plugin.rate_)); if (relBlocks < 0 && !plugin.touched_) relBlocks = b; }
        std::printf("lift: released after %d blocks (%.1f ms), max rate %.2f, moved %.0f frames\n", relBlocks, relBlocks * 2.9, maxr, std::fabs(plugin.pos_ - tl));
        CHECK(relBlocks >= 0 && relBlocks <= 14, "a lone rest value after movement is a lift (released after %d blocks)", relBlocks);
        CHECK(maxr < 2.0, "...and the record is not dragged to the centre (max %.2fx)", maxr); }
    {   setup(); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100);
        for (int m = 0; m < 12; ++m) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(-1) } : std::vector<mpc::MidiEvent>{}); }
        CHECK(plugin.touched_, "a finger resting on the centre holds the record like anywhere else");
        plugin.pos_ = 0.5 * (double)plugin.samples_[0].size(); plugin.trk_.reset(plugin.pos_); plugin.grabPos_ = plugin.pos_; const double p0 = plugin.pos_;
        for (int m = 0; m < 12; ++m) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(-60 - m * 600) } : std::vector<mpc::MidiEvent>{}); }
        CHECK(plugin.touched_ && plugin.pos_ < p0 - 500, "...and a pull from the centre moves it straight away (%+.0f frames)", plugin.pos_ - p0); }
    {   setup(); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100);
        int seq[] = { 2400, 1800, 1200, 600, -1, -600, -1200, -1800, -2400 };            // a fast pull through the dead band lands on -1
        for (int v : seq) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(v) } : std::vector<mpc::MidiEvent>{}); }
        CHECK(plugin.touched_, "a pull through the centre is not mistaken for a lift"); }


    // Wider tuning range: Lead 200% with Smooth 160 (smoother than the default at similar delay) must not overshoot when the finger stops.
    for (float lead : { 100.0f, 200.0f, 300.0f }) {
      auto x = [](double t) { return t < 0.4 ? -3000.0 + 7500.0 * t : 0.0; };            // pull, then stop dead
      Eval e = evalMotion(x, 0.9, 0.01306, 160.0f, lead);
      double maxPast = 0; const double travel = 3000.0;                                    // finger ends at 0 strip units
      for (size_t i = 0; i < e.head.size(); ++i) if (i / 44100.0 > 0.4) maxPast = std::max(maxPast, e.head[i] - e.truth[i]);
      const double len = (double)plugin.samples_[0].size(), unit = len / 16383.0;
      std::printf("Lead %3.0f Smooth 160: overshoot after a dead stop %.1f%% of the pull\n", lead, 100 * maxPast / (travel * unit));
      CHECK(maxPast < 0.05 * travel * unit, "Lead %.0f%%, Smooth 160: stops without overshoot (%.1f%%)", lead, 100 * maxPast / (travel * unit));
    }


    // Crossfader cap: closed = one end, open = centre; hamster mirrors it.
    for (int ham = 0; ham < 2; ++ham) {
      setup(); plugin.set(kHamster, (float)ham); run(50);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(300);
      const float pressed = plugin.getNormalised(kCutPos);
      blocksRun(1, { mpc::MidiEvent::noteOff(0, 36) }); run(300);
      const float released = plugin.getNormalised(kCutPos);
      std::printf("crossfader cap, hamster %d: pad down %.2f, pad up %.2f\n", ham, pressed, released);
      if (!ham) CHECK(std::fabs(pressed - 0.5f) < 0.02f && released < 0.02f, "cap goes from the left end to the centre when the pad opens the cut");
      else      CHECK(std::fabs(released - 0.5f) < 0.02f && pressed > 0.98f, "hamster: cap rests at the centre and goes to the right end when the pad cuts");
    }
    // One sticker: tap adds it at the needle (12 o'clock), tap again removes it, wherever the record is.
    { setup(); plugin.pos_ = 0.3 * plugin.samples_[0].size(); plugin.set(kSticker, 1.0f); run(20);
      CHECK(plugin.stickerCount_[0] == 1, "tap adds the sticker");
      plugin.pos_ = 0.7 * plugin.samples_[0].size(); plugin.set(kSticker, 0.0f); run(20);
      CHECK(plugin.stickerCount_[0] == 0, "tap removes it even when the record has turned");
      plugin.set(kSticker, 1.0f); run(20); plugin.set(kSticker, 0.0f); run(20); plugin.set(kSticker, 1.0f); run(20);
      CHECK(plugin.stickerCount_[0] == 1, "never more than one sticker (%d)", plugin.stickerCount_[0]); }


    // Strip Zone: scrub with one half of the strip so the rest value (-1, the middle) is the END of the zone and always means "lifted".
    for (int zone : { 1, 2 }) {
      setup(); plugin.set(kStripZone, (float)zone); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100);
      plugin.pos_ = 0.5 * (double)plugin.samples_[0].size(); plugin.trk_.reset(plugin.pos_);
      const int sgn = zone == 1 ? -1 : 1;                                                // Bottom half = negative bend values
      const int first = zone == 1 ? -6000 : 2000;
      for (int m = 0; m < 12; ++m) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(first + sgn * -m * 150) } : std::vector<mpc::MidiEvent>{}); }
      CHECK(plugin.touched_, "zone %d: a pull inside the zone grabs the record", zone);
      const double p0 = plugin.pos_;
      for (int b = 0; b < 40; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(first + sgn * -11 * 150) } : std::vector<mpc::MidiEvent>{});   // held still
      CHECK(plugin.touched_, "zone %d: held still stays held", zone);
      blocksRun(1, { bend(-1) }); double maxr = 0; int rel = -1;
      for (int b = 0; b < 40; ++b) { blocksRun(1); maxr = std::max(maxr, std::fabs(plugin.rate_)); if (rel < 0 && !plugin.touched_) rel = b; }
      std::printf("zone %d: lift released after %d blocks (%.1f ms), max rate %.2f\n", zone, rel, rel * 2.9, maxr);
      CHECK(rel >= 0 && rel <= 14 && maxr < 2.0, "zone %d: the rest value is a lift: released in %d blocks, no zip (%.2fx)", zone, rel, maxr);
      // a finger resting at the zone's end (the middle) is a lift too, and the other half is ignored
      blocksRun(1, { bend(first) }); blocksRun(1, { bend(zone == 1 ? 3000 : -3000) });
      CHECK(plugin.touched_, "zone %d: touching the other half grabs at the end of the zone (clamped)", zone);
      (void)p0;
    }
    { setup(); plugin.set(kStripZone, 0.0f); blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(100); blocksRun(1, { bend(-1) });
      CHECK(plugin.touched_, "Whole strip: the centre is still a position (touch at -1 grabs)"); }


    // Sample browser, pads 1-8 as samples + faders, channel B loop, hamster pad, knob defaults
    { setup(); plugin.set(kStripZone, 0.0f); plugin.reset(); run(5);
      std::string nm; plugin.formatValue(kRow1, plugin.get(kRow1), nm);
      CHECK(plugin.libCount_ >= 3 && nm == "AHH", "list line 1 shows the first library sample (%s, %d samples)", nm.c_str(), plugin.libCount_);
      // scrolling: Browse moves the highlight; the window follows
      plugin.set(kBrowse, 2.0f); run(5);
      plugin.formatValue(kRow3, plugin.get(kRow3), nm);
      CHECK(plugin.getNormalised(kBrowseHi) > 0.2f && nm == "SINE", "Browse 2 highlights line 3 (SINE): hi %.2f, %s", plugin.getNormalised(kBrowseHi), nm.c_str());
      // preview plays through B while scrolling, silent when off
      plugin.set(kPreview, 0.0f); plugin.set(kBrowse, 1.0f); double rOff = run(100);
      plugin.set(kPreview, 1.0f); plugin.set(kBrowse, 2.0f); double rOn = run(100);
      CHECK(rOff < 1e-4 && rOn > 0.01, "Preview off is silent (%.5f), on plays the selected sample (%.4f)", rOff, rOn);
      plugin.set(kPreview, 0.0f); run(300);
      // LOAD armed: the next hardware pad stores the selected sample; pads then switch the record to their sample
      plugin.set(kBrowse, 1.0f); run(5);
      plugin.set(kArm, 1.0f); run(5);
      std::string st; plugin.formatValue(kStatus, plugin.get(kStatus), st);
      CHECK(plugin.getInt(kStatus) == 1 && st.find("PRESS A PAD") == 0, "LOAD armed prompts for a pad (%s)", st.c_str());
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 38, 100) }); blocksRun(1, { mpc::MidiEvent::noteOff(0, 38) }); run(5);   // pad 3 = note 38
      CHECK(plugin.getInt(kPadSmp3) == 2 && !plugin.getBool(kArm), "the hit pad 3 now holds the selected sample (%d) and LOAD disarms", plugin.getInt(kPadSmp3));
      plugin.set(kPadSmp1, 1.0f); run(5);
      // pad 3 (note 38): different sample -> record switches, cut opens with the pad, closes on release
      plugin.set(kHamster, 0.0f); run(5);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 38, 127) }); double rP = run(150);
      CHECK(plugin.getInt(kDeckSample) == 2 && rP > 0.01, "pad 3 puts its sample on the record (deck %d) and the cut opens (%.3f)", plugin.getInt(kDeckSample), rP);
      blocksRun(1, { mpc::MidiEvent::noteOff(0, 38) }); double rR = run(100);
      CHECK(rR < 0.002, "releasing the pad closes the cut (%.4f)", rR);
      // switching to a sample with a sticker cues the record there
      plugin.stickerCount_[0] = 1; plugin.stickers_[0][0] = 0.4f;
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(1);
      CHECK(plugin.getInt(kDeckSample) == 1 && std::fabs(plugin.pos_ / plugin.samples_[0].size() - 0.4) < 0.02, "pad 1 cues sample 1 at its sticker (%.3f of the record)", plugin.pos_ / plugin.samples_[0].size());
      blocksRun(1, { mpc::MidiEvent::noteOff(0, 36) }); run(10);
      const double keep = plugin.pos_; blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 127) }); run(1);
      CHECK(std::fabs(plugin.pos_ - keep) < 20000.0 && plugin.pos_ >= keep, "hitting the pad of the sample already on the record does not restart it");
      blocksRun(1, { mpc::MidiEvent::noteOff(0, 36) }); run(10);
      plugin.stickerCount_[0] = 0;
      // channel B loop: SEND TO B loops the selected sample, wraps without a click; again clears it
      plugin.set(kBrowse, 2.0f); run(5); plugin.set(kSendB, 1.0f); run(5);
      CHECK(plugin.getInt(kBLoop) == 3 && !plugin.getBool(kSendB), "SEND TO B puts the selected sample (3) on channel B (%d)", plugin.getInt(kBLoop));
      blocksRun(1, { mpc::MidiEvent::noteOff(0, 36) });
      { float maxJump = 0, prev = 0; bool first = true; double rms = 0; int cnt = 0;
        const int blocks = (int)(3.2 * 44100 / 128);                     // > 3 loops of the 1 s sine
        for (int b = 0; b < blocks; ++b) { blocksRun(1); for (int i = 0; i < 128; ++i) { float v = L[i]; if (!first) maxJump = std::max(maxJump, std::fabs(v - prev)); prev = v; first = false; rms += v * v; ++cnt; } }
        rms = std::sqrt(rms / cnt);
        std::printf("channel B loop: rms %.3f, largest sample-to-sample step %.4f over 3 loops of a 440 Hz sine\n", rms, maxJump);
        CHECK(rms > 0.02 && maxJump < 0.08, "B loops a sine seamlessly across the join (step %.4f)", maxJump); }
      plugin.set(kSendB, 1.0f); run(5);
      CHECK(plugin.getInt(kBLoop) == 0, "SEND TO B again clears channel B");
      plugin.set(kBrowse, 0.0f); run(5);
      // hamster pad
      plugin.set(kHamster, 0.0f); run(5);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 47, 100) }); run(5);
      CHECK(plugin.getBool(kHamster), "pad 12 (note 47) switches the hamster on");
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 47, 100) }); run(5);
      CHECK(!plugin.getBool(kHamster), "...and off again");
      for (int k : { (int)kAGain, (int)kAHigh, (int)kAMid, (int)kALow, (int)kBGain, (int)kBHigh, (int)kBMid, (int)kBLow })
          CHECK(std::fabs(plugin.getNormalised(k) - 0.5f) < 1e-4f, "mixer knob %d starts at 12 o'clock (%.3f)", k, plugin.getNormalised(k));
      std::string g; plugin.formatValue(kAGain, 0.0f, g); CHECK(g == "+0.0 dB", "centre reads 0 dB (%s)", g.c_str());
      { Scratch fresh; CHECK(std::fabs(fresh.get(kThrow) - 120.0f) < 0.05f && fresh.get(kSmooth) > 299.0f && fresh.get(kLead) > 299.0f, "defaults: Throw 120, Smooth 300, Lead 300 (%.0f %.0f %.0f)", fresh.get(kThrow), fresh.get(kSmooth), fresh.get(kLead)); } }


    // Platter and Throw do not depend on the sample length: 1.8 s of travel = one turn, Throw 100% = 0.8 s of audio per full pull
    for (size_t len : { (size_t)2400, (size_t)48000, (size_t)200000 }) {
      setup(); plugin.set(kStripZone, 0.0f);
      auto& sm = plugin.samples_[0]; sm.l.assign(len, 0.1f); sm.r.assign(len, 0.1f); sm.rate = 48000.0;
      plugin.set(kPlatter, 2.0f); run(300); plugin.set(kPlatter, 0.0f);               // stopped, then 33 1/3
      plugin.pos_ = 0.1 * len; plugin.turnSec_ = 0.0; plugin.trk_.reset(plugin.pos_);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 100) }); run(100);
      const double t0 = plugin.turnSec_; run(900);
      std::printf("sample %6zu frames: platter turned %.3f of a turn in 0.9 s at 33 1/3\n", len, (plugin.turnSec_ - t0) / kRevSec);
      CHECK(std::fabs((plugin.turnSec_ - t0) / kRevSec - 0.5) < 0.08, "platter speed is the same for a %zu-frame sample (%.3f turns in 0.9 s)", len, (plugin.turnSec_ - t0) / kRevSec);
      plugin.pos_ = 0.1 * len; plugin.trk_.reset(plugin.pos_); plugin.touched_ = false;
      for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(-6000) } : std::vector<mpc::MidiEvent>{});
      const double g0 = plugin.pos_;
      for (int m = 1; m <= 40; ++m) { for (int b = 0; b < 4; ++b) blocksRun(1, b == 0 ? std::vector<mpc::MidiEvent>{ bend(-6000 + m * 300) } : std::vector<mpc::MidiEvent>{}); }
      run(400);
      const double moved = (plugin.pos_ - g0) / 48000.0, expect = 12000.0 / 16383.0 * 0.8;
      CHECK(len < 38400 || std::fabs(moved - expect) < 0.12 * expect, "Throw: a 12000-unit pull moves %.3f s of audio in a %zu-frame sample (expect %.3f)", moved, len, expect);
    }


    // Encoder: one line per notch whatever the step size; stop B; gain to silence; slower flash
    { setup(); plugin.reset(); run(5); plugin.set(kBrowse, 0.0f); run(5);
      plugin.setNormalised(kScroll, 0.5f); run(5);
      plugin.setNormalised(kScroll, 0.51f); run(5);
      CHECK(plugin.getInt(kBrowse) == 1, "one tiny encoder notch up moves one line (%d)", plugin.getInt(kBrowse));
      plugin.setNormalised(kScroll, 0.75f); run(5);
      CHECK(plugin.getInt(kBrowse) == 2, "a big jump still moves only one line (%d)", plugin.getInt(kBrowse));
      plugin.setNormalised(kScroll, 0.73f); run(5);
      CHECK(plugin.getInt(kBrowse) == 1, "a notch down moves one line back (%d)", plugin.getInt(kBrowse));
      plugin.setNormalised(kScroll, 0.9f); run(5);
      CHECK(plugin.getNormalised(kScroll) == 0.5f, "near the end the encoder parameter is put back to the middle");
      plugin.set(kBrowse, 0.0f); run(5); plugin.setNormalised(kScroll, 0.4f); run(5);
      CHECK(plugin.getInt(kBrowse) == 0, "the list stops at the top (%d)", plugin.getInt(kBrowse));
      plugin.set(kBrowse, 2.0f); run(5); plugin.set(kSendB, 1.0f); run(5);
      CHECK(plugin.getInt(kBLoop) == 3, "send to B");
      plugin.set(kStopB, 1.0f); run(5);
      CHECK(plugin.getInt(kBLoop) == 0 && !plugin.getBool(kStopB), "STOP B clears the channel B loop");
      // gain all the way down is silent
      plugin.set(kBrowse, 0.0f); run(5);
      blocksRun(1, { mpc::MidiEvent::noteOn(0, 36, 100) }); run(100);
      const double loud = run(100);
      plugin.set(kAGain, -1.0f); run(100); const double quiet = run(100);
      CHECK(loud > 0.01 && quiet < 1e-6, "channel A gain fully down is silent (%.4f -> %.6f)", loud, quiet);
      plugin.set(kAGain, 0.0f);
      // LOAD flash: about 1.4 flashes a second
      plugin.set(kArm, 1.0f); int toggles = 0; float prev = plugin.getNormalised(kSlot1);
      for (int b = 0; b < (int)(3.0 * 44100 / 128); ++b) { blocksRun(1); const float cur = plugin.getNormalised(kSlot3); if ((cur > 0.9f) != (prev > 0.9f)) ++toggles; prev = cur; }
      std::printf("LOAD flash: %d toggles in 3 s\n", toggles);
      CHECK(toggles >= 6 && toggles <= 11, "LOAD flashes calmly (%d toggles in 3 s)", toggles);
      plugin.set(kArm, 0.0f); run(5); }

    // ---- loop snap: the record goes back to where it was when the take began, each time the sequence loops ----
    { std::printf("-- loop snap --\n");
      auto loop = [&](bool snap, bool stopFirst, double& before, double& after, double& anchor)
      {
          setup(); P->anchorValid_ = false; P->set(kLoopSnap, snap ? 1.0f : 0.0f); P->set(kPlatter, 0.0f);
          std::vector<float> L(128), R(128); float* outs[2] = { L.data(), R.data() };
          const double dppq = 128.0 / 44100.0 * 2.0; double ppqNow = 0.0; bool wrapped = false; const double start = 5000.0;
          P->pos_ = start; anchor = start; before = after = -1;
          for (int b = 0; b < 900; ++b)
          {
              mpc::ProcessContext c; c.outputs = outs; c.numOutputs = 2; c.numFrames = 128; c.sampleRate = 44100;
              c.transport.valid = c.transport.ppqValid = c.transport.playing = true; c.transport.recording = true; c.transport.bpm = 120.0;
              if (ppqNow >= 4.0) { ppqNow -= 4.0; wrapped = true; before = P->pos_; }      // the loop is 4 beats = 2 s
              c.transport.ppqPos = ppqNow;
              P->process(c); ppqNow += dppq;
              if (wrapped && after < 0) after = P->pos_;
          }
      };
      double bf, af, an;
      loop(true, false, bf, af, an);
      CHECK(std::fabs(af - an) < 600.0, "loop snap: after the loop the record is back at its start (%.0f vs %.0f; it had drifted to %.0f)", af, an, bf);
      loop(false, false, bf, af, an);
      CHECK(std::fabs(af - an) > 5000.0, "loop snap off: the record keeps drifting (%.0f vs %.0f)", af, an);
      // the anchor survives a save / load of the plugin state
      loop(true, false, bf, af, an);
      std::vector<uint8_t> st; P->saveExtraState(st); Scratch other; other.loadExtraState(st.data(), st.size());
      CHECK(other.anchorValid_ && std::fabs(other.anchorPos_ - an) < 600.0, "loop snap anchor is saved with the project (%.0f)", other.anchorPos_); }

    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
