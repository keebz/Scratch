// Tracker lab: compares strip-tracking algorithms on emulated Live III strip data.
//   g++ -std=c++17 -O2 -w plugins/Scratch/tests/tracker_lab.cpp -o /tmp/tracker_lab && /tmp/tracker_lab
//
// Strip model (from a debug log recorded on a real Live III, Pitch Bend mode):
//  * the strip samples the finger at a steady ~13.06 ms (14-bit values, rounded),
//  * the MPC delivers each message at the start of the next 128-frame audio block
//    (so deliveries are 4 or 5 blocks apart: 11.6 / 14.5 ms),
//  * nothing is sent while the finger is lifted.
//
// For each algorithm and each finger motion we measure
//  * ripple  : speed content above ~14 Hz as a % of RMS speed (this is what is heard as warble),
//  * latency : lag (ms) of the head behind the TRUE finger position, best-fit,
//  * error   : RMS position error after removing that lag, as % of the motion's peak-to-peak.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
#include <deque>
#include <functional>
#include <string>
#include <algorithm>
#include "../src/strip_tracker.h"

static const double SR = 44100.0;
static const int BLOCK = 128;

struct Tracker
{
    virtual ~Tracker() {}
    virtual void reset(double pos) = 0;
    virtual void message(double target) = 0;     // called at a block start when a message was delivered
    virtual double next() = 0;                   // one output sample
};

static double coef(double tauMs) { return 1.0 - std::exp(-1.0 / (std::fmax(0.05, tauMs) * 0.001 * SR)); }

// --- A: one-pole smoothing on the stepped target (the version the user liked), + 3 ms head follow ---------
struct OnePole : Tracker
{
    double c, f = coef(3.0), s = 0, x = 0, tgt = 0;
    explicit OnePole(double tauMs) : c(coef(tauMs)) {}
    void reset(double p) override { s = x = tgt = p; }
    void message(double t) override { tgt = t; }
    double next() override { s += (tgt - s) * c; x += (s - x) * f; return x; }
};

// --- B: jitter buffer + straight segments (previous build) -------------------------------------------------
struct LinearBuf : Tracker
{
    std::deque<double> q; double path = 0, step = 0, remain = 0, segT = 0, tAvg = 0.013 * SR; int since = 1 << 30; bool first = true;
    double s1 = 0, s2 = 0, s3 = 0, c;
    explicit LinearBuf(double smoothMs) : c(coef(smoothMs / 3.0)) {}
    void reset(double p) override { q.clear(); path = segT = s1 = s2 = s3 = p; remain = 0; first = true; since = 1 << 30; }
    void start() { if (q.empty()) { remain = 0; return; } segT = q.front(); q.pop_front(); double len = std::fmax(0.003 * SR, tAvg / (1.0 + 0.2 * q.size())); step = (segT - path) / len; remain = len; }
    void message(double t) override
    {
        if (!first && since < 0.04 * SR) tAvg += (since - tAvg) * 0.05;
        first = false; since = 0;
        if (q.size() == 4) q.pop_front();
        q.push_back(t); if (remain <= 0) start();
    }
    double next() override
    {
        ++since;
        if (remain > 0) { path += step; remain -= 1; if (remain <= 0) { path = segT; start(); } }
        s1 += (path - s1) * c; s2 += (s1 - s2) * c; s3 += (s2 - s3) * c; return s3;
    }
};

