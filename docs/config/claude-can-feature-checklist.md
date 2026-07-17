# Claude CAN tool feature checklist

This is the feature list extracted from `Claude.pdf`. It is a reference for
Photon's CAN workbench; it is not a promise that every item is safe to enable
on a live car without the required ECU protocol details.

## RX monitor

- One live row per CAN ID: DBC message name, update count, cycle time, age, and decoded signals.
- Filter and sort by message/ID, count, or age.
- Show raw/unknown frames and decode short payloads where possible.
- Open a full message detail view with every signal and live updates.
- Use Up/Down to move between messages and PageUp/PageDown to scroll a long message.
- Pause a snapshot and record captures in candump-compatible text format.

## DBC-aware TX

- Send a single DBC message by message name and `Signal=Value` assignments.
- Encode physical DBC values, validate ranges, and expose enum labels rather than raw codes.
- Fill unspecified fields with deliberate defaults or a previously observed frame.
- Support one-shot sends and best-effort periodic sends with adjustable cycle times.
- Support standard and extended CAN IDs, remote frames, and CAN FD where the adapter and protocol require them.

## Bench preset coverage described in the chat

- `Driver_Input_Status`: ignition Off/Array/Motor and gear selection.
- `Pedal_Status`: independent accelerator and brake positions, with `FrameID_Pedals` rolling counter.
- `Brake_Pressure_1` and `Brake_Pressure_2`: independent PSI inputs and counters for disagreement testing.
- `LWS_Standard`: steering angle, message counter, and checksum handling.
- `BPS_Status`: named fault selector and contactor states.
- `MC_Status`: fault injection.
- `MC_VelocityMeasurement`: vehicle speed and motor RPM.
- `TelemLeader_Center`: rolling timestamp.

## Important caveats from the chat

- The test-panel scheduler is best-effort user-space timing, not hard real-time.
- LWS transmission needs the approved `LWS_CHK_SUM` algorithm; a zero checksum is not valid for ECUs that enforce it.
- J1939 requires PGN/source-address handling rather than simple exact arbitration-ID lookup.
- PCAN/SocketCAN must use the bus bitrate (250 kbit/s for this vehicle) and a healthy terminated bus.
