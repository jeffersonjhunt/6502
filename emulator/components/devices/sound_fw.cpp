/**
 * @file sound_fw.cpp
 * @brief The sound card's firmware, revision 3 (sound_fw.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_fw.h"

#include "sound_flash.h"

#include <algorithm>
#include <cmath>

namespace ooe::emu
{
    namespace
    {
        void put16(std::vector<uint8_t> &v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
        }
        void put24(std::vector<uint8_t> &v, uint32_t x)
        {
            put16(v, x);
            v.push_back(static_cast<uint8_t>(x >> 16));
        }
        void put32(std::vector<uint8_t> &v, uint32_t x)
        {
            put16(v, x);
            put16(v, x >> 16);
        }
        uint32_t get16(const uint8_t *p) { return static_cast<uint32_t>(p[0] | p[1] << 8); }
        uint32_t get24(const uint8_t *p) { return get16(p) | static_cast<uint32_t>(p[2]) << 16; }
        uint32_t get32(const uint8_t *p) { return get24(p) | static_cast<uint32_t>(p[3]) << 24; }
        void put_text(std::vector<uint8_t> &v, const std::string &s)
        {
            for (size_t i = 0; i < 32; ++i)
                v.push_back(i < s.size() ? static_cast<uint8_t>(s[i]) : 0);
        }
    } // namespace

    void Crc32::add(uint8_t b)
    {
        state_ ^= b;
        for (int i = 0; i < 8; ++i)
            state_ = (state_ >> 1) ^ (0xEDB88320u & (0u - (state_ & 1u)));
    }

    SoundFw::SoundFw(SoundFwPort &port, unsigned memsize_units)
        : port_(port), memsize_units_(memsize_units), store_(static_cast<size_t>(memsize_units) * 0x10000u)
    {
        power_on();
    }

    SoundFw::~SoundFw() = default;

    uint8_t SoundFw::features() const
    {
        return FEAT_TWO_SIDS | FEAT_TUNES | FEAT_ES5503 | FEAT_PCM | FEAT_MP3 | FEAT_EQ | FEAT_VIS | FEAT_MORE;
    }

    // --- resets ---------------------------------------------------------------------------------------

    void SoundFw::power_on()
    {
        reset();
        free_all();
        std::fill(store_.begin(), store_.end(), 0);
        ready_ = false;
        came_up_ = false;
        ready_at_ = now_ + static_cast<uint64_t>(STARTUP_MS) * SAMPLES_PER_MS;
    }

    void SoundFw::reset()
    {
        reset_path();
        abort_transfer();
        cmd_used_ = 0;
        records_.clear();
        partial_.clear();
        evt_.clear();
        cmd_was_full_ = xdone_ = cmdroom_ = false;
        error_ = OK;
        lost_ = 0;
        mask_ = 0xFF;
        stop_all();
    }

    // --- time -----------------------------------------------------------------------------------------

    void SoundFw::advance(uint64_t samples)
    {
        for (uint64_t i = 0; i < samples; ++i)
            step();
    }

    uint64_t SoundFw::next_action() const
    {
        uint64_t n = SAMPLES_PER_MS - now_ % SAMPLES_PER_MS;
        if (!ready_ && ready_at_ > now_)
            n = std::min<uint64_t>(n, ready_at_ - now_);
        if (xfer_.done)
            n = 1;
        if (!records_.empty() && records_.front().due > now_)
            n = std::min<uint64_t>(n, records_.front().due - now_);
        return std::max<uint64_t>(n, 1);
    }

    void SoundFw::step()
    {
        ++now_;
        if (!ready_ && now_ >= ready_at_)
            ready_ = came_up_ = true;
        if (xfer_.done)
            finish_transfer();
        if (ready_)
            run_commands();
        if (now_ % SAMPLES_PER_MS == 0)
            ms_step();
        mix_frame();
    }

    void SoundFw::ms_step()
    {
        tune_ms();
        if (boot_.active)
        {
            // 240 ticks a second, from the start (tick 0 played then).
            const uint64_t elapsed = ms() - boot_.started_ms;
            while (boot_.active && static_cast<uint64_t>(boot_.ticks) * 1000 <= elapsed * 240)
            {
                boot_tick();
                ++boot_.ticks;
            }
        }
    }

    // --- the digital path ---------------------------------------------------------------------------------

    namespace
    {
        /// The band-limited step's correction near an edge (PolyBLEP), `t` the phase past it, `dt` a sample's.
        double blep(double t, double dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }
        int16_t to16(float x)
        {
            const long v = std::lround(static_cast<double>(x) * 32767.0);
            return static_cast<int16_t>(std::clamp<long>(v, -32768, 32767));
        }
        /// x volume / 255, rounded half away from zero: revision 2's VOLL and VOLR.
        int16_t scaled(int16_t s, uint8_t vol)
        {
            if (vol == 255)
                return s;
            const int32_t n = int32_t{s} * vol;
            return static_cast<int16_t>(n >= 0 ? (n + 127) / 255 : -((-n + 127) / 255));
        }
        constexpr float HP_POLE = 0.997908f; ///< exp(-2 pi 16 / 48,000): the ADC's coupling, a one-pole high-pass at 16 Hz
        constexpr uint8_t TONE_VOLUME = 128;  ///< BEEP's volume 0: the default
    } // namespace

    void SoundFw::reset_path()
    {
        vol_l_ = vol_r_ = 255;
        mix_.fill(255);
        eq_gain_.fill(0);
        eq_on_ = false;
        eq_.set(eq_gain_, false, RATE);
        eq_.clear();
        vis_.clear();
        tone_ = Tone{};
        boot_ = BootSound{};
    }

    void SoundFw::take_output(std::vector<int16_t> &out)
    {
        out.insert(out.end(), out_.begin(), out_.end());
        out_.clear();
    }

    void SoundFw::tone_start(uint32_t hz, uint32_t ms, uint8_t volume)
    {
        if (tone_.left)
            event(EV_BEEP_DONE, 0); // a second replaces the first
        tone_.phase = 0;
        tone_.step = static_cast<double>(hz) / RATE;
        tone_.amp = static_cast<float>(volume ? volume : TONE_VOLUME) / 255.0f * 0.25f;
        tone_.left = static_cast<uint64_t>(ms) * SAMPLES_PER_MS;
        if (!tone_.left)
            event(EV_BEEP_DONE, 0);
    }

    void SoundFw::mix_frame()
    {
        float src[SOURCES][2] = {};
        // The SIDs, from the ADC: its samples resampled to the path's rate (linear, two of the ADC's behind,
        // so the one needed has always come), each chip's DC taken off as the input's coupling does.
        if (const uint32_t rate = port_.adc_hz())
        {
            port_.adc_take(adc_);
            adc_phase_ += rate;
            while (adc_phase_ >= RATE)
            {
                adc_phase_ -= RATE;
                adc_prev_[0] = adc_now_[0], adc_prev_[1] = adc_now_[1];
                if (adc_at_ + 2 + 4 <= adc_.size()) // two behind
                {
                    adc_now_[0] = adc_[adc_at_], adc_now_[1] = adc_[adc_at_ + 1];
                    adc_at_ += 2;
                }
            }
            if (adc_at_ > 8192)
            {
                adc_.erase(adc_.begin(), adc_.begin() + static_cast<std::ptrdiff_t>(adc_at_));
                adc_at_ = 0;
            }
            const float frac = static_cast<float>(adc_phase_) / static_cast<float>(RATE);
            float chip[2];
            for (unsigned n = 0; n < 2; ++n)
            {
                const float x = adc_prev_[n] + (adc_now_[n] - adc_prev_[n]) * frac;
                hp_out_[n] = x - hp_in_[n] + HP_POLE * hp_out_[n];
                hp_in_[n] = x;
                chip[n] = hp_out_[n] / 8.0f;
            }
            float g = static_cast<float>(mix_[SRC_TUNES]) / 255.0f;
            if (tune_.fade_ms && tune_.length_ms && tune_.state != T_STOPPED && tune_.state != T_ENDED &&
                tune_.played_ms + tune_.fade_ms > tune_.length_ms)
                g *= static_cast<float>(tune_.length_ms - std::min(tune_.played_ms, tune_.length_ms)) /
                     static_cast<float>(tune_.fade_ms);
            if (tune_.chips == 3) // a two-SID tune: A left, B right, as revision 2
                src[SRC_TUNES][0] = chip[0] * g, src[SRC_TUNES][1] = chip[1] * g;
            else // a one-SID tune, or none: both chips in both ears
                src[SRC_TUNES][0] = src[SRC_TUNES][1] = (chip[0] + chip[1]) * g;
        }
        // The ES5503, at half of full scale as revision 2's bus mixes it.
        int16_t d[2];
        port_.doc_frame(d);
        for (unsigned ch = 0; ch < 2; ++ch)
            src[SRC_EFFECTS][ch] = 0.5f * static_cast<float>(d[ch]) / 32768.0f * static_cast<float>(mix_[SRC_EFFECTS]) / 255.0f;
        // The tone generator: a band-limited square.
        if (tone_.left)
        {
            const double t = tone_.phase;
            double v = t < 0.5 ? 1.0 : -1.0;
            v += blep(t, tone_.step);
            v -= blep(std::fmod(t + 0.5, 1.0), tone_.step);
            tone_.phase += tone_.step;
            if (tone_.phase >= 1.0)
                tone_.phase -= 1.0;
            const float s = static_cast<float>(v) * tone_.amp * static_cast<float>(mix_[SRC_BEEP]) / 255.0f;
            src[SRC_BEEP][0] = src[SRC_BEEP][1] = s;
            if (--tone_.left == 0)
                event(EV_BEEP_DONE, 0);
        }
        float l = 0, r = 0;
        for (unsigned s = 0; s < SOURCES; ++s)
            l += src[s][0], r += src[s][1];
        int16_t L = to16(l), R = to16(r);
        if (!eq_.idle())
            eq_.run(L, R);
        vis_.feed(L, R);
        L = scaled(L, vol_l_);
        R = scaled(R, vol_r_);
        if (tap_on_)
        {
            if (tap_source_ < SOURCES)
            {
                tap_.push_back(to16(src[tap_source_][0]));
                tap_.push_back(to16(src[tap_source_][1]));
            }
            else
            {
                tap_.push_back(L);
                tap_.push_back(R);
            }
        }
        out_.push_back(L);
        out_.push_back(R);
        if (out_.size() > 2u * RATE)
            out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(out_.size() - 2u * RATE));
    }

    // --- the boot sound: splash/docsnd.s's sequence, the firmware's own player ---------------------------

    namespace
    {
        namespace fl = sound_flash;
        constexpr uint8_t D_FREQLO = 0x00, D_FREQHI = 0x20, D_VOL = 0x40, D_PTR = 0x80, D_CTRL = 0xA0, D_TABLE = 0xC0,
                          D_ENABLE = 0xE1;
        constexpr uint8_t HALT = 0x01, ONESHOT = 0x02, RIGHTCH = 0x10;
        constexpr unsigned THUNKS = 20;
        constexpr uint8_t VBIT[6] = {1, 2, 4, 8, 16, 32};
        constexpr uint8_t VOICE_OSC[6] = {0, 4, 8, 12, 16, 18};
        constexpr bool VOICE_DBL[6] = {true, true, true, true, false, false};
        uint8_t init_ctl(unsigned o)
        {
            if (o < 20)
                return static_cast<uint8_t>(HALT | (o & 1 ? RIGHTCH : 0));
            return static_cast<uint8_t>(HALT | ONESHOT | (o & 1 ? RIGHTCH : 0));
        }
        uint8_t init_table(unsigned o) { return static_cast<uint8_t>(o < 18 ? fl::T_ENGINE : o < 20 ? fl::T_WIND : o < 22 ? fl::T_CRACK : fl::T_PUSH); }
        uint8_t init_ptr(unsigned o) { return static_cast<uint8_t>(o < 18 ? fl::P_ENGINE : o < 20 ? fl::P_WIND : o < 22 ? fl::P_CRACK : fl::P_PUSH); }
        /// A 16-bit signed difference shifted right twice, as the player's CMP #$80, ROR pairs do.
        uint16_t quarter(uint16_t diff) { return static_cast<uint16_t>(static_cast<int16_t>(diff) >> 2); }
    } // namespace

    void SoundFw::boot_start()
    {
        port_.doc_ram(0, fl::WAVE, fl::WAVE_SIZE);
        boot_ = BootSound{};
        boot_.active = true;
        boot_.started_ms = ms();
        port_.doc_write(D_ENABLE, static_cast<uint8_t>((fl::OSCS - 1) << 1));
        for (unsigned o = 0; o < fl::OSCS; ++o)
        {
            port_.doc_write(static_cast<uint8_t>(D_CTRL + o), init_ctl(o));
            port_.doc_write(static_cast<uint8_t>(D_VOL + o), 0);
            port_.doc_write(static_cast<uint8_t>(D_TABLE + o), init_table(o));
            port_.doc_write(static_cast<uint8_t>(D_PTR + o), init_ptr(o));
        }
        for (const unsigned o : {20u, 21u})
        {
            port_.doc_write(static_cast<uint8_t>(D_FREQLO + o), static_cast<uint8_t>(fl::F_CRACK));
            port_.doc_write(static_cast<uint8_t>(D_FREQHI + o), static_cast<uint8_t>(fl::F_CRACK >> 8));
        }
        for (const unsigned o : {22u, 23u})
        {
            port_.doc_write(static_cast<uint8_t>(D_FREQLO + o), static_cast<uint8_t>(fl::F_PUSH));
            port_.doc_write(static_cast<uint8_t>(D_FREQHI + o), static_cast<uint8_t>(fl::F_PUSH >> 8));
        }
        boot_tick(); // tick 0 of frame 0, at once
        boot_.ticks = 1;
    }

    void SoundFw::boot_tick()
    {
        BootSound &b = boot_;
        if (b.tick == 0)
            boot_frame();
        if (!b.mode)
            for (unsigned x = 0; x < fl::NVOICES; ++x)
                if (b.run & VBIT[x])
                {
                    b.fc[x] = static_cast<uint16_t>(b.fc[x] + b.fd[x]);
                    b.vl[x] = static_cast<uint16_t>(b.vl[x] + b.dl[x]);
                    b.vr[x] = static_cast<uint16_t>(b.vr[x] + b.dr[x]);
                    boot_write(x);
                }
        if (++b.tick < 4)
            return;
        b.tick = 0;
        if (++b.frame == fl::FRAMES)
        {
            for (unsigned o = 0; o < fl::OSCS; ++o) // over: every oscillator halted
                port_.doc_write(static_cast<uint8_t>(D_CTRL + o), init_ctl(o));
            b.active = false;
        }
    }

    void SoundFw::boot_frame()
    {
        BootSound &b = boot_;
        if (b.frame > 255)
            return; // the thunk plays out by itself
        const uint8_t f = static_cast<uint8_t>(b.frame);
        if (f > fl::IMPACT)
            return;
        if (f == fl::IMPACT)
        {
            b.mode = 1;
            b.run = 0;
            for (unsigned o = 0; o < 22; ++o) // 0-21 halted (the push plays out)
                port_.doc_write(static_cast<uint8_t>(D_CTRL + o), init_ctl(o));
            for (unsigned o = 0; o < THUNKS; ++o)
            {
                port_.doc_write(static_cast<uint8_t>(D_TABLE + o), static_cast<uint8_t>(o & 1 ? fl::T_THUNK_R : fl::T_THUNK_L));
                port_.doc_write(static_cast<uint8_t>(D_PTR + o), static_cast<uint8_t>(o & 1 ? fl::P_THUNK_R : fl::P_THUNK_L));
                port_.doc_write(static_cast<uint8_t>(D_FREQLO + o), static_cast<uint8_t>(fl::F_THUNK));
                port_.doc_write(static_cast<uint8_t>(D_FREQHI + o), static_cast<uint8_t>(fl::F_THUNK >> 8));
                port_.doc_write(static_cast<uint8_t>(D_VOL + o), 255);
                port_.doc_write(static_cast<uint8_t>(D_CTRL + o), static_cast<uint8_t>(HALT | ONESHOT | (o & 1 ? RIGHTCH : 0)));
            }
            for (unsigned o = 0; o < THUNKS; ++o) // from the top, all together
                port_.doc_write(static_cast<uint8_t>(D_CTRL + o), static_cast<uint8_t>(ONESHOT | (o & 1 ? RIGHTCH : 0)));
            return;
        }
        for (int x = static_cast<int>(fl::NVOICES) - 1; x >= 0; --x)
        {
            const unsigned u = static_cast<unsigned>(x);
            if (f < fl::ent_t0[u])
                continue; // not started
            const unsigned idx = static_cast<uint8_t>(f - fl::ent_t0[u] + fl::ent_i0[u]);
            if (idx >= fl::LEN)
            {
                b.run = static_cast<uint8_t>(b.run & ~VBIT[u]); // past its tables: silent
                boot_ctl(u, HALT);
            }
            else
                boot_voice(u, static_cast<uint8_t>(idx));
        }
        if (f == fl::WHOOSH) // the stamp's push of air, from the top
        {
            port_.doc_write(D_VOL + 22, 200);
            port_.doc_write(D_VOL + 23, 200);
            port_.doc_write(D_CTRL + 22, HALT | ONESHOT);
            port_.doc_write(D_CTRL + 22, ONESHOT);
            port_.doc_write(D_CTRL + 23, HALT | ONESHOT | RIGHTCH);
            port_.doc_write(D_CTRL + 23, ONESHOT | RIGHTCH);
        }
    }

    void SoundFw::boot_voice(unsigned x, uint8_t idx)
    {
        BootSound &b = boot_;
        const uint8_t fl_ = fl::ent_fl[x];
        uint16_t target = static_cast<uint16_t>((fl::pitch_lo[idx] | fl::pitch_hi[idx] << 8) + (fl::ent_dlo[x] | fl::ent_dhi[x] << 8));
        if (fl_ & fl::BODY)
            target = static_cast<uint16_t>(target >> 1); // an octave down
        if (fl_ & fl::WIND)
            target = static_cast<uint16_t>(target >> 1); // the wind's half-rate table, at half
        uint8_t tvl = fl::vol_l[idx], tvr = fl::vol_r[idx];
        if (fl_ & fl::RIGHT)
            std::swap(tvl, tvr);
        if (fl_ & fl::WIND)
            tvl = static_cast<uint8_t>(tvl >> 1), tvr = static_cast<uint8_t>(tvr >> 1);
        if (idx == fl::ent_i0[x]) // its first frame: straight there
        {
            b.fc[x] = target;
            b.vl[x] = static_cast<uint16_t>(tvl << 8);
            b.vr[x] = static_cast<uint16_t>(tvr << 8);
            b.fd[x] = b.dl[x] = b.dr[x] = 0;
            b.run = static_cast<uint8_t>(b.run | VBIT[x]);
            boot_write(x);
            boot_ctl(x, HALT); // halted, then free run: from the top
            boot_ctl(x, 0);
        }
        else // a quarter of the way a tick
        {
            b.fd[x] = quarter(static_cast<uint16_t>(target - b.fc[x]));
            b.dl[x] = quarter(static_cast<uint16_t>((tvl << 8) - b.vl[x]));
            b.dr[x] = quarter(static_cast<uint16_t>((tvr << 8) - b.vr[x]));
        }
        if ((fl_ & fl::MAIN) && idx == fl::SHIFT) // the quickshifter's crack at the upshift
        {
            const bool right = fl_ & fl::RIGHT;
            port_.doc_write(D_VOL + 20, static_cast<uint8_t>(right ? fl::CRACK_VR : fl::CRACK_VL));
            port_.doc_write(D_VOL + 21, static_cast<uint8_t>(right ? fl::CRACK_VL : fl::CRACK_VR));
            port_.doc_write(D_CTRL + 20, HALT | ONESHOT);
            port_.doc_write(D_CTRL + 20, ONESHOT);
            port_.doc_write(D_CTRL + 21, HALT | ONESHOT | RIGHTCH);
            port_.doc_write(D_CTRL + 21, ONESHOT | RIGHTCH);
        }
    }

    void SoundFw::boot_write(unsigned x)
    {
        const BootSound &b = boot_;
        const unsigned y = VOICE_OSC[x];
        const auto vol = [](uint16_t v) { return static_cast<uint8_t>((v >> 8) & 0x80 ? 0 : (v >> 8) << 1); };
        const unsigned pairs = VOICE_DBL[x] ? 2 : 1;
        for (unsigned k = 0; k < pairs; ++k)
        {
            const unsigned o = y + 2 * k;
            port_.doc_write(static_cast<uint8_t>(D_VOL + o), vol(b.vl[x]));
            port_.doc_write(static_cast<uint8_t>(D_VOL + o + 1), vol(b.vr[x]));
            for (const unsigned q : {o, o + 1})
            {
                port_.doc_write(static_cast<uint8_t>(D_FREQLO + q), static_cast<uint8_t>(b.fc[x]));
                port_.doc_write(static_cast<uint8_t>(D_FREQHI + q), static_cast<uint8_t>(b.fc[x] >> 8));
            }
        }
    }

    void SoundFw::boot_ctl(unsigned x, uint8_t a)
    {
        const unsigned y = VOICE_OSC[x];
        port_.doc_write(static_cast<uint8_t>(D_CTRL + y), a);
        port_.doc_write(static_cast<uint8_t>(D_CTRL + y + 1), static_cast<uint8_t>(a | RIGHTCH));
        if (VOICE_DBL[x])
        {
            port_.doc_write(static_cast<uint8_t>(D_CTRL + y + 2), static_cast<uint8_t>(a & ~RIGHTCH));
            port_.doc_write(static_cast<uint8_t>(D_CTRL + y + 3), static_cast<uint8_t>(a | RIGHTCH));
        }
    }

    // --- tunes ------------------------------------------------------------------------------------------

    void SoundFw::tune_ms()
    {
        Tune &u = tune_;
        if (u.state != T_STARTING && u.state != T_PLAYING && u.state != T_STALLED)
            return;
        uint16_t pc = 0;
        if (port_.tune_jammed(pc))
        {
            const uint8_t slot = u.slot;
            stop_tune(T_JAMMED);
            event(EV_TUNE_JAM, slot, {static_cast<uint8_t>(pc), static_cast<uint8_t>(pc >> 8)});
            return;
        }
        const uint64_t calls = port_.play_calls();
        if (calls > u.calls)
        {
            u.calls = calls;
            u.quiet_ms = 0;
            if (!u.first)
            {
                u.first = true;
                u.state = T_PLAYING;
                event(EV_TUNE_STARTED, u.slot, {static_cast<uint8_t>(u.song), static_cast<uint8_t>(u.song >> 8)});
            }
            else if (u.state == T_STALLED)
                u.state = T_PLAYING;
        }
        else if (u.has_play && u.state != T_STALLED && ++u.quiet_ms >= (u.first ? STALL_MS : FIRST_CALL_MS))
        {
            u.state = T_STALLED;
            std::vector<uint8_t> v;
            put32(v, u.quiet_ms);
            event(EV_TUNE_STALL, u.slot, v);
        }
        if (++u.played_ms >= u.length_ms && u.length_ms)
        {
            const uint8_t slot = u.slot;
            const uint32_t played = u.played_ms;
            stop_tune(T_ENDED);
            std::vector<uint8_t> v;
            put32(v, played);
            event(EV_TUNE_ENDED, slot, v);
        }
    }

    void SoundFw::stop_tune(uint8_t state)
    {
        port_.tune_stop();
        tune_.chips = 0;
        tune_.state = state;
    }

    uint8_t SoundFw::play_tune(const uint8_t *p)
    {
        const uint8_t slot = p[0], flags = p[2];
        if (slot >= TUNE_SLOTS || (flags & 3) == 3 || (flags & ~3u))
            return E_PARAM;
        if (!tunes_[slot].used || !files_[slot])
            return E_SLOT;
        SidFile f = *files_[slot];
        const unsigned song = p[1] ? p[1] : f.start_song;
        if (song > f.songs)
            return E_PARAM;
        if (flags & 3) // the clock asked for, not the file's
            f.flags = static_cast<uint16_t>((f.flags & ~0x0Cu) | ((flags & 1) ? 0x04u : 0x08u));
        std::string error;
        const auto plan = plan_sid(f, song, error);
        if (!plan)
            return E_DATA;
        if (tune_.state != T_STOPPED)
            port_.tune_stop();
        tune_ = Tune{};
        tune_.slot = slot;
        tune_.song = static_cast<uint16_t>(song);
        tune_.chips = f.sid2 ? 3 : 1;
        tune_.length_ms = get32(p + 3);
        tune_.fade_ms = get16(p + 7);
        tune_.has_play = f.play != 0;
        port_.tune_start(*plan, f.ntsc(), f.sid2 == 0xD500);
        if (tune_.has_play)
            tune_.state = T_STARTING;
        else
        {
            tune_.first = true;
            tune_.state = T_PLAYING;
            event(EV_TUNE_STARTED, slot, {static_cast<uint8_t>(song), static_cast<uint8_t>(song >> 8)});
        }
        return OK;
    }

    // --- the host's side --------------------------------------------------------------------------------

    bool SoundFw::cmd_write(uint8_t b)
    {
        if (cmd_used_ >= CMD_BYTES)
        {
            cmd_was_full_ = true;
            return false;
        }
        ++cmd_used_;
        if (cmd_used_ == CMD_BYTES)
            cmd_was_full_ = true;
        partial_.push_back(b);
        if (partial_.size() >= 3 && partial_.size() == 3u + partial_[1])
        {
            // At the door: a command the firmware knows, with its n.
            const int want = params_for(partial_[0]);
            error_ = want == -2 ? E_UNKNOWN : (want >= 0 && partial_[1] != want) || partial_[1] > 253 ? E_PARAM : OK;
            records_.push_back({std::move(partial_), now_ + 1, error_});
            partial_.clear();
        }
        return true;
    }

    uint8_t SoundFw::evt_read()
    {
        if (evt_.empty())
            return 0;
        const uint8_t b = evt_.front();
        evt_.pop_front();
        return b;
    }

    bool SoundFw::data_write(uint8_t b)
    {
        if (!xfer_.open)
            return false;
        Object &o = *xfer_.obj;
        store_[o.at + (xfer_.len - xfer_.left)] = b;
        xfer_.crc.add(b);
        if (--xfer_.left == 0)
        {
            xfer_.open = false;
            xfer_.done = true; // checked at the next sample: XFERDONE
            xdone_ = true;
        }
        return true;
    }

    size_t SoundFw::store_free() const
    {
        size_t used = 0;
        auto add = [&](const Object &o) { used += o.used ? o.len : 0; };
        for (const auto &o : tunes_)
            add(o);
        for (const auto &o : samples_)
            add(o);
        for (const auto &o : music_)
            add(o);
        for (const auto &o : chunks_)
            add(o);
        add(effects_);
        add(image_);
        return store_.size() - used;
    }

    // --- commands -----------------------------------------------------------------------------------------

    int SoundFw::params_for(uint8_t cmd)
    {
        switch (cmd)
        {
        case NOP: case IDENTIFY: case STOPALL: case FREEALL: case LOSTCLR: case TIME: case BOOTSOUND:
        case FW_COMMIT: case XFERABORT: case TUNE_STOP: case TUNE_PAUSE: case TUNE_RESUME: case TUNE_STATUS:
        case MUSIC_STOP: case MUSIC_PAUSE: case MUSIC_RESUME: case MUSIC_STATUS: case BELL: case VIS: case SIDREGS:
        case LEVELS:
            return 0;
        case EVENTS: case SELFTEST: case TUNE_INFO: case TUNE_FREE: case VOICE_STOP: case BANK_FREE:
        case MUSIC_THRESH: case STREAM_END: case STREAM_PAUSE: case STREAM_RESUME: case STREAM_FLUSH:
        case STREAM_STATUS: case STREAM_CLOSE: case EQ_ON:
            return 1;
        case CHECKSUM: case MUSIC_PLAY: case STREAM_NEXT: case VOLUME: case MIX: case EQ:
            return 2;
        case FW_BEGIN: case TUNE_LOAD: case EFFECT_LOAD: case MUSIC_LOAD: case STREAM_DATA:
            return 4;
        case BEEP:
            return 5;
        case PLAY:
            return 7;
        case TUNE_PLAY:
            return 9;
        case STREAM_OPEN:
            return 10;
        case EQ_ALL:
            return 11;
        case SAMPLE_LOAD:
            return 13;
        case EFFECT_DEF:
            return -1;
        default:
            return -2;
        }
    }

    size_t SoundFw::reply_room(uint8_t cmd)
    {
        size_t results = 0;
        switch (cmd)
        {
        case IDENTIFY: results = 32; break;
        case LOSTCLR: results = 2; break;
        case TIME: results = 4; break;
        case SELFTEST: results = 2; break;
        case CHECKSUM: results = 8; break;
        case XFERABORT: results = 3; break;
        case TUNE_STATUS: results = 16; break;
        case TUNE_INFO: results = 104; break;
        case PLAY: results = 1; break;
        case MUSIC_STATUS: results = 24; break;
        case STREAM_STATUS: results = 21; break;
        case VIS: results = 98; break;
        case SIDREGS: results = 50; break;
        case LEVELS: results = 10; break;
        default: break;
        }
        return 4 + results;
    }

    void SoundFw::run_commands()
    {
        while (run_one())
        {
        }
    }

    bool SoundFw::run_one()
    {
        if (records_.empty())
            return false;
        Pending &p = records_.front();
        if (p.due > now_ || evt_room() < reply_room(p.rec[0]))
            return false;
        if (p.refused == OK)
        {
            const Object *o = waits_on(p.rec);
            if (o && o->loading)
                return false; // in order: it, and everything behind it, waits for the transfer
        }
        const Pending q = std::move(p);
        records_.pop_front();
        cmd_used_ -= q.rec.size();
        if (cmd_was_full_)
            cmd_was_full_ = false, cmdroom_ = true;
        if (q.refused != OK)
            reply(q.rec[2], q.refused);
        else
            execute(q.rec);
        return true;
    }

    SoundFw::Object *SoundFw::waits_on(const std::vector<uint8_t> &rec)
    {
        if (rec[0] == CHECKSUM)
            return object(rec[3], rec[4]);
        if ((rec[0] == TUNE_PLAY || rec[0] == TUNE_INFO) && rec[3] < TUNE_SLOTS)
            return &tunes_[rec[3]];
        return nullptr;
    }

    SoundFw::Object *SoundFw::object(uint8_t kind, uint8_t slot)
    {
        switch (kind)
        {
        case TUNE_LOAD: return slot < TUNE_SLOTS ? &tunes_[slot] : nullptr;
        case SAMPLE_LOAD: return &samples_[slot];
        case EFFECT_LOAD: return &effects_;
        case MUSIC_LOAD: return slot < MUSIC_SLOTS ? &music_[slot] : nullptr;
        case STREAM_DATA: return slot < STREAMS ? &chunks_[slot] : nullptr;
        case FW_BEGIN: return &image_;
        default: return nullptr;
        }
    }

    void SoundFw::execute(const std::vector<uint8_t> &rec)
    {
        const uint8_t cmd = rec[0], tag = rec[2];
        const uint8_t *p = rec.data() + 3;
        switch (cmd)
        {
        case NOP:
            reply(tag, OK);
            break;
        case IDENTIFY:
        {
            std::vector<uint8_t> r;
            r.push_back(CARD_TYPE);
            r.push_back(VERSION_MAJOR);
            r.push_back(VERSION_MINOR);
            put16(r, BUILD);
            r.push_back(memsize());
            r.push_back(features());
            r.push_back(FEAT2_TONE | FEAT2_SID_MODEL | FEAT2_UPDATE);
            put32(r, static_cast<uint32_t>(store_free()));
            put32(r, static_cast<uint32_t>(store_.size()));
            r.push_back(TUNE_SLOTS);
            r.push_back(MUSIC_SLOTS);
            r.push_back(STREAMS);
            r.push_back(SAMPLE_SLOTS - 1);
            r.push_back(EFFECTS - 1);
            r.push_back(6);  // SID voices
            r.push_back(32); // oscillators
            r.push_back(1);  // SID A: a 6581 until a tune asks
            r.push_back(1);
            r.push_back(0);  // image A
            r.resize(32, 0);
            reply(tag, OK, r);
            break;
        }
        case STOPALL:
            stop_all();
            reply(tag, OK);
            break;
        case FREEALL:
            stop_all();
            free_all();
            reply(tag, OK);
            break;
        case EVENTS:
            mask_ = p[0];
            reply(tag, OK);
            break;
        case LOSTCLR:
        {
            std::vector<uint8_t> r;
            put16(r, std::min<uint32_t>(lost_, 0xFFFF));
            lost_ = 0;
            reply(tag, OK, r);
            break;
        }
        case TIME:
        {
            std::vector<uint8_t> r;
            put32(r, ms());
            reply(tag, OK, r);
            break;
        }
        case CHECKSUM:
        {
            const Object *o = object(p[0], p[1]);
            if (!o || !o->used)
            {
                reply(tag, E_SLOT);
                break;
            }
            std::vector<uint8_t> r;
            put32(r, o->crc);
            put32(r, static_cast<uint32_t>(o->len));
            reply(tag, OK, r);
            break;
        }
        case XFERABORT:
        {
            if (!xfer_.open)
            {
                reply(tag, E_XFER);
                break;
            }
            std::vector<uint8_t> r;
            put24(r, xfer_.len - xfer_.left);
            abort_transfer();
            reply(tag, OK, r);
            break;
        }
        case SAMPLE_LOAD:
        {
            const uint32_t len = get24(p + 1);
            if (len == 0)
            {
                reply(tag, E_PARAM);
                break;
            }
            reply(tag, open_transfer(cmd, tag, p[0], len, samples_[p[0]]));
            break;
        }
        case TUNE_LOAD:
        {
            const uint32_t len = get24(p + 1);
            if (p[0] >= TUNE_SLOTS || len < 0x76 || len > 0x10000 + 0x7C + 2)
            {
                reply(tag, E_PARAM);
                break;
            }
            if (tune_.state != T_STOPPED && tune_.slot == p[0])
                stop_tune(T_STOPPED);
            files_[p[0]].reset();
            reply(tag, open_transfer(cmd, tag, p[0], len, tunes_[p[0]]));
            break;
        }
        case TUNE_PLAY:
            reply(tag, play_tune(p));
            break;
        case TUNE_STOP:
            if (tune_.state != T_STOPPED)
                stop_tune(T_STOPPED);
            reply(tag, OK);
            break;
        case TUNE_PAUSE:
            if (tune_.state != T_STARTING && tune_.state != T_PLAYING && tune_.state != T_STALLED)
            {
                reply(tag, E_STATE);
                break;
            }
            tune_.held_state = tune_.state;
            tune_.state = T_PAUSED;
            port_.tune_hold(true);
            reply(tag, OK);
            break;
        case TUNE_RESUME:
            if (tune_.state != T_PAUSED)
            {
                reply(tag, E_STATE);
                break;
            }
            port_.tune_hold(false);
            tune_.state = tune_.held_state;
            tune_.quiet_ms = 0; // the pause isn't a stall
            reply(tag, OK);
            break;
        case TUNE_STATUS:
        {
            std::vector<uint8_t> r{tune_.state, tune_.slot};
            put16(r, tune_.song);
            put32(r, tune_.played_ms);
            put32(r, static_cast<uint32_t>(std::min<uint64_t>(tune_.calls, 0xFFFFFFFFu)));
            put16(r, std::min<uint32_t>(tune_.state == T_STOPPED ? 0 : port_.play_cycles(), 0xFFFF));
            put16(r, std::min<uint32_t>(tune_.quiet_ms, 0xFFFF));
            reply(tag, OK, r);
            break;
        }
        case TUNE_INFO:
        {
            if (p[0] >= TUNE_SLOTS || !files_[p[0]])
            {
                reply(tag, p[0] >= TUNE_SLOTS ? E_PARAM : E_SLOT);
                break;
            }
            const SidFile &f = *files_[p[0]];
            std::vector<uint8_t> r;
            put16(r, f.songs);
            put16(r, f.start_song);
            r.push_back(static_cast<uint8_t>(f.clock()));
            r.push_back(static_cast<uint8_t>((f.rsid ? 1 : 0) | (f.sid2 ? 2 : 0) | (f.model() == Sid::Model::Mos8580 ? 4 : 0) |
                                             (f.cia_timed(f.start_song) ? 8 : 0)));
            put16(r, f.sid2);
            put_text(r, f.name);
            put_text(r, f.author);
            put_text(r, f.released);
            reply(tag, OK, r);
            break;
        }
        case TUNE_FREE:
            if (p[0] >= TUNE_SLOTS)
            {
                reply(tag, E_PARAM);
                break;
            }
            if (tune_.state != T_STOPPED && tune_.slot == p[0])
                stop_tune(T_STOPPED);
            if (xfer_.obj == &tunes_[p[0]])
                abort_transfer();
            free_object(tunes_[p[0]]);
            files_[p[0]].reset();
            reply(tag, OK);
            break;
        case BOOTSOUND:
            boot_start();
            reply(tag, OK);
            break;
        case BEEP:
        {
            const uint32_t hz = get16(p), len = get16(p + 2);
            if (hz < 20 || hz > 20000)
            {
                reply(tag, E_PARAM);
                break;
            }
            reply(tag, OK);
            tone_start(hz, len, p[4]);
            break;
        }
        case BELL:
            reply(tag, OK);
            tone_start(sound_flash::BELL_HZ, sound_flash::BELL_MS, static_cast<uint8_t>(sound_flash::BELL_VOLUME));
            break;
        case VOLUME:
            vol_l_ = p[0], vol_r_ = p[1];
            reply(tag, OK);
            break;
        case MIX:
            if (p[0] >= SOURCES)
            {
                reply(tag, E_PARAM);
                break;
            }
            mix_[p[0]] = p[1];
            reply(tag, OK);
            break;
        case EQ:
        case EQ_ALL:
        {
            std::array<int8_t, 11> g = eq_gain_;
            const unsigned first = cmd == EQ ? p[0] : 0, count = cmd == EQ ? 1 : 11;
            const uint8_t *v = cmd == EQ ? p + 1 : p;
            bool ok = first <= 10;
            for (unsigned i = 0; ok && i < count; ++i)
            {
                const int8_t x = static_cast<int8_t>(v[i]);
                ok = x >= -24 && x <= 24; // half-dB, revision 2's
                g[first + i] = x;
            }
            if (!ok)
            {
                reply(tag, E_PARAM); // each checked before any is set
                break;
            }
            eq_gain_ = g;
            eq_.set(eq_gain_, eq_on_, RATE);
            reply(tag, OK);
            break;
        }
        case EQ_ON:
            if (p[0] > 1)
            {
                reply(tag, E_PARAM);
                break;
            }
            eq_on_ = p[0] == 1;
            eq_.set(eq_gain_, eq_on_, RATE);
            reply(tag, OK);
            break;
        case VIS:
            vis_.snapshot(vis_page_);
            reply(tag, OK, std::vector<uint8_t>(vis_page_.begin(), vis_page_.end()));
            break;
        case LEVELS:
        {
            // As PB_LEVELS has the vis page: its 32 bands four at a time, averaged, then its VU pair.
            vis_.snapshot(vis_page_);
            std::vector<uint8_t> r;
            for (unsigned b = 0; b < 8; ++b)
                r.push_back(static_cast<uint8_t>((vis_page_[4 * b] + vis_page_[4 * b + 1] + vis_page_[4 * b + 2] + vis_page_[4 * b + 3]) / 4));
            r.push_back(vis_page_[BusVis::VU]);
            r.push_back(vis_page_[BusVis::VU + 1]);
            reply(tag, OK, r);
            break;
        }
        case SIDREGS:
        {
            std::vector<uint8_t> r(50);
            port_.sid_regs(0, r.data());
            port_.sid_regs(1, r.data() + 25);
            reply(tag, OK, r);
            break;
        }
        default:
            reply(tag, E_UNKNOWN);
            break;
        }
    }

    // --- events -----------------------------------------------------------------------------------------

    void SoundFw::reply(uint8_t tag, uint8_t status, const std::vector<uint8_t> &results)
    {
        const size_t size = 4 + results.size();
        if (evt_room() < size) // never, as run_one() waits for the room; a reply is never lost
            return;
        evt_.push_back(EV_REPLY);
        evt_.push_back(static_cast<uint8_t>(2 + results.size()));
        evt_.push_back(tag);
        evt_.push_back(status);
        evt_.insert(evt_.end(), results.begin(), results.end());
    }

    void SoundFw::event(uint8_t kind, uint8_t object, const std::vector<uint8_t> &payload)
    {
        if (!(mask_ & (1u << (kind >> 4))))
            return; // not wanted: not sent, not lost
        const size_t size = 7 + payload.size();
        // The room the replies of the records waiting need is kept: an event never takes it.
        size_t keep = 0;
        for (const Pending &p : records_)
            keep += reply_room(p.rec[0]);
        if (evt_room() < size + keep)
        {
            ++lost_;
            return;
        }
        evt_.push_back(kind);
        evt_.push_back(static_cast<uint8_t>(5 + payload.size()));
        evt_.push_back(object);
        const uint32_t t = ms();
        for (int i = 0; i < 4; ++i)
            evt_.push_back(static_cast<uint8_t>(t >> (8 * i)));
        evt_.insert(evt_.end(), payload.begin(), payload.end());
    }

    // --- the store and its objects ------------------------------------------------------------------------

    std::optional<size_t> SoundFw::allocate(size_t len)
    {
        std::vector<std::pair<size_t, size_t>> runs; // used: at, len
        auto add = [&](const Object &o) {
            if (o.used)
                runs.push_back({o.at, o.len});
        };
        for (const auto &o : tunes_)
            add(o);
        for (const auto &o : samples_)
            add(o);
        for (const auto &o : music_)
            add(o);
        for (const auto &o : chunks_)
            add(o);
        add(effects_);
        add(image_);
        std::sort(runs.begin(), runs.end());
        size_t at = 0;
        for (const auto &[a, l] : runs)
        {
            if (a - at >= len)
                return at;
            at = std::max(at, a + l);
        }
        if (store_.size() - at >= len)
            return at;
        return std::nullopt;
    }

    void SoundFw::free_object(Object &o) { o = Object{}; }

    void SoundFw::stop_all()
    {
        if (tune_.state != T_STOPPED)
            stop_tune(T_STOPPED);
        tone_ = Tone{};
        boot_ = BootSound{};
        port_.silence();
    }

    void SoundFw::free_all()
    {
        abort_transfer();
        for (auto &o : tunes_)
            free_object(o);
        for (auto &o : samples_)
            free_object(o);
        for (auto &o : music_)
            free_object(o);
        for (auto &o : chunks_)
            free_object(o);
        free_object(effects_);
        free_object(image_);
        for (auto &f : files_)
            f.reset();
    }

    // --- transfers ----------------------------------------------------------------------------------------

    uint8_t SoundFw::open_transfer(uint8_t cmd, uint8_t tag, uint8_t slot, uint32_t len, Object &o)
    {
        if (xfer_.open || xfer_.done)
            return E_XFER;
        free_object(o); // a slot loaded again: the old object goes first
        const auto at = allocate(len);
        if (!at)
            return E_MEMORY;
        o.used = o.loading = true;
        o.at = *at;
        o.len = len;
        xfer_ = Transfer{};
        xfer_.open = true;
        xfer_.cmd = cmd, xfer_.tag = tag, xfer_.slot = slot;
        xfer_.len = xfer_.left = len;
        xfer_.obj = &o;
        return OK;
    }

    void SoundFw::finish_transfer()
    {
        Object &o = *xfer_.obj;
        o.loading = false;
        o.crc = xfer_.crc.value();
        const uint8_t cmd = xfer_.cmd, tag = xfer_.tag, slot = xfer_.slot;
        xfer_ = Transfer{};
        std::vector<uint8_t> payload{cmd, OK};
        if (cmd == TUNE_LOAD)
        {
            // The header checked: a .sid, or E_DATA and the slot emptied.
            std::string error;
            const std::vector<uint8_t> bytes(store_.begin() + static_cast<std::ptrdiff_t>(o.at),
                                             store_.begin() + static_cast<std::ptrdiff_t>(o.at + o.len));
            files_[slot] = parse_sid(bytes, error);
            if (!files_[slot])
            {
                free_object(o);
                payload[1] = E_DATA;
            }
            else
            {
                const SidFile &f = *files_[slot];
                put16(payload, f.songs);
                put16(payload, f.start_song);
                payload.push_back(static_cast<uint8_t>(f.clock()));
                payload.push_back(f.sid2 ? 2 : 1);
                payload.push_back(static_cast<uint8_t>((f.rsid ? 1 : 0) | (f.cia_timed(f.start_song) ? 2 : 0)));
            }
        }
        event(EV_XFERDONE, tag, payload);
    }

    void SoundFw::abort_transfer()
    {
        if (xfer_.obj && (xfer_.open || xfer_.done))
            free_object(*xfer_.obj);
        xfer_ = Transfer{};
    }
} // namespace ooe::emu