// --- C: jitter buffer + cubic (C1) segments, so the speed has no steps at message boundaries ---------------
struct CubicBuf : Tracker
{
    std::deque<double> q; double P0 = 0, P1 = 0, P2 = 0;      // consumed points: P2 newest
    double t = 0, len = 1, tAvg = 0.013 * SR, p = 0; int since = 1 << 30; bool first = true, playing = false;
    double s1 = 0, s2 = 0, c;
    explicit CubicBuf(double smoothMs) : c(coef(smoothMs / 2.0)) {}
    void reset(double pos) override { q.clear(); P0 = P1 = P2 = p = s1 = s2 = pos; playing = false; first = true; since = 1 << 30; }
    void begin()
    {
        if (q.empty()) { playing = false; return; }
        P0 = P1; P1 = P2; P2 = q.front(); q.pop_front();      // segment runs from P1 to P2
        len = std::fmax(0.003 * SR, tAvg / (1.0 + 0.2 * q.size())); t = 0; playing = true;
    }
    void message(double v) override
    {
        if (!first && since < 0.04 * SR) tAvg += (since - tAvg) * 0.05;
        first = false; since = 0;
        if (q.size() == 4) q.pop_front();
        q.push_back(v); if (!playing) begin();
    }
    double next() override
    {
        ++since;
        if (playing)
        {
            double u = t / len;
            // Hermite: tangents at P1 (central, from P0..P2) and at P2 (one-sided quadratic from P0,P1,P2)
            double m1 = (P2 - P0) / 2.0, m2 = (3 * P2 - 4 * P1 + P0) / 2.0;      // per-segment units (T = 1)
            double h00 = 2*u*u*u - 3*u*u + 1, h10 = u*u*u - 2*u*u + u, h01 = -2*u*u*u + 3*u*u, h11 = u*u*u - u*u;
            p = h00 * P1 + h10 * m1 + h01 * P2 + h11 * m2;
            t += 1; if (t >= len) { p = P2; begin(); }
        }
        s1 += (p - s1) * c; s2 += (s1 - s2) * c; return s2;
    }
};

// --- C2: cubic jitter buffer with exact speed continuity (each segment starts with the previous end speed) ---
struct CubicBuf2 : Tracker
{
    std::deque<double> q; double P0 = 0, P1 = 0, P2 = 0, m1 = 0, m2 = 0, damp;
    double t = 0, len = 1, tAvg = 0.013 * SR, p = 0; int since = 1 << 30; bool first = true, playing = false;
    double s1 = 0, s2 = 0, c;
    CubicBuf2(double smoothMs, double dampEnd) : c(coef(smoothMs / 2.0)), damp(dampEnd) {}
    void reset(double pos) override { q.clear(); P0 = P1 = P2 = p = s1 = s2 = pos; m1 = m2 = 0; playing = false; first = true; since = 1 << 30; }
    void begin()
    {
        if (q.empty()) { playing = false; m2 = 0; return; }
        P0 = P1; P1 = P2; P2 = q.front(); q.pop_front();
        m1 = m2;                                                    // continue with the speed we ended on
        m2 = damp * (3 * P2 - 4 * P1 + P0) / 2.0;                   // end speed: quadratic extrapolation (per segment)
        len = std::fmax(0.003 * SR, tAvg / (1.0 + 0.2 * q.size())); t = 0; playing = true;
    }
    void message(double v) override
    {
        if (!first && since < 0.04 * SR) tAvg += (since - tAvg) * 0.05;
        first = false; since = 0;
        if (q.size() == 4) q.pop_front();
        q.push_back(v); if (!playing) begin();
    }
    double next() override
    {
        ++since;
        if (playing)
        {
            double u = t / len;
            double h00 = 2*u*u*u - 3*u*u + 1, h10 = u*u*u - 2*u*u + u, h01 = -2*u*u*u + 3*u*u, h11 = u*u*u - u*u;
            p = h00 * P1 + h10 * m1 + h01 * P2 + h11 * m2;
            t += 1; if (t >= len) { p = P2; begin(); }
        }
        if (c >= 0.999) return p;
        s1 += (p - s1) * c; s2 += (s1 - s2) * c; return s2;
    }
};

