



## Mar. 28th
### API Contract — `Software/doc/api_state_contract.md`
- Created new contract doc freezing all state names, canonical spice strings, and JSON payload shapes for Quest ↔ Backend ↔ ESP32
- Full endpoint reference table

### Backend — `Software/src/web_app/backend.py`
- Built full FastAPI state-machine orchestrator from scratch
- `system_state` dict as single source of truth (`demo_state`, ingredients, recipe, steps, dispense status, weight, containers)
- All endpoints implemented: `/state`, `/reset`, `/candidate_ingredients`, `/ingredients_confirmed`, `/generate_recipe`, `/current_step`, `/advance_step`, `/dispense_step`, `/pending_command`, `/command_result`, `/status_update`, `/set_containers`, `/manual_override`, `/manual_dispense`
- Mock recipe generator placeholder (`_generate_mock_recipe`) — swap for LLM call later

### Web Dashboard — `Software/src/web_app/main/`
- `index.html` — three-column layout: ingredients, recipe steps, hardware
- `script.js` — polls `/state` every 2s; all button actions wired; renders live state badge, step list, container level bars, load cell weight, pending ESP32 command
- `style.css` — dark theme, state-colored badges, step progress visualization

### ESP32 Firmware — `Software/src/esp_driver/src/main.cpp`
- Full rewrite from passive `WebServer` → active `HTTPClient` polling loop
- Polls `GET /pending_command` every 1s; runs closed-loop dispense; POSTs to `/command_result`
- HX711 load cell tared on boot; ±0.2g tolerance, 15s timeout
- 3 motors in `AccelStepper::DRIVER` (step/dir) mode, indexed by `container_index`
- `TEST_MOTOR_INDEX` constant: set `0/1/2` for single-motor testing, `-1` for production
- `platformio.ini`: added `HX711` + `ArduinoJson`, removed TB6612

### Unity — New Files (`DetectionManager/Scripts/`)
- `DetectionResult.cs` — structured detection model (`ClassId`, `ClassName`, `Score`, `BoundingBox`); replaces raw tuple
- `IngredientInventoryManager.cs` — candidate stability tracking (SeenCount + timeout), confirmed list, periodic POST on set change
- `BackendClient.cs` — all HTTP traffic via `UnityWebRequest` coroutines; configurable `m_baseUrl`

### Unity — Modified Files
- `SentisInferenceRunManager.cs` — `m_detections` → `List<DetectionResult>`; NMS preserves score + class name; calls `m_ingredientInventory.UpdateCandidates()` each inference frame
- `SentisInferenceUiManager.cs` — added `DrawUIBoxes(List<DetectionResult>...)` overload; existing rendering untouched
- `DetectionManager.cs` — added `m_ingredientInventory` + `m_backendClient` references; A button confirms ingredients + triggers recipe generation; B button also clears inventory

### Next Steps
> Inside the esp_driver folder, change backend IP address to ***new IP address every time***

> Inside the Unity files
    - Add IngredientInventoryManager and BackendClient as components on a GameObject in your scene
    - Wire the m_ingredientInventory and m_backendClient serialized fields in SentisInferenceRunManager and DetectionManager in the Inspector
    - Set m_baseUrl in BackendClient to your backend machine's LAN IP

