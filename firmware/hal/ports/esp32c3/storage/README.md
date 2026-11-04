# ESP32-C3 configuration storage policy

`cfgpart` recognizes the exact reviewed partition-table bytes and write window.
`cfgslot` verifies the complete layout and bounds two fixed 12 KiB slots, using
its existing 256-byte internal read scratch. `../c3store` supplies synchronous
SDK flash calls and the native region guard. The generic record/commit algorithm
remains in `src/config/cfgstore`; it does not select this board's flash geometry.

See [layout, error contracts and manual review](../../../../src/config/README.md#static-partition-access).
The relocation changes no C bytes, layout constants, callback signatures or
scratch storage. Build staging preserves this storage subdirectory. Host tests
compile these same modules and attack the complete table and every slot boundary;
a fresh native build checks the SDK integration. Historical qualification paths
and source hashes still identify their original snapshots and remain unchanged.