// --- G: cubic buffer + "lead": add lead x T x (path speed). The path speed is C1, so this stays continuous. ---
struct CubicLead : Tracker
{
    std::deque<double> q; double P0 = 0, P1 = 0, P2 = 0, m2 = 0, damp = 1.0, lead;
    double t = 0, len = 1, tAvg = 0.013 * SR, p = 0, dp = 0; int since = 1 << 30; bool first = true, playing = false;
    double s1 = 0, s2 = 0, s3 = 0, c, m1 = 0;
    int limit;
    CubicLead(double leadFrac, double smoothMs, int lim = 0) : lead(leadFrac), c(coef(smoothMs / 3.0)), limit(lim) {}
    void reset(double pos) override { q.clear(); P0 = P1 = P2 = p = s1 = s2 = s3 = pos; m1 = m2 = dp = 0; playing = false; first = true; since = 1 << 30; }
    void begin()
    {
        if (q.empty()) { playing = false; m2 = 0; dp = 0; return; }
        P0 = P1; P1 = P2; P2 = q.front(); q.pop_front();
        m1 = m2; m2 = (3 * P2 - 4 * P1 + P0) / 2.0;
        if (limit == 2) { if (P2 == P1) m2 = 0.0; }
        else if (limit)                                                  // no overshoot: end speed never reverses or exceeds 2x the last step
        {
            const double d1 = P2 - P1;
            if (d1 == 0.0 || (m2 > 0) != (d1 > 0)) m2 = 0.0; else if (std::fabs(m2) > 2.0 * std::fabs(d1)) m2 = 2.0 * d1;
        }
        len = std::fmax(0.003 * SR, tAvg / (1.0 + 0.2 * q.size())); t = 0; playing = true;
    }
    void message(double v) override
    {
        if (!first && since < 0.04 * SR) tAvg += (since - tAvg) * 0.05;
        first = false; since = 0;
        if (q.size() == 4) q.pop_front();
        q.push_back(v); if (!playing) begin();
    }
    double next() override
    {
        ++since;
        if (playing)
        {
            double u = t / len;
            double h00 = 2*u*u*u - 3*u*u + 1, h10 = u*u*u - 2*u*u + u, h01 = -2*u*u*u + 3*u*u, h11 = u*u*u - u*u;
            p = h00 * P1 + h10 * m1 + h01 * P2 + h11 * m2;
            double d00 = 6*u*u - 6*u, d10 = 3*u*u - 4*u + 1, d01 = -6*u*u + 6*u, d11 = 3*u*u - 2*u;
            dp = (d00 * P1 + d10 * m1 + d01 * P2 + d11 * m2) / len;            // frames per sample
            t += 1; if (t >= len) { p = P2; begin(); }
        }
        double x = p + lead * tAvg * dp;
        s1 += (x - s1) * c; s2 += (s1 - s2) * c; s3 += (s2 - s3) * c; return s3;
    }
};

// --- D: type-II tracking loop (PLL): follows ramps with no lag, speed is a state so it is continuous ------
struct Pll : Tracker
{
    double x = 0, v = 0, tgt = 0, kp, ki;
    Pll(double fcHz, double zeta) { double w = 2 * M_PI * fcHz / SR; kp = 2 * zeta * w; ki = w * w; }
    void reset(double p) override { x = tgt = p; v = 0; }
    void message(double t) override { tgt = t; }
    double next() override { double e = tgt - x; v += ki * e; x += v + kp * e; return x; }
};

// --- E: PLL that is fed the jitter-buffered linear path (rejects arrival jitter, then tracks without lag) ---
struct PllBuf : Tracker
{
    LinearBuf lb; Pll pll;
    PllBuf(double fcHz, double zeta) : lb(0.0), pll(fcHz, zeta) {}
    void reset(double p) override { lb.reset(p); pll.reset(p); }
    void message(double t) override { lb.message(t); }
    double next() override { pll.tgt = lb.next(); return pll.next(); }
};


// --- F: dead-reckoning predictor: extrapolate with the speed implied by the last two messages, then smooth ---
struct Predict : Tracker
{
    double Pk = 0, Pk1 = 0, v = 0, tAvg = 0.013 * SR, since = 1e9; int count = 0; double gain;
    double s1 = 0, s2 = 0, s3 = 0, c;
    Predict(double smoothMs, double predictGain) : c(coef(smoothMs / 3.0)), gain(predictGain) {}
    void reset(double p) override { Pk = Pk1 = s1 = s2 = s3 = p; v = 0; count = 0; since = 1e9; }
    void message(double t) override
    {
        if (count > 0 && since < 0.04 * SR) tAvg += (since - tAvg) * 0.05;
        Pk1 = Pk; Pk = t; ++count;
        v = (count > 1 && since < 0.04 * SR) ? (Pk - Pk1) / tAvg : 0.0;     // frames per sample
        since = 0;
    }
    double next() override
    {
        double tau = std::fmin(since, tAvg);                                 // do not run past one interval
        double x = Pk + gain * v * tau;
        since += 1;
        s1 += (x - s1) * c; s2 += (s1 - s2) * c; s3 += (s2 - s3) * c; return s3;
    }
};

