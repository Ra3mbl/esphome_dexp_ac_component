# ESPHome DEXP AC Component

External ESPHome climate component for DEXP indoor AC units using the
decoded UART protocol documented in `protocol.md` and `protocol_ru.md`.

The component is based on the ESPHome external-component shape used by
`GrKoR/esphome_aux_ac_component`, but the UART parser and command builder are
implemented for the DEXP protocol:

- UART: `9600 8N1`
- Startup handshake: `AA 03 03 03 B3`
- Poll request: `AA 02 01 AD`
- Status and command frames: `AA 14 <19-byte payload> CHECKSUM`
- Checksum: `sum(all bytes before checksum) & 0xFF`

## Features

- Power on/off
- HVAC modes: auto, cool, dry, heat, fan only
- Fan modes: auto, low, medium, high
- Target temperature: 16..32 C
- Current temperature from status payload
- Horizontal swing: `payload[4]` `18 <-> 1A`
- Sleep preset
- Custom fan modes: `turbo`, `quiet`
- Optional binary sensors for turbo, ionizer, and quiet state

On boot the component sends the startup handshake frame and waits for the same
valid frame from the indoor unit. Periodic polling and climate commands start
only after that response is received.

## Example

```yaml
external_components:
  - source: github://Ra3mbl/esphome_dexp_ac_component
    components: [ dexp_ac ]

uart:
  id: ac_uart_bus
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600
  data_bits: 8
  parity: NONE
  stop_bits: 1

climate:
  - platform: chiq_ac
    name: "CHiQ AC"
```

See `examples/simple/dexp_ac_simple.yaml` for a fuller example.

## Current Protocol Assumptions

Command frames are generated with this payload template:

```text
02 POWER 00 06 SWING MODE SPECIAL FAN TARGET CURRENT 00 00 00 00 00 HH MM 00 00
```

`HH` and `MM` are currently generated from device uptime because ESPHome does
not always have wall-clock time configured. Captures show these bytes matching
local command time, but it is still an open hardware-validation point.

The component preserves the latest observed context bytes where that is safer,
notably `payload[2]` and `payload[3]`, and uses documented defaults before the
first status frame is received.
