# Tests

The host-side tests cover the pure logic that should stay independent from ESPHome:

- DSMRloggerAPI `actual` parsing.
- Missing value handling.
- Net power sign and phase fallback.
- EM112 PF.B scaling and LSW/MSW packing.
- Modbus RTU CRC and FC03/FC04 responses.
- Courtesy unknown register behavior.
- Stale data fail-safe behavior.

Run:

```bash
make test
```

Full validation:

```bash
make check
```