// --- H: the production tracker from src/strip_tracker.h ---
struct Production : Tracker
{
    StripTracker t; double lead, smoothMs;
    Production(double l, double sm) : lead(l), smoothMs(sm) { t.prepare(SR); t.setParams(l, sm); }
    void reset(double p) override { t.reset(p); }
    void message(double v) override { t.message(v); }
    double next() override { return t.next(); }
};

// ------------------------------------------------------------------------------------------------------------
struct Motion { std::string name; std::function<double(double)> x; double seconds; double stopAt = 0; };   // finger position (strip units) vs time (s)

struct Result { double ripple, latency, error, wobbleAudio; double overshootMs = 0, settleMs = 0; };

static double gTphys = 0.01306;                         // strip sampling interval (s); the real Live III is ~13.06 ms
static Result evaluate(Tracker& tr, const Motion& m, double framesPerUnit)
{
    const double Tphys = gTphys;
    const int totalBlocks = (int)(m.seconds * SR / BLOCK);
    tr.reset(m.x(0) * framesPerUnit);
    std::vector<double> head, truth;
    double nextMsg = 0;                                  // physical time of the next strip sample
    for (int b = 0; b < totalBlocks; ++b)
    {
        const double blockStart = b * BLOCK / SR;
        // all strip samples taken before this block start are delivered now (latest wins; normally one)
        while (nextMsg <= blockStart) { tr.message(std::round(m.x(nextMsg)) * framesPerUnit); nextMsg += Tphys; }
        for (int i = 0; i < BLOCK; ++i)
        {
            head.push_back(tr.next());
            truth.push_back(m.x(blockStart + i / SR) * framesPerUnit);
        }
    }
    const size_t skip = (size_t)(0.3 * SR), n = head.size();
    // ripple: share of the head SPEED's energy above 20 Hz (spectrum). Real scratch motion is below ~15 Hz;
    // message-rate artefacts show up at 38 Hz (every other message) and 77 Hz (every message).
    std::vector<double> v(n, 0.0); for (size_t i = 1; i < n; ++i) v[i] = head[i] - head[i - 1];
    size_t N = 1; while (N < n - 2 * skip) N <<= 1; if (N > n - 2 * skip) N >>= 1;
    std::vector<std::complex<double>> X(N);
    for (size_t i = 0; i < N; ++i) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)); X[i] = v[skip + i] * w; }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(X[i], X[j]); }
    for (size_t len = 2; len <= N; len <<= 1) { double ang = -2 * M_PI / len; std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < N; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; ++k) { auto u = X[i + k], t = X[i + k + len / 2] * w; X[i + k] = u + t; X[i + k + len / 2] = u - t; w *= wl; } } }
    double eHi = 0, eAll = 0; for (size_t k = 1; k < N / 2; ++k) { double f = k * SR / N, e = std::norm(X[k]); if (f > 0.5) eAll += e; if (f > 20.0) eHi += e; }
    Result r; r.ripple = 100.0 * std::sqrt(eHi / std::fmax(eAll, 1e-12));
    // latency: lag of truth that best matches the head (search 0..60 ms), then residual error
    double best = 1e30; int bl = 0;
    for (int lag = 0; lag <= (int)(0.12 * SR); lag += 4)
    {
        double e = 0; size_t c = 0;
        for (size_t i = skip + (size_t)(0.12 * SR); i < n; i += 3) { double d = head[i] - truth[i - lag]; e += d * d; ++c; }
        e /= c; if (e < best) { best = e; bl = lag; }
    }
    double lo = 1e30, hi = -1e30; for (size_t i = skip; i < n; ++i) { lo = std::fmin(lo, truth[i]); hi = std::fmax(hi, truth[i]); }
    if (m.stopAt > 0)
    {
        const double finalPos = m.x(m.seconds) * framesPerUnit; const double stopS = m.stopAt * SR;
        double over = 0; size_t settle = 0;
        for (size_t i = (size_t)stopS; i < n; ++i) { over = std::fmax(over, (head[i] - finalPos) * (m.x(m.seconds) > m.x(0) ? 1 : -1)); if (std::fabs(head[i] - finalPos) > 0.5 * framesPerUnit * 3) settle = i; }
        double speed = std::fabs(m.x(m.stopAt) - m.x(m.stopAt - 0.05)) / 0.05 * framesPerUnit / SR;   // frames per sample before stop
        r.overshootMs = over / std::fmax(1e-9, speed) / 44.1; r.settleMs = (settle > stopS ? (settle - stopS) : 0) * 1000.0 / SR;
    }
    r.latency = bl * 1000.0 / SR; r.error = 100.0 * std::sqrt(best) / std::fmax(1e-9, hi - lo); r.wobbleAudio = 0;
    return r;
}

