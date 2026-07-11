# AGENTS.md

- Keep the project buildable after every milestone.
- Use the devcontainer for tooling and build verification.
- Keep commands and filenames project-named: `wallbox-powerboost-emulator`.
- Prefer ESPHome native API over MQTT.
- Do not introduce MQTT unless explicitly requested later.
- Do not use WiFiManager in v1.
- Keep the Modbus response path non-blocking and allocation-free.
- Do not block UART/Modbus servicing while polling HTTP.
- Add tests for pure logic.
- Never claim MID, fiscal, reimbursement, billing, or legal metrology compliance.
- Implement and document only the explicitly selected Carlo Gavazzi EM112 PF.B, EM330, and EM530 profiles.
- Keep EM112 PF.B, EM330, and EM530 as separate selectable profiles with profile-specific register mappings and tests.
- Do not imply support for other EM112, EM330, or EM530 variants.
- Do not use or interact with the CAN bus.
- Document every hardware assumption.
- Cite external inspiration and do not copy unsupported mappings.
- Run `make test`, `esphome config wallbox-powerboost-emulator.yaml`, and `esphome compile wallbox-powerboost-emulator.yaml` before final response.
