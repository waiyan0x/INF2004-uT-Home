# ADXL345 peripheral description

Analog Devices ADXL345 datasheet, Rev. G.

Use I2C mode with CS tied to VDD I/O (p. 17). ALT ADDRESS high selects seven-bit
0x1D; low selects 0x53. Neither pin should float. The host requires selection
of the address that matches wiring. Figure 41 on p. 17 permits the register
write followed by repeated START used by this prototype.

| Operation | Peripheral payload sent | Bytes received | Source |
|---|---|---|---|
| device_id | `00` (one byte) | 1; specified ID `e5` | pp. 17, 23-24 |

This identification read does not enable measurement mode or configure sampling.
SPI operation is not represented by this I2C description. Source-reviewed and
host-tested only; no ADXL345 hardware test was performed here.

## Machine-readable contract

Only this fenced block supplies executable fields. Prose is explanatory.

```peripheral-json
{
  "schema_version": "1.0.0",
  "device": "adxl345",
  "bus": "i2c",
  "addresses": [
    29,
    83
  ],
  "source": {
    "file": "adxl345.pdf",
    "sha256": "87ae2212498c35a6759d8732adee0ec9b9d8d60fa95688bc5904f1f07ceb8ff6",
    "page_count": 36
  },
  "operations": {
    "device_id": {
      "kind": "i2c_register_read",
      "register": 0,
      "read_length": 1,
      "pages": [
        17,
        23,
        24
      ],
      "notes": "Expected ID 0xE5. CS high selects I2C; select the actual ALT ADDRESS wiring."
    }
  }
}
```