int main()
{
    const double framesPerUnit = 0.5 * 37500.0 / 16383.0;        // Throw 50%, 0.78 s sample at 48 kHz
    std::vector<Motion> motions = {
        { "steady pull 1x",          [](double t) { return -4000 + 22000 * t; },                                  1.2 },
        { "scratch 3 Hz +-2500",     [](double t) { return 2500 * std::sin(2 * M_PI * 3 * t); },                   2.0 },
        { "fast scratch 6 Hz +-3000",[](double t) { return 3000 * std::sin(2 * M_PI * 6 * t); },                   2.0 },
        { "pull 1x then STOP",      [](double t) { return t < 0.8 ? -4000 + 22000 * t : -4000 + 22000 * 0.8; }, 1.6, 0.8 },
        { "fast pull 3x then STOP", [](double t) { return t < 0.5 ? -6000 + 66000 * t : -6000 + 66000 * 0.5; }, 1.2, 0.5 },
        { "slow drag 0.3x",          [](double t) { return -2000 + 6500 * t; },                                   1.5 },
    };
    struct Cand { std::string name; std::function<Tracker*()> make; };
    std::vector<Cand> cands = {
        { "A one-pole 80ms (user's favourite)", []() { return new OnePole(80); } },
        { "G lead 0.5 + 4ms + stop-only limit", []() { return new CubicLead(0.5, 4.0, 2); } },
        { "H PRODUCTION lead 0.5, 4ms",         []() { return new Production(0.5, 4.0); } },
        { "H PRODUCTION lead 0.0, 4ms",         []() { return new Production(0.0, 4.0); } },
        { "H PRODUCTION lead 0.75, 4ms",        []() { return new Production(0.75, 4.0); } },
    };
    // Regression: the strip's message interval is not known exactly, and block-quantised delivery makes the
    // gaps alternate. A tracker that is fragile to that shows speed wobble on a perfectly steady pull.
    {
        Motion steady = { "steady pull 1x", [](double t) { return -4000 + 22000 * t; }, 1.6 };
        std::printf("\n== steady pull, different strip intervals (ripple %%; lower is better) ==\n%-38s", "algorithm");
        const double Ts[] = { 0.0119, 0.0124, 0.01306, 0.0136, 0.0142 };
        for (double T : Ts) std::printf("  %5.2fms", T * 1000);
        std::printf("\n");
        for (const Cand& c : cands)
        {
            std::printf("%-38s", c.name.c_str());
            for (double T : Ts) { gTphys = T; Tracker* t = c.make(); Result r = evaluate(*t, steady, framesPerUnit); delete t; std::printf("  %7.1f", r.ripple); }
            std::printf("\n");
        }
        gTphys = 0.01306;
    }
    for (const Motion& m : motions)
    {
        std::printf("\n== %s ==\n%-38s %9s %10s %9s\n", m.name.c_str(), "algorithm", "ripple %", "latency ms", "error %");
        for (const Cand& c : cands)
        {
            Tracker* t = c.make(); Result r = evaluate(*t, m, framesPerUnit); delete t;
            std::printf("%-38s %9.1f %10.1f %9.2f", c.name.c_str(), r.ripple, r.latency, r.error);
            if (m.stopAt > 0) std::printf("   overshoot %.1f ms, settle %.0f ms", r.overshootMs, r.settleMs);
            std::printf("\n");
        }
    }
}
