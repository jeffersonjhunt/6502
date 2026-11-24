/**
 * @file sound_fw.cpp
 * @brief The sound card's firmware, revision 3 (sound_fw.h).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_fw.h"

#include "sound_flash.h"

#include "minimp3.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

    struct SoundFw::Mp3
    {
        /// What a search for a frame is given: ten frames of the largest kind, as minimp3 checks a sync by.
        static constexpr size_t WINDOW = 32'768;
        static constexpr size_t NO_SYNC = 65'536;
        /// minimp3 takes a sync once this many frames follow in step (or the data runs out after one).
        static constexpr unsigned MATCHES = 10;
        /// An ID3v2 tag declaring more than this isn't taken for one: its bytes are junk. And tags adding up to
        /// this with no frame between are S_NO_SYNC.
        static constexpr size_t TAG_MAX = 8u << 20;

        mp3dec_t dec{};
        std::vector<uint8_t> window = std::vector<uint8_t>(WINDOW);
        std::deque<int16_t> pcm;   ///< decoded frames, left then right, held back by the trim's tail
        bool header_done{false};   ///< the first frame looked at (an encoder's gapless header, or not)
        bool end_decoded{false};   ///< no more frames: the data's end decoded
        uint32_t skip{0};          ///< frames still to drop at the start (the encoder's delay)
        uint32_t tail{0};          ///< frames to drop at the end (its padding)
        uint32_t hz{0};
        size_t junk{0};     ///< bytes since the last frame that made samples, tags left out
        size_t tagged{0};   ///< tags' bytes since the last frame that made samples
        size_t tag_left{0}; ///< of an ID3v2 tag being skipped
        bool fresh{false};  ///< junk passed over since the last frame: the next is found by a search, afresh
        size_t looked{0};   ///< how far past the frame at the front a look for anything frame-like has gone
    };

    namespace
    {
        // Revision 2's MP3 feeding rules (sound_card.cpp), on the ring: the same decisions, so a stream fed as it
        // plays decodes as the whole data would.

        /// A layer III header's frame length in bytes, padding included: 0 if `h` isn't one.
        size_t mp3_frame_bytes(const uint8_t *h)
        {
            if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0 || (h[1] & 0x06) != 0x02)
                return 0;
            const unsigned version = (h[1] >> 3) & 3, rate = (h[2] >> 2) & 3, index = h[2] >> 4;
            if (version == 1 || rate == 3 || index == 0 || index == 15)
                return 0;
            static const uint16_t kbps[2][15] = {{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
                                                 {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
            static const uint32_t hz[3] = {44'100, 48'000, 32'000};
            const bool one = version == 3;
            const uint32_t sample_hz = hz[rate] >> (one ? 0 : version == 2 ? 1 : 2);
            const uint32_t samples = one ? 1'152 : 576;
            return samples * kbps[one][index] * 125 / sample_hz + ((h[2] >> 1) & 1);
        }
        /// The same stream as `prev`: version, layer and rate, as the decoder's own check has it.
        bool mp3_same(const uint8_t *prev, const uint8_t *h)
        {
            return prev[0] == 0xFF && h[0] == 0xFF && ((prev[1] ^ h[1]) & 0xFE) == 0 && ((prev[2] ^ h[2]) & 0x0C) == 0;
        }
        /// An ID3v2 tag's whole length (header, body, footer) from its 10-byte header, or 0 if `h` isn't one.
        size_t id3_bytes(const uint8_t *h)
        {
            static const uint8_t flags[5] = {0, 0, 0xC0, 0xE0, 0xF0};
            if (h[0] != 'I' || h[1] != 'D' || h[2] != '3' || h[3] < 2 || h[3] > 4 || h[4] == 0xFF || (h[5] & ~flags[h[3]]))
                return 0;
            size_t size = 0;
            for (unsigned i = 6; i < 10; ++i)
            {
                if (h[i] & 0x80)
                    return 0;
                size = size << 7 | h[i];
            }
            return 10 + size + (h[5] & 0x10 ? 10 : 0);
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
        ready_at_ = now_ + static_cast<uint64_t>(startup_ms_) * SAMPLES_PER_MS;
        running_ = boot_slot_; // the image marked to boot
        fw_pending_ms_ = 0;
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
        if (seq_.state == M_PLAYING)
            music_tick();
        mix_frame();
    }

    void SoundFw::ms_step()
    {
        if (fw_pending_ms_ && --fw_pending_ms_ == 0) // the image checked and written: whether it will boot
        {
            if (fw_status_ == OK) // the write done: the slot holds it, marked to boot (the next power-on or FWRESTART)
            {
                slots_[fw_new_slot_] = fw_new_;
                boot_slot_ = fw_new_slot_;
            }
            event(EV_FWRESULT, 0, {fw_status_, fw_major_, fw_minor_});
        }
        tune_ms();
        streams_ms();
        voices_ms();
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
            // Each chip at its holder's level: a tune's chip at the tunes' (and the tune's fade), else music's when
            // a music voice is on it, else the effects'.
            float tg = static_cast<float>(mix_[SRC_TUNES]) / 255.0f;
            if (tune_.fade_ms && tune_.length_ms && tune_.state != T_STOPPED && tune_.state != T_ENDED &&
                tune_.played_ms + tune_.fade_ms > tune_.length_ms)
                tg *= static_cast<float>(tune_.length_ms - std::min(tune_.played_ms, tune_.length_ms)) /
                      static_cast<float>(tune_.fade_ms);
            for (unsigned n = 0; n < 2; ++n)
            {
                unsigned holder = SRC_EFFECTS;
                if (tune_.chips & (1u << n))
                    holder = SRC_TUNES;
                else
                    for (unsigned v = 3 * n; v < 3 * n + 3; ++v)
                        if (voices_[v].use == V_MUSIC)
                            holder = SRC_MUSIC;
                const float g = holder == SRC_TUNES ? tg : static_cast<float>(mix_[holder]) / 255.0f;
                if (tune_.chips == 3) // a two-SID tune: A left, B right, as revision 2
                    src[holder][n] += chip[n] * g;
                else // a one-SID tune, or none: both chips in both ears
                    src[holder][0] += chip[n] * g, src[holder][1] += chip[n] * g;
            }
        }
        // The ES5503, at half of full scale as revision 2's bus mixes it: the effects' and the boot sound's
        // oscillators (channels 0 and 1) at the effects' level, music's (channels 2 and 3) at music's.
        int16_t d[4];
        port_.doc_frame(d);
        for (unsigned ch = 0; ch < 2; ++ch)
        {
            src[SRC_EFFECTS][ch] += 0.5f * static_cast<float>(d[ch]) / 32768.0f * static_cast<float>(mix_[SRC_EFFECTS]) / 255.0f;
            src[SRC_MUSIC][ch] += 0.5f * static_cast<float>(d[2 + ch]) / 32768.0f * static_cast<float>(mix_[SRC_MUSIC]) / 255.0f;
        }
        // The streams, at half of full scale as revision 2's bus mixes its stream.
        unsigned ran = 0; // each stream once a frame: one that took over at a join has had its frame
        for (unsigned s = 0; s < STREAMS; ++s)
        {
            if (ran & (1u << s))
                continue;
            float f[2] = {0, 0};
            ran |= 1u << s | 1u << stream_frame(s, f);
            for (unsigned ch = 0; ch < 2; ++ch)
                src[SRC_STREAMS][ch] += 0.5f * f[ch] * static_cast<float>(mix_[SRC_STREAMS]) / 255.0f;
        }
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
        if (mtone_.on) // music's tone generator voice: a square as the beep's
        {
            const double tt = mtone_.phase;
            double v = tt < 0.5 ? 1.0 : -1.0;
            v += blep(tt, mtone_.step);
            v -= blep(std::fmod(tt + 0.5, 1.0), mtone_.step);
            mtone_.phase += mtone_.step;
            if (mtone_.phase >= 1.0)
                mtone_.phase -= 1.0;
            const float s = static_cast<float>(v) * mtone_.amp * static_cast<float>(mix_[SRC_MUSIC]) / 255.0f;
            src[SRC_MUSIC][0] += s;
            src[SRC_MUSIC][1] += s;
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
        for (unsigned v = SID_VOICES; v < VOICES; ++v) // the ES5503 is the boot sound's while it plays
            if (voices_[v].use != V_FREE)
                free_voice(v, false);
        for (Sample &s : sample_info_) // its samples over the cache's lower 80 KB
            if (s.page >= 0 && s.page < 320)
            {
                for (unsigned i = 0; i < (1u << s.size); ++i)
                    wave_pages_[static_cast<size_t>(s.page) + i] = false;
                s.page = -1;
            }
        doc_all_ = false;
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
            doc_all_ = false; // the bank sets all 32 again when it next plays
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

    // --- the sound bank: effects and the voice manager -------------------------------------------------

    namespace
    {
        constexpr uint8_t DOC_HALT = 0x01, DOC_ONESHOT = 0x02, DOC_RIGHT = 0x10, DOC_MUSIC = 0x20;
        constexpr uint32_t DOC_RATE_32 = 7'159'090 / (8 * 34); ///< the ES5503's samples a second with all 32: 26,320
        constexpr size_t SID_STEP = 13, SAMPLE_DEF = 11;
        int32_t sgn16(const uint8_t *p) { return static_cast<int16_t>(static_cast<uint16_t>(p[0] | p[1] << 8)); }
    } // namespace

    unsigned SoundFw::cached_samples() const
    {
        unsigned n = 0;
        for (const Sample &s : sample_info_)
            n += s.page >= 0;
        return n;
    }

    bool SoundFw::parse_effect(uint8_t effect, uint8_t kind, const uint8_t *def, size_t n)
    {
        if (kind == 0)
        {
            if (n < 2 || def[0] < 1 || def[0] > 16 || n != 2 + SID_STEP * def[0] || (def[1] != 0xFF && def[1] >= def[0]))
                return false;
            for (unsigned s = 0; s < def[0]; ++s)
                if (get16(def + 2 + SID_STEP * s) == 0)
                    return false;
        }
        else if (kind == 1)
        {
            if (n != SAMPLE_DEF)
                return false;
        }
        else
            return false;
        effects_def_[effect].kind = kind;
        effects_def_[effect].def.assign(def, def + n);
        return true;
    }

    bool SoundFw::sid_voice_free_for(unsigned v) const
    {
        const unsigned chip = v / 3;
        return !(tune_.chips & (1u << chip)) && voices_[v].use != V_MUSIC;
    }

    int SoundFw::give_voice(bool sid, uint8_t prio, uint8_t &stolen_handle, uint8_t &stolen_effect)
    {
        // A free voice first; else the lowest priority, oldest, at or below this one's. Never a tune's chip, never
        // music's voice.
        const unsigned lo = sid ? 0 : SID_VOICES, hi = sid ? SID_VOICES : VOICES;
        int best = -1;
        for (unsigned v = lo; v < hi; ++v)
        {
            if (sid && !sid_voice_free_for(v))
                continue;
            if (voices_[v].use == V_FREE)
                return static_cast<int>(v);
            if (voices_[v].use != V_EFFECT || voices_[v].prio > prio)
                continue;
            if (best < 0 || voices_[v].prio < voices_[static_cast<size_t>(best)].prio ||
                (voices_[v].prio == voices_[static_cast<size_t>(best)].prio && voices_[v].order < voices_[static_cast<size_t>(best)].order))
                best = static_cast<int>(v);
        }
        if (best >= 0)
        {
            stolen_handle = voices_[static_cast<size_t>(best)].handle;
            stolen_effect = voices_[static_cast<size_t>(best)].effect;
            free_voice(static_cast<unsigned>(best), false);
        }
        return best;
    }

    void SoundFw::silence_voice(unsigned v)
    {
        if (v < SID_VOICES)
        {
            const unsigned chip = v / 3, r = 7 * (v % 3);
            uint8_t regs[25];
            port_.sid_regs(chip, regs);
            port_.sid_write(chip, r + 4, static_cast<uint8_t>(regs[r + 4] & ~1u)); // the gate off: its release
        }
        else
        {
            const unsigned o = 2 * (v - SID_VOICES);
            for (const unsigned q : {o, o + 1})
                port_.doc_write(static_cast<uint8_t>(0xA0 + q), static_cast<uint8_t>(DOC_HALT | (q & 1 ? DOC_RIGHT : 0)));
        }
    }

    void SoundFw::free_voice(unsigned v, bool done_event)
    {
        Voice &x = voices_[v];
        if (x.use == V_FREE)
            return;
        silence_voice(v);
        const uint8_t handle = x.handle, effect = x.effect;
        const bool effect_voice = x.use == V_EFFECT;
        x = Voice{};
        if (done_event && effect_voice)
            event(EV_VOICE_DONE, handle, {effect});
    }

    void SoundFw::stop_voices_on_chips(uint8_t chips)
    {
        for (unsigned v = 0; v < SID_VOICES; ++v)
            if ((chips & (1u << (v / 3))) && voices_[v].use == V_EFFECT)
            {
                const uint8_t handle = voices_[v].handle, effect = voices_[v].effect;
                free_voice(v, false);
                event(EV_VOICE_STOLEN, handle, {effect, 0xFF}); // taken by a tune
            }
    }

    void SoundFw::write_sid_freq(unsigned v, int32_t hz16)
    {
        const unsigned chip = v / 3, r = 7 * (v % 3);
        const uint64_t f = static_cast<uint64_t>(std::max<int32_t>(hz16, 0)) * (1u << 20) / sid_clock(); // Hz x 2^24 / clock
        const uint16_t reg = static_cast<uint16_t>(std::min<uint64_t>(f, 0xFFFF));
        port_.sid_write(chip, r, static_cast<uint8_t>(reg));
        port_.sid_write(chip, r + 1, static_cast<uint8_t>(reg >> 8));
    }

    void SoundFw::sid_step(unsigned v)
    {
        // A step of a SID effect begins: its registers as the step has them.
        Voice &x = voices_[v];
        const uint8_t *s = effects_def_[x.effect].def.data() + 2 + SID_STEP * x.step;
        const unsigned chip = v / 3, r = 7 * (v % 3);
        x.step_ms = get16(s);
        x.freq = static_cast<int32_t>(get24(s + 3));
        x.pw = static_cast<int32_t>(get16(s + 6) & 0x0FFF);
        const int32_t played = pitched(x);
        uint8_t regs[25];
        port_.sid_regs(chip, regs);
        if ((regs[0x18] & 0x0F) == 0)
            port_.sid_write(chip, 0x18, static_cast<uint8_t>((regs[0x18] & 0xF0) | 0x0F)); // the chip's volume up
        write_sid_freq(v, played);
        port_.sid_write(chip, r + 2, static_cast<uint8_t>(x.pw));
        port_.sid_write(chip, r + 3, static_cast<uint8_t>(x.pw >> 8));
        port_.sid_write(chip, r + 5, s[8]);
        port_.sid_write(chip, r + 6, s[9]);
        port_.sid_write(chip, r + 4, static_cast<uint8_t>(s[2] & ~0x08u)); // its control (the test bit kept 0)
    }

    bool SoundFw::cache_sample(unsigned slot)
    {
        // The sample into wave RAM, if it isn't there: a table of the next power of two (1 KB at least), at a
        // place of its size; the least recently used samples no voice plays are put out for room.
        Sample &s = sample_info_[slot];
        s.used = ++cache_clock_;
        if (s.page >= 0)
            return true;
        const Object &o = samples_[slot];
        // (A sample longer than the ES5503's largest table, 32 KB, plays its first 32 KB: the model's limit.)
        const size_t region = std::min<size_t>(s.loop_end ? s.loop_end - s.loop_start : o.len, 32768);
        uint8_t size = 2;
        while ((256u << size) < region && size < 7)
            ++size;
        const unsigned pages = 1u << size;
        const unsigned lo = boot_.active ? 320 : 0; // the boot sound's samples, while it plays
        for (;;)
        {
            for (unsigned at = (lo + pages - 1) / pages * pages; at + pages <= 512; at += pages)
            {
                bool free = true;
                for (unsigned i = 0; i < pages && free; ++i)
                    free = !wave_pages_[at + i];
                if (!free)
                    continue;
                for (unsigned i = 0; i < pages; ++i)
                    wave_pages_[at + i] = true;
                s.page = static_cast<int>(at);
                s.size = size;
                // The table: a one-shot's bytes then $00 (the chip stops there); a loop's region, repeated.
                std::vector<uint8_t> table(256u << size, 0);
                if (s.loop_end)
                    for (size_t i = 0; i < table.size(); ++i)
                        table[i] = store_[o.at + s.loop_start + i % region];
                else
                    std::copy(store_.begin() + static_cast<std::ptrdiff_t>(o.at),
                              store_.begin() + static_cast<std::ptrdiff_t>(o.at + std::min<size_t>(o.len, table.size())), table.begin());
                port_.doc_ram(at * 256u, table.data(), table.size());
                return true;
            }
            // Put out the least recently used sample that no voice is playing.
            int lru = -1;
            for (unsigned k = 0; k < SAMPLE_SLOTS; ++k)
            {
                if (k == slot || sample_info_[k].page < 0)
                    continue;
                bool playing = false;
                for (unsigned v = SID_VOICES; v < VOICES; ++v)
                    playing = playing || (voices_[v].use != V_FREE && voices_[v].sample == static_cast<int>(k));
                if (!playing && (lru < 0 || sample_info_[k].used < sample_info_[static_cast<size_t>(lru)].used))
                    lru = static_cast<int>(k);
            }
            if (lru < 0)
                return false;
            Sample &out = sample_info_[static_cast<size_t>(lru)];
            for (unsigned i = 0; i < (1u << out.size); ++i)
                wave_pages_[static_cast<size_t>(out.page) + i] = false;
            out.page = -1;
        }
    }

    void SoundFw::doc_voice_volume(unsigned v, uint32_t level)
    {
        const Voice &x = voices_[v];
        const unsigned o = 2 * (v - SID_VOICES);
        const uint32_t vol = x.vol * level / 255;
        port_.doc_write(static_cast<uint8_t>(0x40 + o), static_cast<uint8_t>(vol * (255u - x.pan) / 255u));
        port_.doc_write(static_cast<uint8_t>(0x40 + o + 1), static_cast<uint8_t>(vol * x.pan / 255u));
    }

    uint8_t SoundFw::doc_channel(unsigned v, unsigned q) const
    {
        // Even oscillators left, odd right; a music voice's on channels 2 and 3, the mix's music source.
        return static_cast<uint8_t>((q & 1 ? DOC_RIGHT : 0) | (voices_[v].use == V_MUSIC ? DOC_MUSIC : 0));
    }

    void SoundFw::doc_voice_start(unsigned v, unsigned slot, uint32_t rate)
    {
        // Its two oscillators (even left, odd right) on the sample's table, at `rate` samples a second.
        const Sample &s = sample_info_[slot];
        const unsigned o = 2 * (v - SID_VOICES);
        if (!doc_all_)
        {
            port_.doc_write(0xE1, 31 << 1); // all 32 oscillators: the bank's rate
            doc_all_ = true;
        }
        const uint64_t f = static_cast<uint64_t>(rate) * (1u << (16 - s.size)) / DOC_RATE_32;
        const uint16_t freq = static_cast<uint16_t>(std::min<uint64_t>(f, 0xFFFF));
        const uint8_t table = static_cast<uint8_t>((s.page >= 256 ? 0x40 : 0) | s.size << 3 | 7);
        const uint8_t mode = voices_[v].looped ? 0 : DOC_ONESHOT;
        for (const unsigned q : {o, o + 1})
        {
            const uint8_t ch = doc_channel(v, q);
            port_.doc_write(static_cast<uint8_t>(0xA0 + q), static_cast<uint8_t>(DOC_HALT | mode | ch));
            port_.doc_write(static_cast<uint8_t>(0x00 + q), static_cast<uint8_t>(freq));
            port_.doc_write(static_cast<uint8_t>(0x20 + q), static_cast<uint8_t>(freq >> 8));
            port_.doc_write(static_cast<uint8_t>(0xC0 + q), table);
            port_.doc_write(static_cast<uint8_t>(0x80 + q), static_cast<uint8_t>(s.page & 0xFF));
        }
        doc_voice_volume(v, voices_[v].attack ? 0 : 255);
        for (const unsigned q : {o, o + 1}) // from the top, together
            port_.doc_write(static_cast<uint8_t>(0xA0 + q), static_cast<uint8_t>(mode | doc_channel(v, q)));
    }

    uint8_t SoundFw::play_effect(const uint8_t *p, uint8_t &handle)
    {
        const uint8_t effect = p[0], vol = p[1], pan = p[2], prio = p[3], chip = p[6];
        const int32_t cents = sgn16(p + 4);
        const EffectDef &d = effects_def_[effect];
        if (prio > 3 || chip > 2)
            return E_PARAM;
        if (d.kind == 0xFF)
            return E_SLOT;
        const bool sid = d.kind == 0;
        if ((sid && chip == 2) || (!sid && chip == 1))
            return E_PARAM;
        if (!sid)
        {
            const unsigned slot = d.def[0];
            if (!samples_[slot].used || samples_[slot].loading || !sample_info_[slot].ready)
                return E_SLOT;
            if (boot_.active)
                return E_BUSY; // the ES5503 is the boot sound's while it plays
        }
        if (!sid && !cache_sample(d.def[0]))
            return E_BUSY; // wave RAM all in use by voices playing: refused before any voice is stolen
        uint8_t sh = 0, se = 0;
        const int v = give_voice(sid, prio, sh, se);
        if (v < 0)
            return E_BUSY;
        if (sh)
            event(EV_VOICE_STOLEN, sh, {se, prio});
        // A handle no voice has.
        for (int guard = 0; guard < 256; ++guard)
        {
            bool taken = next_handle_ == 0;
            for (const Voice &x : voices_)
                taken = taken || (x.use != V_FREE && x.handle == next_handle_);
            if (!taken)
                break;
            ++next_handle_;
        }
        Voice &x = voices_[static_cast<unsigned>(v)];
        x = Voice{};
        x.use = V_EFFECT;
        x.handle = handle = next_handle_++;
        x.effect = effect;
        x.prio = prio;
        x.order = ++voice_order_;
        x.vol = vol;
        x.pan = pan;
        if (sid)
        {
            x.step = 0;
            x.ratio = std::pow(2.0, cents / 1200.0);
            sid_step(static_cast<unsigned>(v));
        }
        else
        {
            const uint8_t *s = d.def.data();
            x.sample = s[0];
            const Sample &smp = sample_info_[s[0]];
            const uint32_t rate = get16(s + 1) ? get16(s + 1) : smp.rate;
            x.looped = s[3] == 1;
            x.attack = get16(s + 4);
            x.hold = get16(s + 6);
            x.release = get16(s + 8);
            doc_voice_start(static_cast<unsigned>(v), s[0], static_cast<uint32_t>(rate * std::pow(2.0, cents / 1200.0)));
        }
        return OK;
    }

    void SoundFw::voices_ms()
    {
        for (unsigned v = 0; v < VOICES; ++v)
        {
            Voice &x = voices_[v];
            if (x.use != V_EFFECT)
                continue;
            if (v < SID_VOICES)
            {
                const auto &def = effects_def_[x.effect].def;
                const uint8_t *s = def.data() + 2 + SID_STEP * x.step;
                if (--x.step_ms == 0)
                {
                    if (x.step + 1 < def[0])
                        ++x.step;
                    else if (def[1] != 0xFF)
                        x.step = def[1];
                    else
                    {
                        free_voice(v, true); // the last step done
                        continue;
                    }
                    sid_step(v);
                    continue;
                }
                const int32_t fs = sgn16(s + 10), ps = static_cast<int8_t>(s[12]);
                if (fs || ps) // the sweeps, a ms at a time
                {
                    x.freq += fs;
                    x.pw = std::clamp<int32_t>(x.pw + ps, 0, 0x0FFF);
                    write_sid_freq(v, pitched(x));
                    const unsigned chip = v / 3, r = 7 * (v % 3);
                    port_.sid_write(chip, r + 2, static_cast<uint8_t>(x.pw));
                    port_.sid_write(chip, r + 3, static_cast<uint8_t>(x.pw >> 8));
                }
                continue;
            }
            // A sample's envelope: attack, hold, release (a ms at a time); one-shot's end when its oscillators halt.
            ++x.env_ms;
            const unsigned o = 2 * (v - SID_VOICES);
            const bool halted = (port_.doc_read(static_cast<uint8_t>(0xA0 + o)) & DOC_HALT) &&
                                (port_.doc_read(static_cast<uint8_t>(0xA0 + o + 1)) & DOC_HALT);
            if (halted)
            {
                free_voice(v, true);
                continue;
            }
            if (!x.releasing && x.hold && x.env_ms >= x.attack + x.hold)
                x.releasing = true, x.release_ms = 0;
            if (x.releasing)
            {
                if (++x.release_ms >= x.release)
                {
                    free_voice(v, true);
                    continue;
                }
                doc_voice_volume(v, 255 - 255 * x.release_ms / std::max<uint32_t>(x.release, 1));
            }
            else if (x.attack && x.env_ms <= x.attack)
                doc_voice_volume(v, 255 * x.env_ms / x.attack);
        }
    }

    // --- the emulator's session: a tune outside the bus -----------------------------------------------------

    uint8_t SoundFw::host_tune(const std::vector<uint8_t> &file, unsigned song, std::string &why)
    {
        constexpr uint8_t SLOT = TUNE_SLOTS - 1;
        auto parsed = parse_sid(file, why);
        if (!parsed)
            return E_DATA;
        if (tune_.state != T_STOPPED && tune_.slot == SLOT)
            stop_tune(T_STOPPED);
        if (xfer_.obj == &tunes_[SLOT])
            abort_transfer();
        free_object(tunes_[SLOT]);
        files_[SLOT].reset();
        const auto at = allocate(file.size());
        if (!at)
        {
            why = "the card's store is full";
            return E_MEMORY;
        }
        std::copy(file.begin(), file.end(), store_.begin() + static_cast<std::ptrdiff_t>(*at));
        tunes_[SLOT].used = true;
        tunes_[SLOT].at = *at;
        tunes_[SLOT].len = file.size();
        tunes_[SLOT].crc = Crc32::of(file.data(), file.size());
        files_[SLOT] = std::move(parsed);
        return host_song(song, why);
    }

    uint8_t SoundFw::host_song(unsigned song, std::string &why)
    {
        constexpr uint8_t SLOT = TUNE_SLOTS - 1;
        if (!files_[SLOT])
        {
            why = "no tune";
            return E_SLOT;
        }
        if (song > 255 || song > files_[SLOT]->songs)
        {
            why = "it has " + std::to_string(files_[SLOT]->songs) + " songs";
            return E_PARAM;
        }
        const uint8_t p[9] = {SLOT, static_cast<uint8_t>(song), 0, 0, 0, 0, 0, 0, 0};
        const uint8_t status = play_tune(p);
        if (status == E_DATA)
            plan_sid(*files_[SLOT], song, why); // the planner's reason
        else if (status == E_BUSY)
            why = "music holds its chips";
        else if (status != OK)
            why = "refused";
        return status;
    }

    void SoundFw::host_stop()
    {
        if (tune_.state != T_STOPPED)
            stop_tune(T_STOPPED);
    }

    // --- the firmware update ------------------------------------------------------------------------------

    uint8_t SoundFw::check_image(Image &out) const
    {
        // Its header (the S1 appendix's), its length and both CRCs: else E_FIRMWARE, and the old image boots.
        const uint8_t *h = &store_[image_.at];
        const size_t n = image_.len;
        if (n < 32 || std::memcmp(h, "OOSD", 4) || h[4] != CARD_TYPE || h[5] != 1)
            return E_FIRMWARE;
        const uint32_t body = get32(h + 12);
        if (static_cast<size_t>(body) + 32 != n || Crc32::of(h, 28) != get32(h + 28) || Crc32::of(h + 32, body) != get32(h + 16))
            return E_FIRMWARE;
        out.valid = true;
        out.major = h[6];
        out.minor = h[7];
        out.build = static_cast<uint16_t>(get16(h + 8));
        return OK;
    }

    // --- music: note blocks and their sequencer ----------------------------------------------------------

    uint8_t SoundFw::parse_block(unsigned slot)
    {
        // The header, an instrument for each voice in its mask, then its entries (the spec's S1 appendix).
        const Object &o = music_[slot];
        const uint8_t *b = &store_[o.at];
        const size_t n = o.len;
        if (n < 8 || b[0] != 1 || b[1] == 0)
            return E_DATA;
        Block blk;
        blk.mask = b[1];
        const uint32_t count = get32(b + 4);
        size_t at = 8;
        for (unsigned v = 0; v < 8; ++v)
            if (blk.mask & (1u << v))
            {
                if (at + 8 > n || b[at] > 2)
                    return E_DATA;
                std::copy(b + at, b + at + 8, blk.inst[v].b);
                at += 8;
            }
        static constexpr size_t SIZE[6] = {10, 12, 5, 9, 2, 2};
        blk.entries.reserve(count);
        for (uint32_t k = 0; k < count; ++k)
        {
            if (at >= n)
                return E_DATA;
            const uint8_t kind = b[at] >> 4, voice = b[at] & 0x0F;
            if (kind > 5 || voice > 7 || !(blk.mask & (1u << voice)) || at + SIZE[kind] > n)
                return E_DATA;
            const uint8_t *e = b + at + 1;
            Entry en;
            en.kind = kind;
            en.voice = voice;
            switch (kind)
            {
            case 0: // a note: 69 the A at 440 Hz
                en.freq16 = static_cast<uint32_t>(std::lround(440.0 * 16.0 * std::pow(2.0, (static_cast<int>(e[0] & 0x7F) - 69) / 12.0)));
                en.len = get32(e + 1);
                en.gate = get32(e + 5);
                break;
            case 1:
                en.freq16 = get24(e);
                en.len = get32(e + 3);
                en.gate = get32(e + 7);
                break;
            case 2:
                en.len = get32(e);
                break;
            case 3:
                if (e[0] > 2)
                    return E_DATA;
                std::copy(e, e + 8, en.inst.b);
                break;
            default:
                en.value = e[0];
                break;
            }
            if ((kind <= 1 && en.gate > en.len) || (kind == 5 && en.value == 255))
                return E_DATA;
            if (kind <= 2)
                ++blk.notes[voice];
            blk.entries.push_back(en);
            at += SIZE[kind];
        }
        if (at != n)
            return E_DATA;
        blocks_[slot] = std::move(blk);
        return OK;
    }

    uint32_t SoundFw::queued_notes(unsigned lv) const
    {
        uint32_t n = 0;
        for (const auto &q : seq_.queue)
            if (blocks_[q.first])
                n += blocks_[q.first]->notes[lv];
        return n;
    }

    uint32_t SoundFw::notes_waiting() const
    {
        uint32_t n = 0;
        for (unsigned lv = 0; lv < 8; ++lv)
            n += seq_.v[lv].waiting + queued_notes(lv);
        return n;
    }

    uint8_t SoundFw::start_block(uint8_t slot, bool free_when_done)
    {
        // The voices the block names: SID voices of chips no tune holds, the ES5503's, the tone generator's one.
        // All of them free, or E_BUSY: music is never stolen and doesn't steal.
        const Block &blk = *blocks_[slot];
        std::array<int, 8> phys{};
        phys.fill(-2);
        bool tone = false;
        for (unsigned lv = 0; lv < 8; ++lv)
        {
            if (!(blk.mask & (1u << lv)))
                continue;
            const uint8_t src = blk.inst[lv].b[0];
            if (src == 2)
            {
                if (tone)
                    return E_BUSY;
                tone = true;
                phys[lv] = -1;
                continue;
            }
            if (src == 1 && boot_.active)
                return E_BUSY;
            const unsigned lo = src == 0 ? 0 : SID_VOICES, hi = src == 0 ? SID_VOICES : VOICES;
            for (unsigned v = lo; v < hi && phys[lv] == -2; ++v)
            {
                if (voices_[v].use != V_FREE || (src == 0 && !sid_voice_free_for(v)) ||
                    std::find(phys.begin(), phys.end(), static_cast<int>(v)) != phys.end())
                    continue;
                phys[lv] = static_cast<int>(v);
            }
            if (phys[lv] == -2)
                return E_BUSY;
        }
        seq_.state = M_PLAYING;
        seq_.slot = slot;
        seq_.free_when_done = free_when_done;
        const uint64_t t0 = now_us();
        if (seq_.played_us == 0)
            seq_.started_us = t0;
        seq_.played_us = 1;
        for (unsigned lv = 0; lv < 8; ++lv)
        {
            MusicVoice &mv = seq_.v[lv];
            mv = MusicVoice{};
            if (!(blk.mask & (1u << lv)))
            {
                mv.done = true;
                continue;
            }
            mv.phys = phys[lv];
            mv.tone = phys[lv] == -1;
            if (mv.phys >= 0)
            {
                Voice &x = voices_[static_cast<size_t>(mv.phys)];
                x = Voice{};
                x.use = V_MUSIC;
            }
            mv.next_us = t0;
            mv.waiting = blk.notes[lv];
            music_instrument(lv, blk.inst[lv]);
        }
        for (uint32_t k = 0; k < blk.entries.size(); ++k)
            seq_.v[blk.entries[k].voice].order.push_back(k);
        return OK;
    }

    void SoundFw::stop_music()
    {
        for (unsigned lv = 0; lv < 8; ++lv)
        {
            MusicVoice &mv = seq_.v[lv];
            if (mv.phys >= 0)
                free_voice(static_cast<unsigned>(mv.phys), false);
            mv = MusicVoice{};
        }
        mtone_ = MusicTone{};
        if (seq_.free_when_done && seq_.slot >= 0)
        {
            free_object(music_[static_cast<size_t>(seq_.slot)]);
            blocks_[static_cast<size_t>(seq_.slot)].reset();
        }
        const uint32_t thresh = seq_.thresh;
        seq_ = Music{};
        seq_.thresh = thresh;
    }

    void SoundFw::music_instrument(unsigned lv, const Instrument &in)
    {
        MusicVoice &mv = seq_.v[lv];
        mv.inst = in;
        mv.vol = in.b[6];
        if (mv.phys >= 0 && static_cast<unsigned>(mv.phys) < SID_VOICES)
        {
            const unsigned v = static_cast<unsigned>(mv.phys), chip = v / 3, r = 7 * (v % 3);
            port_.sid_write(chip, r + 2, in.b[4]);
            port_.sid_write(chip, r + 3, static_cast<uint8_t>(in.b[5] & 0x0F));
            port_.sid_write(chip, r + 5, in.b[2]);
            port_.sid_write(chip, r + 6, in.b[3]);
            uint8_t regs[25];
            port_.sid_regs(chip, regs);
            port_.sid_write(chip, 0x18, static_cast<uint8_t>((regs[0x18] & 0xF0) | (in.b[6] >> 4))); // the chip's volume, music's
        }
    }

    void SoundFw::music_gate(unsigned lv, bool on)
    {
        MusicVoice &mv = seq_.v[lv];
        if (mv.tone)
        {
            mtone_.on = on;
            return;
        }
        const unsigned v = static_cast<unsigned>(mv.phys);
        if (v < SID_VOICES)
        {
            const unsigned chip = v / 3, r = 7 * (v % 3);
            port_.sid_write(chip, r + 4, static_cast<uint8_t>((mv.inst.b[1] & 0xFE) | (on ? 1 : 0)));
        }
        else if (!on)
            silence_voice(v);
    }

    void SoundFw::music_entry(unsigned lv, const Entry &e, uint64_t at)
    {
        MusicVoice &mv = seq_.v[lv];
        switch (e.kind)
        {
        case 0:
        case 1:
        {
            --mv.waiting;
            if (notes_waiting() + 1 == seq_.thresh && seq_.thresh) // fell from n to n - 1
            {
                std::vector<uint8_t> v;
                put16(v, notes_waiting());
                v.push_back(static_cast<uint8_t>(seq_.thresh));
                event(EV_MUSIC_COUNT, 0, v);
            }
            if (mv.tone)
            {
                mtone_.step = e.freq16 / 16.0 / RATE;
                mtone_.amp = static_cast<float>(mv.vol) / 255.0f * 0.25f;
            }
            else if (static_cast<unsigned>(mv.phys) < SID_VOICES)
                write_sid_freq(static_cast<unsigned>(mv.phys), static_cast<int32_t>(e.freq16));
            else
            {
                // The sample at the note's pitch: its rate x 2^((note - its root) / 12), as the Hz say.
                const unsigned slot = mv.inst.b[1];
                if (samples_[slot].used && sample_info_[slot].ready && cache_sample(slot))
                {
                    const double root = 440.0 * 16.0 * std::pow(2.0, (static_cast<int>(mv.inst.b[2]) - 69) / 12.0);
                    Voice &x = voices_[static_cast<size_t>(mv.phys)];
                    x.sample = static_cast<int>(slot);
                    x.vol = mv.vol;
                    x.pan = mv.inst.b[7];
                    x.looped = mv.inst.b[3] == 1;
                    doc_voice_start(static_cast<unsigned>(mv.phys), slot,
                                    static_cast<uint32_t>(sample_info_[slot].rate * (e.freq16 / root)));
                }
            }
            mv.gated = e.gate > 0;
            music_gate(lv, mv.gated);
            mv.gate_off_us = at + e.gate;
            mv.next_us = at + e.len;
            break;
        }
        case 2:
            --mv.waiting;
            if (notes_waiting() + 1 == seq_.thresh && seq_.thresh)
            {
                std::vector<uint8_t> v;
                put16(v, notes_waiting());
                v.push_back(static_cast<uint8_t>(seq_.thresh));
                event(EV_MUSIC_COUNT, 0, v);
            }
            if (mv.gated)
                music_gate(lv, false), mv.gated = false;
            mv.next_us = at + e.len;
            break;
        case 3:
            music_instrument(lv, e.inst);
            break;
        case 4:
            mv.vol = e.value;
            mv.inst.b[6] = e.value;
            if (mv.tone)
                mtone_.amp = static_cast<float>(mv.vol) / 255.0f * 0.25f;
            break;
        default:
            event(EV_MUSIC_MARK, static_cast<uint8_t>(lv), {e.value, static_cast<uint8_t>(seq_.slot)});
            break;
        }
    }

    void SoundFw::music_tick()
    {
        const uint64_t now = now_us();
        const Block &blk = *blocks_[static_cast<size_t>(seq_.slot)];
        bool all_done = true;
        for (unsigned lv = 0; lv < 8; ++lv)
        {
            MusicVoice &mv = seq_.v[lv];
            if (mv.done)
                continue;
            if (mv.gated && mv.gate_off_us <= now && mv.gate_off_us < mv.next_us)
                music_gate(lv, false), mv.gated = false;
            while (!mv.done && mv.next_us <= now)
            {
                if (mv.pos == mv.order.size())
                {
                    if (mv.gated)
                        music_gate(lv, false), mv.gated = false;
                    mv.done = true;
                    break;
                }
                const uint64_t at = mv.next_us;
                music_entry(lv, blk.entries[mv.order[mv.pos++]], at);
            }
            all_done = all_done && mv.done;
        }
        if (!all_done)
            return;
        // The block has finished: the next queued, or MUSIC_DONE, once, for the last.
        const uint8_t last = static_cast<uint8_t>(seq_.slot);
        const uint64_t started = seq_.started_us;
        const uint32_t thresh = seq_.thresh;
        std::deque<std::pair<uint8_t, bool>> queue = seq_.queue;
        stop_music();
        seq_.thresh = thresh;
        while (!queue.empty())
        {
            const auto [slot, free] = queue.front();
            queue.pop_front();
            if (blocks_[slot] && start_block(slot, free) == OK)
            {
                seq_.queue = queue;
                seq_.started_us = started;
                return;
            }
        }
        std::vector<uint8_t> v;
        put32(v, static_cast<uint32_t>((now - started) / 1000));
        event(EV_MUSIC_DONE, last, v);
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
        for (unsigned v = 0; v < SID_VOICES; ++v) // music is never stolen: its chips aren't the tune's to take
            if (voices_[v].use == V_MUSIC && ((f.sid2 ? 3u : 1u) & (1u << (v / 3))))
                return E_BUSY;
        if (tune_.state != T_STOPPED)
            port_.tune_stop();
        tune_ = Tune{};
        tune_.ntsc = f.ntsc();
        stop_voices_on_chips(f.sid2 ? 3 : 1); // a tune takes whole chips: effects on them stop
        tune_.slot = slot;
        tune_.song = static_cast<uint16_t>(song);
        tune_.chips = f.sid2 ? 3 : 1;
        tune_.length_ms = get32(p + 3);
        tune_.fade_ms = get16(p + 7);
        tune_.has_play = f.play != 0;
        port_.tune_start(*plan, f.ntsc(), f.sid2 == 0xD500);
        sid_model_ = plan->model == SidModel::Mos8580 ? 2 : 1;
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

    bool SoundFw::cmd_write(uint8_t b, uint32_t fraction)
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
            // Due at the first sample LATENCY_US or more after the byte's card time (now_ and its fraction).
            const uint64_t ppm = uint64_t{fraction} + uint64_t{latency_us_} * RATE; // millionths of a sample
            const uint64_t due = now_ + std::max<uint64_t>(1, (ppm + 999'999) / 1'000'000);
            records_.push_back({std::move(partial_), due, error_});
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
        if (xfer_.cmd == STREAM_DATA)
        {
            Stream &st = streams_[xfer_.slot];
            const Object &ring = rings_[xfer_.slot];
            store_[ring.at + st.wr % ring.len] = b;
            ++st.wr;
        }
        else
        {
            Object &o = *xfer_.obj;
            store_[o.at + (xfer_.len - xfer_.left)] = b;
        }
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
        for (const auto &o : rings_)
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
            {
                // In order: it, and everything behind it, waits for the transfer; but XFERABORT, which ends the
                // transfer, runs at once (its reply before the waiting record's), so a host can always end a load.
                for (auto it = records_.begin() + 1; it != records_.end(); ++it)
                    if (it->rec[0] == XFERABORT && it->refused == OK && it->due <= now_ &&
                        evt_room() >= reply_room(XFERABORT))
                    {
                        const Pending a = std::move(*it);
                        records_.erase(it);
                        cmd_used_ -= a.rec.size();
                        running_room_ = reply_room(XFERABORT);
                        execute(a.rec);
                        running_room_ = 0;
                        return true;
                    }
                return false;
            }
        }
        const Pending q = std::move(p);
        records_.pop_front();
        cmd_used_ -= q.rec.size();
        if (cmd_was_full_)
            cmd_was_full_ = false, cmdroom_ = true;
        // Its reply's room stays kept while it runs: an event it raises (VOICE_STOLEN, TUNE_STARTED) can't take it.
        running_room_ = reply_room(q.rec[0]);
        if (q.refused != OK)
            reply(q.rec[2], q.refused);
        else
            execute(q.rec);
        running_room_ = 0;
        return true;
    }

    SoundFw::Object *SoundFw::waits_on(const std::vector<uint8_t> &rec)
    {
        if (rec[0] == CHECKSUM)
            return object(rec[3], rec[4]);
        if ((rec[0] == TUNE_PLAY || rec[0] == TUNE_INFO) && rec[3] < TUNE_SLOTS)
            return &tunes_[rec[3]];
        if (rec[0] == MUSIC_PLAY && rec[3] < MUSIC_SLOTS)
            return &music_[rec[3]];
        if (rec[0] == PLAY) // its effect's table, or its sample, still coming
        {
            if (effects_.loading)
                return &effects_;
            const EffectDef &d = effects_def_[rec[3]];
            if (d.kind == 1 && samples_[d.def[0]].loading)
                return &samples_[d.def[0]];
        }
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
            r.push_back(version_major());
            r.push_back(version_minor());
            put16(r, slots_[running_].build);
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
            r.push_back(sid_model_); // each SID's model as a tune last set it (the chips share the setting)
            r.push_back(sid_model_);
            r.push_back(running_); // image A or B
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
            std::vector<uint8_t> r; // the bytes that had come; of a stream's chunk, those kept (already played)
            put24(r, static_cast<uint32_t>(xfer_.cmd == STREAM_DATA ? chunk_kept() : xfer_.len - xfer_.left));
            abort_transfer();
            reply(tag, OK, r);
            break;
        }
        case SAMPLE_LOAD:
        {
            const uint32_t len = get24(p + 1);
            if (len == 0 || (p[4] & ~3u) || get16(p + 5) == 0)
            {
                reply(tag, E_PARAM);
                break;
            }
            if (const uint8_t refused = check_open(len, samples_[p[0]]))
            {
                reply(tag, refused); // the slot as it was
                break;
            }
            for (unsigned v = SID_VOICES; v < VOICES; ++v) // a slot loaded again: its voices stop first
                if (voices_[v].use != V_FREE && voices_[v].sample == p[0])
                    free_voice(v, false);
            Sample &s = sample_info_[p[0]];
            if (s.page >= 0)
                for (unsigned i = 0; i < (1u << s.size); ++i)
                    wave_pages_[static_cast<size_t>(s.page) + i] = false;
            s = Sample{};
            s.flags = p[4];
            s.rate = get16(p + 5);
            s.loop_start = get24(p + 7);
            s.loop_end = get24(p + 10);
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
            if (const uint8_t refused = check_open(len, tunes_[p[0]]))
            {
                reply(tag, refused); // the slot, and its tune playing, as they were
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
            r.push_back(static_cast<uint8_t>((f.rsid ? 1 : 0) | (f.sid2 ? 2 : 0) | (f.model() == SidModel::Mos8580 ? 4 : 0) |
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
        case STREAM_OPEN:
            reply(tag, open_stream(p));
            break;
        case STREAM_DATA:
        {
            const unsigned s = p[0];
            const uint32_t len = get24(p + 1);
            if (s >= STREAMS || !streams_[s].open || streams_[s].ended || len == 0)
            {
                reply(tag, s >= STREAMS || len == 0 ? E_PARAM : E_STATE);
                break;
            }
            Stream &st = streams_[s];
            if (rings_[s].len - (st.wr - st.rd) < len)
            {
                reply(tag, E_MEMORY); // the ring hasn't room
                break;
            }
            if (xfer_.open || xfer_.done)
            {
                reply(tag, E_XFER);
                break;
            }
            st.need_out = false;
            st.chunk_start = st.wr;
            xfer_ = Transfer{};
            xfer_.open = true;
            xfer_.cmd = cmd, xfer_.tag = tag, xfer_.slot = static_cast<uint8_t>(s);
            xfer_.len = xfer_.left = len;
            chunks_[s] = Object{};
            chunks_[s].loading = true;
            xfer_.obj = &chunks_[s];
            reply(tag, OK);
            break;
        }
        case STREAM_END:
        case STREAM_PAUSE:
        case STREAM_RESUME:
        case STREAM_FLUSH:
        case STREAM_CLOSE:
        {
            const unsigned s = p[0];
            if (s >= STREAMS || !streams_[s].open)
            {
                reply(tag, s >= STREAMS ? E_PARAM : E_STATE);
                break;
            }
            Stream &st = streams_[s];
            uint8_t status = OK;
            if (cmd == STREAM_END)
            {
                // What is in plays out: the chunk coming too, so the end waits for it.
                if (xfer_.cmd == STREAM_DATA && xfer_.slot == s && (xfer_.open || xfer_.done))
                    st.end_pending = true;
                else
                    st.ended = true;
            }
            else if (cmd == STREAM_PAUSE)
            {
                if (st.paused || st.state == S_ENDED)
                    status = E_STATE;
                else
                    st.paused = true;
            }
            else if (cmd == STREAM_RESUME)
            {
                if (!st.paused)
                    status = E_STATE;
                else
                    st.paused = false;
            }
            else if (cmd == STREAM_FLUSH)
            {
                if (xfer_.cmd == STREAM_DATA && xfer_.slot == s && (xfer_.open || xfer_.done))
                    abort_transfer();
                st.rd = st.wr;
                st.ended = st.need_out = st.underrun = st.end_pending = false;
                st.state = S_BUFFERING;
                // A seek: the decoder and its queue afresh, the gapless trim kept (the encoder's delay applied at
                // the start, its padding still owed at the end).
                const bool header = st.mp3 && st.mp3->header_done;
                const uint32_t tail = st.mp3 ? st.mp3->tail : 0;
                reset_decoder(st);
                if (st.mp3)
                    st.mp3->header_done = header, st.mp3->tail = tail, st.mp3->skip = 0;
            }
            else
                close_stream(s);
            reply(tag, status);
            break;
        }
        case STREAM_NEXT:
        {
            const unsigned s = p[0], n = p[1];
            if (s >= STREAMS || n >= STREAMS || s == n || !streams_[s].open || !streams_[n].open)
            {
                reply(tag, s >= STREAMS || n >= STREAMS || s == n ? E_PARAM : E_STATE);
                break;
            }
            streams_[s].next = static_cast<int>(n);
            streams_[n].queued = true;
            reply(tag, OK);
            break;
        }
        case STREAM_STATUS:
        {
            const unsigned s = p[0];
            if (s >= STREAMS)
            {
                reply(tag, E_PARAM);
                break;
            }
            const Stream &st = streams_[s];
            std::vector<uint8_t> r{st.paused && st.open ? uint8_t{S_PAUSED} : st.state, st.codec};
            put16(r, st.codec == 0 ? st.rate : (st.mp3 ? st.mp3->hz : 0));
            r.push_back(st.fmt);
            put32(r, static_cast<uint32_t>(st.wr - st.rd));
            put32(r, st.open ? buffered_ms(st) : 0);
            put32(r, static_cast<uint32_t>(st.frames));
            put16(r, st.kbps);
            put16(r, st.underruns);
            reply(tag, OK, r);
            break;
        }
        case MUSIC_LOAD:
        {
            const uint32_t len = get24(p + 1);
            if (p[0] >= MUSIC_SLOTS || len < 8)
            {
                reply(tag, E_PARAM);
                break;
            }
            if (seq_.slot == p[0] || std::any_of(seq_.queue.begin(), seq_.queue.end(), [&](const auto &q) { return q.first == p[0]; }))
            {
                reply(tag, E_BUSY); // playing or queued
                break;
            }
            if (const uint8_t refused = check_open(len, music_[p[0]]))
            {
                reply(tag, refused);
                break;
            }
            blocks_[p[0]].reset();
            reply(tag, open_transfer(cmd, tag, p[0], len, music_[p[0]]));
            break;
        }
        case MUSIC_PLAY:
        {
            const uint8_t slot = p[0], flags = p[1];
            if (slot >= MUSIC_SLOTS || (flags & ~3u))
            {
                reply(tag, E_PARAM);
                break;
            }
            if (!blocks_[slot])
            {
                reply(tag, E_SLOT);
                break;
            }
            if ((flags & 1) && seq_.state != M_STOPPED)
            {
                seq_.queue.push_back({slot, (flags & 2) != 0}); // after what plays (PLAY's MB)
                reply(tag, OK);
                break;
            }
            stop_music();
            reply(tag, start_block(slot, (flags & 2) != 0));
            break;
        }
        case MUSIC_STOP:
            stop_music();
            reply(tag, OK);
            break;
        case MUSIC_PAUSE:
            if (seq_.state != M_PLAYING)
            {
                reply(tag, E_STATE);
                break;
            }
            seq_.state = M_PAUSED;
            seq_.paused_at = now_us();
            for (unsigned lv = 0; lv < 8; ++lv)
            {
                MusicVoice &mv = seq_.v[lv];
                if (!mv.gated)
                    continue;
                if (!mv.tone && mv.phys >= static_cast<int>(SID_VOICES)) // a sample: halted if it sounds, kept for resume
                {
                    const unsigned o = 2 * (static_cast<unsigned>(mv.phys) - SID_VOICES);
                    mv.doc_held = !(port_.doc_read(static_cast<uint8_t>(0xA0 + o)) & DOC_HALT);
                }
                music_gate(lv, false), mv.gated = true; // silent, still its note's
            }
            reply(tag, OK);
            break;
        case MUSIC_RESUME:
        {
            if (seq_.state != M_PAUSED)
            {
                reply(tag, E_STATE);
                break;
            }
            const uint64_t held = now_us() - seq_.paused_at;
            for (unsigned lv = 0; lv < 8; ++lv)
            {
                MusicVoice &mv = seq_.v[lv];
                mv.next_us += held;
                mv.gate_off_us += held;
                if (mv.gated)
                    music_gate(lv, true);
                if (mv.doc_held) // a sample halted by the pause: its oscillators again (the ES5503 starts them from the top)
                {
                    const unsigned v = static_cast<unsigned>(mv.phys), o = 2 * (v - SID_VOICES);
                    const uint8_t mode = voices_[v].looped ? 0 : DOC_ONESHOT;
                    for (const unsigned q : {o, o + 1})
                        port_.doc_write(static_cast<uint8_t>(0xA0 + q), static_cast<uint8_t>(mode | doc_channel(v, q)));
                    mv.doc_held = false;
                }
            }
            seq_.started_us += held;
            seq_.state = M_PLAYING;
            reply(tag, OK);
            break;
        }
        case MUSIC_STATUS:
        {
            std::vector<uint8_t> r{seq_.state, static_cast<uint8_t>(seq_.queue.size())};
            for (unsigned lv = 0; lv < 8; ++lv)
                put16(r, std::min<uint32_t>(seq_.v[lv].waiting + queued_notes(lv), 0xFFFF));
            const uint64_t played = seq_.state == M_STOPPED ? 0
                                    : (seq_.state == M_PAUSED ? seq_.paused_at : now_us()) - seq_.started_us;
            put32(r, static_cast<uint32_t>(played / 1000));
            put16(r, std::min<uint32_t>(notes_waiting(), 0xFFFF));
            reply(tag, OK, r);
            break;
        }
        case MUSIC_THRESH:
            seq_.thresh = p[0];
            reply(tag, OK);
            break;
        case SELFTEST:
        {
            // What it was asked to test, and what failed (the model's parts don't; a test's faults do).
            const uint8_t tested = static_cast<uint8_t>(p[0] & 0x0F);
            reply(tag, OK, {tested, static_cast<uint8_t>(tested & faults_)});
            break;
        }
        case FW_BEGIN:
        {
            const uint32_t len = get32(p);
            if (len < 32 || len > 0xFFFFFF)
            {
                reply(tag, E_PARAM);
                break;
            }
            reply(tag, open_transfer(cmd, tag, 0, len, image_));
            break;
        }
        case FW_COMMIT:
        {
            if (!image_.used || image_.loading || fw_pending_ms_)
            {
                reply(tag, image_.loading ? E_XFER : E_STATE);
                break;
            }
            // The reply at once; FWRESULT once the image is checked and written into the slot not running. The
            // write takes FLASH_MS: the slot is written and marked to boot only at its end, so a restart before
            // then (power-on, FWRESTART) drops the update, and the old image boots.
            Image img;
            fw_status_ = check_image(img);
            fw_major_ = img.major, fw_minor_ = img.minor;
            fw_new_ = img;
            fw_new_slot_ = static_cast<uint8_t>(1 - running_);
            free_object(image_);
            fw_pending_ms_ = FLASH_MS;
            reply(tag, OK);
            break;
        }
        case EFFECT_DEF:
        {
            const size_t n = rec[1];
            if (n < 2 || !parse_effect(p[0], p[1], p + 2, n - 2))
            {
                reply(tag, E_PARAM);
                break;
            }
            reply(tag, OK);
            break;
        }
        case EFFECT_LOAD:
        {
            const uint32_t len = get24(p + 1);
            if (len == 0)
            {
                reply(tag, E_PARAM);
                break;
            }
            reply(tag, open_transfer(cmd, tag, p[0], len, effects_));
            break;
        }
        case PLAY:
        {
            uint8_t handle = 0;
            const uint8_t status = play_effect(p, handle);
            reply(tag, status, status == OK ? std::vector<uint8_t>{handle} : std::vector<uint8_t>{});
            break;
        }
        case VOICE_STOP:
            for (unsigned v = 0; v < VOICES; ++v)
                if (voices_[v].use == V_EFFECT && (p[0] == 0 || voices_[v].handle == p[0]))
                    free_voice(v, false);
            reply(tag, OK);
            break;
        case BANK_FREE:
            if (p[0] > 2)
            {
                reply(tag, E_PARAM);
                break;
            }
            for (unsigned v = 0; v < VOICES; ++v)
                if (voices_[v].use == V_EFFECT)
                    free_voice(v, false);
            if (p[0] != 1)
                effects_def_.fill(EffectDef{});
            if (p[0] != 0)
            {
                for (unsigned s = 0; s < SAMPLE_SLOTS; ++s)
                    free_object(samples_[s]);
                sample_info_.fill(Sample{});
                wave_pages_.fill(false);
            }
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
        if (evt_room() < size) // never, as run_one() waits for the room and keeps it; a reply is never lost
            return;
        running_room_ = 0; // the reply has its room now: what the command raises after it is an event's to take
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
        size_t keep = running_room_;
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
        for (const auto &o : rings_)
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
        for (unsigned s = 0; s < STREAMS; ++s) // every stream flushed: stopped, its ring kept
            if (streams_[s].open)
            {
                Stream &st = streams_[s];
                if (xfer_.cmd == STREAM_DATA && xfer_.slot == s && (xfer_.open || xfer_.done))
                    abort_transfer();
                st.rd = st.wr;
                st.ended = st.need_out = st.underrun = st.paused = false;
                st.state = S_BUFFERING;
                reset_decoder(st);
            }
        tone_ = Tone{};
        boot_ = BootSound{};
        stop_music();
        for (unsigned v = 0; v < VOICES; ++v)
            if (voices_[v].use != V_FREE)
                free_voice(v, false);
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
        for (unsigned s = 0; s < STREAMS; ++s)
            close_stream(s);
        free_object(effects_);
        free_object(image_);
        for (auto &f : files_)
            f.reset();
        sample_info_.fill(Sample{});
        effects_def_.fill(EffectDef{});
        wave_pages_.fill(false);
        for (auto &b : blocks_)
            b.reset();
    }

    // --- transfers ----------------------------------------------------------------------------------------

    uint8_t SoundFw::check_open(uint32_t len, Object &o)
    {
        if (xfer_.open || xfer_.done)
            return E_XFER;
        const Object was = o; // a trial: the slot's own run counted free, as the load would free it
        o.used = false;
        const bool room = allocate(len).has_value();
        o = was;
        return room ? OK : E_MEMORY;
    }

    uint64_t SoundFw::chunk_kept() const
    {
        if (xfer_.cmd != STREAM_DATA || !(xfer_.open || xfer_.done))
            return 0;
        const Stream &st = streams_[xfer_.slot];
        return std::min(st.rd, st.wr) > st.chunk_start ? std::min(st.rd, st.wr) - st.chunk_start : 0;
    }

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
        if (xfer_.cmd == STREAM_DATA)
        {
            o.used = true, o.len = xfer_.len; // a record of the last chunk (CHECKSUM's), not a run of the store
            Stream &st = streams_[xfer_.slot];
            if (st.end_pending) // STREAM_END came while it was coming: the data ends here
                st.ended = true, st.end_pending = false;
        }
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
        if (cmd == SAMPLE_LOAD)
        {
            // As the ES5503 wants it: unsigned (bit 0: it came signed), and $00 made $01 (the chip's stop) unless
            // bit 1 keeps it as one.
            Sample &s = sample_info_[slot];
            for (size_t i = 0; i < o.len; ++i)
            {
                uint8_t &b = store_[o.at + i];
                if (s.flags & 1)
                    b = static_cast<uint8_t>(b ^ 0x80);
                if (b == 0 && !(s.flags & 2))
                    b = 1;
            }
            if (s.loop_end > o.len || s.loop_start >= std::max<uint32_t>(s.loop_end, 1))
                s.loop_start = s.loop_end = 0;
            s.ready = true;
        }
        if (cmd == MUSIC_LOAD)
        {
            const uint8_t status = parse_block(slot);
            if (status != OK)
            {
                free_object(o);
                payload[1] = status;
            }
            else
                put32(payload, static_cast<uint32_t>(blocks_[slot]->entries.size()));
        }
        if (cmd == EFFECT_LOAD)
        {
            // A run of definitions: each its length, then its kind and definition; numbered from the first.
            unsigned taken = 0;
            size_t at = 0;
            unsigned e = slot; // the last is effect 255: one past it is E_DATA
            while (at < o.len)
            {
                const size_t n = store_[o.at + at];
                if (e >= EFFECTS || n < 1 || at + 1 + n > o.len ||
                    !parse_effect(static_cast<uint8_t>(e), store_[o.at + at + 1], &store_[o.at + at + 2], n - 1))
                {
                    payload[1] = E_DATA;
                    break;
                }
                ++taken;
                ++e;
                at += 1 + n;
            }
            payload.push_back(static_cast<uint8_t>(taken));
        }
        event(EV_XFERDONE, tag, payload);
    }

    void SoundFw::abort_transfer()
    {
        if (xfer_.cmd == STREAM_DATA && (xfer_.open || xfer_.done))
        {
            // What of the chunk had come and not played is dropped; what played stays (the ring never runs back
            // past what was read). The host resends from the bytes kept (XFERABORT's count).
            Stream &st = streams_[xfer_.slot];
            st.wr = st.chunk_start + chunk_kept();
            if (st.end_pending)
                st.ended = true, st.end_pending = false;
        }
        if (xfer_.obj && (xfer_.open || xfer_.done))
            free_object(*xfer_.obj);
        xfer_ = Transfer{};
    }

    // --- streams --------------------------------------------------------------------------------------------


    size_t SoundFw::frame_bytes(const Stream &st) const
    {
        return (st.fmt & 1 ? 2u : 1u) * (st.fmt & 2 ? 2u : 1u);
    }

    uint32_t SoundFw::buffered_ms(const Stream &st) const
    {
        const uint64_t bytes = st.wr - st.rd;
        if (st.codec == 0)
            return st.rate ? static_cast<uint32_t>(bytes * 1000 / (st.rate * frame_bytes(st))) : 0;
        const uint32_t kbps = st.kbps ? st.kbps : 128; // by the first frames' bitrate, once known
        return static_cast<uint32_t>(bytes * 8 / kbps);
    }

    uint8_t SoundFw::ring_byte(unsigned s, uint64_t at) const
    {
        const Object &r = rings_[s];
        return store_[r.at + at % r.len];
    }

    void SoundFw::reset_decoder(Stream &st)
    {
        st.phase = 0;
        st.prev[0] = st.prev[1] = st.cur[0] = st.cur[1] = 0;
        if (st.codec == 1)
        {
            st.mp3 = std::make_shared<Mp3>();
            mp3dec_init(&st.mp3->dec);
        }
        else
            st.mp3.reset();
    }

    uint8_t SoundFw::open_stream(const uint8_t *p)
    {
        const unsigned s = p[0];
        const uint8_t codec = p[1], fmt = p[4];
        const uint32_t rate = get16(p + 2), chunk = get24(p + 5), prebuf = get16(p + 8);
        if (s >= STREAMS || codec > 1 || (fmt & ~3u) || (codec == 0 && (rate < 8000 || rate > 48000)))
            return E_PARAM;
        close_stream(s);
        Stream &st = streams_[s];
        st = Stream{};
        st.codec = codec;
        st.fmt = fmt;
        st.rate = codec == 0 ? rate : 0;
        st.chunk = chunk ? chunk : CHUNK_DEFAULT;
        st.prebuffer_ms = prebuf ? prebuf : PREBUFFER_DEFAULT;
        // The ring: as big as the store allows, half of what's free (the bank, tunes and the other stream
        // have the rest) and at most 4 MB, whole frames; at least a chunk.
        size_t len = std::min(store_free() / 2, RING_MOST);
        len -= len % 4;
        if (len < st.chunk)
            return E_MEMORY;
        const auto at = allocate(len);
        if (!at)
            return E_MEMORY;
        rings_[s].used = true;
        rings_[s].at = *at;
        rings_[s].len = len;
        st.open = true;
        st.state = S_BUFFERING;
        reset_decoder(st);
        return OK;
    }

    void SoundFw::close_stream(unsigned s)
    {
        if (xfer_.cmd == STREAM_DATA && xfer_.slot == s && (xfer_.open || xfer_.done))
            abort_transfer();
        for (auto &o : streams_)
            if (o.next == static_cast<int>(s))
                o.next = -1;
        streams_[s] = Stream{};
        free_object(rings_[s]);
    }

    bool SoundFw::decode_mp3(unsigned s)
    {
        // The next frame of the ring's data decoded into the stream's queue, by revision 2's rules: a frame in step
        // with the stream is decoded once its end and the next header have come (or the data has ended); one out of
        // step only once minimp3's own sync rule has decided on it (a run of frames in step, or the data's end); an
        // ID3v2 tag is skipped by its size. Until a decision can be made it waits for more: false. True when the
        // queue gained frames or the header was taken; the data's end sets end_decoded.
        Stream &st = streams_[s];
        Mp3 &m = *st.mp3;
        const bool eof = st.ended;
        const auto at = [&](size_t i) { return ring_byte(s, st.rd + i); };
        const auto head = [&](size_t i, uint8_t *g, size_t n) {
            for (size_t k = 0; k < n; ++k)
                g[k] = at(i + k);
        };
        const auto drop = [&](size_t n, bool junk, bool lost = true) {
            st.rd += n;
            m.looked = 0;
            if (junk)
                m.junk += n;
            if (junk && lost)
                m.fresh = true;
        };
        const auto no_sync = [&] {
            if (m.junk < Mp3::NO_SYNC && m.tagged < Mp3::TAG_MAX)
                return false;
            st.state = S_NO_SYNC;
            return true;
        };
        const auto tag_at = [&](size_t i, size_t have) -> size_t {
            if (i + 10 > have || at(i) != 'I')
                return 0;
            uint8_t g[10];
            head(i, g, 10);
            const size_t tg = id3_bytes(g);
            return tg <= Mp3::TAG_MAX ? tg : 0;
        };
        const auto frame_at = [&](size_t i, size_t have) -> size_t {
            if (i + 4 > have || at(i) != 0xFF)
                return 0;
            uint8_t g[4];
            head(i, g, 4);
            return mp3_frame_bytes(g);
        };
        for (;;)
        {
            const size_t have = static_cast<size_t>(st.wr - st.rd);
            if (m.tag_left)
            {
                const size_t d = std::min(m.tag_left, have);
                drop(d, false);
                m.tag_left -= d;
                if (m.tag_left)
                    return false;
                continue;
            }
            if (have == 0)
            {
                if (eof)
                    m.end_decoded = true; // every frame decoded
                return eof;
            }
            if (const size_t tg = tag_at(0, have))
            {
                m.tag_left = tg;
                m.tagged += tg;
                if (no_sync())
                    return false;
                continue;
            }
            if (have < 10 && !eof && at(0) == 'I')
                return false;
            uint8_t h[4] = {};
            head(0, h, std::min<size_t>(4, have));
            const size_t fb = frame_at(0, have);
            size_t n = 0;
            bool search = true;
            if (fb && !m.fresh && mp3_same(m.dec.header, h))
            {
                if (have < fb)
                {
                    if (!eof)
                        return false; // the frame's end still to come
                    drop(have, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                uint8_t next[4] = {};
                if (have >= fb + 4)
                    head(fb, next, 4);
                if (have >= fb + 4 && mp3_frame_bytes(next) && mp3_same(h, next))
                {
                    n = fb + 4;
                    search = false;
                }
                else
                {
                    const size_t end = std::min(have, fb + Mp3::WINDOW);
                    size_t i = fb + m.looked;
                    bool like = false, tag = false;
                    for (; i < end && !like && !tag; ++i)
                    {
                        if (i + 10 > have && !eof && have < fb + Mp3::WINDOW)
                            break;
                        tag = tag_at(i, have) != 0;
                        like = !tag && frame_at(i, have) != 0;
                    }
                    if (tag)
                    {
                        n = fb;
                        search = false;
                    }
                    else if (!like)
                    {
                        m.looked = i - fb;
                        if (!eof && have < fb + Mp3::WINDOW)
                            return false;
                        n = fb;
                        search = false;
                    }
                }
            }
            if (search)
            {
                size_t p = 0;
                for (; p < have; ++p)
                {
                    if (frame_at(p, have) || tag_at(p, have))
                        break;
                    if (!eof && (p + 10 > have) && at(p) == 'I')
                        break;
                    if (!eof && p + 4 > have && at(p) == 0xFF)
                        break;
                }
                if (p)
                {
                    drop(p, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                if (!frame_at(0, have))
                {
                    if (!eof)
                        return false;
                    drop(have, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                size_t i = fb;
                unsigned matched = 0;
                bool decided = false, take = false;
                if (fb > have)
                {
                    if (!eof)
                        return false;
                    decided = true;
                }
                else if (fb == have && eof)
                    decided = take = true;
                while (!decided)
                {
                    if (i + 10 > have && !eof && i < have && at(i) == 'I')
                        return false;
                    if (tag_at(i, have))
                    {
                        decided = take = true;
                        break;
                    }
                    if (i + 4 > have)
                    {
                        if (!eof)
                            return false;
                        decided = true;
                        take = matched > 0;
                        break;
                    }
                    uint8_t g[4];
                    head(i, g, 4);
                    const size_t gb = mp3_frame_bytes(g);
                    if (!gb || !mp3_same(h, g))
                    {
                        decided = true;
                        break;
                    }
                    if (++matched == Mp3::MATCHES)
                    {
                        decided = take = true;
                        i += 4;
                        break;
                    }
                    i += gb;
                }
                if (!take)
                {
                    drop(1, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                n = std::min({have, i, Mp3::WINDOW});
                mp3dec_init(&m.dec);
            }
            for (size_t k = 0; k < n; ++k)
                m.window[k] = at(k);
            if (!m.header_done && fb && fb <= n)
            {
                // The first frame: an encoder's gapless header (a Xing or Info frame with a LAME tag after it)
                // carries the delay and padding it added; that frame is the header, not audio (the spec's Q11).
                m.header_done = true;
                const uint8_t *xing = nullptr;
                for (size_t i = 0; i + 4 <= std::min<size_t>(fb, 64); ++i)
                    if (!std::memcmp(m.window.data() + i, "Xing", 4) || !std::memcmp(m.window.data() + i, "Info", 4))
                    {
                        xing = m.window.data() + i;
                        break;
                    }
                if (xing && static_cast<size_t>(xing - m.window.data()) + 120 + 24 <= fb && !std::memcmp(xing + 120, "LAME", 4))
                {
                    const uint8_t *d = xing + 120 + 21;
                    const uint32_t delay = static_cast<uint32_t>(d[0] << 4 | d[1] >> 4);
                    const uint32_t padding = static_cast<uint32_t>((d[1] & 0x0F) << 8 | d[2]);
                    m.skip = delay + 529; // and the decoder's own delay, as decoders keeping it do
                    m.tail = padding > 529 ? padding - 529 : 0;
                    std::memcpy(m.dec.header, m.window.data(), 4); // in step with the stream from here
                    drop(fb, false);
                    m.fresh = false;
                    return true;
                }
            }
            std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
            mp3dec_frame_info_t info{};
            const int got = mp3dec_decode_frame(&m.dec, m.window.data(), static_cast<int>(n), pcm.data(), &info);
            if (info.hz)
            {
                const size_t frame = static_cast<size_t>(info.frame_bytes - info.frame_offset);
                if (info.frame_offset) // a backstop: the rules above start every window at a frame minimp3 takes
                {
                    drop(1, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                if (info.layer != 3 || info.bitrate_kbps == 0)
                {
                    drop(frame, true);
                    mp3dec_init(&m.dec);
                    if (no_sync())
                        return false;
                    continue;
                }
                if (got <= 0)
                {
                    drop(frame, true, false);
                    m.fresh = false;
                    if (no_sync())
                        return false;
                    continue;
                }
                if (info.hz > 48000)
                {
                    st.state = S_BAD_FORMAT;
                    return false;
                }
                drop(frame, false);
                m.junk = m.tagged = 0;
                m.fresh = false;
                m.hz = static_cast<uint32_t>(info.hz);
                st.kbps = static_cast<uint16_t>(info.bitrate_kbps);
                for (int i = 0; i < got; ++i)
                {
                    if (m.skip)
                    {
                        --m.skip;
                        continue;
                    }
                    const int16_t l = pcm[static_cast<size_t>(i * info.channels)];
                    m.pcm.push_back(l);
                    m.pcm.push_back(info.channels == 2 ? pcm[static_cast<size_t>(i * 2 + 1)] : l);
                }
                return true;
            }
            drop(1, true); // a backstop, as above: a byte, junk, and on
            if (no_sync())
                return false;
        }
    }

    bool SoundFw::next_frame(unsigned &s, int16_t out[2])
    {
        for (int guard = 0; guard < 4; ++guard)
        {
            Stream &st = streams_[s];
            bool exhausted = false;
            if (st.codec == 0)
            {
                const size_t fb = frame_bytes(st);
                if (st.wr - st.rd >= fb)
                {
                    const auto sample = [&](uint64_t i) -> int16_t {
                        if (st.fmt & 2)
                            return static_cast<int16_t>(static_cast<uint16_t>(ring_byte(s, i) | ring_byte(s, i + 1) << 8));
                        return static_cast<int16_t>((ring_byte(s, i) - 128) * 256); // unsigned, as revision 2
                    };
                    const uint64_t step = st.fmt & 2 ? 2 : 1;
                    out[0] = sample(st.rd);
                    out[1] = st.fmt & 1 ? sample(st.rd + step) : out[0];
                    st.rd += fb;
                    return true;
                }
                exhausted = st.ended;
            }
            else
            {
                Mp3 &m = *st.mp3;
                while (m.pcm.size() / 2 <= m.tail && !m.end_decoded && st.state < 0x80)
                    if (!decode_mp3(s))
                        break;
                if (m.pcm.size() / 2 > m.tail || (m.end_decoded && m.pcm.size() / 2 > m.tail))
                {
                    out[0] = m.pcm.front();
                    m.pcm.pop_front();
                    out[1] = m.pcm.front();
                    m.pcm.pop_front();
                    return true;
                }
                exhausted = m.end_decoded;
                if (st.state >= 0x80)
                    return false;
            }
            if (!exhausted)
                return false;
            // The data's end: the next stream's first frame follows with no gap, or the stream has ended.
            if (st.next >= 0 && streams_[static_cast<unsigned>(st.next)].open)
            {
                const unsigned n = static_cast<unsigned>(st.next);
                Stream &nx = streams_[n];
                nx.queued = false;
                nx.state = S_PLAYING;
                nx.phase = st.phase;
                nx.prev[0] = st.prev[0], nx.prev[1] = st.prev[1];
                nx.cur[0] = st.cur[0], nx.cur[1] = st.cur[1];
                const unsigned was = s;
                close_stream(was);
                event(EV_TRACK, static_cast<uint8_t>(n), {static_cast<uint8_t>(was)});
                s = n;
                continue;
            }
            st.state = S_ENDED;
            std::vector<uint8_t> v;
            put32(v, static_cast<uint32_t>(st.frames));
            event(EV_STREAM_ENDED, static_cast<uint8_t>(s), v);
            return false;
        }
        return false;
    }

    unsigned SoundFw::stream_frame(unsigned s, float out[2])
    {
        Stream *st = &streams_[s];
        if (!st->open || st->queued || st->paused)
            return s;
        if (st->state == S_BUFFERING && (buffered_ms(*st) >= st->prebuffer_ms || st->ended))
            st->state = S_PLAYING;
        if (st->state != S_PLAYING && st->state != S_UNDERRUN)
            return s;
        const uint32_t rate = st->codec == 0 ? st->rate : (st->mp3->hz ? st->mp3->hz : 0);
        st->phase += rate ? rate : RATE;
        while (st->phase >= RATE)
        {
            st->phase -= RATE;
            int16_t f[2];
            unsigned now = s;
            if (next_frame(now, f))
            {
                st = &streams_[now];
                if (st->state == S_UNDERRUN || st->underrun)
                {
                    st->underrun = false;
                    st->state = S_PLAYING;
                    std::vector<uint8_t> v;
                    put32(v, static_cast<uint32_t>(st->frames));
                    put32(v, static_cast<uint32_t>(st->missed_now)); // this underrun's
                    st->missed_now = 0;
                    event(EV_UNDERRUN_END, static_cast<uint8_t>(now), v);
                }
                st->prev[0] = st->cur[0], st->prev[1] = st->cur[1];
                st->cur[0] = f[0], st->cur[1] = f[1];
                ++st->frames;
                if (stream_tap_on_)
                    stream_tap_.insert(stream_tap_.end(), f, f + 2);
                s = now;
                continue;
            }
            st = &streams_[now];
            s = now;
            if (!st->open || st->state == S_ENDED || st->state >= 0x80)
                return s;
            // No data and no end: silence, the frame due counted as missed; sound comes back at the next due.
            ++st->missed_now;
            ++st->missed;
            st->prev[0] = st->prev[1] = st->cur[0] = st->cur[1] = 0;
            if (!st->underrun)
            {
                st->underrun = true;
                st->state = S_UNDERRUN;
                ++st->underruns;
                std::vector<uint8_t> v;
                put32(v, static_cast<uint32_t>(st->frames));
                put16(v, st->underruns);
                event(EV_UNDERRUN, static_cast<uint8_t>(s), v);
            }
        }
        const float frac = static_cast<float>(st->phase) / static_cast<float>(RATE);
        for (unsigned ch = 0; ch < 2; ++ch)
            out[ch] += (st->prev[ch] + (st->cur[ch] - st->prev[ch]) * frac) / 32768.0f;
        return s;
    }

    void SoundFw::streams_ms()
    {
        // NEED: room for a whole chunk and none outstanding.
        for (unsigned s = 0; s < STREAMS; ++s)
        {
            Stream &st = streams_[s];
            if (!st.open || st.ended || st.end_pending || st.need_out || st.state >= 0x80 || st.state == S_ENDED)
                continue;
            if (xfer_.cmd == STREAM_DATA && xfer_.slot == s && (xfer_.open || xfer_.done))
                continue; // its chunk still coming: the room it will take isn't room

            const uint64_t room = rings_[s].len - (st.wr - st.rd);
            if (room < st.chunk)
                continue;
            st.need_out = true;
            const uint32_t ms_in = buffered_ms(st);
            std::vector<uint8_t> v;
            put32(v, static_cast<uint32_t>(room));
            put32(v, ms_in);
            v.push_back(ms_in < URGENT_MS ? 1 : 0);
            event(EV_NEED, static_cast<uint8_t>(s), v);
        }
    }
} // namespace ooe::emu
