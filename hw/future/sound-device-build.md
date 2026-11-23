# Sound card revision 3, the device: the build plan #

**For main `ee3207f7`** (ROM BIOS 0.20, OODOS 0.16, OOLIB interface 17, GW-BASIC 7f merged, the SID noise seed
removed, north bridge revision 3 merged). Approved by the owner on 2026-11-23, with the answers in
[Decisions](#decisions) at the end; the build follows it unit by unit.

It builds [sound-device.md](sound-device.md) (the merged spec, with the owner's answers of 2026-11-21:
"recommendations as listed" for Q1-Q15) and the decision record kept in [the spec's
appendix](sound-device.md#appendix-the-owners-decisions-2026-11-21). The spec is settled. Where the code contradicts
it, or the spec leaves a gap, section 3 says so, and section 10 and the questions handle it. Nothing settled is
reopened.

**Each unit** is one commit (or two, as marked) that builds, tests and leaves the tree green. **Per unit:** the
suites that `tools/test_map.py select` picks for the unit's own paths, plus its new cases. Each must-fail case is
seen failing under its mutant, on that case alone. `make -C oodos/sys sizes` and the ROM's map are compared with
the previous unit's whenever either changes. **At each branch's end:** `tools/check.sh changed main` and
`make -C docs check`, a review round, the triage, a merge on the owner's word, then main itself gated. **At the
release (step 7):** the full tier (`tools/check.sh full --jobs 4`), because the emulator's C++ changes.

---

## 1. Today's figures (measured on `ee3207f7`, 2026-11-23) ##

Measured from a `git archive` copy of `ee3207f7` outside the repo: OODOS with
`make -C oodos/sys BUILD=<scratch> sizes` and its map; the ROM with ld65's `-m` over the Makefile's objects;
GW-BASIC with `make -C oodos/gwbasic BUILD=<scratch> sizes` (its `tools/gwsize.py` lines). Free bytes.

| Where | Used | Free | What the sound card owns there today |
|---|---|---|---|
| **Block 60, the BDOS** (`$4000-$7FFF`) | 15,473 of 16,384 | **911** | only F_SOUND's slot in the function table (bdos.s:238) |
| **Block 61, the BIOS** (`$8000-$BFFF`) | 14,988 of 16,384 | **1,396** | **sound.s: 2,740 bytes** (segment at `$A5C0`, aligned to 64, 38 bytes of fill for the beep loop's page); crt.s 740 (the console, where BEL goes) |
| **Block 62's window** (RES + RES2 + RESBSS) | 12,203 of 12,288 | **85** | IRQOTH's sound branch (input.s:153-157), WBQUIET's CALL61 (input.s:106-107), cold.s's boot-status entry (cold.s:387) |
| **Common memory** (`$F800-$FFFF`) | JUMPS 90, COMMON 1,905, vectors 6 | **47** (`$FFCB-$FFF9`) | the frame's sound call (irq.s:424-430), DEAFEN's `SOUND_IEN` and channel 2 rule (irq.s:595-596, :611-614), CLKOFF (irq.s:655) |
| **SYSBSS** (`$0300-$07FF`) | 1,231 of 1,280 | **49** | sound.s's 69 bytes (sound.s:76-102), of which HTSAVE is 16 (`HK_LEN`, sys.inc:202) |
| **The system's zero page** | 240 of 240 | **0** | none of its own (EVP, PX, PP are shared) |
| **ROM block 31, the BIOS** (`$C800-$FFF3`) | to `$EC64` | **5,007** | QUIET's sound part (bios.s:471-490); SPLASH 3,712 bytes (`$D864-$E6E3`), of which the two 6510 players are 3,431 (`TUNE` 1,942, `DOCTUNE` 1,489: splash.s:162-165) |
| **Flash blocks 24-28** (WAVE) | 81,920 | 0 | the ES5503's boot samples, `splash/wave.bin` (bios.cfg's WAVE; splash.s:168) |
| **ROM block 22, diagnostics** | to `$BD55` | **682** | stage 10: code `$9F9C-$A89B`, **2,304 bytes** (diag22.s:2187-2745), and `card6510.bin` 345 (diag22.s:2813) |
| Block 22's variables | D22ZP 93 of 160, D22BSS 140 of 144 | 67, **4** | stage 10's state |
| ROM block 21 | to `$A12C` | 7,891 | S10's tones `$8BF8-$8D0D`, 278 bytes (diag21.s:879-967); U5 (diag21.s:1072) |
| ROM block 23 | to `$A06B` of `$BEFF` | 7,828 | none |
| **GW-BASIC P7** | 5,744 of 12,288 | **6,544** | sound.s 785 + play.s 677 + event.s 329; OOLIB's mu.s 739 code + **939 data (the 6510 driver's image)**, mutone.s 145, physof.s 68 |
| GW-BASIC P4 / P5 / P6 / P8 | | 1,265 / 976 / 623 / 4,453 | P6's CBEEP (scndrv.s:1297); P8's stubs (p8/sound.s) |
| GW-BASIC free for BASIC | | **34,772** (gate 32,768) | none |

Other figures this build moves, recorded now so the end can compare (none is a guard; section 7 says what each is):

- **The tick's cost** (`test_oodos_input`'s `the_tick_costs_its_measured_cycles`, :638): pinned exactly at 142
  cycles with no area (:650) and 188 with layout 1 (:657); layout 2 between 189 and 300 (:663).
- **The full budget** (`test_oodos_video`'s `the_full_budget_the_pointer_and_a_stream_end_inside_the_blank`, :761):
  the latest end line of a frame's copies with a 44.1 kHz stream playing, gated at line 524 (:818).
- **GW-BASIC's PLAY** (`test_gwbasic_sound`'s `play_s_note_cost_is_timed`, :809): 8,243 cycles a note at the
  north bridge build (nb3's as-built), gated at 9,000 and floored at 1,000 (:826).
- **The diagnostics' quick run** (`test_bios_diag`, :330): bounded 30-75 s.
- **What the spec's step 1 asked to measure and nobody has yet:** the music driver's share of the 6510 (the books'
  19% with six notes), and the stream's and the ES5503 hook's cost to the host under OODOS. Unit S0 prints them.

---

## 2. Today's inventory, and what happens to each ##

The first inventory (taken outside the repo) was at `fa501cfb`. Main has since merged 7f, the noise removal and the
north bridge; every line number below was looked up again on `ee3207f7`. "Rebuilt" means rebuilt on the device,
"removed" means it goes, "unchanged" means it stays as it is.

### 2.1 The emulator's card and its chips ###

| What | Where | Fate |
|---|---|---|
| The card model, revisions 1 and 2: registers, the 6510 and its C64-like map, mailbox, CIA, frame interrupt, RAMADDR/RAMDATA, PCM/MP3 FIFO, the ES5503's port, EQ, vis, volumes | `sound_card.h` (740 lines), `sound_card.cpp` (1,714) | **unchanged**, kept for revision 1 and 2's tests and the record (spec, "The emulator's model") |
| The 6510's map: SID B at `$D420`, or `$D500` by CTRL; every other `$D400-$D7FF` address SID A | `sound_card.cpp:1518-1522` (`sid_at`), :1582-1612 (`card_write`) | reused behind the firmware (section 4) |
| BOTH: SID A's writes copied to SID B for a one-SID tune | `sound_card.cpp:1488-1489` | **not used on revision 3** (the decided ADC path: spec :410-411) |
| The SIDs' audio: SID A left, SID B right, a one-pole high-pass, /8, mixed after the digital bus | `sound_card.cpp:1092-1160` (`render`) | on revision 3 the SIDs go through the ADC into the digital path (decided) |
| The SIDs, the ES5503, the DSP, minimp3 | `sid.{h,cpp}`, `es5503.{h,cpp}`, `sound_dsp.{h,cpp}`, `minimp3.cpp` | **unchanged**, driven from the firmware model |
| The `.sid` parser and planner, and `start_sid`/`stop_sid`, which write CTRL, RAMADDR, RAMDATA and both SIDs straight into the card | `sid_file.h:136-143`, `sid_file.cpp:212-312` (plan), :316-339 (start, stop) | **split**: parse and plan become portable and move behind the firmware; start/stop stay for revision 2's tests |
| The machine file's sound line | `machine.cpp:520-527` (no options); `backplane.machine:121` | gains `rev=` and `memsize=`; moves to revision 3 in unit S20 |
| The USB card's speaker | `usb_card.h` (USB_SPKR) | **unchanged** (Apple II mode's speaker, diag U5) |

### 2.2 The BIOS and the ROM ###

| What | Where | Fate |
|---|---|---|
| QUIET's sound part: CTRL RUN off, IEN 0, every SID register 0, on revision 2 PCM FLUSH and DOCCTL RUN off | `bios.s:471-490` | **rebuilt**: one write of CTRL's RESET (question 7) |
| The diagnostics' copy of QUIET | `diag.inc:275-294` | **rebuilt** as QUIET |
| The boot sound: the ES5503's 80 KB into wave RAM, the 32 oscillators halted, a 6510 player loaded, CTRL at the vertical blank | `splash.s:39-160`; players `splash/bootsnd.s`, `splash/docsnd.s` (incbins :162-165); `splash/wave.bin` (:168); `bios.cfg`'s WAVE; Makefile's splash rules | **rebuilt**: one `BOOTSOUND` record; both players and the WAVE blocks **removed** from the ROM; their tables and samples go into the firmware model's flash (section 4.8) |
| `$C818`, the dispatching entry: `$A1` Apple II mode, `$A0` and `$A2-$AF` refused | `bios.s:30-36`, :86, :326-341 | **grows**: `$A2` BEEP and `$A3` STOPALL (question 8) |
| Stage 10: S1 ID, S2 the 6510's 64 KB, S3 DMA into it, S4 the 6510 program, S5 the mailbox, S6 the 6510's sum, S7 the SIDs through OSC3/ENV3, S8 wave RAM, S9 the PCM stream | `diag22.s:2187-2745` | **rebuilt** on IDENTIFY, SELFTEST, CHECKSUM, the event path and a short stream (spec :459-463); S2-S8 **removed** |
| The diagnostics' 6510 program | `backplane/oohw/card6510.s`, `card6510.cfg`; `diag22.s:2813`; Makefile:52-57 | **removed** |
| S10's tones: host writes to the SIDs and the ES5503 | `diag21.s:879-967` (SIDTONE :905, DOCTONE :930) | **rebuilt**: a music block on a SID voice, a sample effect |
| U5, the USB speaker at 1 kHz | `diag21.s:1072` | **unchanged** |
| The card's known IDs | `backplane/oohw/hwname.s:59` | gains `SOUND_ID_V3` |
| The rev 1/2 equates (58 `SOUND_*` names) | `backplane.inc:361-418` | renamed `SOUND2_*` first, rev 3's added; the old ones go at the end (question 11) |
| The ROM console's bell | none: no BEL handling in `video.s`, `wozmon.s`, `bios.s`, `input.s` | nothing to change (the spec's "the monitor's bell calls BEEP" has no target: flag 10) |

### 2.3 OODOS ###

