



## Apr. 26th
### Backend — `Software/src/web_app/backend.py` (1238 lines, heavily expanded)

**LLM Recipe Planner (replaces mock placeholder)**
- `_generate_llm_recipe()` calls the OpenAI Responses API with a strict JSON schema (`RECIPE_PLAN_SCHEMA`) and a `RECIPE_PLANNER_INSTRUCTIONS` system prompt.
- Pydantic models `RecipePlan`, `RecipeStep`, `DispenseSpec`, `SelectorTarget` validate LLM output before any state change.
- `_postprocess_recipe_plan()` normalizes + sanitizes the LLM result; falls back to mock on any failure.
- Controlled by env vars: `USE_LLM_PLANNER=1`, `OPENAI_API_KEY`, `OPENAI_MODEL`. Auto-loads a `.env` file.
- `system_state["last_planner_status"]` tracks whether LLM succeeded or fell back.

**Richer Step Schema**
- Every step now carries: `step_id` (unique hash), `render_plan` (focus + assist presets), `completion_mode` (`user_confirm` / `dispense_done` / `timer_done` / `auto`), and `targets` / `destination` (selector objects for AR cue resolution).
- `_compile_render_plan(step)` maps action types (`grab`, `season`, `move`, `mix`, `wait`, `complete`, etc.) to `focus_preset` + `assist_preset` pairs.
- `_resolve_default_completion_mode(action)` maps action → completion_mode automatically.
- `_build_step_status(step)` builds the enriched `/current_step` response including live dispense weight.
- `schema_version` field added to step payloads for forward compatibility.

**New Endpoints**
- `POST /vision_frame` — Quest posts structured YOLO frame (≤20 detections); updates candidate ingredients, frame count, receive timestamp.
- `POST /rv_event` — R&V latency probe; Quest sends `quest_send_ms`, backend computes end-to-end latency and updates p50/p95/max.
- `GET /rv_state` — Compact PASS/FAIL verification snapshot for the dashboard R&V panel.
- `POST /tare` — Zeroes the load cell reference weight in system_state.

**R&V Telemetry added to system_state**
- `last_vision_frame`, `vision_frame_count`, `vision_last_receive_ms`
- `rv_event_count`, `rv_duplicate_count`, `rv_latency_p50_ms`, `rv_latency_p95_ms`, `rv_latency_max_ms`

---

### ESP32 Firmware — `Software/src/esp_driver/src/main.cpp` (complete rewrite to BLE)
- Was: WiFi HTTP polling loop (`HTTPClient`).
- Now: BLE GATT peripheral advertising as `"NuChef-Dispenser"` with a custom GATT service.
- Three GATT characteristics:
  - `CMD_CHAR` (Write) — receives dispense commands as JSON
  - `RSLT_CHAR` (Notify) — sends `command_result` JSON back
  - `WT_CHAR` (Notify) — sends periodic weight heartbeats
- Motor wiring changed to TB6612 + FULL4WIRE steppers; all 3 motors share 4 coil-drive pins, selected by individual ENABLE lines.

---

### New File — `Software/src/web_app/ble_bridge.py`
- Async Python script (`bleak` + `httpx`) acting as BLE ↔ HTTP glue layer.
- Scans and connects to ESP32 peripheral by service UUID, with device-name fallback.
- Polls `GET /pending_command` every second; writes command JSON to `CMD_CHAR`.
- Subscribes to `RSLT_CHAR` and `WT_CHAR` notifications; forwards to `POST /command_result` and `POST /status_update`.
- Handles auto-reconnect on disconnect.

---

### New Unity Scripts (`DetectionManager/Scripts/`)

**RecipeGuidanceManager.cs**
- Polls `/current_step` at 0.3 s intervals; detects `step_id` changes to avoid redundant cue updates.
- Resolves target ingredient transforms via SceneObjectRegistry; hands step + targets to CueRenderer.
- Runs completion watchers: `WatchDispenseDone()`, `WatchTimerDone(duration_s)`, `WatchAutoAdvance()`.
- Exposes static `IsGuiding` bool that gates all DetectionManager input.
- `StopGuidance()` clears AR cues + hides HUD; does NOT reset backend recipe.

