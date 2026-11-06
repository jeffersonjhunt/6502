/**
 * @file sound_card.cpp
 * @brief The backplane's sound card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "sound_card.h"

#include <algorithm>
#include <cmath>

namespace ooe::emu
{
    void SoundCard::reset()
    {
        catch_up();
        chips_[0].reset();
        chips_[1].reset();
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
        started_ = false;
        play_rate_ = 0;
        play_fmt_ = 0;
        pcm_phase_ = 0;
        underruns_ = 0;
        tap_.clear();
        free_hi_ = static_cast<uint8_t>(free_units() >> 8);
        pos_ = pos_latch_ = 0;
        vol_ = {0xFF, 0xFF};
        eqidx_ = 0;
        eq_gain_.fill(0);
        eq_on_ = false;
        visidx_ = 0;
        vis_snapshot();
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

    void SoundCard::vis_snapshot()
    {
        // 0-31 the spectrum and 32-33 the VU at -60 dBFS or below; 34-97 the samples at zero.
        std::fill(vis_.begin(), vis_.begin() + 34, uint8_t{0});
        std::fill(vis_.begin() + 34, vis_.end(), uint8_t{0x80});
    }

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
            pcm_advance(cycles); // the stream keeps the card's time, whether or not sound is made
        // SID clocks = host cycles x SID Hz / host Hz, the remainder carried.
        phase_ += cycles * sid_hz();
        const uint64_t due = phase_ / host_hz_;
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
            fifo_head_ = fifo_count_ = 0; // the FIFO, and a partial frame with it
        pcmctl_ = static_cast<uint8_t>(data & (PCMCTL_RUN | PCMCTL_PAUSE | PCMCTL_EOF));
        if ((pcmctl_ & PCMCTL_RUN) && !(was & PCMCTL_RUN))
        {
            started_ = false;
            pcm_phase_ = 0;
            if (rate_ < RATE_MIN || rate_ > RATE_MAX || codec_ != 0) // MP3 comes in step 4
                state_ = STATE_BAD_FORMAT;
            else
            {
                play_rate_ = rate_;
                play_fmt_ = pcmfmt_;
                state_ = STATE_BUFFERING;
            }
        }
        else if (!(pcmctl_ & PCMCTL_RUN))
            state_ = STATE_IDLE; // the FIFO kept, unless FLUSH was written too
        pcm_update();
    }

    void SoundCard::pcm_update()
    {
        if (!(pcmctl_ & PCMCTL_RUN) || state_ == STATE_BAD_FORMAT || state_ == STATE_ENDED)
            return;
        if (pcmctl_ & PCMCTL_PAUSE)
        {
            state_ = STATE_PAUSED;
            return;
        }
        if (state_ == STATE_PAUSED)
            state_ = started_ ? STATE_PLAYING : STATE_BUFFERING;
        if (state_ == STATE_BUFFERING && (fifo_count_ >= START_BYTES || (pcmctl_ & PCMCTL_EOF)))
        {
            state_ = STATE_PLAYING;
            started_ = true;
            pcm_phase_ = 0; // the first frame a whole frame from now
        }
        if (state_ == STATE_UNDERRUN && fifo_count_ >= frame_bytes())
            state_ = STATE_PLAYING; // data again: on at once, from the next frame
        // Running dry is found when a frame falls due (pcm_advance), not here: a FIFO emptied and refilled
        // between two frames hasn't underrun.
    }

    void SoundCard::pcm_advance(uint64_t cycles)
    {
        pcm_phase_ += cycles * play_rate_;
        uint64_t due = pcm_phase_ / host_hz_;
        pcm_phase_ %= host_hz_;
        while (due)
        {
            const size_t ready = fifo_count_ / frame_bytes();
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
                state_ = STATE_ENDED; // a partial frame left is never played
                return;
            }
            if (state_ != STATE_UNDERRUN)
            {
                state_ = STATE_UNDERRUN;
                ++underruns_;
            }
            if (tap_on_)
                tap_.insert(tap_.end(), 2 * due, int16_t{0}); // silence until data comes
            return;
        }
    }

    void SoundCard::pcm_play(size_t n)
    {
        const size_t fb = frame_bytes();
        if (tap_on_)
        {
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
                const int16_t l = sample(b);
                tap_.push_back(l);
                tap_.push_back(play_fmt_ & PCMFMT_STEREO ? sample(b + step) : l); // mono on both sides
            }
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
            for (unsigned ch = 0; ch < 2; ++ch)
            {
                // the output capacitor: a one-pole high-pass, y = x - x' + p y'
                hp_out_[ch] = in[ch] - hp_in_[ch] + hp_pole_ * hp_out_[ch];
                hp_in_[ch] = in[ch];
                audio_.push_back(std::fmax(-1.0f, std::fmin(1.0f, hp_out_[ch] / 8.0f))); // headroom: of 192 tunes, one clips, 12 samples
            }
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
        case KBPS + 1:
            return 0;
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
        chips_[chip & 1].write(reg, data);
        if ((chip & 1) == 0 && (ctrl_ & CTRL_BOTH))
            chips_[1].write(reg, data);
    }

    uint8_t SoundCard::sid_read(unsigned chip, unsigned reg) const
    {
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
