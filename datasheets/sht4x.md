# SHT4x peripheral description

Sensirion SHT4x, version 6.4, November 2023.

**Reference only: the current register-read rule cannot run this sequence.**

Confirm the exact ordering code: address variants are 0x44, 0x45 and 0x46
(pp. 20-21); these are not freely switchable choices on one device.
For high-repeatability measurement, Table 8 (p. 12) specifies command 0xFD and
six response bytes, including two CRC bytes. Figure 14 (p. 11) shows a command
write terminated by STOP, followed later by a separate read after conversion.
Table 5 (p. 10) lists a maximum high-repeatability duration of 8.3 ms.

This differs from the current immediate repeated-START register read. There is
no clock stretching; a busy read returns NACK, and data is consumed on the first
acknowledged read header (pp. 10-11). A future driver/model needs explicit
conversion waiting and the separate read. Preserve all six raw bytes; CRC and
physical-unit interpretation can remain host/user responsibilities. Heater and
reset commands are not part of this example.

## Machine-readable contract

Only this fenced block supplies executable fields. Prose is explanatory.

```peripheral-json
{
  "schema_version": "1.0.0",
  "device": "sht4x",
  "bus": "i2c",
  "addresses": [
    68,
    69,
    70
  ],
  "source": {
    "file": "Sensirion_Datasheet_SHT4x.pdf",
    "sha256": "c615676c5b1ddc7b1e389eb7c93345f20e4a88dadc36ebe2eb93074559cbc614",
    "page_count": 24
  },
  "operations": {
    "measure_high_precision": {
      "kind": "unsupported",
      "reason": "Needs command 0xFD, STOP, conversion wait, then a separate six-byte read; immediate repeated-START v1 is incompatible.",
      "pages": [
        10,
        11,
        12,
        20,
        21
      ],
      "notes": "Return six raw bytes including CRC bytes. Address depends on the exact SHT4x ordering code; no hardware qualification."
    }
  }
}
```