**RecipeStepPayload.cs**
- Full serializable data model for `/current_step` JSON: `schema_version`, `step_id`, `RenderPlan`, `SelectorTarget`, `StepStatus`, `completion_mode`, `targets`, `destination`.

**CueRenderer.cs**
- Spawns / repositions / destroys AR cue GameObjects from Inspector prefab slots.
- Maps `focus_preset` values (`soft_highlight`, `pulse_highlight`, `success_pulse`, `text_panel_only`) and `assist_preset` values (`ghost_hand_grab`, `arrow_to_target`, `ghost_hand_move`, `arrow_dispenser_to_target`, `ghost_hand_sprinkle`, `timer_ring`) to prefabs.

**ArrowCueController.cs**
- Animated bouncing arrow positioned above a target transform.

**HighlightCueController.cs**
- Soft / pulse alpha+scale highlight that follows a target transform. Two modes: `soft_highlight`, `pulse_highlight`.

**GuidanceHudController.cs**
- In-HMD World Space HUD: step instruction text, spice name, gram progress bar, timer countdown.
- All UI fields optional (null-safe); supports legacy `UnityEngine.UI.Text` or TMP swap.

**HudFollowCamera.cs**
- Lazy lerp follow so the HUD drifts with head pose (`m_posLerpSpeed`, `m_rotLerpSpeed`, `m_billboardYOnly`) instead of rigidly attaching to the camera.

**SceneObjectRegistry.cs**
- Resolves a label string → `Transform`. Priority: spawned spatial marker > live YOLO detection box > null.

**VisionRvHudController.cs**
- Togglable in-headset R&V debug overlay: live inference FPS, per-detection list, planner/backend state, latency stats (p50/p95/max).

**MarkerReviewRaySelector.cs**
- Ray-selector for ingredient-review phase using `Physics.Raycast` against marker colliders (NOT `EnvironmentRaycastManager` — that hits real-world geometry, not Unity GameObjects).

---

### New Test Tools — `Software/src/web_app/tools/`

**connection_soak_test.py**
- 30-minute soak test polling `GET /state` (or `GET /rv_state`) every second.
- Logs to `results/connection_soak.csv`; reports max reconnect gap.
- Acceptance criterion: no connectivity gap > 5 s.

**latency_burst_test.py**
- Bursts `POST /rv_event` at 10 Hz for 60 s; measures end-to-end latency.
- Logs to `results/latency_burst.csv`; reports PASS/FAIL.
- Acceptance criterion: p95 ≤ 150 ms AND dropped = 0.

---

### `backend_llm_ready.py` (721 lines)
- Clean snapshot of the backend with LLM integration + core state machine but without R&V telemetry endpoints. Kept as reference/backup alongside the full `backend.py`.

---

### Architectural Shifts Summary

| Area | Mar. 28 baseline | Apr. 26 |
|---|---|---|
| Recipe generation | `_generate_mock_recipe()` placeholder | Real OpenAI LLM call with strict JSON schema + Pydantic validation, mock as fallback |
| ESP32 comms | WiFi HTTP polling loop | BLE GATT peripheral + separate `ble_bridge.py` relay |
| Step payload | `instruction`, `type`, `dispense` | + `step_id`, `render_plan`, `completion_mode`, `targets`, `destination`, `schema_version` |
| AR guidance | Not implemented | Full `RecipeGuidanceManager` + `CueRenderer` + 4 cue controllers |
| HUD | Not implemented | `GuidanceHudController` + `HudFollowCamera` lazy follow |
| R&V verification | Not implemented | `/vision_frame`, `/rv_event`, `/rv_state` + `VisionRvHudController` + 2 CLI test tools |
| Label resolution | N/A | `SceneObjectRegistry` (marker-priority lookup) |

---

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

