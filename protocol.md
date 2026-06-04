# DEXP AC UART protocol

This document describes the currently decoded UART protocol between the stock
Wi-Fi module and the DEXP indoor AC unit.

The protocol was decoded from UART captures made with a logic analyzer and
cross-checked against Home Assistant state changes and remote-control videos.

## UART

- Baud rate: `9600`
- Data bits: `8`
- Parity: none
- Stop bits: `1`
- Logic analyzer pins:
  - `D11`: AC to Wi-Fi module
  - `D12`: Wi-Fi module to AC

## Frame Format

All known frames start with `AA`.

```text
AA LEN ... CHECKSUM
```

`LEN` is the number of bytes remaining after the `LEN` byte, including the
checksum byte.

The checksum is:

```text
sum(all bytes before checksum) & 0xFF
```

Example:

```text
AA 02 01 AD
AA + 02 + 01 = AD
```

## Polling

The Wi-Fi module sends a periodic poll request about every 5 seconds:

```text
AA 02 01 AD
```

The AC responds with a status frame:

```text
AA 14 <19-byte payload> CHECKSUM
```

For `AA 14` status frames, byte indexes below are zero-based indexes inside the
payload, excluding `AA`, `LEN`, and `CHECKSUM`.

## Power-On Handshake

A capture from the moment power was applied showed a short startup handshake
before normal polling begins:

```text
wifi_to_ac: AA 03 03 03 B3
ac_to_wifi: AA 03 03 03 B3
```

This exchange was observed twice, about 1.2 seconds apart. In the same capture,
normal polling started after that with `AA 02 01 AD` at about 14 seconds after
capture start.

The startup frame follows the same checksum rule:

```text
AA + 03 + 03 + 03 = B3
```

No `AA 14` command frame from Wi-Fi to AC was observed during this power-on
capture before normal polling.

## Command Frames

The Wi-Fi module also sends `AA 14` frames when controlling the AC.

Observed command frames use the same length and checksum rule as status frames,
but the payload starts with `02`:

```text
AA 14 <19-byte command payload> CHECKSUM
```

Example command frame:

```text
AA 14 02 01 00 06 18 01 10 03 17 17 00 00 00 00 00 16 38 00 00 6F
```

Payload:

```text
02 01 00 06 18 01 10 03 17 17 00 00 00 00 00 16 38 00 00
```

Known command payload fields:

| Payload index | Known meaning |
| ---: | --- |
| `0` | Frame kind: `02` command |
| `1` | Desired power |
| `2` | Unknown, observed `00` |
| `3` | Unknown/display-related candidate, observed `06` |
| `4` | Unknown in command frames, observed `18` or `00` |
| `5` | Desired HVAC mode |
| `6` | Desired special functions bitmask |
| `7` | Desired fan speed |
| `8` | Desired target temperature |
| `9` | Current temperature copy or `00` |
| `10..14` | Observed `00` |
| `15` | Command timestamp hour, observed as binary hour value, for example `01` at 01:xx |
| `16` | Command timestamp minute, observed as binary minute value, for example `0C` = 12, `11` = 17, `16` = 22, `17` = 23 |
| `17..18` | Observed `00` |

The AC replies to a command with an `AA 14` frame whose payload also starts with
`02`. These ack/response frames mirror the desired state fields but usually zero
some non-essential fields, for example `payload[4]`, `payload[9]`, and
`payload[15..18]`.

Example command and immediate AC response:

```text
wifi_to_ac: AA 14 02 01 00 06 18 01 10 03 17 17 00 00 00 00 00 16 38 00 00 6F
ac_to_wifi: AA 14 02 01 00 06 00 01 10 03 17 00 00 00 00 00 00 00 00 00 00 ...
```

The next normal poll response then returns a `payload[0] == 01` status frame.

Clean HA-driven captures show this working command pattern:

```text
02 POWER 00 06 18 MODE SPECIAL FAN TARGET CURRENT 00 00 00 00 00 01 XX 00 00
```

Observed `payload[15..16]` values line up with local command time:

| Capture time | Observed `payload[15..16]` | Decoded time |
| --- | --- | --- |
| 01:12 | `01 0C` | 01:12 |
| 01:13 | `01 0D` | 01:13 |
| 01:17 | `01 11` | 01:17 |
| 01:22 | `01 16` | 01:22 |
| 01:23 | `01 17` | 01:23 |

This suggests that `payload[15]` is hour and `payload[16]` is minute, encoded
as plain binary values and displayed in logs as hex bytes. It is not BCD:
`0x16` means decimal minute 22, not minute 16.

For a first command builder, replaying the latest observed context bytes from
the stock Wi-Fi module may be safer than hard-coding one value, but generating
current local hour/minute is now the best hypothesis to validate on hardware.

## Status Payload

Example status frame:

```text
AA 14 01 01 00 06 18 01 10 00 18 18 00 00 00 00 00 00 00 00 00 1F
```

Payload:

```text
01 01 00 06 18 01 10 00 18 18 00 00 00 00 00 00 00 00 00
```

| Payload index | Known meaning |
| ---: | --- |
| `0` | Frame kind: `01` normal status, `02` command ack/response |
| `1` | Power: `00` off, `01` on |
| `2` | Unknown, usually `00` |
| `3` | Unknown/display-related candidate: observed `06` normal, `00` alternate/off |
| `4` | Swing flags/state: observed `18` normal/off, `1A` horizontal swing |
| `5` | HVAC mode |
| `6` | Special functions bitmask |
| `7` | Fan speed |
| `8` | Target temperature |
| `9` | Current temperature |
| `10..18` | Unknown, usually `00` in normal status frames |

## Power

Stored in `payload[1]`.

| Value | Meaning |
| --- | --- |
| `00` | Off |
| `01` | On |

## HVAC Mode

Stored in `payload[5]`.

| Value | Meaning |
| --- | --- |
| `00` | Auto |
| `01` | Cool |
| `02` | Dry |
| `03` | Heat |
| `04` | Fan only |

## Fan Speed

Stored in `payload[7]`.

| Value | Meaning |
| --- | --- |
| `00` | Auto |
| `01` | Low |
| `02` | Middle |
| `03` | High |

## Temperature

Target temperature is stored in `payload[8]`.

The raw byte is the temperature in degrees Celsius:

| Raw range | Meaning |
| --- | --- |
| `10..20` | `16..32` degrees C |

Examples:

| Value | Temperature |
| --- | ---: |
| `10` | 16 C |
| `18` | 24 C |
| `20` | 32 C |

Current temperature is stored in `payload[9]`.

The raw byte is also the temperature in degrees Celsius.

Examples:

| Value | Temperature |
| --- | ---: |
| `16` | 22 C |
| `17` | 23 C |
| `18` | 24 C |
| `19` | 25 C |

In some ack/command-response frames, current temperature may be `00`.

## Special Functions

Special functions are stored in `payload[6]`.

This field is a bitmask with a normal/base marker bit `0x10`.

```text
payload[6] = 0x10 | flags
```

Known flags:

| Bit | Mask | Function |
| ---: | --- | --- |
| 0 | `0x01` | Sleep |
| 1 | `0x02` | Ionizer |
| 2 | `0x04` | Mute / quiet |
| 3 | `0x08` | Strong / turbo |

Observed values:

| Value | Meaning |
| --- | --- |
| `10` | No special functions |
| `11` | Sleep |
| `12` | Ionizer |
| `14` | Mute / quiet |
| `18` | Strong / turbo |
| `1A` | Strong / turbo + ionizer |

`1A` confirms that the field is a bitmask:

```text
0x1A = 0x10 | 0x08 | 0x02
```

## Swing

Swing state is stored in `payload[4]`.

Observed values:

| Value | Meaning |
| --- | --- |
| `18` | Normal/off |
| `1A` | Horizontal swing |

This may also be a bitmask, but only the horizontal swing case has been
confirmed so far.

## Known Remote Buttons

| Remote button | Status field |
| --- | --- |
| Power | `payload[1]` |
| Mode / `РЕЖИМ` | `payload[5]` |
| Fan / `СКОРОСТЬ` | `payload[7]` |
| `+` / `-` | `payload[8]` |
| Turbo / `ТУРБО` | `payload[6] & 0x08` |
| Ionizer, leaf button | `payload[6] & 0x02` |
| Quiet / `ТИХИЙ` | `payload[6] & 0x04` |
| Sleep / `СОН` | `payload[6] & 0x01` |
| Horizontal swing button | `payload[4]`, observed `18 <-> 1A` |

## Open Questions

- Exact meaning of `payload[3]`; observed `06 <-> 00`, possibly display/light.
- Exact encoding of all swing modes in `payload[4]`.
- Meaning of `payload[2]`.
- Meaning of `payload[10..18]` in normal status frames.
- Whether command timestamp bytes `payload[15..16]` must match real local time
  or are only informational.
- Whether any special-function combinations are rejected by the AC.

## Implementation Notes

For a status parser:

1. Wait for header byte `AA`.
2. Read `LEN`.
3. Read `LEN` bytes.
4. Validate checksum with `sum(all bytes before checksum) & 0xFF`.
5. For `LEN == 0x14`, parse the 19-byte payload and publish climate state.

For special functions:

```cpp
const uint8_t special = payload[6];
const bool sleep = special & 0x01;
const bool ionizer = special & 0x02;
const bool mute = special & 0x04;
const bool turbo = special & 0x08;
```

The `0x10` base marker is expected in normal status frames.
