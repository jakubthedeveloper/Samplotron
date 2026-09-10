# Save error codes

The OLED displays `SAVE E15` instead of a long error message. The code stays
visible for 15 seconds. Serial output includes the same code alongside the
detailed failure message. Codes identify the failed stage, not necessarily
the underlying hardware or software cause.

| Code | Meaning |
| --- | --- |
| E00 | Unknown save failure. |
| E01 | Save callback missing or failed without a more specific code. |
| E02 | Save service not initialized. |
| E03 | Playback could not be stopped before saving. |
| E04 | Could not enqueue the sample preparation request. |
| E05 | Sample preparation reported failure. |
| E06 | Could not allocate the JSON output buffer. |
| E07 | JSON capacity exceeded or serialization length mismatch. |
| E08 | Could not open the temporary file for writing. |
| E09 | Could not configure the SD write buffer. |
| E10 | Incomplete write to the temporary file. |
| E11 | Could not reopen the temporary file for verification. |
| E12 | Could not configure the SD read buffer. |
| E13 | Stored file size differs from the expected size. |
| E14 | Verification read stopped or returned an invalid byte count. |
| E15 | Read-back data differs from the generated JSON. |
| E16 | Could not move the previous configuration to the backup path. |
| E17 | Could not move the verified temporary file to the configuration path. |

When investigating a failure that disappears with the serial monitor open,
leave the monitor closed, reproduce the failure, and record the OLED code.
An error keeps the settings marked as unsaved so the operation can be retried.

The numeric assignments in `include/save_diagnostics.h` are stable: new codes
should be appended without renumbering existing ones.
