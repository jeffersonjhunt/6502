# 8 USB

The USB card (module B, `$C580`): the keyboard, the mouse and the game port.

The design record, for the reasons: [hw/design.md, "Input and Ports"](../../hw/design.md#input-and-ports).

## The keyboard

Status: **emulated**

The keyboard reaches a program two ways. **KEY** ([`$C580`](#reg-c580)) is cooked: a character, with key repeat,
for simple software such as the monitor and OODOS's console. **The event FIFO** ([EVENT](#reg-c584)) is raw: every
press and release, for games and an OS.

**KEY** holds one character, as the Apple's keyboard latch does. Bit 7 is the strobe: 1 when a new character has
come. Any access to [KEYCLR](#reg-c581) clears it. A new character replaces the old one, taken or not. The card
cooks characters in the US layout, with the Apple's conventions:

- The letters, digits and punctuation as a US keyboard has them, Shift included. The keypad gives its digits,
  `/ * - + .` and Return, whatever Num Lock says.
- Return `$0D`, Esc `$1B`, Backspace `$08`, Tab `$09`, Delete `$7F`.
- The arrows as the Apple's: left `$08`, right `$15`, down `$0A`, up `$0B`.
- Ctrl with a letter, or with `$40–$5F` (`@ [ \ ] ^ _`), gives `$00–$1F`. With `` ` { | } ~ `` it does the same,
  as a terminal cooks them, so Ctrl-`` ` `` is NUL and Ctrl-`{` is ESC.
- Caps Lock acts on letters only, and Shift undoes it. It is on at power-up, as a IIe is usually left. The card keeps
  Caps Lock's state itself.
- The function keys, the modifiers, Caps Lock and Pause give no character.

**Repeat.** The last character key pressed repeats while it is held. The first repeat comes after
(REPEAT's bits 4–5 + 1) × 250 ms, and the rest every (bits 0–3 + 1) × 33 ms. A repeat sets KEY's strobe again, but
puts nothing in the event FIFO.

**The event FIFO** holds 128 bytes, 64 events. An event is two bytes: the key's USB HID usage code, then its flags,
bit 7 set for a release. The modifiers come as keys too, `$E0–$E7`. A key held down sends one press, however long it
is held. A full FIFO drops a new event whole, so the bytes always come in pairs. [EVCOUNT](#reg-c583) says how many
bytes wait.

**Pace.** Key changes that come close together reach the card at the keyboard's USB polling rate, one every 8 ms.
While KEY still holds a character not taken, the next character waits for it, up to 250 ms, so a paste doesn't
overwrite one character with the next. A lone key is never held back. The emulator does this for the keys the WebUI
sends.

The BIOS reads KEY when it has the strobe, clears the strobe and makes the letter a capital, for the monitor:

<!-- listing: ../../backplane/bios/input.s:60-69 -->
```
; A = KEY with the strobe set: clear it, drop bit 7, make letters capitals.
TAKE:
                BIT     USB_KEYCLR     ; any access clears the strobe (BIT leaves A alone)
                AND     #$7F
                CMP     #'a'
                BCC     @got
                CMP     #'z' + 1
                BCS     @got
                AND     #$DF           ; a capital
@got:           RTS
```

OODOS's console reads KEY the same way, and keeps the lower case.

### Pause/Break and Ctrl-Reset

Status: **emulated**

**Pause/Break** pulls /NMI when KBCFG's bit 7 is set, once for each press. The BIOS's KINIT sets it at boot, if a
USB card answers, so Break reaches the monitor ([chapter 4](04-interrupts.md#pausebreak)):

<!-- listing: ../../backplane/bios/input.s:19-25 -->
```
KINIT:
                LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @done
                LDA     #USB_NMI_PAUSE
                STA     USB_KBCFG
@done:          RTS
```

OODOS's BIOS sets it again when it sets up its console. A /RESET clears KBCFG, so after one only the BIOS's boot
sets it again. Entering Apple II mode is a /RESET, and nothing there sets the bit, so Pause/Break does nothing in
Apple II mode.

**Ctrl-Delete** is the IIe's Ctrl-Reset, in either mode: the card pulses /RESET and the CPU resets. It gives no
character. In native mode the windows go back to the flash and the BIOS starts again
([chapter 3](03-boot.md#reset-and-the-flash)); in Apple II mode the mode stays.

In the emulator the WebUI delivers both. The terminal app has no USB keyboard.

## The mouse

Status: **emulated**

The mouse has a relative mode and an absolute mode, picked by MCTRL's bit 0 ([`$C590`](#reg-c590)).

- **Relative** (MCTRL 0): MX and MY are the movement since the last latch, signed 16-bit, held at
  −32,768–32,767.
- **Absolute** (MCTRL 1): MX and MY are the pointer's position, from 0 to MAXX and MAXY. The limits clamp the
  position; they don't scale it. A new limit holds the pointer inside it at once, when its high byte is written,
  so write the low byte first. The limits reset to 639 and 479, the screen's size, so an OS pointer goes straight
  into a sprite.
- **Latching.** Reading MX's low byte, `$C592`, latches MX, MY and MWHEEL together, and starts the sums for the
  next latch at 0. Then read the rest in any order.
- **The wheel** (MWHEEL) is the steps since the last latch, signed 8-bit, held at −128–127. In the emulator a
  step scrolling down is +1, and one scrolling up −1.
- **The buttons** (MBTN) are live, not latched: bit 0 left, bit 1 right, bit 2 middle, bits 3–4 buttons 4 and 5.

In the emulator the WebUI gives the card the pointer's place over the screen, as 0–639 and 0–479, and the card
clamps it to MAXX and MAXY. With smaller limits, everything past them reads as the limit. Relative mode gets the
differences of the clamped place, so its sums stop when the pointer reaches 0 or MAXX and MAXY.

## The game port

Status: **emulated**

The game port is an Apple-style joystick port: four paddles and three buttons. [GAME](#reg-c5a0) holds the
paddles' timer outputs and the buttons.

- **The timers.** An access to [PTRIG](#reg-c5a1) starts the 558. Each paddle's GAME bit is then 1 for about 11 µs
  per count of its setting, in real time, as on the Apple, so a counting loop reads it as Apple software does.
- **The settings.** [PADDLE0–3](#reg-c5a2) give each paddle's setting directly, 0–255, as the card measures it. No
  counting loop is needed.
- **The buttons** are GAME's bits 4–6, 1 = pressed.
- **The speaker.** [SPKR](#reg-c5a6) is a flip-flop driving a speaker, as on the Apple II. Any access flips it, so a
  program makes a tone by touching it in a timed loop. In the WebUI the speaker plays in both ears, mixed with the
  SIDs. A speaker left high clicks once and goes quiet.

The emulator's apps have no control for the paddles or the buttons yet, so the settings read 0 and the buttons 0.

### In Apple II mode

Status: **emulated**

Apple II mode uses the card for the IIe's keyboard, game port and speaker: the north bridge sends the IIe's
addresses for them to the card, and the native block is hidden. Open Apple and Closed Apple are the left and right
GUI or Alt keys, on buttons 0 and 1. [Chapter 13](13-apple-ii-mode.md) has the addresses.

### Proposed

Status: **proposed**

- **Other layouts:** KBCFG's bits 0–3 choose the layout in the design. The emulator keeps them and cooks in US
  only.
- **A USB gamepad on the paddles,** an option the design leaves for later. DEVICES's bit 2 is for it.
- **Device events:** a keyboard, mouse or gamepad plugged in or out, on ISR's bit 3. In the emulator a keyboard and
  a mouse are always there, and the bit is never set.
- **The FIFO on DMA:** the card asserts DRQ while the event FIFO isn't empty, and the design shares DMA channel 2
  between the USB card and the sound card's RAM ([chapter 5](05-dma.md#transfers-for-a-card)). `backplane.machine`
  wires channel 2 to the sound card only, so the FIFO's DRQ reaches no channel.
- **The real 558,** so that the timers stay right whatever the CPU's speed. The emulator times them in software.

## Registers

Status: **emulated**

<a id="reg-c580"></a>
### $C580 USB_KEY: the cooked key

#### Equates

`USB`, `USB_KEY`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | STROBE | 1: a new character; cleared by any access to KEYCLR |
| 6–0 | | the character, in ASCII |

#### Reset

`$00` at power-on. A /RESET leaves it, strobe and all.

#### Status

**emulated**

#### Notes

A repeat sets the strobe again. The monitor and OODOS clear the strobe as they take a key.

<a id="reg-c581"></a>
### $C581 USB_KEYCLR: clears the strobe

#### Equates

`USB_KEYCLR`

#### Module

B, the USB card

#### Access

R/W. Any access, read or write, clears KEY's strobe. A read gives its value first.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | 1: a key is held down, not counting the modifiers |
| 6–0 | | KEY's character |

#### Reset

Nothing of its own: it reads KEY's bits 6–0 and the keys held.

#### Status

**emulated**

#### Notes

The same as the IIe's `$C010`. `BIT USB_KEYCLR` clears the strobe and leaves A alone.

<a id="reg-c582"></a>
### $C582 USB_MODS: the modifiers held

#### Equates

`USB_MODS`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | right GUI |
| 6 | | right Alt |
| 5 | | right Shift |
| 4 | | right Ctrl |
| 3 | | left GUI |
| 2 | | left Alt |
| 1 | | left Shift |
| 0 | | left Ctrl |

1 = held, in USB HID order.

#### Reset

`$00` at power-on. A /RESET leaves it: the keys held are the keyboard's.

#### Status

**emulated**

<a id="reg-c583"></a>
### $C583 USB_EVCOUNT: bytes in the event FIFO

#### Equates

`USB_EVCOUNT`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

All: the bytes waiting, 0–128, two for each event.

#### Reset

`$00`: a /RESET empties the FIFO.

#### Status

**emulated**

#### Notes

hw/design.md has the count stop at 255; the emulator's FIFO holds 128 bytes, so it never gets there.

<a id="reg-c584"></a>
### $C584 USB_EVENT: the event FIFO

#### Equates

`USB_EVENT`

#### Module

B, the USB card

#### Access

R. A read takes the FIFO's next byte. Writes are ignored.

#### Bits

All: an event's usage code, then its flags, and so on in turn. The flags' bit 7 is 1 for a release; their bits 0–6
are 0. An empty FIFO reads `$00`.

#### Reset

Empty: a /RESET empties the FIFO.

#### Status

**emulated**

#### Notes

The card asserts DRQ while the FIFO isn't empty, and a fly-by DMA read takes a byte as a read here does. In the
emulator that DRQ reaches no channel ([Proposed](#proposed)). Read EVCOUNT first, and read the two bytes of an event
together.

<a id="reg-c585"></a>
### $C585 USB_LEDS: the keyboard's lights

#### Equates

`USB_LEDS`

#### Module

B, the USB card

#### Access

R/W. Bits 3–7 aren't kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | | Scroll Lock's light |
| 1 | | Caps Lock's light |
| 0 | | Num Lock's light |

#### Reset

`$00`, at any reset.

#### Status

**emulated**

#### Notes

hw/design.md has LEDS write-only; the emulator reads it back. It lights nothing in the emulator, and writing it
doesn't change Caps Lock, whose state the card keeps itself.

<a id="reg-c586"></a>
### $C586 USB_REPEAT: key repeat's delay and rate

#### Equates

`USB_REPEAT`

#### Module

B, the USB card

#### Access

R/W. Bits 6–7 aren't kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–6 | | 0 |
| 5–4 | DELAY | the first repeat after (DELAY + 1) × 250 ms |
| 3–0 | RATE | then one every (RATE + 1) × 33 ms: 30 to about 2 a second |

#### Reset

`$10`, at any reset: 500 ms, then 30 a second.

#### Status

**emulated**

<a id="reg-c587"></a>
### $C587 USB_KBCFG: the keyboard's configuration

#### Equates

`USB_KBCFG`

#### Module

B, the USB card

#### Access

R/W. Bits 4–6 aren't kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | NMI | 1: Pause/Break pulls /NMI |
| 6–4 | | 0 |
| 3–0 | LAYOUT | kept as written; the card cooks in US whatever they say |

#### Reset

`$00`, at any reset. The BIOS's KINIT writes `$80` at boot, and Break leaves it, so under the monitor it is `$80`.
OODOS's BIOS writes `$80` too. In Apple II mode it stays `$00`.

#### Status

**emulated**

#### Notes

`USB_NMI_PAUSE` (`$80`) is bit 7. hw/design.md has bits 0–3 choose the layout, 0 for US; the emulator keeps them
but knows only US ([Proposed](#proposed)).

<a id="reg-c590"></a>
### $C590 USB_MCTRL: the mouse's mode

#### Equates

`USB_MCTRL`

#### Module

B, the USB card

#### Access

R/W. Only bit 0 is kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0 |
| 0 | ABSOLUTE | 1: MX and MY are the position; 0: the movement |

#### Reset

`$00`, at any reset: relative.

#### Status

**emulated**

<a id="reg-c591"></a>
### $C591 USB_MBTN: the mouse's buttons

#### Equates

`USB_MBTN`

#### Module

B, the USB card

#### Access

R. Writes are ignored. Live, not latched.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | | 0 |
| 4–3 | | buttons 5 and 4 |
| 2 | | the middle button |
| 1 | | the right button |
| 0 | | the left button |

1 = pressed.

#### Reset

`$00` at power-on. A /RESET leaves it: the buttons are the mouse's.

#### Status

**emulated**

<a id="reg-c592"></a>
### $C592 USB_MX: the mouse's X

#### Equates

`USB_MX`

#### Module

B, the USB card

#### Access

R, 2 bytes, low byte first. Reading `$C592` latches MX, MY and MWHEEL, and starts the next sums at 0, in either
mode. Reading `$C593` gives the latch's high byte. Writes are ignored.

#### Bits

All: in relative mode, the movement since the last latch, signed, held at −32,768–32,767. In absolute mode, the
position, 0–MAXX.

#### Reset

`$00 $00`: a /RESET clears the latch and the sums. The position in absolute mode is the host's, and runs on.

#### Status

**emulated**

<a id="reg-c594"></a>
### $C594 USB_MY: the mouse's Y

#### Equates

`USB_MY`

#### Module

B, the USB card

#### Access

R, 2 bytes, low byte first, latched with [MX](#reg-c592). Reading it latches nothing. Writes are ignored.

#### Bits

All: as MX's, for Y, to MAXY.

#### Reset

`$00 $00`.

#### Status

**emulated**

<a id="reg-c596"></a>
### $C596 USB_MWHEEL: the mouse's wheel

#### Equates

`USB_MWHEEL`

#### Module

B, the USB card

#### Access

R, latched with [MX](#reg-c592). Writes are ignored.

#### Bits

All: the wheel's steps since the last latch, signed, held at −128–127, in either mode.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c597"></a>
### $C597 USB_MAXX: absolute mode's X limit

#### Equates

`USB_MAXX`

#### Module

B, the USB card

#### Access

R/W, 2 bytes, low byte first. Writing the high byte holds the pointer inside the new limit at once.

#### Bits

All: the largest X in absolute mode.

#### Reset

`$7F $02`, 639, at any reset.

#### Status

**emulated**

#### Notes

Write the low byte first: until the high byte is written, the old limit's high byte stands.

<a id="reg-c599"></a>
### $C599 USB_MAXY: absolute mode's Y limit

#### Equates

`USB_MAXY`

#### Module

B, the USB card

#### Access

R/W, 2 bytes, low byte first, as [MAXX](#reg-c597).

#### Bits

All: the largest Y in absolute mode.

#### Reset

`$DF $01`, 479, at any reset.

#### Status

**emulated**

<a id="reg-c5a0"></a>
### $C5A0 USB_GAME: the paddles' timers and the buttons

#### Equates

`USB_GAME`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | 0 |
| 6 | | button 2 |
| 5 | | button 1 |
| 4 | | button 0 |
| 3 | | paddle 3's timer: 1 while it runs |
| 2 | | paddle 2's timer |
| 1 | | paddle 1's timer |
| 0 | | paddle 0's timer |

1 = pressed, for a button.

#### Reset

The timers' bits read 0 until PTRIG starts them; a /RESET stops them. The buttons are the host's, and run on.

#### Status

**emulated**

#### Notes

A timer runs for about 11 µs per count of its paddle's setting after PTRIG, so a setting of 0 never shows 1.

<a id="reg-c5a1"></a>
### $C5A1 USB_PTRIG: starts the paddles' timers

#### Equates

`USB_PTRIG`

#### Module

B, the USB card

#### Access

R/W. Any access, read or write, starts all four timers. A read gives `$FF`.

#### Bits

All: no data. A read gives `$FF`.

#### Reset

No state of its own: a /RESET stops the timers.

#### Status

**emulated**

#### Notes

The same as the Apple's `$C070`. hw/design.md has a write start the timers; in the emulator a read does too.

<a id="reg-c5a2"></a>
### $C5A2 USB_PADDLE0: paddle 0's setting

#### Equates

`USB_PADDLE0`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

All: the paddle's setting, 0–255, as the card measures it.

#### Reset

`$00` at power-on. A /RESET leaves it: the setting is the paddle's.

#### Status

**emulated**

#### Notes

No counting loop is needed. Nothing in the emulator's apps sets a paddle yet ([The game port](#the-game-port)).

<a id="reg-c5a3"></a>
### $C5A3 USB_PADDLE1: paddle 1's setting

#### Equates

`USB_PADDLE1`

#### Module

B, the USB card

#### Access

As [PADDLE0](#reg-c5a2).

#### Bits

As PADDLE0, for paddle 1.

#### Reset

As PADDLE0.

#### Status

**emulated**

<a id="reg-c5a4"></a>
### $C5A4 USB_PADDLE2: paddle 2's setting

#### Equates

`USB_PADDLE2`

#### Module

B, the USB card

#### Access

As [PADDLE0](#reg-c5a2).

#### Bits

As PADDLE0, for paddle 2.

#### Reset

As PADDLE0.

#### Status

**emulated**

<a id="reg-c5a5"></a>
### $C5A5 USB_PADDLE3: paddle 3's setting

#### Equates

`USB_PADDLE3`

#### Module

B, the USB card

#### Access

As [PADDLE0](#reg-c5a2).

#### Bits

As PADDLE0, for paddle 3.

#### Reset

As PADDLE0.

#### Status

**emulated**

<a id="reg-c5a6"></a>
### $C5A6 USB_SPKR: the speaker

#### Equates

`USB_SPKR`

#### Module

B, the USB card

#### Access

R/W. Any access, read or write, flips the speaker. A read gives `$FF`.

#### Bits

All: no data. A read gives `$FF`.

#### Reset

A /RESET leaves the flip-flop as it was.

#### Status

**emulated**

#### Notes

The same as the Apple's `$C030`.

<a id="reg-c5b0"></a>
### $C5B0 USB_IEN: the card's interrupt enables

#### Equates

`USB_IEN`

#### Module

B, the USB card

#### Access

R/W. Bits 4–7 aren't kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3 | DEVICE | a device plugged in or out |
| 2 | GAME | a game port button changed |
| 1 | MOUSE | the mouse moved, its wheel turned, or a button changed |
| 0 | KEY | a key event |

1 = interrupt, on IRQH bit 3, while that ISR bit is set.

#### Reset

`$00`, at any reset. The BIOS never sets it; QUIET and OODOS's BIOS write `$00`.

#### Status

**emulated**

<a id="reg-c5b1"></a>
### $C5B1 USB_ISR: the card's pending interrupts

#### Equates

`USB_ISR`

#### Module

B, the USB card

#### Access

R/W. Writing a 1 clears that bit; a 0 leaves it.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3 | DEVICE | never set in the emulator |
| 2 | GAME | a game port button changed |
| 1 | MOUSE | the mouse moved, its wheel turned, or a button changed |
| 0 | KEY | a press or release, whether the FIFO had room for it or not, or a repeat |

1 = pending. The card pulls IRQH bit 3 while ISR AND IEN isn't 0.

#### Reset

`$00`, at any reset.

#### Status

**emulated**

#### Notes

A bit is set whether IEN enables it or not. Nothing clears ISR under the monitor, so it fills with the keys typed
and the mouse's movement: write `$0F` to it before setting IEN. See [IRQH](11-north-bridge-and-rtc.md#reg-c689).

<a id="reg-c5b2"></a>
### $C5B2 USB_DEVICES: what is plugged in

#### Equates

`USB_DEVICES`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | | a gamepad: never set in the emulator |
| 1 | | a mouse |
| 0 | | a keyboard |

#### Reset

`$03`, always: the emulator has a keyboard and a mouse from power-on.

#### Status

**emulated**

#### Notes

Bit 2 is reserved for a USB gamepad, which the design leaves for later ([Proposed](#proposed)).

<a id="reg-c5ff"></a>
### $C5FF USB_ID: the card's ID

#### Equates

`USB_ID`

#### Module

B, the USB card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | TYPE | the module ID: `$B` |
| 3–0 | REV | the revision: 1 |

#### Reset

`$B1`.

#### Status

**emulated**

#### Notes

`USB_ID_V1` (`$B1`) is the value. The BIOS and OODOS check it before they touch the card. The offsets with no
register read `$FF` and ignore writes.
