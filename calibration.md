# Calibration

The load does not measure the current it actually draws. The MCU drives the
control loop "blind" by setting a PWM value; there is no ADC feedback of the
current (this hardware design is discussed in
[upstream issue #2](https://github.com/herm/ZPB30A1_Firmware/issues/2)).

The setpoint to raw-PWM conversion is linear and **unit-specific**. The
constants in `software/config.h` (`LOAD_CAL_T`, `LOAD_CAL_M`) were determined
on the author's device and can be significantly different for another unit of
the same model. Typical symptom on an uncalibrated unit: the real current is
far off from the setpoint, e.g. the load draws 650 mA while set to 500 mA.

The `Vl`/`Vs` voltage channels and temperature channel have constants
(`ADC_CAL_*`) as well. They are usually closer between units, but the same
method applies if their readings are off (see also `hardware/v_sense.ods` and
`hardware/Temperature.ods`).

## Current/PWM relationship

The firmware (`load.c`) converts the current setpoint to the raw PWM counter
value (16 bit `TIM1->CCR1`) like this:

    PWM = (current_mA * LOAD_CAL_M - LOAD_CAL_T) / 2^16

* `LOAD_CAL_M` (`m`): slope, in 2^16 PWM LSBs per mA
* `LOAD_CAL_T` (`t`): offset, negative because the PWM grows over-proportionally at low currents

## Measuring

1. Flash the firmware as-is (see README for flashing instructions).
2. Connect a trusted ammeter in series with the load (and a suitable power
   supply and the usual P+/P- wiring).
3. Run the load in CC mode at several setpoints spread across your usual
   working range and note `I_set` (setpoint) together with the current shown
   by the ammeter, e.g.:

   | I_set (mA) | I_measured (mA) |
   |------------|-----------------|
   | 350        | 421             |
   | 830        | 1000            |
   | 1500       | 1720            |

Your data will differ — this is expected and precisely why calibration is
needed.

## Fitting the constants

1. Open `hardware/i_load.ods` (LibreOffice etc.). It implements exactly the
   firmware formula above: you enter your measured points, the spreadsheet
   computes the linear fit and gives `m` and `t` and the achieved error per
   point.
2. Replace the measurement column with your own values and overwrite the
   resulting `m` and `t`.
3. Error of ~1-2% over the measured range is a realistic result.

Alternative (quicker, suggested by the author in the upstream issue): scale
`LOAD_CAL_T` down by the relative error you measure (e.g. if real current is
20% too high, reduce `LOAD_CAL_T` by 20%), reflash and repeat until close
enough. This works because the offset dominates the error in most setups.

## Applying

1. Set your `m`/`t` in `software/config.h`:
   `LOAD_CAL_T`/`LOAD_CAL_M` (both are compile-time constants; EEPROM stored
   calibration does not exist yet).
2. Rebuild and reflash, then verify your measurement points again and refine
   if necessary. On-device flash endurance is high enough for many erase
   cycles, see the README ("Chip").

## Verifying from the serial interface

The `VAL:` lines continuously report the state (115200 Baud, details in
[serial protocol.md](serial%20protocol.md)). Note that the `I` field reports
the **setpoint**, not a measured current — the birds-eye verification during
calibration always comes from the external ammeter.
