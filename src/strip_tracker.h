// strip_tracker.h - turns the touch strip's position messages into a smooth record-head position.
//
// Written from scratch for this plugin (MIT, like the rest of the SDK). Design and numbers: see
// docs/scratch-tracking.md and tests/tracker_lab.cpp, which scores it against other approaches.
//
// The problem: the Live III strip sends a clean 14-bit position about every 13 ms, but the MPC hands the
// messages to the plugin at the start of 128-frame audio blocks, so they arrive 11.6 / 14.5 ms apart. A head
// that jumps to each new position (or glides toward it) turns that into audible speed steps ("warble"), and
// heavy smoothing hides them only by adding 60-90 ms of lag and a slow wind-down after the finger stops.
//
// The approach:
//  1. Jitter buffer: positions are queued and played back one per average message interval, so the uneven
//     arrival times do not matter. Cost: about one interval of latency.
//  2. Cubic Hermite path between consecutive positions, with matching speeds at the joins, so the head speed
//     is continuous (no steps) as well as the position.
//  3. Lead: the output is pushed ahead along its own (smooth) speed by lead x interval x speed. This buys back
//     most of the interval at the price of a little extra ripple. 0 = smoothest, 1 = least latency.
//  4. A light 3-pole smoother (a few ms) takes off what is left.
//  5. Every move starts from where the head actually is (position and speed), never from stored points, so a
//     message that is a few ms late costs nothing; the head just carries on at its speed. If the silence lasts
//     the speed dies out in ~25 ms, and a repeated position (finger stopped) ends a move at zero speed.
//
// All positions are in sample frames of the deck sample. Call next() once per output sample.

#ifndef SCRATCH_STRIP_TRACKER_H
#define SCRATCH_STRIP_TRACKER_H

#include <algorithm>
#include <cmath>

class StripTracker
{
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        tAvg_ = 0.013 * sr_;
        setParams(lead_, smoothMs_);
    }

    // lead: 0..3 (fraction of one message interval to look ahead; above 1 it also makes up for the smoother's delay). smoothMs: total light smoothing in ms.
    void setParams(double lead, double smoothMs)
    {
        lead_ = lead < 0.0 ? 0.0 : (lead > 3.0 ? 3.0 : lead);
        smoothMs_ = smoothMs;
        c_ = 1.0 - std::exp(-3.0 / (std::fmax(0.3, smoothMs) * 0.001 * sr_));   // three equal stages
        idleHold_ = (int)(0.005 * sr_);
        idleDecay_ = std::exp(-1.0 / (0.008 * sr_));
    }

    void reset(double pos)
    {
        qn_ = 0;
        c0_ = c1_ = p_ = ps_ = pe_ = pos;
        v_ = vs_ = me_ = 0.0;
        s1_ = s2_ = s3_ = pos;
        playing_ = false;
        first_ = true;
        since_ = 1 << 30;
        idle_ = 0;
        nGap_ = 0;
    }

    // A new strip position (in frames) was delivered.
    void message(double target)
    {
        if (!first_ && since_ < 0.04 * sr_)                                     // longer gaps are pauses, not the interval
        {
            // Running average at first (locks on within a few messages), then a slow average. Deliveries
            // alternate 4/5 blocks apart, so only an average over several messages gives the true interval.
            ++nGap_;
            tAvg_ = nGap_ == 1 ? (double)since_ : tAvg_ + (since_ - tAvg_) * std::fmax(0.02, 1.0 / nGap_);
        }
        first_ = false;
        since_ = 0;
        if (qn_ == kQ) { for (int i = 0; i + 1 < kQ; ++i) q_[i] = q_[i + 1]; --qn_; }   // overflow: drop the oldest
        q_[qn_++] = target;
        if (!playing_) begin();
    }

    double next()
    {
        ++since_;
        if (playing_)
        {
            const double u = t_ / len_, u2 = u * u, u3 = u2 * u;
            const double ms = vs_ * len_;                                  // start speed, per segment
            p_ = (2 * u3 - 3 * u2 + 1) * ps_ + (u3 - 2 * u2 + u) * ms + (-2 * u3 + 3 * u2) * pe_ + (u3 - u2) * me_;
            v_ = ((6 * u2 - 6 * u) * ps_ + (3 * u2 - 4 * u + 1) * ms + (-6 * u2 + 6 * u) * pe_ + (3 * u2 - 2 * u) * me_) / len_;
            t_ += 1.0;
            if (t_ >= len_) { p_ = pe_; v_ = me_ / len_; begin(); }
        }
        else
        {
            // No position waiting. Carry on at the current speed (messages can be a few ms late); if the silence
            // lasts, the speed dies out quickly so the record stops with the finger.
            p_ += v_;
            if (++idle_ > idleHold_) v_ *= idleDecay_;
        }
        const double x = p_ + lead_ * tAvg_ * v_;
        s1_ += (x - s1_) * c_;
        s2_ += (s1_ - s2_) * c_;
        s3_ += (s2_ - s3_) * c_;
        return s3_;
    }

    // The deck loop wrapped: move every stored position by the same amount.
    void shift(double d)
    {
        c0_ += d; c1_ += d; ps_ += d; pe_ += d; p_ += d; s1_ += d; s2_ += d; s3_ += d;
        for (int i = 0; i < qn_; ++i) q_[i] += d;
    }

    double intervalSamples() const { return tAvg_; }

private:
    static constexpr int kQ = 4;

    // Start the next move: from where the head is NOW (position and speed) to the next queued position.
    void begin()
    {
        if (qn_ == 0) { playing_ = false; return; }
        const double target = q_[0];
        for (int i = 0; i + 1 < qn_; ++i) q_[i] = q_[i + 1];
        --qn_;
        ps_ = p_; vs_ = v_; pe_ = target;
        me_ = target - c1_;                                    // finger speed at the new position (per segment): the plain slope; a 3-point
                                                               // estimate amplifies the strip's step noise (measured on real log data)
        if (target == c1_) me_ = 0.0;                          // strip repeated itself: the finger is still
        c0_ = c1_; c1_ = target;
        len_ = std::fmax(0.003 * sr_, tAvg_ * (qn_ >= 2 ? 0.97 : 1.0));   // only a gentle catch-up if the queue backs up
        t_ = 0.0;
        idle_ = 0;
        playing_ = true;
    }

    double sr_ = 44100.0, tAvg_ = 570.0, lead_ = 0.5, smoothMs_ = 4.0, c_ = 0.5;
    double q_[kQ] = {};
    int qn_ = 0, since_ = 1 << 30;
    bool playing_ = false, first_ = true;
    double c0_ = 0, c1_ = 0, p_ = 0, v_ = 0, ps_ = 0, vs_ = 0, pe_ = 0, me_ = 0, t_ = 0, len_ = 1;
    int nGap_ = 0;
    int idle_ = 0, idleHold_ = 220;               // ~5 ms of lateness is tolerated before the speed starts to die out
    double idleDecay_ = 0.9972;                    // ~8 ms time constant at 44.1 kHz
    double s1_ = 0, s2_ = 0, s3_ = 0;
};

#endif // SCRATCH_STRIP_TRACKER_H