| What | Where | Fate |
|---|---|---|
| Function 82's interface | `oodos.inc:250-285` (SN_INFO 0 to SN_VOLUME 19, `SN_OPS = 20`, SN_BUSY, SC_*); `oodos.h` | **rebuilt** (section 5.4) |
| The event area's sound part | `oodos.inc:185-196` (+96-115, ES_NOQUEUE) | +96-107 **unchanged**; +108-115 **removed** (reserved, 0); +116-123 new |
| EK_SOUND, "a byte from the card's 6510" | `oodos.inc:232` | kept as a kind; its code becomes the event's kind (spec :552-556) |
| sound.s, function 82 whole, in block 61 | `sys/sound.s` (1,440 lines) | **rebuilt** (sizes in section 5.2) |
| SNDINIT (ID, CODECS, the parts) | `sound.s:126-160` | rebuilt: `$A3` only; FEATURES; CTRL RESET; READY; FREEALL |
| SNDQUIET, the warm boot's (IEN, VOLL/R 255, EQ flat) | `sound.s:168-189` | rebuilt as the spec's warm boot (spec :565-566) |
| SMTIME and MBOXIN, the mailbox stamp | `sound.s:192-201`, :358-410 | **removed** |
| PQUIET, SIDSTOP | `sound.s:207-247` | rebuilt (STOPALL, TUNE_STOP, STREAM_CLOSE per part) |
| C2STOP, channel 2 | `sound.s:251-275` | **unchanged** |
| SIENSET: the mailbox's and the ES5503's interrupt | `sound.s:278-302`; called at irq.s:828, :889, :900 | rebuilt: EVT's interrupt whenever the card is there |
| SNDIRQ, DOCIRQ, DOCBACK | `sound.s:304-470` | SNDIRQ rebuilt (events, replies, the stream's NEED); DOCIRQ and DOCBACK **removed** |
| The stream engine: CNEXT, BSTART, PIECE, SFIELDS, SOBS | `sound.s:485-650` | rebuilt: buffers sent on NEED into DATA; SFIELDS from status replies |
| DMALOAD, RAMEND, SRC | `sound.s:653-760`, :979-1027 | DMALOAD and SRC kept for every load into DATA; RAMEND removed |
| The beep: BEEPD, BEEPQ, BEEPGO, BEEPHALF on USB_SPKR | `sound.s:475`, :765-843 | **removed**; SN_BEEP sends BEEP |
| The raw operations SN_LOAD, SN_START, SN_SEND, SN_WAVE, SN_DOCHOOK, SN_HOLD, SN_GO | `sound.s:955-1131` (SN_STOPX inside it, :1072, stays), :1145, :1337-1413 | **removed** (decided), refused |
| The frame's sound call, while the stream is claimed | `irq.s:424-430` | widened to SC_TUNE and SC_MUSIC (same bytes) |
| DEAFEN, CLKOFF | `irq.s:595-596`, :611-614, :655 | the equate's address changes (IEN at `$C50C`); channel 2's rule **unchanged** |
| The cold boot's R61_INIT | `irq.s:674-675` | unchanged call |
| IRQOTH's sound branch, WBQUIET | `input.s:153-157`, :106-107 | **unchanged** |
| DMAWARM's assert, channel 2 is module A | `video.s:1353` | **unchanged** |
| The boot status's "sound" | `cold.s:387`, :452 | **unchanged** (the high nibble stays `$A`) |
| The console's BEL: "nothing (no bell yet)" | `crt.s:6` | **rebuilt**: BELL (spec :614-615) |
| OOLIB's version | `sys.inc:156` (17) | 18 |

### 2.4 OOLIB ###

| Module | Today (where) | Fate |
|---|---|---|
| `sid.s` (1,018 lines): SID_PARSE, SID_PLAN (the stub), SID_PLAY (SN_CLAIM SC_6510 :922, SN_LOAD :859, SN_START :980), SID_STOP (:999-1004); SID_CTRL (:39) | `lib/sid.s`, `zpsid.s` | SID_PARSE **unchanged**; SID_PLAY on SN_TUNE and SN_PLAY, RSIDs too; SID_STOP on SN_STOP; SID_PLAN per question 10 |
| `sidv.s`: both SIDs' registers read straight off the card, no claim | `sidv.s:26`, :37 | **rebuilt** on SN_SIDREGS (the same 50 bytes) |
| `sidnote.s`, `sidenv.s` | | **unchanged** (arithmetic on sidv's block) |
| `pb.s`: PB_PAUSE/RESUME by SN_HOLD/SN_GO or SN_PAUSE/SN_RESUME | `pb.s:81-109` | rebuilt: SN_TPAUSE/SN_TRESUME |
| `pbstat.s`: DOCCTL's RUN (:78), CTRL's RUN (:107) and RATE (:227) read straight off the card; the stall rule on EV_SMMS (:124) and EV_SMCNT (:153) | | **rebuilt**: SN_TSTATUS, SN_STATUS, SN_MSTATUS; +116-123 |
| `pblev.s`: CTRL's clock bit off the card (:136) | | rebuilt on SN_LEVELS |
| `pbeq.s`, `pbvis.s`, `pbcard.s`: EQIDX/EQVAL, VISIDX/VISDATA, the ID straight off the card | `pbeq.s:26-63`, `pbvis.s:23-31`, `pbcard.s:16-17` | rebuilt on SN_EQ, SN_VIS, SN_INFO |
| `pb.inc`'s private copy of the card's addresses | `pb.inc:10-24` | **removed** |
| `pbvol.s` (SN_VOLUME), `pbmath.s` | | **unchanged** |
| `mu.s` (467 lines): MU_INIT claims SC_6510 (:63), loads and starts the driver (:80, :97), each call a mailbox byte by SN_SEND (:426) | `lib/mu.s`, `mu.inc`, `zpmu.s` | **rebuilt** on note blocks (decided Q7; question 9) |
| `munote.s`, `mutone.s` | | arithmetic kept; the send rebuilt |
| `mudrv/`, the 6510 music driver (627 lines) | `lib/mudrv/mudrv.s`, `.cfg`; oodos Makefile; test-map's `oodos/lib/mudrv/` rule | **removed** (decided) |
| `physof.s` | | **unchanged** (every load takes a physical address) |
| The interface's sound part | `oolib.inc:19-55`, :277-298, :306-340, :344-388, :401-449 | grows (PB_MUSIC; MU_BLOCK), PB_DOC refused |

### 2.5 The utilities ###

| What | Where | Fate |
|---|---|---|
| PLAYSID: SID_PARSE :167, SID_PLAN :171, SID_PLAY :173, SID_STOP :208 | `utils/playsid.s` (323 lines) | **rebuilt**: RSIDs play, the header's text, the tune's status |
| SYSINFO's SOUND section: SN_INFO (:846-850), the parts SC_6510/SC_STREAM/SC_DOC (:908-922), "CODECS $" (:1584) | `utils/sysinfo.s` | **rebuilt** on SN_IDENTIFY: firmware, store, features |
| VIEW | `utils/view.s` | **unchanged**: it has no sound (checked: no function 82 call) |
| SNDFW.COM | none | **new** (decided) |
| The test programs: l3test.s (DOCCTL and `$D418`s hard-coded), l3strm.s, pbtest.s, mutest.s, sidovl.s, sidplug.s | `oodos/sys/test/`, `oodos/test/{pb,mu,ovl}/` | rebuilt with their suites |

### 2.6 GW-BASIC (7f, merged) ###

| What | Where | Fate |
|---|---|---|
| SNDINI/SNDRST (MU_QUIT, MU_STOP), BEEPP/BELLP (SN_BEEP, 800 Hz 250 ms, :473), SOUNDP, DONOTE (MU_INIT :207, MU_TONE :329, MU_COUNT :408, MU_STOP :438) | `p7/sound.s` (500 lines) | **rebuilt** in branch C on the tone generator (spec :592-617) |
| PLAY, PLAY(n) (MU_COUNT :61) | `p7/play.s` | rebuilt: one block a statement |
| ON PLAY(n): EK_SOUND to MU_EVENT and MU_COUNT (:57-73), MUQMAX 32 (:225) | `p7/event.s` | rebuilt: MUSIC_THRESH and MUSIC_COUNT |
| CBEEP (^G) | `p6/scndrv.s:1297` | **unchanged** (it calls BELLP) |
| P7 links mu.s, mutone.s, physof.s and builds the driver's image | `gwbasic/Makefile:95-104` | mu.s and mutone.s unlinked in branch C; physof stays |
| The home block's stubs | `p8/sound.s` | **unchanged** |

### 2.7 The WebUI ###

| What | Where | Fate |
|---|---|---|
| Play .sid through the session | `session.cpp:384-425` (play_sid, play_song, stop_sid) | **rebuilt**: on revision 3 the file goes to the firmware model's tune player directly |
| The audio mix of the card and the speaker | `session.cpp:332-380` | **unchanged** |
| The WASM API: `loadSid`, `sidSong`, `stopSid`, `sidInfo`, `soundRunning` (CTRL's RUN, bindings.cpp:556-560), `soundCpu`, `soundVoice` | `bindings.cpp:458-560`, :709-713, :746-747 | `soundRunning` from TUNE_STATUS; `soundCpu`/`soundVoice` from the model's 6510 and SID shadows |
| The tune bar: "ended" when `soundRunning` falls | `ui/src/ui/sid-bar.ts:121` | **unchanged** code, new meaning |
| The debugger's sound pane: `$C500`'s old map, the SIDs' voices (`sidVoice`, the "visualiser"), the 6510's registers | `ui/src/debugger/devices.ts:170-205` | **rebuilt**: the device's registers, FIFOs and objects |
| `apps/sidcheck` | `apps/sidcheck/main.cpp` | follows `sid_file`'s split |
| The web tests | `ui/src/port/wasm-port.test.ts:91`, :94 ("the boot tune is playing"); `sid-bar.test.ts:65`, :71; `devices.test.ts`, `pane.test.ts`; `ui/e2e/sound.spec.ts:23` (@smoke: SID registers written at WOZMON, :36-37); `ui/e2e/sid.spec.ts` | rewritten where they reach the old registers (block A) |

### 2.8 OOUI and OO-Amp ###

- **OOUI** (`oodos/uiprog/`): no sound. Unchanged.
- **OO-Amp** (`~/workspace/ooamp` at `148ebe7`; read only, nothing changed). It never writes the card's registers
  (no `$C5xx` or `SND_` use; `backplane.inc` is included at `oa_hal.s:55` and `gui/ooaui.s:26` for the video
  card). What it would need, in its own repo, as a step for the owner to schedule:
  - **Removed things it uses:** `ES_NOQUEUE` (`oa_hal.s:153`); `EV_SMMS` and `EV_SMCNT`, its stall and playing
    test (`oa_show.s:697`, :989, :1005), which move to +116-119 or `PB_STATUS`; `SID_PLAN` (`plugins/oafsid.s:106`)
    and `SID_CTRL` (:116, and `oaf.inc:93`'s INF_CTRL, `oa_voices.s:70`, :220), whose NTSC bit comes from the
    header instead.
  - **Kept, unchanged in shape:** SN_CLAIM/SN_STREAM/SN_BUFFER/SN_END/SN_SEEK/SN_RELEASE (`oa_stream.s:116-164`,
    :611), PB_*, SID_VOICES/SID_NOTE/SID_ENV, SID_PARSE/PLAY/STOP.
  - **Relinked:** its overlay of `lib-sid` (Plan B's `SID_ZP`): sid.s's zero page changes size.
  - **Behaviour it will see:** RSIDs play; the SIDs show in the vis page and the EQ; `PB_LEVELS` for a SID is
    measured, not estimated.

### 2.9 The books and the record ###

- **Technical Reference:** `07-sound.md` (1,569 lines, 32 register entries `reg-c500` to `reg-c57f`): rewritten for
  revision 3, revision 2 reduced to a pointer to `hw/sound-rev2.md`. Notes in chapters 3 (`$C818`), 4 (IRQH bit 2),
  5 (channel 2), 6 (the splash's sound), 13 (the reset pulse), 14 (the emulator), 15 (stage 10, S10).
- **Programmer's Guide:** `07-function-reference.md#bdos-82` (from :3439) and #bdos-80's interface list;
  `08-game-services.md` "Sound" (:332-650: the 6510, the stream, the ES5503, the beep, .sid tunes, EQ and vis, the
  SIDs' voices, playback, music, what sound costs); `06-program-formats.md:116`; appendix D.
- **User's Guide:** PLAYSID, SYSINFO, the new SNDFW, DIAG's notes.
- **Indexes:** `docs/index/registers.json` (32 SOUND entries), `functions.json` (:627, bdos-82).
- **docs/examples/:** none uses sound (cargs, dice, writestr). Nothing to change.
- **The record:** hw/design.md "Sound" (:1633) and "Sound card revision 2" (:1764), "The splash's sound" (:1553);
  hw/oolib.md "Sound" (:180) and L3-L6; hw/oodos.md (versions, 7f's limits); hw/diag.md (stage 10, S10);
  hw/testing.md (the suites' table); AGENTS.md:31 ("the sound card's 6510").

### 2.10 The tests ###

The sound-labelled suites (`emulator/test/host/CMakeLists.txt:950-1031`), with their cases today:

| Suite | Cases | Fate |
|---|---|---|
| test_sound (41), test_sound_pcm (18), test_sound_mix (13), test_sound_mp3 (30), test_sound_eq (19), test_sound_doc (19), test_sound_driver (4) | 144 | **unchanged**: they test revisions 1 and 2's model, which stays (flag 11 on test_sound_driver) |
| test_sid (20), test_sid_audio (20), test_es5503 (17) | 57 | **unchanged** |
| test_sid_file (29) | | unchanged, plus the split's cases |
| test_sid_corpus (4) | | gains the device mode and the recorder (S0, S4) |
| test_mudrv (14) | | **removed** with the driver |
| test_oodos_sound (34) | | 16 rewritten on the device, 17 retired (block B lists them), the interface pin moved |
| test_oodos_pb (24), test_oodos_mu (12), test_oodos_playsid (9) | | rewritten on the device |
| test_oodos_load (`a_stream_plays_through_a_64_kb_load_with_no_underrun`, :342) | | unchanged calls (SN_STREAM, SN_BUFFER kept) |
| test_oodos_video (`:840` peeks SOUND_STATE at `$50`) | | that line reads SN_STATUS instead |
| test_oodos_sysinfo (12) and its goldens | | the SOUND line and section |
| test_bios (75): `the_boot_splash_plays_with_its_sound_then_hands_back_the_screen_and_the_sids` :585, `c800_after_an_es5503_program_halts_every_oscillator_before_the_splash_plays` :671, `break_during_the_boot_splash_stops_it_and_its_sound` :700, `break_stops_the_pcm_stream_and_the_es5503_on_revision_2` :845, `the_demos_end_stops_the_pcm_stream_and_the_es5503` :872, `break_stops_a_paused_stream_and_clears_a_refused_run` :894, `break_stops_the_dma_and_quiets_what_would_interrupt` :1946 | | rewritten for revision 3 (block C) |
| test_bios_sd: `the_flash_update_runs_quiet_and_sets_its_windows_however_it_was_entered` :947 | | its sound check rewritten |
| test_bios_diag (75): `break_stops_the_sound_cards_6510` :1092, `a_stuck_bit_in_the_sound_cards_ram_is_named_by_s2` :1265, `the_6510_program_not_running_fails_s4_within_half_a_second_and_skips_s5_to_s7` :1277, `no_sound_card_skips_stage_10_and_no_dma_skips_stage_8_v7_s3_and_sd3` :1334, `the_interactive_tests_with_switch_16_every_answer_given` :1427, `break_during_s10s_first_tone_leaves_the_sids_silent_at_the_boot` :1563; `golden/romdiag.txt` | | stage 10's cases rewritten (block C) |
| test_session: `the_sound_cards_audio_keeps_real_time_and_outlasts_a_power_cycle` :309, `a_sid_tune_plays_on_the_card_changes_song_stops_and_goes_with_a_power_cycle` :353, `the_speaker_and_the_sids_mix_into_the_same_stream` :394 | | :353 rewritten on the firmware; :309 and :394 kept, run on revision 3 (block A) |
| test_backplane: `CHECK_EQ(bus.peek(0xC57F), 0xA2)` :458 | | `$A3` when the default moves |
| test_gwbasic_sound (29, of which 15 are sound) | | branch B keeps 7f's behaviour; branch C rewrites them (block E) |
| test_gwbasic_core: BEEP by SN_BEEP, its block watched (:1178-1206) | | unchanged (SN_BEEP's block is kept) |
| test_usb's speaker cases, test_iie's bell | | **unchanged** |
| The interface-17 pins: test_oodos_sound :1574, test_oodos_clock :107, test_oodos_input :773, test_oodos_video :204, test_oodos_gfx :677 and :1502, test_oodos_sys :77 (0.16) | | moved to 18 and 0.17 (block D) |

---

## 3. What the code says that the spec doesn't (flags, not re-litigation) ##

1. **The corpus can't be identical in every sense the spec names.** The spec asks for the same SID writes and the
   same audio hash (spec :659-662). Three decided changes make the final mix differ: the SIDs go through the ADC
   into the digital path (:104-108), a one-SID tune plays on SID A alone with no BOTH copy to SID B (:410-411), and
   the master volume stops scaling `$D418` (:107). Today's mix (sound_card.cpp:1092-1160) is SID A left and SID B
   right after a high-pass. What can be identical, by construction, is the 6510's own bus writes and each SID chip's
   own output. Question 2.
2. **The spec's 6510 map isn't today's.** Today a write to `$D420-$D43F` reaches SID B whatever the tune
   (sound_card.cpp:1518-1522), and a second SID named at any address but `$D420` or `$D500` gets a warning and stays
   at `$D420` (sid_file.cpp:301-306). The spec's map (:407-409) mirrors SID A over `$D400-$D7FF` for a one-SID tune
   and puts the second SID where the header says. Either changes some tunes. S4 keeps today's decode, so the sweep
   can be identical, and counts the tunes the spec's map would change. Question 2.
3. **The spec leaves layouts open.** Event kind numbers ("`$10` up the events below", :209, never numbered); the
   payloads of NEED, XFERDONE, UNDERRUN, TRACK and the rest; `BOOTSOUND` `$09` (:452) is missing from the command
   table (:275-289); the note block's bytes (:341-347), the SID effect's step table (:322-324), the sample effect's
   definition (:325-326); the replies of IDENTIFY, SELFTEST, TUNE_STATUS (16 bytes), TUNE_INFO (104), MUSIC_STATUS
   (24), STREAM_STATUS; the firmware image's header (:120). Unit S1 writes them as one table for approval
   (question 3).
4. **Operation 20 is free.** The spec numbers the new operations from 21, "SN_NOISE (20) is removed by the noise
   branch ahead of this" (:496, :522). The noise branch then took it out plainly, its number "simply free again"
   (decisions, the owner on the noise seed), and `SN_OPS` is 20 (oodos.inc:281). Question 4.
5. **SN_BEEP blocks today; the device's BEEP doesn't.** Today's SN_BEEP returns after its length, timed by the CPU
   with interrupts on (sound.s:1414 on; L3's decision). BEEP replies at once and sends BEEP_DONE later. The spec
   keeps SN_BEEP "with its number, block and results" (:511-519). GW-BASIC's BEEP relies on the wait. Question 6.
6. **The BIOS's BEEP can't take A/X and Y.** The spec gives BEEP "A/X the pitch, Y the length" (:487-489), but
   `$C818` takes Y = `$5A` (the token) and X = `$A0` + the function, A the argument alone (bios.s:30-32,
   :329-341). Question 8.
7. **A half-written record survives a Break.** BIOS 0.20's QUIET zeroes `$C500-$C53C` (bios.s:479-482). On a
   revision 3 card that writes one `$00` into CMD (`$C503`), the start of a record that then swallows the next
   writer's first bytes. A Break can also land inside OODOS's own record. So QUIET can't rely on sending a record,
   and OODOS's cold boot must clear the parser before its first record. CTRL's RESET strobe does both
   (spec :469-474). Question 7.
8. **Who waits for a REPLY.** The spec has the interrupt handler hand each REPLY to the function 82 call waiting for
   it (:245-246). The handler would need a buffer for up to 250 bytes of results; SYSBSS has 49 bytes. Section 5.2's
   design has the call read its own reply, with the card's EVT interrupt off while it waits (flag only; the effect
   is the spec's).
9. **The handler must send commands too.** A stream's next buffer goes when the card's NEED says there is room
   (spec :518), and NEED arrives in the handler, as the next piece's terminal count does today (sound.s:485-535).
   So the handler sends STREAM_DATA, and a program's record and the handler's must never interleave: every record
   is written to CMD with interrupts off, and the handler's commands carry tags of its own whose replies it takes.
10. **There is no monitor bell.** The spec has "the monitor's bell calls BEEP" (:489). The ROM's console has no BEL
    handling (checked: video.s, wozmon.s, bios.s, input.s). Nothing to change; the line is dropped from the books.
11. **test_sound_driver needn't go.** The spec lists it among the retired tests (:664). It drives revision 2's card
    with a host-side 6510 program (`emulator/test/host/sound/driver.s`); revision 2's model stays, so the suite
    stays unchanged.
12. **The event area's per-frame fields need a source.** OODOS writes the stream's fields each frame today from the
    card's registers (SFIELDS, sound.s:596-637). On revision 3 the numbers are in STREAM_STATUS and TUNE_STATUS
    replies, and a handler can't wait for a reply. Section 5.2: the frame sends the status requests and the reply,
    when it comes, writes the fields (one frame late). The full-budget gate (`test_oodos_video:818`) measures it.
13. **The machine file has no sound options** (machine.cpp:520-527). The spec names `rev=3` (:623); testing the 2
    and 4 MB stores needs `memsize=` too.
14. **The spec keeps the revision 1 and 2 equates "for the record"** (:693). Nothing would use them after branch B,
    and `docs/tools/check_docs.py` holds every equate in backplane.inc to a register entry, so keeping them keeps
    32 entries in chapter 7 for a card OODOS no longer drives. Question 11.

---

## 4. The emulator's device ##

### 4.1 Where the firmware lives ###

Yes: C++ in the emulator stands in for the card's controller, as decided (Q2): **`sound_fw.{h,cpp}`, portable C++
with no emulator dependency**, written so that it can be the card's code on a C++ part, or the FPGA's reference
model. Its arithmetic (the EQ, the resampling, the ADC path's level) is the card's, bit for bit.

The firmware drives the card's chips, which on real hardware are chips or FPGA cores, not code. So it talks to them
through **one port interface** (`SoundFwPort`, in `sound_fw.h`):

- the 6510: write its RAM, reset it, run, hold, stop; its play-call signal (the stub's `$DE00` write, which today is
  the mailbox: on revision 3 it reaches the firmware, not the host); its JAM; its cycle count;
- the SIDs: write a register (chip, reg, value), set the model, read the ADC's samples;
- the ES5503: write a register, write wave RAM, read its output and interrupt;
- the codec: the DAC's samples out;
- the clock: card time, in SID clocks and in 48 kHz samples.

**In the emulator** the port is implemented by the card's revision 3 front end over the existing parts: `cpu6502`
for the 6510 (with today's C64-like map, CIA 1 and frame interrupt, `sound_card.cpp:1582-1612`), `sid.h`,
`es5503.h`. **On an RP2350** it would be GPIO/PIO to the chips; **on an FPGA**, the cores' ports.

**Proposed file layout** (beyond the spec, which grows `sound_card.{h,cpp}`: section 9, item 1):

| File | What |
|---|---|
| `sound_fw.{h,cpp}` | the firmware: the command parser, the records, the event FIFO, the store and its allocator, the object slots, the voice manager, the tune player (with the `.sid` planner), the music sequencer, the effect players, the stream rings and decoders, the tone generator, the digital path (ADC input, mix, EQ, vis, volumes) |
| `sid_plan.{h,cpp}` | the `.sid` parser and planner, out of `sid_file.cpp:212-312`, portable; `sid_file.{h,cpp}` keeps `start_sid`/`stop_sid` for revision 2 and includes it |
| `sound_card3.{h,cpp}` | the revision 3 front end: the registers at `$C500-$C57F`, CMD and EVT FIFOs (1 KB, 2 KB), DATA and its landing buffer, DRQ, the interrupt, CTRL's strobes, /RESET, READY; the port over `cpu6502`, `Sid`, `Es5503` |
| `sound_flash.h` | the card's flash as the emulator has it: the boot sound's samples and sequence, the bell, the default bank (made from `splash/wave.bin` and the docsnd tables by a script, as `splash_rom.h` is) |

`Machine` holds either card behind the sound line (`rev=1|2|3`, `memsize=2|4|8`, in MB; 8 the default on
revision 3). `Machine::sound_card()` keeps returning the revision 1/2 card (null on revision 3) and a new
`sound_device()` returns the revision 3 one, so nothing that uses today's accessor changes meaning silently.

### 4.2 How the 6510, the SIDs and the ES5503 are driven ###

- **A tune:** TUNE_LOAD's bytes go into the store; TUNE_PLAY plans it (the planner, unchanged), copies the plan's
  blocks into the 6510's RAM through the port, sets the clock and the second SID's decode (today's rule, flag 2),
  and resets the 6510, exactly as `start_sid` does today (sid_file.cpp:326-339) minus BOTH. The 6510 then runs
  cycle-exact at the SID clock in the front end, as it does in revision 2's model; its SID writes go straight to the
  chips on their cycle and through a port callback into the firmware's shadows (for SIDREGS).
- **Effects and music** write the SIDs of chips no tune holds, and the ES5503's oscillators, through the port, on
  the firmware's millisecond steps.
- **The ES5503's interrupt** is the firmware's (spec :417-418).
- **The SIDs' sound** comes back through the port's ADC samples into the digital path.

### 4.3 Time ###

Every timed thing derives from the host's cycle count by exact integer ratios, as revision 2's phase accumulators do
(`sound_card.h:348`, `set_cpu_hz`). The firmware's millisecond k falls at a fixed SID-clock count. A command runs
at the card time of its last byte plus a constant latency (20 µs, the spec's), and the card's start-up after power
or FWRESTART takes a constant (Proposed 20 ms, inside the spec's 100 ms), both with test hooks. Results are the same
at any audio rate and however the front end is ticked; each feature's suite has that case.

### 4.4 The FIFOs, DMA channel 2, the store ###

- **CMD and EVT** as the spec's registers (:169-186). Every command gets one REPLY in order; the firmware keeps room
  in the EVT FIFO for the replies of the commands it has taken, and a command waits in CMD until there is.
- **DATA and DMA:** DRQ only while CTRL's DRQ is set, a transfer is open and the landing buffer has room. The landing
  buffer drains into the store at a modelled rate that is never the limit, so a load runs at a byte a cycle. WIDE is
  modelled (two bytes a word grant) but nothing sets it.
- **The store:** a byte array of MEMSIZE × 64 KB, with a simple allocator (first fit, freed on FREE commands, every
  object's bytes in one run). E_MEMORY when no run fits.

### 4.5 Streams ###

Each stream's ring is an allocation in the store (at least 1 MB on an 8 MB card, the spec's). NEED is sent when a
whole chunk fits and none is outstanding; the urgent flag below 1 s; UNDERRUN and UNDERRUN_END; STREAM_NEXT joins the
next stream's first frame to the last's with no gap. Raw PCM and MP3 reuse revision 2's decoder state and minimp3.
The MP3 gapless trim reads the encoder's header (the Xing/Info tag with its delay and padding) by a small parser of
our own (minimp3 doesn't trim).

### 4.6 The sound bank ###

Samples in the store, copied into wave RAM on demand (a small LRU cache of wave RAM pages); SID effects and sample
effects in an effect table; the voice manager across SID A, SID B (the chips no tune holds) and the 32 oscillators,
with the spec's priority and stealing rules.

### 4.7 Firmware update and version ###

FWVER and IDENTIFY give the model's version (1.0). FW_BEGIN/FW_COMMIT accept an image whose header and CRC-32 check
(the header's layout: S1's table) into the slot not running, and mark it to boot; FWRESTART or a power cycle "boots"
it: FWVER then reports its version. The model can't change its own C++, so behaviour stays the model's; the update
path is what is tested. The slots live in the card object, so they last a FWRESTART and a /RESET; a session power
cycle that rebuilds the machine starts from the built-in image (recorded; a `fwslots=` file option is possible later).
`tools/mksndfw.py` makes test images (good, bad CRC, wrong card type).

### 4.8 The boot sound and the bell ###

BOOTSOUND plays today's boot sound from the card's flash: the same 80 KB of samples (`splash/wave.bin`) and today's
ES5503 sequence (`splash/doctables.inc`, as `docsnd.s` plays it) re-expressed as a built-in bank and note block,
played by the firmware's own sequencer. It sounds the same; it isn't sample-identical, since the 6510 player's
timing becomes the sequencer's (question 12). BELL plays a short tone-generator sound from the same flash.

### 4.9 Staying accurate for PSID and RSID ###

- **What is the same as today:** the planner and its stub (unchanged code, moved), the 6510 core, the CIA and
  frame model, the SID chips and their clocks, the map's decode (flag 2), and so the 6510's bus writes, cycle for
  cycle, from its reset.
- **The guard:** `test_sid_corpus` gains a recorder (S0) and a device mode (S4). For each tune, the recorder hashes
  the 6510's SID writes (address, value, SID-clock cycle from the 6510's reset), each SID chip's output samples
  before any mixing, and the play-call count, on today's register path. The device mode plays the tune by
  TUNE_LOAD and TUNE_PLAY records through the front end and compares. The known failures stay the same set (148
  left out).
- **What is new by decision:** the final mix (the ADC path; a one-SID tune on SID A only, heard in both ears). Its
  hash becomes a new baseline once (question 2).
- **RSIDs** play as today at first (spec :303-307): the same planner, which refuses BASIC RSIDs.

---

## 5. The interface ##

### 5.1 The card's registers, records and events ###

The register map (spec :169-186), the record layouts (:196-211), the errors (:251-266) and the command set
(:268-394) are built as the spec gives them. The gaps (flag 3) are filled by S1's table; its proposals:

- **Event kinds:** `$01` REPLY; `$10` XFERDONE; `$20` TUNE_STARTED, `$21` TUNE_STALL, `$22` TUNE_JAM, `$23`
  TUNE_ENDED; `$30` VOICE_DONE, `$31` VOICE_STOLEN; `$40` MUSIC_MARK, `$41` MUSIC_COUNT, `$42` MUSIC_DONE; `$50`
  NEED, `$51` UNDERRUN, `$52` UNDERRUN_END, `$53` TRACK, `$54` STREAM_ENDED; `$60` BEEP_DONE; `$70` FWRESULT. The
  high nibble is the command group's, as the spec numbers commands. Every event carries the card's ms stamp (4
  bytes) after its object.
- **BOOTSOUND** `$09`, no parameters, in the device group.
- **The note block, an entry:** `+0` a kind and voice (high nibble the kind: note, frequency, rest, instrument,
  volume, marker; low nibble the voice 0-7), then per kind: a note number (1 byte) or a frequency × 16 (3 bytes),
  the length in µs (4), the gate in µs (4); 9 to 12 bytes an entry.
- **The firmware image:** a 32-byte header (a magic, the card type `$A3`, the version, the body's length, the
  body's CRC-32, the header's CRC-32), then the body.

### 5.2 OODOS's driver ###

**The shape of the new sound.s** (block 61, as now):

- **The record sender:** a whole record written to CMD with interrupts off (PHP/SEI ... PLP), after CMDRDY. A
  record is 3-25 bytes for every function 82 operation, so the masked span is under 300 cycles.
- **The reply wait (flag 8):** the call clears IEN's EVT bit, reads EVT itself until its REPLY (its tag), passing
  every event it meets to the handler's own event routine (which queues it as the interrupt would), then copies the
  results straight into the program's block through PPUT (as SN_STATUS does today, sound.s:1325-1331), and sets
  IEN back. Time-out 100 ms: A = `SN_BUSY`, X = `$FF`. With no reply buffer, SYSBSS stays within its 49 bytes.
- **The handler (SNDIRQ, through CALL61 from IRQOTH, unchanged):** drains EVT; a REPLY with a handler tag is taken
  and dropped (or, for a status request, written into the event area's fields); events become EK_SOUND events
  (code the kind, value the object) in a layout-2 area's queue; NEED starts the next buffer (STREAM_DATA with a
  handler tag, then channel 2's pieces into DATA, as PIECE does today).
- **Transfers** (SN_TUNE, SN_SAMPLE, SN_EFFECTS, SN_MUSIC, SN_FIRMWARE): the command, its REPLY, then DMALOAD's
  pieces into DATA (sound.s:682), then XFERDONE awaited. SN_BUSY while the stream's transfer is open, as today's
  `a_load_is_refused_busy_while_a_stream_buffer_moves`.
- **The frame (flag 12):** while SC_STREAM, SC_TUNE or SC_MUSIC is claimed and a layout-2 area is open, the frame
  sends STREAM_STATUS/TUNE_STATUS/MUSIC_STATUS with handler tags (the claimed ones), and the replies write
  +96-107, +116-119 and +120-123.
- **Cold boot (SNDINIT):** the ID `$A3` (anything else: no card for OODOS, SN_INFO says the revision); CTRL's
  RESET strobe (flag 7); READY awaited up to 250 ms; FREEALL; EVENTS with OODOS's mask; IEN EVT.
- **The warm boot (SNDQUIET):** XFERABORT, STOPALL, FREEALL, VOLUME 255/255, EQ_ON 0 and EQ_ALL 0s, channel 2
  aborted (C2STOP), claims released, IEN as OODOS's.

**Sizes (estimated from today's routines, measured per unit):** of sound.s's 2,740 bytes, about 1,020 go (MBOXIN 76,
DOCIRQ 116, DOCBACK 32, the beep 177 with its fill, SN_LOADX 58, SN_STARTX/SMDROP/SMRUN 112, SN_HOLDX 58, SN_GOX 49,
SN_SENDX 37, SN_WAVEX 116, SN_DOCHOOKX 61, SIDSTOP 25, RAMEND 46, SMTIME 15; from the label map). About 1,300 come:
the record engine and the reply wait (~350), NEED's stream changes (~150), 21 new operations table-driven (an entry
of command, parameter and result counts, claim; ~480), the transfer engine (~120), the frame's status (~80),
SN_FIRMWARE (~80), the warm and cold boot (~60). **Net about +300: block 61 about 1,100 free.** Stop line: if
block 61 falls under 512 free, stop and report (risk 1).

### 5.3 The BIOS entry points ###

- **`$C818` `$A2` BEEP** and **`$A3` STOPALL** (question 8 for BEEP's argument). Each: no card (ID not `$A3`),
  carry set at once; else READY awaited (250 ms), the record sent, its REPLY awaited (100 ms), carry clear; a
  time-out, carry set. A, X, Y and the windows as the existing entries keep them.
- **QUIET** (Break, BRK, the demo's end, START): CTRL's RESET, one write, no wait (question 7).
- **The splash:** BOOTSOUND at the vertical blank frame 0 begins on, after READY (250 ms at most, else silent).

### 5.4 Function 82 and OOLIB, with versions ###

**Kept** (numbers, blocks and results): 0 SN_INFO (A the revision 3, X FEATURES, was CODECS: a changed meaning,
decided), 1 SN_CLAIM, 2 SN_RELEASE (SC_TUNE `$01` was SC_6510; SC_STREAM `$02`; SC_MUSIC `$08` new; SC_DOC `$04`
refused), 5 SN_STOP (TUNE_STOP), 7-13 the stream (stream 0), 16 SN_BEEP (on the card; blocking per question 6;
refused with no revision 3 card), 19 SN_VOLUME.

**New** (spec :522-545), numbered from 20 per question 4: SN_TUNE, SN_PLAY, SN_TPAUSE, SN_TRESUME, SN_TSTATUS,
SN_TINFO, SN_SAMPLE, SN_EFFECTS, SN_EFFECT, SN_ESTOP, SN_MUSIC, SN_MCTL, SN_MSTATUS, SN_NEXT, SN_EQ, SN_VIS,
SN_SIDREGS, SN_LEVELS, SN_MIX, SN_IDENTIFY, SN_FIRMWARE (21 operations: 20-40; `SN_OPS` 41).

**The event area:** +96-107 as now; +108-115 reserved, 0; +116-119 the tune's state and ms played; +120-123 music's
state and notes waiting; +124-127 reserved.

**OOLIB:** `PB_MUSIC` (source 3), `MU_BLOCK` new; `PB_DOC` refused (`PE_REFUSED`); MU_* rebuilt on blocks
(question 9); SID_PLAN per question 10.

**Versions:** OODOS 0.17 (`SYS_MINOR`, system.inc:88), OOLIB interface 18 (sys.inc:156), ROM BIOS 0.21
(diag.inc:37), raised once, in unit S23.

### 5.5 What is removed, named for the owner under AGENTS.md's interface rule ###

Removed by the owner's decision before 1.0 (spec :495-509, decided), listed here so the merge names them:

- **Function 82:** SN_LOAD (3), SN_START (4), SN_SEND (6), SN_WAVE (14), SN_DOCHOOK (15), SN_HOLD (17), SN_GO
  (18): refused, A = X = `$FF` (question 5 on reuse). SC_DOC (`$04`): refused. SN_BUSY's "mailbox" sense.
- **Changed meaning:** SN_INFO's X (CODECS to FEATURES); SC_6510's bit 0 becomes SC_TUNE; SN_STATUS's +3-4 (the
  FIFO's room to the ring's); a claim no longer lets a program write the card's registers.
- **The event area:** EV_SMBOX, EV_SMMS, EV_SMCNT (+108-114), EV_SFLAGS and ES_NOQUEUE (+115); EK_SOUND's
  mailbox-byte meaning.
- **OOLIB:** the music driver (`mudrv/`) and mu.s's loading of it; sid.s's stub (SID_PLAN's planning, SID_STUB,
  SID_STUBLEN); SE_NOPAGE, SE_VECTORS, SE_CLAIM's "the 6510" sense, SE_RSID (RSIDs now play); PB_DOC; pb.inc's
  register equates.
- **The ROM:** the two splash players and the WAVE blocks; card6510; stage 10's S2-S8; the revision 1/2 equates
  (question 11).
- **AGENTS.md:31 and `06-program-formats.md:116`:** "the sound card's 6510" becomes "the sound card: drive it through
  function 82 only".

---

## 6. The callers rebuilt ##

- **GW-BASIC** (branch C, spec :592-617): SOUND f,d one entry (frequency × 16; d × 54,925 µs in 32 bits); PLAY parses
  its string into one block in P7's own RAM (up to 255 notes, about 3 KB) and sends it with SN_MUSIC (queued for
  MB; MF waits for MUSIC_DONE); PLAY(n) from SN_MSTATUS; ON PLAY(n) by SN_MCTL's threshold and MUSIC_COUNT; BEEP
  and ^G through SN_BEEP; the tone generator the only voice. P7 stops linking mu.s and mutone.s (physof stays). The
  32-note background limit kept by GW-BASIC (the manual's). In branch B, 7f's code keeps working on the rebuilt
  MU_* calls (MU_TONE sends a one-entry block), so main is never without GW-BASIC's sound between B and C.
- **PLAYSID** (S21): SID_PARSE, then SN_TUNE and SN_PLAY; RSIDs play; it prints the name, author and released
  fields and the song; a key stops it; the tune's status (ms played) shown as it plays.
- **SYSINFO** (S21): its SOUND section on SN_IDENTIFY: the card's revision and firmware version, the store (MEMSIZE),
  the features, the SIDs' models if the card says. The CARDS row unchanged (`$A3`, hwname). SYSINFO reads no sound
  register; AGENTS.md's SYSINFO exception doesn't change.
- **The diagnostics** (S19): stage 10's S1 ID `$A3` and READY; S2 IDENTIFY (version, MEMSIZE, FEATURES); S3
  SELFTEST, a FAIL naming each bit (the store, wave RAM, the SIDs, the codec's loop); S4 channel 2 into DATA, 4 KB of
  a pattern, then CHECKSUM against the host's CRC-32; S5 the event path and its interrupt (a NOP's REPLY with IEN's
  EVT set: IRQH bit 2, then clear); S6 a short raw PCM stream's frames at its rate (today's S9's check). S10 plays a
  SID-voice music block and a sample effect. QUIETM as QUIET.
- **OOUI:** nothing. **OO-Amp:** section 2.8, its own step.
- **The WebUI** (S10, S20): Play .sid hands the file to the firmware model's tune player on a revision 3 machine;
  the tune bar's "ended" from TUNE_STATUS; the debugger pane shows the device's registers, the FIFOs' levels and the
  firmware's objects, voices and streams; `soundCpu` and `soundVoice` from the model's 6510 and SID shadows (as a
  debugger may; nothing on the bus can); `e2e/sound.spec.ts` types a BEEP record and a short music block at WOZMON
  into `$C503` and reads the events back from `$C505`.

---

## 7. The units ##

**Branches (question 1):** three, and the release.

- **A, `feat/sound3-model`:** step 1's measurements and step 2 (S0-S11). Emulator only; the machine's default
  stays revision 2, so nothing else changes behaviour.
- **B, `feat/sound3-system`:** steps 3, 4 and 5 and the default's move (S12-S25). OODOS, OOLIB, the ROM, the
  utilities and the books land together, because each half alone leaves main with a BIOS, an OODOS and a card that
  don't match (OODOS 0.17 drives revision 3 only; BIOS 0.20 writes revision 2's registers).
- **C, `feat/gwbasic-sound3`:** step 6 (S26-S28).
- **The release (step 7):** the cards and the site, staged by the SDLC skill, the full tier first.

### Branch A: the model ###

**S0. The baselines (step 1's measurements, the corpus recorder).**
- *Builds:* in `test_sid_corpus.cpp`, `SID_RECORD=path`: per tune, the hash of the 6510's SID writes (address,
  value, SID clock from the 6510's reset), each SID chip's output samples before the mix, the final mix, and the
  play-call count, written to a file outside the repo (it names the owner's tunes). New measurement cases, printed
  only: `the_music_driver_s_share_of_the_6510` (test_mudrv, six notes, PC sampling as L6a measured), and in
  test_oodos_sound `the_stream_s_cost_to_the_host` (cycles a 4 KB piece and a second at 44.1 kHz) and
  `the_es5503_hook_s_cost`.
- *Then:* the full sweep on this commit (8 shards, `SID_COUNT=0`, fixtures linked as one directory), the baseline
  kept outside the repo.
- *Tests:* the recorder's own case on the synthetic tunes (no fixtures): two runs give one hash; a changed write
  changes it. Mutant: the cycle left out of the hash (two tunes differing only in timing hash alike).
- *Gate:* test_sid_corpus, test_mudrv, test_oodos_sound. *Expectation changes:* none.

**S1. The interface details.**
- *Builds:* an appendix to `hw/future/sound-device.md`, "Interface details (proposed for S1)": every layout of flag 3,
  as section 5.1 proposes. No code.
- *Gate:* docs. *Approval:* question 3.

**S2. The firmware's core (`sound_fw`, `SoundFwPort`).**
- *Builds:* the parser (records, n checked per command, E_UNKNOWN, E_PARAM), replies in order with room reserved,
  the event FIFO and EVLOST/LOSTCLR, EVENTS' mask, NOP, IDENTIFY, TIME, the store and its allocator, the object
  slots, CHECKSUM, transfers (XFER, XLEFT, XFERDONE, XFERABORT, E_XFER), STOPALL, FREEALL. A fake port for tests.
- *Tests* (`test_sound_fw`, new; labels `emu sound`; a map rule `*sound_fw*` → `sound backplane web`):
  - `every_command_gets_one_reply_in_order` (mutant: the reply of a command that waits for a transfer sent first);
  - `a_full_event_fifo_loses_events_never_replies` (an event flood, then 20 commands: every reply comes; EVLOST
    and LOSTCLR's count right). Guards a real card whose event FIFO overflows under a chatty stream. Mutant: no room
    reserved for replies;
  - `a_record_with_the_wrong_n_is_refused_at_the_door` (ERROR, STATUS's ERR; the next record parses). Mutant: the
    parser resyncs on the wrong byte;
  - `the_store_refuses_what_doesn_t_fit_at_each_size` (2, 4, 8 MB). Mutant: MEMSIZE's units read as KB;
  - `checksum_is_crc_32_of_what_came` (against a table computed in the test). Mutant: the last byte left out.
- *Gate:* sound, backplane, web. *Expectation changes:* none.

**S3. The front end (`sound_card3`), the machine file.**
- *Builds:* the registers; CMD/EVT FIFOs; DATA's landing buffer; DRQ; the interrupt (ISR/IEN, EVT a level, XDONE,
  CMDROOM, READY); CTRL's DRQ, FWRESTART, RESET; /RESET's rules (spec :469-474); READY after the start-up constant;
  `rev=` and `memsize=` on the sound line; `Machine::sound_device()`. The default stays revision 2.
- *Tests* (`test_sound_device`, new; labels `emu sound dma bridge`; map rule `*sound_card3*` is caught by
  `*sound_card*`):
  - `a_tune_s_bytes_go_in_by_channel_2_demand_dma_and_checksum` (DMA end to end; DRQ falls when the transfer
    ends). Mutant: revision 2's standing request (DRQ with no transfer open): the case's "nothing asks between
    transfers" fails;
  - `evt_is_a_level_and_no_event_is_lost_between_the_drain_and_the_clear` (an event arriving after the last read,
    before ISR's write: the line stays). Mutant: EVT as an edge cleared by the write;
  - `xleft_latches_all_three_bytes_on_the_low_read`. Mutant: no latch (a torn read across a byte carry);
  - `reset_abandons_a_transfer_and_empties_both_fifos_but_keeps_the_store` and `fwrestart_empties_the_store`;
  - `a_byte_to_data_with_no_transfer_is_dropped`; `cmdfree_and_cmdrdy_hold_a_whole_record`;
  - `the_machine_takes_rev_and_memsize` in test_machine (rev=4 and memsize=3 refused).
- *Gate:* sound, dma, bridge, backplane, machine (machine.cpp is `all`: the whole fast run). *Expectation changes:*
  test_machine's sound-line parse gains options (block A).

**S4. Tunes.**
- *Builds:* `sid_plan.{h,cpp}` split out of `sid_file.cpp` (no change of behaviour: test_sid_file unchanged);
  TUNE_LOAD (the header checked; XFERDONE with the songs, start song, clock, chips, PSID/RSID), TUNE_PLAY (song,
  clock override, length, fade), STOP, PAUSE (the 6510 held, its chips at volume 0, as SN_HOLD), RESUME, STATUS,
  INFO, FREE; the SID shadows and SIDREGS; the SID model per tune; TUNE_STARTED, TUNE_STALL (2 s; 10 s before the
  first), TUNE_JAM (E_JAM), TUNE_ENDED; the chips taken and given back; today's decode (flag 2).
- *Tests:* in test_sound_fw (with the real front end): `a_tune_plays_its_song_and_its_play_calls_are_counted`;
  `pause_holds_the_6510_s_cycles_and_silences_its_chips` (mutant: the 6510 left running at volume 0, its cycle count
  moves); `a_stalled_tune_says_so_after_2_s` (a tune whose play never comes); `a_jam_stops_the_tune_with_e_jam`;
  `the_length_and_fade_end_it_once` (one TUNE_ENDED); `a_tune_takes_whole_chips_and_gives_them_back`.
  **test_sid_corpus's device mode:** `SID_DEVICE=1` plays each tune of the sample by TUNE_LOAD and TUNE_PLAY records
  and judges it by the same `passes()` rule; with `SID_COMPARE=path`, it compares with S0's hashes (question 2's
  meaning of identical). By default the sample of 12, both modes.
- *Gate:* sid, sound, backplane, web. *Expectation changes:* none in old cases.

**S5. The digital path: the ADC, the mix, the EQ, the vis page, the beep, the bell, the boot sound.**
- *Builds:* the SIDs' samples through the ADC path (its level and resampling the firmware's); the mixer by source
  (tunes, music, effects, streams, the beep) with MIX; VOLUME; EQ, EQ_ALL, EQ_ON (revision 2's `sound_dsp`); VIS
  (98 bytes, revision 2's layout), LEVELS; the tone generator (band-limited square: BEEP, BELL), BEEP_DONE;
  BOOTSOUND from `sound_flash.h` (made by `tools/mksndflash.py` from `splash/wave.bin` and `doctables.inc`).
- *Tests:* `the_beep_is_at_its_pitch_within_0_1_percent_over_a_tune_and_a_stream` (the tap's zero crossings);
  mutant: the beep on a SID voice (the tune's SID writes change). `a_mono_tune_is_heard_in_both_ears`.
  `the_eq_and_vis_page_cover_the_sids` (a SID tone shows in the vis page's bands). `each_source_s_output_is_the_same_at_rate_0_and_48000`.
  `the_boot_sound_plays_from_the_card_s_flash` (its sequence's first notes on the right oscillators).
- *Gate:* sound, backplane, web. *Expectation changes:* none.

**S6. Streams (`test_sound_stream3`, new).**
- *Builds:* STREAM_OPEN/DATA/END/NEXT/PAUSE/RESUME/FLUSH/STATUS/CLOSE; rings; NEED (one outstanding, urgent below
  1 s); prebuffer; UNDERRUN/UNDERRUN_END; gapless joins; the MP3 trim from the encoder's header.
- *Tests:* `a_stream_bigger_than_the_store_plays_with_no_underrun` (60 s of 44.1 kHz stereo, 10.6 MB, on 8 MB, fed on
  NEED; the tap equals the file); mutant: a NEED sent while one is outstanding (the feeder sends twice, E_MEMORY).
  `two_streams_queued_are_one_stream_at_the_tap` (raw PCM, frame for frame); mutant: one frame of silence at the
  join. `an_mp3_pair_with_gapless_headers_is_trimmed` (an encoder's pair made by `make_mp3_fixtures.sh`, kept in
  `test/host/mp3/`). `underruns_counted_as_status_says_and_sound_back_at_the_next_frame_due` (mutant: a new
  prebuffer after an underrun). `the_same_at_rate_0_and_48000`.
- *Gate:* sound, backplane, web. *Expectation changes:* none.

**S7. The sound bank (`test_sound_bank`, new).**
- *Builds:* SAMPLE_LOAD (signed/unsigned, the `$00` rule), EFFECT_DEF, EFFECT_LOAD, PLAY (voice handles), the voice
  manager (free voice first; else the lowest priority, oldest, at or below; never a tune's chip or music's voice),
  VOICE_STOP, BANK_FREE; VOICE_DONE, VOICE_STOLEN; wave RAM as a cache of the store.
- *Tests:* `a_tune_s_writes_are_the_same_with_effects_on_sid_b_and_the_es5503` (the spec's sharing acceptance);
  mutant: an effect allowed onto a tune's chip. `stealing_takes_the_lowest_priority_oldest_voice` (mutant: the
  newest). `a_bank_bigger_than_wave_ram_plays_each_sample` (300 KB of samples).
- *Gate:* sound, es5503, sid, backplane, web.

**S8. Music (`test_sound_music`, new).**
- *Builds:* the block parser (S1's layout), MUSIC_LOAD/PLAY (queue, free when done)/STOP/PAUSE/RESUME/STATUS/THRESH;
  the sequencer on SID voices, ES5503 voices and the tone generator; MUSIC_MARK, MUSIC_COUNT, MUSIC_DONE.
- *Tests:* `ten_thousand_notes_in_one_block_end_with_one_done` (mutant: a done each note);
  `notes_waiting_are_exact_and_the_threshold_fires_once_below_n` (mutant: fired at n, not below);
  `a_3600_s_note_is_one_entry` (32-bit µs; mutant: the length cut to 16 bits); `music_is_refused_e_busy_when_a_tune_holds_its_chips`.
- *Gate:* sound, sid, es5503, backplane, web.

**S9. Self-test and the firmware update.**
- *Builds:* SELFTEST (each bit's result, with fault hooks for tests), FW_BEGIN/FW_COMMIT/FWRESTART/FWVER, the
  slots; `tools/mksndfw.py` (a map rule: `tools/mksndfw.py` → `sound`).
- *Tests* (test_sound_device): `a_good_image_boots_after_fwrestart_and_says_its_version`; `a_bad_crc_or_another_card_s_image_leaves_the_old_one`
  (mutant: the CRC not checked); `selftest_names_the_part_that_fails`.
- *Gate:* sound, backplane, web, tools.

**S10. The session, the WASM API, the page, sidcheck.**
- *Builds:* the session's Play .sid on a revision 3 machine (the firmware model's player, outside the bus; on
  revision 2 as today); `soundRunning`, `soundCpu`, `soundVoice` for both; the debugger pane's revision 3 view
  (`devices.ts`), its vitest; `apps/sidcheck` on `sid_plan`.
- *Tests:* test_session gains `a_sid_tune_plays_on_a_revision_3_card_changes_song_stops_and_goes_with_a_power_cycle`
  (the :353 case's twin); vitest for the pane. Mutant: `soundRunning` left on CTRL's RUN (false on revision 3 while a
  tune plays: the tune bar ends it at once).
- *Gate:* web (vitest, the page build, e2e), machine (session is `all`).

**S11. Branch A's gate.**
- The corpus: the full device-mode sweep against S0's baseline: every tune identical in question 2's sense; the
  known failures the same 148. The counts (played, own interrupt, BASIC) printed beside S0's.
- `tools/check.sh changed main` (it runs the emu suites under the sanitizers: the C++ changed); `make -C docs check`.
- Review round, triage, merge, main gated.

### Branch B: the system ###

**S12. Equates.**
- *Builds:* backplane.inc's revision 2 names renamed `SOUND2_*` mechanically in every user (ROM, OODOS, OOLIB,
  oohw); then revision 3's `SOUND_*` added (`SOUND_ID_V3`, CTRL to ID, the commands, the event kinds, the errors);
  Technical Reference register entries for the new equates (short, from the spec's table) and registers.json, so
  the docs check holds them from the start.
- *Tests:* the ROM image and OODOS.SYS byte-identical before and after the rename (compared in the commit's check).
- *Gate:* bios, oodos, docs, web (backplane.inc chooses them). *Expectation changes:* none.

**S13. OODOS's driver core.**
- *Builds:* section 5.2's record sender, reply wait, handler and event decoding; SNDINIT, SNDQUIET; SIENSET's new
  rule; operations 0, 1, 2, 5, 16, 19; the removed numbers refused; the event area's +108-127; the rig's
  `sound_revision` (as its `video_revision`, oodos_rig.h:285), set to 3 for the sound suites.
- *Tests* (test_oodos_sound, rewritten): `info_claims_and_the_retired_operations_refused` (3, 4, 6, 14, 15, 17, 18
  give A = X = `$FF`; SC_DOC refused); `a_program_s_record_and_the_handler_s_never_mix` (a stream's NEED lands while
  a program's IDENTIFY is being written; both get their replies); mutant: the sender without SEI (the records
  interleave: E_PARAM). `a_card_that_never_answers_is_sn_busy_in_100_ms` (a fault hook: the firmware stops); mutant:
  no time-out (the call hangs; the case's 1 s bound fails). `the_cold_boot_clears_a_half_record_left_by_the_rom`
  (a stray `$00` in CMD before boot; OODOS's first record parses); mutant: no RESET strobe. The warm boot cases
  (by WBOOT, ^C and Break: STOPALL, FREEALL, volumes 255, EQ flat, claims released), rewritten.
  `sn_beep_lasts_its_length_at_its_pitch_on_the_card` (the tap; mutant: SN_BEEP returning at the REPLY, if question
  6 keeps it blocking).
- *Gate:* oodos, sound, bios, docs.

**S14. OODOS's stream on NEED.**
- *Builds:* SN_STREAM to SN_STATUS on STREAM_*; buffers sent on NEED; EV_SBUFS as before; SN_NEXT; the frame's
  status requests; SFIELDS from replies.
- *Tests:* `acceptance_44_1_khz_16_bit_stereo_raw_pcm_from_b_with_no_underrun` and
  `acceptance_an_mp3_from_b_plays_as_minimp3_decodes_it`, kept (their numbers the same); new
  `a_file_bigger_than_the_store_streams_from_b_with_no_underrun` (the spec's acceptance, from an OODOS program);
  `two_files_queued_by_sn_next_join_with_no_gap`. `a_stream_s_buffers_chain_across_terminal_counts_with_no_gap`
  rewritten (across NEEDs). test_oodos_load :342 unchanged. test_oodos_video :840 reads SN_STATUS.
- *Gate:* oodos, sound, dma, video, docs.

**S15. OODOS's new operations.**
- *Builds:* operations 20-40 (question 4), the transfer engine, SC_TUNE/SC_MUSIC, SN_FIRMWARE (refused while any part
  is claimed). `OOLIB_IFACE` 18 (with the version bump of S23; the pins move there).
- *Tests:* one case a group: tunes (load, play, pause, status, info), bank (sample, effects, play, stop), music
  (block, control, status), EQ/VIS/SIDREGS/LEVELS/MIX, IDENTIFY, firmware (a good image, a bad one, refused while
  claimed); each address checked as function 51's segment (mutant: a block past memory accepted).
- *Gate:* oodos, sound, docs.

**S16. The console's bell.** crt.s's BEL sends BELL (no wait for the sound to end). *Test:* test_oodos_sound
`a_bel_rings_the_card_s_bell`. Mutant: BEL dropped. *Gate:* oodos.

**S17. OOLIB.**
- *Builds:* sid.s, sidv.s, pb*.s, mu.s, munote.s/mutone.s as section 2.4 says; `mudrv/` removed (its map rule and
  the Makefile's lib rule with it); `pb.inc`'s equates removed; PB_MUSIC, MU_BLOCK. The test programs pbtest, mutest,
  sidovl/sidplug rebuilt.
- *Tests:* test_oodos_pb (the stall rule on +116-119; PB_LEVELS for a SID now measured; PB_DOC refused), test_oodos_mu
  (MU_* on blocks; a note's count exact from the card), test_oodos_playsid (RSIDs play now; the overlay cases kept).
  test_gwbasic_sound still passes, but for the cases block E names.
- *Gate:* oodos, gwbasic, gwlocale, sound, docs.

**S18. The ROM.**
- *Builds:* QUIET's CTRL RESET (and QUIETM); a record sender in the ROM (one copy in block 31, ~70 bytes); `$C818`
  `$A2` and `$A3`; the splash's BOOTSOUND; the players, `splash/bootsnd.*`, `docsnd.*`, `wave.bin` and WAVE removed
  (the tables kept for `mksndflash.py`, under `emulator/` or `tools/`); hwname's `SOUND_ID_V3`. The ROM drives
  revision 3 only (decided Q12): on `$A1`/`$A2` QUIET does nothing to the card and the splash is silent.
- *Tests* (test_bios, on explicit `rev=3` machines until S20): `break_stops_every_sound_on_the_card` (a tune and a
  stream playing; after Break, STOPALL's effect: nothing at the tap); mutant: QUIET's write removed.
  `the_splash_sends_bootsound_at_frame_0_s_blank`; mutant: sent before the blank. `c818_beep_and_stopall`
  (carry clear with a card; carry set with none, and with a card that never comes READY, inside 250 ms + 100 ms);
  mutant: no time-out. test_bios_sd :947's quiet check on revision 3.
- *Gate:* bios, docs, web, sd.

**S19. The diagnostics.**
- *Builds:* stage 10 as section 6 says; card6510 removed; S10 by command; `fault_devices.h` gains the card's faults:
  `never_ready`, `selftest_bit`, `dma_drops_a_byte`, `irq_dead`.
- *Tests* (test_bios_diag): `stage_10_passes_on_revision_3` (gate share); `stage_10_names_a_selftest_failure`,
  `stage_10_s_checksum_catches_a_dropped_byte`, `stage_10_s_event_path_catches_a_dead_interrupt`,
  `a_card_that_never_comes_ready_fails_s1_and_skips_the_rest` (slow share). Mutant each: the compare removed.
  `no_sound_card_skips_stage_10...` kept. The interactive case's S10 answers on revision 3.
- *Gate:* bios, diagslow, docs, web. Measure block 22 (682 free today).

**S20. The default becomes revision 3.**
- *Builds:* `backplane.machine:121` `rev=3 memsize=8`; its comment; the rig's default follows; the page runs
  revision 3.
- *Tests:* every case that checks `$A2` or revision 2's registers on the default machine reviewed one at a time:
  takes `$A3`, or is pinned to `rev=2` where it is about revision 2 (test_backplane :458 takes `$A3`; test_session
  :309 and :394 on revision 3; the revision 2 cases of test_bios pinned). Web: `wasm-port.test.ts:91`/:94 (no 6510
  tune at boot: the boot sound is the firmware's), `sid-bar.test.ts`, `e2e/sound.spec.ts` rewritten (bare metal at
  WOZMON).
- *Gate:* `all` (backplane.machine): the whole fast run, the diagnostics' slow share, web.

**S21. PLAYSID, SYSINFO, SNDFW.COM.** As section 6. *Tests:* test_oodos_playsid's output cases; test_oodos_sysinfo
and its goldens; a new `test_oodos_sndfw` (or cases in test_oodos_sound) updating the model's slot from M:. *Gate:*
oodos, sound, docs.

**S22. AGENTS.md** (section 5.5's line; the owner's approval of the text). *Gate:* docs.

**S23. Versions:** OODOS 0.17, OOLIB 18, BIOS 0.21; the pins of block D; the cards built (demo.sparse's stamps).
*Gate:* oodos, bios, docs, web.

**S24. The books and the record** (section 2.9): chapter 7 rewritten, the others' notes, both indexes; hw/design.md
"Sound card revision 3" with the decisions; hw/oolib.md, hw/oodos.md, hw/diag.md, hw/testing.md;
`hw/future/sound-device.md` removed once its text is home (as nb3's question 7). *Gate:* docs.

**S25. Branch B's gate.** The measurement records of section 7 compared with section 1; `make sizes` and the ROM's
map written into hw/oodos.md and the merge message; `tools/check.sh changed main`, `make -C docs check`; review,
triage, merge, main gated.

### Branch C: GW-BASIC ###

**S26. SOUND and BEEP on the tone generator.** SOUND f,d as one entry; SOUND 0 stops the queue; BEEP and ^G by
SN_BEEP. *Tests* (test_gwbasic_sound): `sound_plays_its_tone_on_the_tone_generator` (the tap's pitch; was
`_on_the_sid`), `sound_s_ranges_by_hand` (f past 3,848 Hz plays; d to 65,535 ticks is one entry: the 2 min 44 s cap
gone), `beep_and_ctrl_g_sound_the_bell`. Mutant: d's µs cut to 16 bits.

**S27. PLAY, PLAY(n), ON PLAY(n) on blocks.** One block a statement, in P7's RAM; MF and MB; PLAY(n) exact;
ON PLAY by MUSIC_COUNT. P7 stops linking mu.s and mutone.s. *Tests:* the play_* cases, `play_mb_runs_in_the_background_and_play_n_counts`,
`on_play_fires_when_the_buffer_falls_to_n_minus_1` (kept as written: the manual's behaviour), `p7_s_driver_image_loads_from_its_block`
retired, `run_and_system_free_the_6510` renamed `..._free_the_music`. Mutant: the threshold set to n.

**S28. Branch C's gate:** P7's free bytes and BASIC's free bytes; the PLAY cost record (section 7); hw/oodos.md's 7f
limits (C7, C9) closed; `tools/check.sh changed main`; review, merge.

### The release (step 7) ###

The full tier; the catalog's cards rebuilt on OODOS 0.17 (question 15); the site's emulator deployed with revision 3
by default and a release note; staged by the SDLC skill.

### The expectation changes, as blocks for approval ###

- **A, the emulator and the page:** test_machine's sound-line parse; test_backplane :458 (`$A3`); test_session :309,
  :394 on revision 3, :353's revision 3 twin; `wasm-port.test.ts:91`, :94; `sid-bar.test.ts:65`, :71;
  `devices.test.ts`, `pane.test.ts`; `e2e/sound.spec.ts` rewritten; the corpus's final-mix hash re-based (question 2).
- **B, the removed interfaces' tests:** test_mudrv (14, removed); in test_oodos_sound the 17 cases of the raw
  operations, the mailbox and its stamp, the ES5503 hook and revision 1 (:322, :485, :526, :551, :774, :910, :939,
  :1026, :1043, :1064, :1124, :1231, :1272, :1298, :1316, :1344, :1367: `the_card_s_ram_is_loaded_by_dma...` through
  `with_a_layout_1_area_nothing_is_stamped`) retired, each listed in the commit; test_oodos_pb's stamp,
  `ES_NOQUEUE` and ES5503 cases; test_oodos_mu and test_oodos_playsid rewritten; test_oodos_video :840.
- **C, the ROM:** test_bios :585, :671, :700, :845, :872, :894, :1946 rewritten for revision 3; test_bios_sd :947;
  test_bios_diag :1092, :1265, :1277, :1334, :1427, :1563 and `golden/romdiag.txt` (stage 10's lines).
- **D, versions and goldens:** the interface pins (section 2.10's list) to 18 and 0.17; the BIOS's version strings;
  `golden/sysinfo*.txt`'s SOUND line and section; PLAYSID's printout.
- **E, GW-BASIC:** in branch B, `sound_plays_its_tone_on_the_sid` (it watches a SID voice: now the music block's
  voice), `p7_s_driver_image_loads_from_its_block` (no image); in branch C the cases S26-S27 name.

---

## 8. Measurement tests that will move ##

Each is a record or a bound, not a functional guard. Each is re-measured once and brought to the owner once, plainly.
No fix loops.

1. **`play_s_note_cost_is_timed`** (test_gwbasic_sound :809). *What it is:* the cycles a PLAY note costs in MB, the
   interrupts left out; gated at 9,000 and floored at 1,000. *What it records:* 8,243 now. *What moves it:* branch C
   sends one block a statement instead of a function 82 call a note, so the cost a note falls, perhaps under the
   floor. *What we'll do:* print it at S27; if it leaves 1,000-9,000, bring the figure and a new bound to the owner
   once. The functional guards are the play_* cases.
2. **`the_tick_costs_its_measured_cycles`** (test_oodos_input :650, :657, exact 142 and 188). *What it is:* the
   tick's cycles, pinned exactly; they move with code placement in common memory (nb3's N16). *What moves it:*
   nothing planned changes common memory's size (the frame's test is the same bytes), so it should not move. If it
   does, it is a placement shift: bring it once, as nb3 did.
3. **The full budget** (test_oodos_video :818, line 524). *What it is:* a bound on where a frame's copies end with a
   stream playing. *What moves it:* the frame's status requests (flag 12) add a command and a reply drain per frame.
   *What we'll do:* measure at S14; a crossing is a finding for the owner (the alternative is fewer requests), not
   a re-pin.
4. **The diagnostics' quick run** (test_bios_diag :330, 30-75 s). *What moves it:* stage 10 loses its 64 KB and 128 KB
   RAM walks. It should get shorter; measured at S19.
5. **The books' "what sound costs"** (08-game-services.md, from :635) and S0's records (the driver's 6510 share,
   the stream's and the hook's host cost). Records: re-measured on the device at S25 and written into the book.
6. **`the_step_leaves_the_6510_time_for_the_mailbox`** (test_mudrv) goes with the driver; S0 keeps its figure.

---

## 9. Free space: today, and the plan's effect ##

| Area | Today | After | Why |
|---|---|---|---|
| Block 60 (BDOS) | 911 | 911 | nothing in it changes |
| Block 61 (BIOS) | 1,396 | **~1,100** (estimate; measured at S13-S16) | sound.s about +300 (section 5.2); crt.s's BEL +~15. Stop under 512 |
| Block 62's window | 85 | 85 | IRQOTH, WBQUIET, cold.s unchanged |
| Common memory | 47 | 47 | the frame's test, DEAFEN, CLKOFF: the same bytes |
| SYSBSS | 49 | **~60** | HTSAVE (16), SDCTL/SDAL/SDBANK (4) and SHELD/SVOLA/SVOLB (3) go; the tags, the transfer's and the wait's state (~12) come. Fallback: sound.s's own variables in block 61's RAM |
| Zero page | 0 | 0 | none needed |
| ROM block 31 | 5,007 | **~8,300** | the players (3,431) and the WAVE copy loop go; QUIET shrinks to one write; the sender, BEEP and STOPALL (~150) come |
| Flash blocks 24-28 | 0 | **81,920** | the samples move into the card's flash |
| ROM block 22 | 682 | **~2,200** | stage 10 (2,304) and card6510 (345) go; the new stage 10 (~1,100) comes |
| D22ZP / D22BSS | 67 / 4 | ≥ 67 / ≥ 4 | the new stage keeps its state in D22ZP |
| ROM block 21 | 7,891 | ~7,950 | S10's tones by command |
| GW-BASIC P7 | 6,544 | after B ~7,400; after C **~5,000** | B: the driver's image (939) goes; C: mu.s and mutone.s (~1,050) go, the block builder (~300) and PLAY's block buffer (~3 KB) come |
| GW-BASIC P4/P5/P6/P8 | 1,265 / 976 / 623 / 4,453 | the same | nothing there changes |
| Free for BASIC | 34,772 | 34,772 | the resident part doesn't change |

---

## 10. Risks ##

1. **Block 61's room.** The estimate (+300) could be off by a few hundred. The table-driven operations keep it
   small. Stop line 512; the fallback is to move SN_FIRMWARE and the less used operations' table into block 60 (911
   free) behind the BDOS's dispatch.
2. **Records interleaving.** Any CMD write outside the sender corrupts both records (flag 9). The review names every
   CMD write; S13's case guards it.
3. **The reply wait with EVT's interrupt off.** A stream's NEED waits for the call to end; the call's wait is the
   card's latency (tens of µs), its worst the 100 ms time-out. The reply wait passes events on as it meets them.
4. **The corpus sweeps cost hours** (twice: S0 and S11), with the fixtures linked as one directory (per-file links
   break the known failures). Detached, waited on the log's `exit` line, never a `pgrep -f` loop.
5. **Determinism.** Every feature is checked at audio rate 0 and 48,000 and under odd tick sizes; the SIDs' ADC path
   is new float arithmetic, built `-ffp-contract=off` as `sound_card.cpp` is (devices/CMakeLists.txt:14).
6. **The store's memory.** 8 MB a card in every test machine and in the page's WASM heap: check the WASM build's
   memory limit at S3; tests that don't need it can take `memsize=2`.
7. **GW-BASIC between B and C** runs 7f's code on the rebuilt MU_ calls. Its tests must pass in B except block E's.
8. **OO-Amp breaks on main after B** (ES_NOQUEUE, EV_SMMS, SID_PLAN) until its port, a step for the owner.
9. **Old cards on the site.** A card with OODOS 0.16 or older on a revision 3 card writes revision 2's registers
   (SIDSTOP's loop puts bytes in CMD): no sound, harmless otherwise; its SN_BEEP still uses the USB speaker
   (question 15).
10. **The boot sound** is re-expressed (question 12); a listener may hear a difference in its timing.
11. **The MP3 trim** needs a test pair with an encoder's gapless header; the fixture script must make one (S6).
12. **Measurement records** (section 8) may cross their bounds; each goes to the owner once.
13. **The firmware update in the emulator** tests only the path, never a behaviour change (section 4.7).

---

## 11. Where the code goes beyond the spec, and how the plan handles it ##

1. **Separate classes** (`sound_card3`, `sound_fw`, `sid_plan`, `sound_flash.h`) instead of revision 3 inside
   `sound_card.{h,cpp}`, and `Machine::sound_device()` beside `sound_card()`. Handled: question 13 (the deviations
   as a block).
2. **The port interface** between the firmware and the chips: needed for Q2's "no emulator dependency". Recorded in
   the spec's appendix (S1).
3. **The layouts the spec leaves open** (flag 3): S1, question 3.
4. **The reply wait and handler tags** (flags 8, 9): recorded in hw/oodos.md; the effect is the spec's.
5. **The frame's status requests** (flag 12): recorded; measured by the full-budget gate.
6. **CTRL's RESET at the cold boot and as QUIET** (flag 7): question 7.
7. **Operation numbers from 20** (flag 4): question 4.
8. **SN_BEEP blocking** (flag 5): question 6.
9. **The BIOS BEEP's argument** (flag 6): question 8.
10. **No monitor bell to change** (flag 10): recorded.
11. **test_sound_driver kept** (flag 11): question 13.
12. **The revision 1/2 equates removed** (flag 14): question 11.
13. **`rev=` and `memsize=`** on the sound line (flag 13); the start-up and command latencies as constants with test
    hooks (section 4.3). Recorded.
14. **The firmware image's header and `mksndfw.py`; the slots in the card object only** (section 4.7). S1, recorded.
15. **SID_PLAN kept as a thin call** (question 10) and **MU_FLUSH not added** (question 9).
16. **The corpus acceptance's meaning, and today's 6510 decode kept** (flags 1, 2): question 2.
17. **The boot sound re-expressed** (section 4.8): question 12.
18. **The sid_file split** (`sid_plan`): recorded.
19. **The rig's `sound_revision`** (S13), as its `video_revision`: recorded.

---

## 12. Questions for the owner ##

1. **Branches.** *Recommendation: three: A the model (step 2, with step 1's measurements), B the system (steps 3,
   4 and 5 and the default's move, one merge, one version bump), C GW-BASIC (step 6); then the release.* B holds
   three steps because each alone leaves main with a BIOS, OODOS and card that don't match. *Alternative:* one
   branch a step, as the spec's steps say, with main running OODOS 0.17 on a revision 2 default machine (no sound)
   between steps 3 and 4.
2. **What "plays identically" means for the corpus.** *Recommendation: identical means, for every tune, the 6510's
   SID writes (address, value, cycle from its reset) and each SID chip's own output, against S0's baseline, with the
   same 148 known failures. The final mix's hash is re-based once, because the ADC path and a one-SID tune on SID A
   alone are decided changes. S4 keeps today's decode of the 6510's map (SID B at `$D420` or `$D500`); the sweep
   counts the tunes the spec's map (mirrors, a header-named second SID) would change, and that map comes as a later
   firmware step with its own baseline, as the spec allows for RSIDs.* *Alternative:* the spec's map now, and the
   tunes it changes accepted as a list.
3. **The interface details** (flag 3: event numbers, payloads, BOOTSOUND `$09`, the note block's bytes, the SID and
   sample effect formats, the replies' layouts, the firmware header). *Recommendation: approve S1's table as one
   block when it is written, section 5.1's proposals as its start.*
4. **The new operations' numbers.** *Recommendation: from 20 (SN_TUNE) to 40 (SN_FIRMWARE), since the noise removal
   freed 20.* *Alternative:* the spec's 21-41, with 20 refused.
5. **The removed operations' numbers** (3, 4, 6, 14, 15, 17, 18). *Recommendation: refused and never given to
   another operation, as the spec proposes: they shipped in OODOS 0.5-0.16 and are on the catalog's cards, unlike
   the noise operation.* *Alternative:* free, as the noise number was.
6. **SN_BEEP's wait.** *Recommendation: SN_BEEP keeps returning when the beep has ended (it waits for BEEP_DONE,
   interrupts on), so its meaning is kept and GW-BASIC's BEEP needs no change.* *Alternative:* return at the REPLY
   and let callers wait for the event (a changed meaning, under the interface rule).
7. **QUIET and the cold boot.** *Recommendation: QUIET writes CTRL's RESET (one strobe: every sound stopped, both
   FIFOs emptied, a half-written record dropped, no reply to wait for), and OODOS's cold boot strobes it before
   FREEALL; the `$C818` STOPALL entry sends the STOPALL record for programs.* *Alternative:* QUIET sends STOPALL as
   the spec says, after clearing the parser with the same strobe.
8. **The BIOS BEEP's argument.** `$C818` gives a function only A. *Recommendation: `$A2` BEEP plays 800 Hz (the PC's
   beep) for A × 10 ms (A = 0: 250 ms); any other pitch is the BEEP record itself, the card's documented interface.
   `$A3` STOPALL.* *Alternative:* A the low byte of a zero-page block's address holding pitch and length.
9. **MU_FLUSH.** *Recommendation: MU_NOTE and MU_TONE send at once as one-entry blocks, as today's calls play at
   once (7f relies on it); MU_BLOCK sends a block the program built; MU_FLUSH isn't added, since nothing gathers.*
   *Alternative:* MU_NOTE gathers and MU_FLUSH sends (7f's DONOTE and every MU_ caller then needs a flush).
10. **SID_PLAN.** *Recommendation: keep the name as a thin call that checks the song and sets SID_SONG (the planning
    is the card's), so PLAYSID's call and OO-Amp's link unchanged; SID_STUB and SID_STUBLEN read 0.*
    *Alternative:* remove it, as "retired" can be read.
11. **The revision 1 and 2 equates.** *Recommendation: remove them from backplane.inc at the end of branch B, with
    their 32 register entries; hw/sound-rev2.md and git are the record, and the emulator's tests use their own C++
    constants.* *Alternative:* keep them, and chapter 7 keeps a revision 2 section of entries.
12. **The boot sound.** *Recommendation: the same samples and sequence, played by the firmware's sequencer from the
    card's flash: heard the same, not sample-identical; test_bios's splash case checks BOOTSOUND at frame 0's blank
    and the sequence's start.* *Alternative:* keep docsnd's 6510 player inside the firmware with the ES5503 mapped
    for it (identical, but the ES5503 back in the 6510's map, against the spec).
13. **Approve section 11's deviations as one block** (the separate classes, the port, the reply wait and tags, the
    frame's status requests, the sound line's options, test_sound_driver kept). *Recommendation: yes; anything else
    found while building comes back as a finding.*
14. **Approve the expectation changes as blocks A-E** (section 7). *Recommendation: yes; anything outside them that
    changes output comes back.*
15. **Old cards on the site.** *Recommendation: rebuild every catalog card on OODOS 0.17 at the release.*
    *Alternative:* leave the older cards as drafts, silent on revision 3, with the release note saying so.
16. **OO-Amp's port.** *Recommendation: schedule it right after branch B merges (it breaks on OODOS 0.17: ES_NOQUEUE,
    EV_SMMS/EV_SMCNT, SID_PLAN), with section 2.8 as its brief.*

---

## Decisions ##

The owner's answers to section 12, 2026-11-23: "recommendations as listed, with the clean table".

1. **Branches:** three: A the emulator (S0-S11), B the system (S12-S25, steps 3-5 together), C GW-BASIC (S26-S28);
   then the release. (Branch A is built as `feat/sound-device-emu`.)
2. **"Plays identically"** means the 6510's SID writes plus each chip's own output; the final mix's hash is re-based
   once. Today's 6510 decode is kept for now, and the sweep counts the tunes the spec's map would change.
3. **S1's interface details** (event numbers, payloads, the note block's and the effects' layouts, the firmware
   header) are approved as one block; the builder brings S1's table to the owner before going past it.
4. **and 5. Revised by the owner:** function 82's operations are laid out afresh as **one clean table**, grouped by
   job. Removed numbers may be reused; there are no gaps and nothing is kept "refused". Before 1.0 nothing has
   shipped. The guard is OOLIB interface 18: every rebuilt caller (GW-BASIC's loader, `ui_load`, PLAYSID, SYSINFO,
   OO-Amp's port) checks it and refuses an older OODOS. The break is recorded here and in S22's AGENTS.md change
   (function 82 renumbered before 1.0 on the owner's word). This replaces section 5.4's numbering from 20 and
   section 5.5's "refused" numbers.
6. **SN_BEEP** keeps waiting until the beep ends.
7. **QUIET** writes CTRL's RESET strobe (not STOPALL); OODOS's cold boot strobes it first; `$C818`'s STOPALL entry
   still sends the record.
8. **The BIOS entries:** `$A2` BEEP is 800 Hz for A × 10 ms (A = 0: 250 ms); `$A3` STOPALL.
9. **MU_NOTE and MU_TONE** send at once; MU_BLOCK is added; no MU_FLUSH.
10. **SID_PLAN** is kept as a thin call.
11. **The revision 1 and 2 equates** are removed at the end of branch B.
12. **The boot sound:** the same samples and sequence, played by the card's firmware; it sounds the same, and isn't
    sample-identical.
13. **Section 11's deviations** from the spec are approved as one block.
14. **The expectation blocks A-E** are approved, exactly as listed in their units.
15. **Every catalog card** is rebuilt on OODOS 0.17 at the release.
16. **OO-Amp's port** is scheduled right after branch B.
