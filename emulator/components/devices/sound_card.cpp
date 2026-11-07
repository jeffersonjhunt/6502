/**
 * @file sound_card.cpp
 * @brief The backplane's sound card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_card.h"

#include "minimp3.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace ooe::emu
{
    // --- MP3 (CODEC 1) ----------------------------------------------------------------

    struct SoundCard::Mp3
    {
        /// What a search for a frame is given: ten frames of the largest kind, as minimp3 checks a sync by.
        static constexpr size_t WINDOW = 32'768;
        static constexpr size_t NO_SYNC = 65'536;
        /// minimp3 takes a sync once this many frames follow in step (or the data runs out after one).
        static constexpr unsigned MATCHES = 10;
        /// An ID3v2 tag declaring more than this isn't taken for one: its bytes are junk. And tags adding up to
        /// this with no frame between are $81.
        static constexpr size_t TAG_MAX = 8u << 20;

        mp3dec_t dec{};
        std::vector<uint8_t> window = std::vector<uint8_t>(WINDOW);
        std::array<int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm{};
        size_t samples{0}, at{0}; ///< the frame's samples per channel, and the next one taken
        unsigned channels{1};
        uint32_t hz{0};
        unsigned kbps{0};
        size_t junk{0};     ///< bytes since the last frame that made samples, tags left out
        size_t tagged{0};   ///< tags' bytes since the last frame that made samples
        size_t tag_left{0}; ///< of an ID3v2 tag being skipped
        bool fresh{false};  ///< junk passed over since the last frame: the next is found by a search, afresh
        /// How far past the frame at the FIFO's front a look for anything frame-like has gone, finding none.
        size_t looked{0};

        void restart()
        {
            mp3dec_init(&dec);
            samples = at = 0;
            junk = tagged = tag_left = 0;
            fresh = false;
            looked = 0;
        }
    };

    namespace
    {
        /// A layer III header's frame length in bytes, padding included: 0 if `h` isn't one: another layer,
        /// free format (refused: its length isn't in its header), or a bad bitrate or rate.
        size_t mp3_frame_bytes(const uint8_t *h)
        {
            if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0 || (h[1] & 0x06) != 0x02)
                return 0;
            const unsigned version = (h[1] >> 3) & 3, rate = (h[2] >> 2) & 3, index = h[2] >> 4; // 3 MPEG-1, 2 -2, 0 -2.5
            if (version == 1 || rate == 3 || index == 0 || index == 15)
                return 0;
            static const uint16_t kbps[2][15] = {
                {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},       // MPEG-2 and 2.5
                {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}}; // MPEG-1
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
            // Versions 2.2 to 2.4, a revision, and only the flags each version defines (the footer 2.4's alone).
            static const uint8_t flags[5] = {0, 0, 0xC0, 0xE0, 0xF0};
            if (h[0] != 'I' || h[1] != 'D' || h[2] != '3' || h[3] < 2 || h[3] > 4 || h[4] == 0xFF ||
                (h[5] & ~flags[h[3]]))
                return 0;
            size_t size = 0;
            for (unsigned i = 6; i < 10; ++i)
            {
                if (h[i] & 0x80)
                    return 0; // not syncsafe: not a tag
                size = size << 7 | h[i];
            }
            return 10 + size + (h[5] & 0x10 ? 10 : 0);
        }
    } // namespace

    SoundCard::SoundCard() : mp3_(std::make_unique<Mp3>()), cpu_(bus_, Cpu6502::Model::Nmos6502) { reset(); }

    SoundCard::~SoundCard() = default;

    bool SoundCard::mp3_next()
    {
        Mp3 &m = *mp3_;
        if (m.at < m.samples)
            return true;
        const bool eof = pcmctl_ & PCMCTL_EOF;
        const auto at = [&](size_t i) { return (*fifo_)[(fifo_head_ + i) % FIFO_BYTES]; };
        const auto head = [&](size_t i, uint8_t *g, size_t n) {
            for (size_t k = 0; k < n; ++k)
                g[k] = at(i + k);
        };
        // `junk`: counted toward $81; `lost`: the stream's thread lost there, so the next frame is searched for
        // afresh, as minimp3 does after bytes that aren't frames (a frame that made nothing is junk, not lost).
        const auto drop = [&](size_t n, bool junk, bool lost = true) {
            fifo_drop(n);
            m.looked = 0;
            if (junk)
                m.junk += n;
            if (junk && lost)
                m.fresh = true;
        };
        const auto no_sync = [&] {
            if (m.junk < Mp3::NO_SYNC && m.tagged < Mp3::TAG_MAX)
                return false;
            state_ = STATE_NO_SYNC;
            bus_on_ = false;
            return true;
        };
        /// A valid ID3v2 header at `i` (10 bytes there): its whole length; 0 if none.
        const auto tag_at = [&](size_t i, size_t have) -> size_t {
            if (i + 10 > have || at(i) != 'I')
                return 0;
            uint8_t g[10];
            head(i, g, 10);
            const size_t t = id3_bytes(g);
            return t <= Mp3::TAG_MAX ? t : 0;
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
            const size_t have = fifo_count_;
            if (m.tag_left) // an ID3v2 tag, skipped by its own size, not junk
            {
                const size_t d = std::min(m.tag_left, have);
                drop(d, false);
                m.tag_left -= d;
                if (m.tag_left)
                    return false;
                continue;
            }
            if (have == 0)
                return false;
            if (const size_t t = tag_at(0, have))
            {
                m.tag_left = t;
                m.tagged += t;
                if (no_sync())
                    return false;
                continue;
            }
            if (have < 10 && !eof && at(0) == 'I')
                return false; // a tag's header, perhaps, still coming in
            uint8_t h[4] = {};
            head(0, h, std::min<size_t>(4, have));
            const size_t fb = frame_at(0, have);
            size_t n = 0;          // the bytes the decoder is given, if it is called
            bool search = true;    // out of step: the frame at the front must win minimp3's sync first
            if (fb && !m.fresh && mp3_same(m.dec.header, h))
            {
                // In step with the stream. If the next header follows, the decoder takes this frame as it is,
                // given just that, as a decoder with the whole file would see it.
                if (have < fb)
                {
                    if (!eof)
                        return false; // the frame's end still to come
                    drop(have, true);  // at EOF it can't be finished: junk
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
                    // Something else follows. A tag (before anything frame-like): this frame by its own length,
                    // then the tag skipped. Anything frame-like (another stream's header, at a change of rate):
                    // the search minimp3 makes there, which drops this frame. Neither, to EOF or for a whole
                    // window: this frame by its own length, what follows ignored (CP/M's $1A padding, say).
                    // Neither yet, without EOF: wait for more, so EOF's timing doesn't matter.
                    const size_t end = std::min(have, fb + Mp3::WINDOW);
                    size_t i = fb + m.looked;
                    bool like = false, tag = false;
                    for (; i < end && !like && !tag; ++i)
                    {
                        if (i + 10 > have && !eof && have < fb + Mp3::WINDOW)
                            break; // a tag's header could start here, its rest still to come
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
                // Out of step: pass over everything to the next layer III header we can size, or a tag, as junk.
                // Other layers' frames and free format's are junk too.
                size_t p = 0;
                for (; p < have; ++p)
                {
                    if (frame_at(p, have) || tag_at(p, have))
                        break;
                    if (!eof && (p + 10 > have) && at(p) == 'I')
                        break; // a tag's header, perhaps: wait for its rest
                    if (!eof && p + 4 > have && at(p) == 0xFF)
                        break; // a frame's header, perhaps
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
                        return false; // the start of a header or tag: its rest to come
                    drop(have, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                // minimp3's own sync rule, on the FIFO: the frame, then a run of frames in step with it, MATCHES
                // of them, or one at least when the data runs out (EOF); a header out of step in the run, or
                // none at the end of the data, and it isn't one. Until the run decides, wait for more.
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
                    decided = take = true; // the whole of what's left is one frame
                while (!decided)
                {
                    if (i + 10 > have && !eof && i < have && at(i) == 'I')
                        return false; // a tag's header, perhaps, ending the run: wait for its rest
                    if (tag_at(i, have))
                    {
                        // A tag ends the run as the data's end would: a short clip before a tag plays, a single
                        // frame before one too.
                        decided = true;
                        take = true;
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
                    drop(1, true); // not where minimp3 syncs: on from the next byte
                    if (no_sync())
                        return false;
                    continue;
                }
                n = std::min({have, i, Mp3::WINDOW}); // what its sync check reads
                mp3dec_init(&m.dec); // and from a fresh start, as minimp3 after junk
            }
            for (size_t k = 0; k < n; ++k)
                m.window[k] = at(k);
            mp3dec_frame_info_t info{};
            const int got = mp3dec_decode_frame(&m.dec, m.window.data(), static_cast<int>(n), m.pcm.data(), &info);
            if (info.hz) // a frame
            {
                const size_t frame = static_cast<size_t>(info.frame_bytes - info.frame_offset);
                if (info.frame_offset)
                {
                    // Unreachable while the card's sync replica matches minimp3: every window starts at a frame
                    // minimp3 takes at offset 0. Kept as a backstop (a minimp3 update, say): a byte, junk, and on.
                    assert(!"minimp3 found its frame past the window's start");
                    drop(1, true);
                    if (no_sync())
                        return false;
                    continue;
                }
                if (info.layer != 3 || info.bitrate_kbps == 0)
                {
                    drop(frame, true); // another layer's, or free format's: junk
                    mp3dec_init(&m.dec);
                    if (no_sync())
                        return false;
                    continue;
                }
                if (got <= 0)
                {
                    drop(frame, true, false); // a frame that made nothing counts as junk: a stream of them is $81
                    m.fresh = false;          // but the decoder is in the stream's thread now
                    if (no_sync())
                        return false;
                    continue;
                }
                drop(frame, false);
                m.junk = m.tagged = 0;
                m.fresh = false;
                m.samples = static_cast<size_t>(got);
                m.at = 0;
                m.channels = static_cast<unsigned>(info.channels);
                m.kbps = static_cast<unsigned>(info.bitrate_kbps);
                m.hz = static_cast<uint32_t>(info.hz);
                play_fmt_ = static_cast<uint8_t>(PCMFMT_16BIT | (m.channels == 2 ? PCMFMT_STEREO : 0));
                return true;
            }
            // No frame, though one was expected. Unreachable while the card's sync replica matches minimp3 (the
            // window starts at a frame it takes). Kept as a backstop, so a minimp3 that judges otherwise can't
            // stall the stream: a byte, junk, and on.
            assert(!"minimp3 found no frame where the card's replica found one");
            drop(1, true);
            if (no_sync())
                return false;
        }
    }

    size_t SoundCard::mp3_ready() const { return mp3_->samples - mp3_->at; }

    void SoundCard::mp3_rate(uint32_t hz)
    {
        if (hz == play_rate_)
            return;
        if (play_rate_)
            pcm_phase_ = pcm_phase_ * hz / play_rate_; // the part of a frame owed, at the new rate
        play_rate_ = hz;
        bus_on_ = false; // the mix's lead and resampling for the new rate
    }

    void SoundCard::reset()
    {
        mp3_->restart();
        mp3_->hz = 0;
        mp3_->kbps = 0;
        play_codec_ = 0;
        catch_up();
        chips_[0].reset();
        chips_[1].reset();
        written_ = {};
        sids_at_ = clock_;
        ctrl_ = ien_ = 0;
        ram_addr_ = 0;
        to_card_ = to_host_ = 0;
        to_card_full_ = to_host_full_ = false;
        ddr_ = port_ = 0;
        cia_reset();
        phase_ = 0;
        owed_ = 0;
        tune_clocks();
        reset_rev2();
    }

    void SoundCard::reset_rev2()
    {
        pcmctl_ = codec_ = pcmfmt_ = 0;
        rate_ = 0;
        fifo_head_ = fifo_count_ = 0;
        state_ = STATE_IDLE;
        resume_ = STATE_BUFFERING;
        play_rate_ = 0;
        play_fmt_ = 0;
        pcm_phase_ = 0;
        underruns_ = 0;
        tap_.clear();
        pcm_mix_.clear();
        bus_on_ = false;
        mix_skip_ = 0;
        mix_offset_ = 0;
        mix_frames_ = 0;
        mix_dry_ = 0;
        free_hi_ = static_cast<uint8_t>(free_units() >> 8);
        pos_ = pos_latch_ = 0;
        vol_ = {0xFF, 0xFF};
        eqidx_ = 0;
        eq_gain_.fill(0);
        eq_on_ = false;
        eq_.clear();
        eq_dirty_ = true;
        eq_rate_ = 0;
        vis_bus_.clear();
        vis_phase_ = 0;
        visidx_ = 0;
        vis_snapshot();
        sid_volume(); // back to unity
        docctl_ = 0;
        doc_addr_ = 0;
        drqsel_ = DRQ_RAMDATA;
        doc_.reset(); // its wave RAM stays, as the card's own does
    }

    void SoundCard::set_revision(unsigned rev)
    {
        rev_ = rev == 1 ? 1 : 2;
        reset_rev2();
    }

    void SoundCard::vis_snapshot() { vis_bus_.snapshot(vis_); }

    void SoundCard::reset_cpu()
    {
        ++cpu_resets_;
        ddr_ = port_ = 0;
        cia_reset();
        cpu_.reset();
        owed_ = 0;
    }

    // --- time ---------------------------------------------------------------

    void SoundCard::tick(uint64_t cycles)
    {
        if (state_ == STATE_PLAYING || state_ == STATE_UNDERRUN)
        {
            pcm_advance(cycles); // the stream keeps the card's time, whether or not sound is made
            vis_phase_ = pcm_phase_; // if it stops, the vis tap's silence goes on from the same place
        }
        else
        {
            bus_on_ = false; // time passes with the bus stopped: its next run anchors afresh
            vis_quiet(cycles);
        }
        // SID clocks = host cycles x SID Hz / host Hz, the remainder carried. In whole seconds of the host's
        // clock and the rest, so a single tick of years doesn't overflow (cycles x SID Hz would past 29 days).
        phase_ += cycles % host_hz_ * sid_hz();
        const uint64_t due = cycles / host_hz_ * sid_hz() + phase_ / host_hz_;
        phase_ %= host_hz_;
        if (!(ctrl_ & CTRL_RUN))
        {
            clock_ += due; // the SIDs' clock runs on; they catch up when next touched
            return;
        }
        owed_ += static_cast<int64_t>(due);
        while (owed_ > 0)
        {
            step_start_ = cpu_.cycles();
            in_step_ = true;
            cia_tick(cpu_.step());
            if (cia_irq() || vic_irq())
            {
                const uint64_t entry = cpu_.cycles();
                cpu_.irq(); // ignored while I is set; level-sensitive, so it comes back
                cia_tick(static_cast<unsigned>(cpu_.cycles() - entry)); // its 7 cycles are SID clocks too
            }
            in_step_ = false;
            const uint64_t n = cpu_.cycles() - step_start_;
            clock_ += n;
            owed_ -= static_cast<int64_t>(n);
            if (!(ctrl_ & CTRL_RUN)) // it stopped itself ($DE02): the SIDs' clock runs on
            {
                if (owed_ > 0)
                    clock_ += static_cast<uint64_t>(owed_);
                owed_ = 0;
            }
        }
    }

    // --- the PCM stream ----------------------------------------------------------

    void SoundCard::pcm_write(uint8_t data)
    {
        if (fifo_count_ == FIFO_BYTES)
            return; // full: dropped
        (*fifo_)[(fifo_head_ + fifo_count_) % FIFO_BYTES] = data;
        ++fifo_count_;
        pcm_update();
    }

    void SoundCard::pcm_control(uint8_t data)
    {
        const uint8_t was = pcmctl_;
        if (data & PCMCTL_FLUSH)
        {
            fifo_head_ = fifo_count_ = 0; // the FIFO, and a partial frame with it
            mp3_->restart();              // and the decoder
            eq_.clear();                  // and the EQ's filters
        }
        pcmctl_ = static_cast<uint8_t>(data & (PCMCTL_RUN | PCMCTL_PAUSE | PCMCTL_EOF));
        if ((data & PCMCTL_FLUSH) && (pcmctl_ & was & PCMCTL_RUN) && state_ != STATE_BAD_FORMAT &&
            state_ != STATE_NO_SYNC)
        {
            // A seek: the stream buffers again, to 4 KB or EOF, and no underrun is counted for it. An MP3's
            // first frame after it is found as at the start, its rate with it.
            if (mp3())
                play_rate_ = 0;
            if (state_ == STATE_PAUSED)
                resume_ = STATE_BUFFERING;
            else
                state_ = STATE_BUFFERING;
        }
        if ((pcmctl_ & PCMCTL_RUN) && !(was & PCMCTL_RUN))
        {
            resume_ = STATE_BUFFERING;
            pcm_phase_ = 0;
            eq_.clear(); // a new stream: the EQ's filters from silence
            play_codec_ = codec_;
            mp3_->restart();
            mp3_->hz = 0;
            mp3_->kbps = 0;
            if (codec_ == 1) // MP3: the rate and the format come with each frame
            {
                play_rate_ = 0;
                play_fmt_ = PCMFMT_16BIT;
                state_ = STATE_BUFFERING;
            }
            else if (codec_ != 0 || rate_ < RATE_MIN || rate_ > RATE_MAX)
                state_ = STATE_BAD_FORMAT;
            else
            {
                play_rate_ = rate_;
                play_fmt_ = pcmfmt_;
                state_ = STATE_BUFFERING;
            }
        }
        else if (!(pcmctl_ & PCMCTL_RUN))
        {
            state_ = STATE_IDLE; // the FIFO kept, unless FLUSH was written too
            vis_bus_.clear();    // the vis tap's frames and peaks: the next snapshot silence, the page taken kept
        }
        pcm_update(); // last: a RUN, a seek or a pause leaves the state stopped first, so this re-anchors the mix
    }

    void SoundCard::pcm_update()
    {
        const uint8_t before = state_;
        pcm_step();
        if (!bus_runs(before) || !bus_runs(state_))
            bus_on_ = false; // started (from buffering or paused) or stopped: anchor afresh
    }

    void SoundCard::pcm_step()
    {
        if (!(pcmctl_ & PCMCTL_RUN) || state_ == STATE_BAD_FORMAT || state_ == STATE_NO_SYNC || state_ == STATE_ENDED)
            return;
        if (pcmctl_ & PCMCTL_PAUSE)
        {
            if (state_ != STATE_PAUSED)
                resume_ = state_; // buffering, playing or underrun: where PAUSE's release goes back to
            state_ = STATE_PAUSED;
            return;
        }
        if (state_ == STATE_PAUSED)
            state_ = resume_; // an underrun paused is the same dry spell, not a new one
        if (state_ == STATE_BUFFERING && (fifo_count_ >= START_BYTES || (pcmctl_ & PCMCTL_EOF)))
        {
            if (mp3() && !play_rate_)
            {
                // MP3 buffers on until its first frame has decoded (its sync run decided): it gives the rate, and
                // nothing has played yet, so the wait is buffering, not an underrun. At EOF with none, the end.
                if (!mp3_next())
                {
                    if (state_ == STATE_NO_SYNC)
                        return;
                    if (pcmctl_ & PCMCTL_EOF)
                    {
                        state_ = STATE_ENDED;
                        fifo_count_ = 0;
                    }
                    return;
                }
                if (mp3_->hz > RATE_MAX)
                {
                    state_ = STATE_BAD_FORMAT;
                    return;
                }
                play_rate_ = mp3_->hz;
            }
            state_ = STATE_PLAYING;
            pcm_phase_ = 0; // the first frame a whole frame from now
        }
        if (state_ == STATE_UNDERRUN && !mp3() && fifo_count_ >= frame_bytes())
            state_ = STATE_PLAYING; // data again: on at once, from the next frame (MP3's when a frame decodes)
        // Running dry is found when a frame falls due (pcm_advance), not here: a FIFO emptied and refilled
        // between two frames hasn't underrun.
    }

    void SoundCard::pcm_advance(uint64_t cycles)
    {
        // In bulk, however long the tick (a backgrounded tab's): the frames due are taken from the FIFO in one
        // step and the rest is dry or the end, so the loop goes round at most twice and, with the tap off, makes
        // no sample of the mix (each frame still goes through the EQ and the vis tap). The frames due are counted
        // in whole seconds of the host's clock and the rest, so no tick overflows them.
        if (mp3() && !play_rate_)
        {
            // Unreachable: an MP3 leaves buffering only with its first frame decoded and its rate set (pcm_step),
            // and whatever clears the rate puts it back to buffering. Kept as a backstop: back to buffering, which
            // finds the first frame as at the start, no underrun counted.
            assert(!"an MP3 playing without its first frame");
            state_ = STATE_BUFFERING;
            bus_on_ = false;
            return;
        }
        pcm_phase_ += cycles % host_hz_ * play_rate_;
        uint64_t due = cycles / host_hz_ * play_rate_ + pcm_phase_ / host_hz_;
        pcm_phase_ %= host_hz_;
        if (mixing() && !bus_on_)
            mix_anchor();
        const bool bulk = mixing() && due > 2ull * play_rate_;
        if (bulk)
        {
            // As the SIDs' backlog: of more than two seconds, only the last second is made into the mix. The
            // SIDs up to now first, so the queue lines up with their next sample; then the frames skipped, and
            // what was queued, become an offset: that many SID samples pass before the last second's frames.
            catch_up();
            mix_skip_ = due - play_rate_;
            mix_offset_ += pcm_mix_.size() / 2 + mix_skip_ * audio_rate_ / play_rate_;
            pcm_mix_.clear();
            mix_prev_[0] = mix_prev_[1] = 0;
            mix_acc_ = 0;
        }
        while (due)
        {
            size_t ready = 0;
            if (!mp3())
                ready = fifo_count_ / frame_bytes();
            else if (mp3_next())
            {
                if (mp3_->hz > RATE_MAX)
                {
                    state_ = STATE_BAD_FORMAT; // MPEG-1 and -2 stop at 48 kHz: not one to expect
                    bus_on_ = false;
                    break;
                }
                mp3_rate(mp3_->hz); // the rest of this tick's frames at the old rate: the next tick's at the new
                ready = mp3_ready();
            }
            else if (state_ == STATE_NO_SYNC)
                break;
            if (ready)
            {
                const size_t n = static_cast<size_t>(std::min<uint64_t>(due, ready));
                state_ = STATE_PLAYING;
                pcm_play(n);
                due -= n;
                continue;
            }
            if (pcmctl_ & PCMCTL_EOF)
            {
                state_ = STATE_ENDED;
                // A partial frame left is dropped, never played; of an MP3, whatever couldn't be decoded.
                fifo_count_ = mp3() ? 0 : fifo_count_ - fifo_count_ % frame_bytes();
                bus_on_ = false;
                break;
            }
            if (state_ != STATE_UNDERRUN)
            {
                state_ = STATE_UNDERRUN;
                ++underruns_;
            }
            pcm_silence(due); // until data comes
            break;
        }
        if (state_ != STATE_PLAYING && state_ != STATE_UNDERRUN)
            vis_bus_.silence(due); // stopped within the tick: the vis tap has its frames from the one that found it
        if (bulk)
            bus_on_ = false; // after a bulk catch-up the lineup is rounded: the next frame anchors afresh
        mix_skip_ = 0;
        // Nobody taking the SIDs' samples: past a second and a half of the bus waiting, the SIDs are brought up
        // to now, which takes it (as a SID access would), so the queue stays lined up with their samples. Under
        // the two seconds past which the SIDs would skip.
        if (pcm_mix_.size() > 2 * (static_cast<size_t>(audio_rate_) * 3 / 2))
            catch_up();
    }

    void SoundCard::mix_anchor()
    {
        catch_up(); // the SIDs' samples up to now
        pcm_mix_.clear();
        mix_offset_ = 0;
        mix_prev_[0] = mix_prev_[1] = 0;
        mix_acc_ = 0;
        bus_on_ = true;
        // A lead of silence: a frame of the stream's makes its outputs in a burst, up to audio rate / stream rate of
        // them, while each SID sample takes one; started this far ahead, the queue never runs dry while the bus
        // runs, however the samples are collected. A latency of about two of the stream's frames, at most.
        const size_t lead = (audio_rate_ + play_rate_ - 1) / play_rate_ + 1;
        pcm_mix_.insert(pcm_mix_.end(), 2 * lead, 0.0f);
    }

    void SoundCard::bus_dsp(int16_t &l, int16_t &r)
    {
        if (eq_dirty_ || eq_rate_ != play_rate_) // an MP3's rate can change with any frame
        {
            eq_.set(eq_gain_, eq_on_, play_rate_);
            eq_rate_ = play_rate_;
            eq_dirty_ = false;
        }
        eq_.run(l, r);
        vis_bus_.feed(l, r);
        // Volume: 255 is unity; otherwise x vol / 255, rounded half away from zero.
        const auto scale = [](int16_t x, uint8_t v) {
            if (v == 0xFF)
                return x;
            const int q = x * v;
            return static_cast<int16_t>(q >= 0 ? (q + 127) / 255 : -((-q + 127) / 255));
        };
        l = scale(l, vol_[0]);
        r = scale(r, vol_[1]);
    }

    void SoundCard::vis_quiet(uint64_t cycles)
    {
        // Running, but the bus stopped (paused, buffering, ended, an error): the vis tap has silence at the stream's
        // rate, counted on the card's clock as the frames are, so the page goes quiet as the sound has, whatever
        // the ticks. Nothing past the vis tap: the EQ keeps its state for the stream's return. An MP3 sought has no
        // rate until its next first frame: the bus's last one, then (none if nothing has played: the tap is quiet).
        const uint32_t rate = play_rate_ ? play_rate_ : eq_rate_;
        if (!(pcmctl_ & PCMCTL_RUN) || !rate)
            return;
        vis_phase_ += cycles % host_hz_ * rate;
        const uint64_t n = cycles / host_hz_ * rate + vis_phase_ / host_hz_;
        vis_phase_ %= host_hz_;
        vis_bus_.silence(n);
    }

    void SoundCard::bus_dsp_silence(uint64_t n)
    {
        // Silence in is silence out once the EQ has rung down: from there the vis tap has it in bulk.
        for (; n && (eq_dirty_ || eq_rate_ != play_rate_ || eq_.ringing()); --n)
        {
            int16_t l = 0, r = 0;
            bus_dsp(l, r);
        }
        vis_bus_.silence(n);
    }

    void SoundCard::bus_frame(int16_t l, int16_t r)
    {
        bus_dsp(l, r);
        if (tap_on_) // a test's only: off, nothing is kept
        {
            tap_.push_back(l);
            tap_.push_back(r);
        }
        if (!mixing())
            return;
        if (mix_skip_)
        {
            --mix_skip_;
            return;
        }
        const float in[2] = {static_cast<float>(l) / 32768.0f, static_cast<float>(r) / 32768.0f};
        while (mix_acc_ < audio_rate_)
        {
            const float t = static_cast<float>(static_cast<double>(mix_acc_) / audio_rate_);
            for (unsigned ch = 0; ch < 2; ++ch)
                pcm_mix_.push_back(mix_prev_[ch] + (in[ch] - mix_prev_[ch]) * t);
            ++mix_frames_;
            mix_acc_ += play_rate_;
        }
        mix_acc_ -= audio_rate_;
        mix_prev_[0] = in[0];
        mix_prev_[1] = in[1];
    }

    void SoundCard::pcm_silence(uint64_t n)
    {
        const bool tap = tap_on_;
        tap_on_ = tap && tap_silence_; // a tap of what played only: none of the silence
        if (!tap_on_)
        {
            // A bulk catch-up's frames make nothing, all at once; at most a second's are made one by one.
            const uint64_t quiet = mixing() ? std::min(n, mix_skip_) : n;
            mix_skip_ -= mixing() ? quiet : 0;
            bus_dsp_silence(quiet); // the vis tap and the EQ have them all the same
            n -= quiet;
        }
        for (uint64_t f = 0; f < n; ++f)
            bus_frame(0, 0);
        tap_on_ = tap;
    }

    void SoundCard::pcm_play(size_t n)
    {
        if (mp3())
        {
            // The decoded frame's samples, mono on both sides; a bulk catch-up's make nothing.
            Mp3 &m = *mp3_;
            size_t quiet = 0;
            if (!tap_on_)
            {
                quiet = mixing() ? static_cast<size_t>(std::min<uint64_t>(n, mix_skip_)) : n;
                mix_skip_ -= mixing() ? quiet : 0;
            }
            for (size_t f = 0; f < n; ++f, ++m.at)
            {
                const int16_t *s = &m.pcm[m.at * m.channels];
                int16_t l = s[0], r = m.channels == 2 ? s[1] : s[0];
                if (f < quiet)
                    bus_dsp(l, r); // none to the mix, but the vis tap and the EQ have it
                else
                    bus_frame(l, r);
            }
            pos_ += static_cast<uint32_t>(n);
            return;
        }
        const size_t fb = frame_bytes();
        size_t quiet = 0; // frames that make nothing, taken in bulk: none to mix, or a bulk catch-up's
        if (!tap_on_)
        {
            quiet = mixing() ? static_cast<size_t>(std::min<uint64_t>(n, mix_skip_)) : n;
            mix_skip_ -= mixing() ? quiet : 0;
        }
        const auto at = [&](size_t i) { return (*fifo_)[(fifo_head_ + i) % FIFO_BYTES]; };
        const auto sample = [&](size_t i) -> int16_t {
            if (play_fmt_ & PCMFMT_16BIT) // signed, low byte first
                return static_cast<int16_t>(static_cast<uint16_t>(at(i) | at(i + 1) << 8));
            return static_cast<int16_t>((at(i) - 128) * 256); // unsigned
        };
        const size_t step = play_fmt_ & PCMFMT_16BIT ? 2 : 1;
        for (size_t f = 0; f < n; ++f)
        {
            const size_t b = f * fb;
            int16_t l = sample(b), r = play_fmt_ & PCMFMT_STEREO ? sample(b + step) : l; // mono on both sides
            if (f < quiet)
                bus_dsp(l, r); // none to the mix, but the vis tap and the EQ have it
            else
                bus_frame(l, r);
        }
        fifo_head_ = (fifo_head_ + n * fb) % FIFO_BYTES;
        fifo_count_ -= n * fb;
        pos_ += static_cast<uint32_t>(n);
    }

    uint64_t SoundCard::now() const
    {
        // Mid-instruction, the 6510's accesses so far: a SID write lands on its own cycle.
        return clock_ + (in_step_ ? cpu_.cycles() - step_start_ : 0);
    }

    void SoundCard::catch_up() const
    {
        const uint64_t t = now();
        if (t <= sids_at_)
            return;
        if (audio_rate_)
            render(t - sids_at_);
        else
        {
            chips_[0].clock(t - sids_at_);
            chips_[1].clock(t - sids_at_);
        }
        sids_at_ = t;
    }

    // --- audio ------------------------------------------------------------------

    void SoundCard::set_audio_rate(uint32_t rate)
    {
        catch_up(); // what went before, at the old rate (or none)
        audio_rate_ = rate;
        audio_.clear();
        sum_l_ = sum_r_ = 0;
        sample_clocks_ = sample_length_ = sample_phase_ = 0;
        hp_in_ = hp_out_ = {};
        hp_pole_ = rate ? std::exp(-2.0f * 3.14159265f * 16.0f / static_cast<float>(rate)) : 0.0f;
        pcm_mix_.clear(); // and the bus anchors afresh at its next frame
        mix_offset_ = 0;
        mix_dry_ = 0;
        bus_on_ = false;
        mix_frames_ = 0;
    }

    void SoundCard::set_model(Sid::Model m)
    {
        catch_up();
        chips_[0].set_model(m);
        chips_[1].set_model(m);
    }

    void SoundCard::tune_clocks()
    {
        chips_[0].set_clock_hz(sid_hz());
        chips_[1].set_clock_hz(sid_hz());
    }

    void SoundCard::render(uint64_t clocks) const
    {
        // Only the last half second waits: of a longer backlog (nobody taking samples), all but a
        // second is clocked in bulk, its samples, which would be dropped, never made.
        if (clocks > 2ull * sid_hz())
        {
            const uint64_t skip = clocks - sid_hz();
            chips_[0].clock(skip);
            chips_[1].clock(skip);
            audio_.clear();
            sum_l_ = sum_r_ = 0;
            sample_clocks_ = sample_length_ = 0;
            clocks -= skip;
            // The samples skipped would have taken the bus's frames meant for them: the offset first, then the
            // queue's front. A stream that ended before the gap is then gone, not played late.
            uint64_t skipped = skip * audio_rate_ / sid_hz();
            const uint64_t passed = std::min(skipped, mix_offset_);
            mix_offset_ -= passed;
            skipped -= passed;
            const size_t drop = static_cast<size_t>(std::min<uint64_t>(skipped, pcm_mix_.size() / 2));
            pcm_mix_.erase(pcm_mix_.begin(), pcm_mix_.begin() + static_cast<std::ptrdiff_t>(2 * drop));
        }
        while (clocks)
        {
            if (!sample_length_)
            {
                // Each sample spans SID Hz / rate clocks, the remainder carried: 20 or 21 for PAL at 48 kHz.
                sample_phase_ += sid_hz();
                sample_length_ = sample_phase_ / audio_rate_;
                sample_phase_ %= audio_rate_;
                if (!sample_length_)
                    continue; // a rate above the SID clock: this sample spans none
            }
            const uint64_t n = std::min(clocks, sample_length_ - sample_clocks_);
            sum_l_ += chips_[0].clock_audio(n);
            sum_r_ += chips_[1].clock_audio(n);
            sample_clocks_ += n;
            clocks -= n;
            if (sample_clocks_ < sample_length_)
                continue;
            const float in[2] = {static_cast<float>(sum_l_ / static_cast<double>(sample_clocks_)),
                                 static_cast<float>(sum_r_ / static_cast<double>(sample_clocks_))};
            // The digital bus's frame for this sample, if it is playing; with none, the SIDs' alone, as before.
            // (An offset first: SID samples a bulk catch-up's skipped bus frames were meant for.)
            const bool bus = !mix_offset_ && pcm_mix_.size() >= 2;
            if (mix_offset_)
                --mix_offset_;
            else if (!bus && bus_on_)
                ++mix_dry_; // the bus running with nothing queued: the lead didn't hold
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                // the output capacitor: a one-pole high-pass, y = x - x' + p y'
                hp_out_[ch] = in[ch] - hp_in_[ch] + hp_pole_ * hp_out_[ch];
                hp_in_[ch] = in[ch];
                // Headroom: the SIDs' sound alone, of 192 tunes, clipped in one, 12 samples. With the stream
                // added the sum can pass full scale (three resonant voices and a loud stream do), and is clipped.
                float v = hp_out_[ch] / 8.0f;
                if (bus)
                    v += MIX_DIGITAL * pcm_mix_[ch];
                audio_.push_back(std::fmax(-1.0f, std::fmin(1.0f, v)));
            }
            if (bus)
                pcm_mix_.erase(pcm_mix_.begin(), pcm_mix_.begin() + 2);
            sum_l_ = sum_r_ = 0;
            sample_clocks_ = sample_length_ = 0;
        }
        const size_t most = 2 * static_cast<size_t>(audio_rate_ / 2); // half a second
        if (audio_.size() > most)
            audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(audio_.size() - most));
    }

    size_t SoundCard::take_audio(float *out, size_t max_frames)
    {
        catch_up();
        const size_t frames = std::min(max_frames, audio_.size() / 2);
        std::copy(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(2 * frames), out);
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(2 * frames));
        return frames;
    }

    uint8_t SoundCard::read(uint32_t offset)
    {
        if (hidden(offset & 0x7F))
            return 0xFF;
        switch (offset & 0x7F)
        {
        case RAMDATA:
            return ram_byte();
        case MBIN:
            to_host_full_ = false;
            return to_host_;
        case FREE:
            free_hi_ = static_cast<uint8_t>(free_units() >> 8);
            return static_cast<uint8_t>(free_units());
        case POS:
            pos_latch_ = pos_;
            return static_cast<uint8_t>(pos_);
        case EQVAL:
        {
            const uint8_t v = eq_value();
            eq_step();
            return v;
        }
        case VISDATA:
        {
            const uint8_t v = peek(VISDATA);
            ++visidx_;
            return v;
        }
        case DOCDATA:
            return doc_data_read();
        default:
            return peek(offset);
        }
    }

    uint8_t SoundCard::eq_value() const
    {
        if (eqidx_ & EQIDX_ENABLE)
            return eq_on_ ? 1 : 0;
        return eqidx_ <= EQ_BANDS ? static_cast<uint8_t>(eq_gain_[eqidx_]) : 0;
    }

    uint8_t SoundCard::doc_data_read()
    {
        const uint8_t v = docctl_ & DOCCTL_RAM ? doc_.ram()[doc_addr_] : doc_.read(static_cast<uint8_t>(doc_addr_));
        doc_step();
        return v;
    }

    void SoundCard::doc_data_write(uint8_t data)
    {
        if (docctl_ & DOCCTL_RAM)
            doc_.ram()[doc_addr_] = data;
        else
            doc_.write(static_cast<uint8_t>(doc_addr_), data);
        doc_step();
    }

    uint8_t SoundCard::peek(uint32_t offset) const
    {
        const unsigned o = offset & 0x7F;
        if (o < SID_A + SID_REGS)
            return sid_read(0, o - SID_A);
        if (o >= SID_B && o < SID_B + SID_REGS)
            return sid_read(1, o - SID_B);
        if (hidden(o))
            return 0xFF;
        switch (o)
        {
        case CTRL:
            return ctrl_;
        case STATUS:
            return static_cast<uint8_t>((to_card_full_ ? STATUS_TO_CARD : 0) | (to_host_full_ ? STATUS_TO_HOST : 0) |
                                        (irq() ? STATUS_IRQ : 0));
        case IEN:
            return ien_;
        case RAMADDR:
            return static_cast<uint8_t>(ram_addr_);
        case RAMADDR + 1:
            return static_cast<uint8_t>(ram_addr_ >> 8);
        case RAMDATA:
            return ram_[ram_addr_];
        case MBIN:
            return to_host_;
        case PCMCTL:
            return pcmctl_;
        case CODEC:
            return codec_;
        case RATE: // while it runs, the rate playing
            return static_cast<uint8_t>(state_ != STATE_IDLE && state_ != STATE_BAD_FORMAT ? play_rate_ : rate_);
        case RATE + 1:
            return static_cast<uint8_t>((state_ != STATE_IDLE && state_ != STATE_BAD_FORMAT ? play_rate_ : rate_) >> 8);
        case PCMFMT:
            return state_ != STATE_IDLE && state_ != STATE_BAD_FORMAT ? play_fmt_ : pcmfmt_;
        case FREE: // the debugger's look: no latch
            return static_cast<uint8_t>(free_units());
        case FREE + 1:
            return free_hi_;
        case STATE:
            return state_;
        case POS:
            return static_cast<uint8_t>(pos_);
        case POS + 1:
        case POS + 2:
        case POS + 3:
            return static_cast<uint8_t>(pos_latch_ >> 8 * (o - POS));
        case KBPS:
        case KBPS + 1: // the MP3 frame playing's
            return static_cast<uint8_t>((mp3() && state_ != STATE_IDLE ? mp3_->kbps : 0) >> 8 * (o - KBPS));
        case VOLL:
        case VOLR:
            return vol_[o - VOLL];
        case EQIDX:
            return eqidx_;
        case EQVAL:
            return eq_value();
        case VISIDX:
            return visidx_;
        case VISDATA:
            return visidx_ < VIS_BYTES ? vis_[visidx_] : 0;
        case CODECS:
            return CODECS_PRESENT;
        case DOCCTL:
            return docctl_;
        case DOCDATA:
            return docctl_ & DOCCTL_RAM ? doc_.ram()[doc_addr_] : doc_.peek(static_cast<uint8_t>(doc_addr_));
        case DOCADDR:
            return static_cast<uint8_t>(doc_addr_);
        case DOCADDR + 1:
            return static_cast<uint8_t>(doc_addr_ >> 8);
        case DOCBANK:
            return static_cast<uint8_t>(doc_addr_ >> 16);
        case DRQSEL:
            return drqsel_;
        case ID_REG:
            return static_cast<uint8_t>(MODULE << 4 | rev_);
        default:
            return 0xFF;
        }
    }

    void SoundCard::write(uint32_t offset, uint8_t data)
    {
        const unsigned o = offset & 0x7F;
        if (o < SID_A + SID_REGS || (o >= SID_B && o < SID_B + SID_REGS))
        {
            if (!(ctrl_ & CTRL_RUN)) // the card's 6510 owns the SIDs while it runs
                sid_write(o >= SID_B ? 1 : 0, o & 0x1F, data);
            return;
        }
        if (hidden(o))
            return;
        switch (o)
        {
        case CTRL:
            if ((data ^ ctrl_) & CTRL_NTSC)
            {
                catch_up(); // up to now on the old crystal
                phase_ = 0; // a new crystal: the remainder was in the old one's clocks
            }
            ctrl_ = static_cast<uint8_t>(data & ~CTRL_RESET); // RESET is a strobe
            tune_clocks();
            if (data & CTRL_RESET)
                reset_cpu();
            break;
        case IEN:
            ien_ = static_cast<uint8_t>(data & IEN_MAILBOX);
            break;
        case RAMADDR:
            ram_addr_ = static_cast<uint16_t>((ram_addr_ & 0xFF00) | data);
            break;
        case RAMADDR + 1:
            ram_addr_ = static_cast<uint16_t>((ram_addr_ & 0x00FF) | data << 8);
            break;
        case RAMDATA:
            ram_[ram_addr_++] = data;
            break;
        case MBOUT:
            to_card_ = data;
            to_card_full_ = true;
            break;
        case PCMCTL:
            pcm_control(data); // FLUSH (bit 2) is a strobe; bit 7 is reserved
            break;
        case CODEC:
            if (!(pcmctl_ & PCMCTL_RUN))
                codec_ = data;
            break;
        case RATE:
            rate_ = static_cast<uint16_t>((rate_ & 0xFF00) | data);
            break;
        case RATE + 1:
            rate_ = static_cast<uint16_t>((rate_ & 0x00FF) | data << 8);
            break;
        case PCMFMT:
            pcmfmt_ = static_cast<uint8_t>(data & (PCMFMT_STEREO | PCMFMT_16BIT));
            break;
        case PCMDATA:
            pcm_write(data);
            break;
        case POS:
            pos_ = pos_latch_ = 0;
            break;
        case VOLL:
        case VOLR:
            vol_[o - VOLL] = data;
            sid_volume(); // on the SIDs at once
            break;
        case EQIDX:
            eqidx_ = data;
            break;
        case EQVAL:
            if (eqidx_ & EQIDX_ENABLE)
                eq_on_ = data & 1;
            else if (eqidx_ <= EQ_BANDS)
                eq_gain_[eqidx_] = static_cast<int8_t>(
                    std::clamp(static_cast<int>(static_cast<int8_t>(data)), -EQ_LIMIT, EQ_LIMIT));
            eq_dirty_ = true; // from the next frame
            eq_step();
            break;
        case VISIDX:
            visidx_ = data;
            if (data == 0)
                vis_snapshot();
            break;
        case DOCCTL:
            docctl_ = static_cast<uint8_t>(data & (DOCCTL_RUN | DOCCTL_IRQ_CARD | DOCCTL_AUTOINC | DOCCTL_RAM));
            break;
        case DOCDATA:
            doc_data_write(data);
            break;
        case DOCADDR:
            doc_addr_ = (doc_addr_ & 0x1FF00) | data;
            break;
        case DOCADDR + 1:
            doc_addr_ = (doc_addr_ & 0x100FF) | static_cast<uint32_t>(data) << 8;
            break;
        case DOCBANK:
            doc_addr_ = (doc_addr_ & 0x0FFFF) | static_cast<uint32_t>(data & 1) << 16;
            break;
        case DRQSEL:
            drqsel_ = static_cast<uint8_t>(data & 3);
            break;
        default:
            break;
        }
    }

    uint8_t SoundCard::dack_read(bool)
    {
        switch (drqsel_)
        {
        case DRQ_RAMDATA: return ram_byte();
        case DRQ_DOCDATA: return doc_data_read();
        default: return 0xFF; // PCMDATA is write only; 3 is no source
        }
    }

    void SoundCard::dack_write(uint8_t data, bool)
    {
        switch (drqsel_)
        {
        case DRQ_RAMDATA: ram_[ram_addr_++] = data; break;
        case DRQ_DOCDATA: doc_data_write(data); break;
        case DRQ_PCMDATA: pcm_write(data); break;
        default: break; // 3: no source
        }
    }

    int SoundCard::card_receive()
    {
        if (!to_card_full_)
            return -1;
        to_card_full_ = false;
        return to_card_;
    }

    void SoundCard::card_send(uint8_t byte)
    {
        to_host_ = byte;
        to_host_full_ = true;
    }

    uint8_t SoundCard::card_status() const
    {
        return static_cast<uint8_t>((to_card_full_ ? CARD_FROM_HOST : 0) | (to_host_full_ ? CARD_UNTAKEN : 0));
    }

    void SoundCard::sid_write(unsigned chip, unsigned reg, uint8_t data)
    {
        catch_up(); // the write lands on its clock
        const auto to = [&](unsigned n) {
            if (reg < POTX)
                written_[n][reg] = data; // as written, for reads and sid()
            if (reg == SID_MODE_VOL)
                chips_[n].write(reg, sid_mode_vol(n)); // the chip has the volume scaled (VOLL A, VOLR B)
            else
                chips_[n].write(reg, data);
        };
        to(chip & 1);
        if ((chip & 1) == 0 && (ctrl_ & CTRL_BOTH))
            to(1);
    }

    void SoundCard::sid_volume()
    {
        for (unsigned n = 0; n < 2; ++n)
            if (chips_[n].read(SID_MODE_VOL) != sid_mode_vol(n))
            {
                catch_up(); // the change lands on its clock
                chips_[n].write(SID_MODE_VOL, sid_mode_vol(n));
            }
    }

    uint8_t SoundCard::sid_read(unsigned chip, unsigned reg) const
    {
        if (reg == SID_MODE_VOL)
            return written_[chip & 1][SID_MODE_VOL]; // as written, not as VOLL or VOLR scaled it
        catch_up();
        return chips_[chip & 1].read(reg);
    }

    // --- the card's 6510 and its map -------------------------------------------

    bool SoundCard::io_in() const
    {
        const unsigned lines = (port_ | ~static_cast<unsigned>(ddr_)) & 0x07u; // inputs are pulled high
        return (lines & 0x04) && (lines & 0x03);
    }

    unsigned SoundCard::sid_at(uint16_t addr) const
    {
        const uint16_t b = ctrl_ & CTRL_SID_B_D500 ? 0xD500 : 0xD420;
        return addr >= b && addr < b + 0x20 ? 1 : 0;
    }

    uint8_t SoundCard::card_peek(uint16_t addr) const
    {
        if (addr == 0x0000)
            return ddr_;
        if (addr == 0x0001) // inputs: bits 0-5 pulled high, 6-7 unconnected, holding what was written
            return static_cast<uint8_t>((port_ & ddr_) | (~ddr_ & (0x3F | (port_ & 0xC0))));
        if (addr < 0xD000 || addr > 0xDFFF || !io_in())
            return ram_[addr];
        if (addr == 0xD011 && raster_shown()) // the raster line's ninth bit; the rest as nothing drives them
            return static_cast<uint8_t>(0x7F | (raster_line() >> 8) << 7);
        if (addr == 0xD012 && raster_shown())
            return static_cast<uint8_t>(raster_line());
        if (addr == CARD_FRAME)
            return static_cast<uint8_t>(vic_flags_ | (vic_irq() ? 0x80 : 0));
        if (addr == CARD_FRAME_EN)
            return vic_mask_;
        if (addr >= 0xD400 && addr < 0xD800)
            return sid_read(sid_at(addr), addr & 0x1F);
        if (addr >= 0xDC00 && addr < 0xDD00)
        {
            switch (addr & 0x0F)
            {
            case CIA_TA_LO: return static_cast<uint8_t>(cia_counter_);
            case CIA_TA_HI: return static_cast<uint8_t>(cia_counter_ >> 8);
            case CIA_ICR: return static_cast<uint8_t>(cia_flags_ | (cia_irq() ? 0x80 : 0));
            case CIA_CRA: return cia_cra_;
            default: return 0xFF;
            }
        }
        if (addr == 0xDE00)
            return to_card_;
        if (addr == 0xDE01)
            return card_status();
        return 0xFF;
    }

    uint8_t SoundCard::card_read(uint16_t addr)
    {
        if (io_in() && addr >= 0xDC00 && addr < 0xDD00)
            return cia_read(addr & 0x0F);
        if (io_in() && addr == 0xDE00)
        {
            const int b = card_receive();
            return b < 0 ? to_card_ : static_cast<uint8_t>(b); // empty: the last byte, again
        }
        return card_peek(addr);
    }

    void SoundCard::card_write(uint16_t addr, uint8_t data)
    {
        if (addr == 0x0000)
            ddr_ = data;
        else if (addr == 0x0001)
            port_ = data;
        if (addr < 0xD000 || addr > 0xDFFF || !io_in())
        {
            ram_[addr] = data; // $00 and $01 too
            return;
        }
        if (addr == CARD_FRAME) // writing 1 clears the latch
            vic_flags_ = static_cast<uint8_t>(vic_flags_ & ~data & 0x01);
        if (addr == CARD_FRAME_EN)
            vic_mask_ = static_cast<uint8_t>(data & (FRAMEEN_IRQ | FRAMEEN_RASTER));
        if (addr >= 0xD400 && addr < 0xD800)
            sid_write(sid_at(addr), addr & 0x1F, data);
        else if (addr >= 0xDC00 && addr < 0xDD00)
            cia_write(addr & 0x0F, data);
        else if (addr == 0xDE00)
            card_send(data);
        else if (addr == 0xDE02)
            ctrl_ = static_cast<uint8_t>(ctrl_ & ~CTRL_RUN); // STOP: the 6510 stops itself
    }

    // --- CIA 1, as far as tunes need it -------------------------------------------

    void SoundCard::cia_reset()
    {
        cia_latch_ = cia_counter_ = 0xFFFF;
        cia_cra_ = cia_flags_ = cia_mask_ = 0;
        vic_count_ = 0;
        vic_flags_ = vic_mask_ = 0;
    }

    unsigned SoundCard::raster_line() const
    {
        const bool ntsc = ctrl_ & CTRL_NTSC;
        const uint32_t period = ntsc ? FRAME_NTSC : FRAME_PAL;
        // Clocks into this frame. A clock switched without RESET can leave more than an NTSC
        // frame to count (vic_tick counts it out); the line stays at 0 until it is in range.
        const uint32_t into = vic_count_ && vic_count_ < period ? period - vic_count_ : 0;
        return into / (period / (ntsc ? LINES_NTSC : LINES_PAL));
    }

    void SoundCard::vic_tick(unsigned cycles)
    {
        const uint32_t period = ctrl_ & CTRL_NTSC ? FRAME_NTSC : FRAME_PAL;
        if (vic_count_ == 0)
            vic_count_ = period;
        while (cycles)
        {
            if (vic_count_ > cycles)
            {
                vic_count_ -= cycles;
                return;
            }
            cycles -= vic_count_;
            vic_count_ = period;
            vic_flags_ |= 0x01;
        }
    }

    void SoundCard::cia_tick(unsigned cycles)
    {
        vic_tick(cycles); // the frame interrupt keeps the same clock
        if (!(cia_cra_ & CRA_START))
            return;
        while (cycles)
        {
            if (cia_counter_ >= cycles)
            {
                cia_counter_ = static_cast<uint16_t>(cia_counter_ - cycles);
                return;
            }
            // 0, then one more clock to underflow: reload and flag it
            cycles -= cia_counter_ + 1u;
            cia_counter_ = cia_latch_;
            cia_flags_ |= 0x01;
            if (cia_cra_ & CRA_ONE_SHOT)
            {
                cia_cra_ = static_cast<uint8_t>(cia_cra_ & ~CRA_START);
                return;
            }
        }
    }

    uint8_t SoundCard::cia_read(unsigned reg)
    {
        if (reg != CIA_ICR)
            return card_peek(static_cast<uint16_t>(0xDC00 + reg));
        const uint8_t v = static_cast<uint8_t>(cia_flags_ | (cia_irq() ? 0x80 : 0));
        cia_flags_ = 0; // reading ICR acknowledges
        return v;
    }

    void SoundCard::cia_write(unsigned reg, uint8_t data)
    {
        switch (reg)
        {
        case CIA_TA_LO:
            cia_latch_ = static_cast<uint16_t>((cia_latch_ & 0xFF00) | data);
            break;
        case CIA_TA_HI:
            cia_latch_ = static_cast<uint16_t>((cia_latch_ & 0x00FF) | data << 8);
            if (!(cia_cra_ & CRA_START))
                cia_counter_ = cia_latch_;
            break;
        case CIA_ICR:
            if (data & 0x80)
                cia_mask_ = static_cast<uint8_t>(cia_mask_ | (data & 0x1F));
            else
                cia_mask_ = static_cast<uint8_t>(cia_mask_ & ~data);
            break;
        case CIA_CRA:
            if (data & CRA_LOAD)
                cia_counter_ = cia_latch_;
            cia_cra_ = static_cast<uint8_t>(data & ~CRA_LOAD); // LOAD is a strobe
            break;
        default:
            break;
        }
    }
} // namespace ooe::emu
