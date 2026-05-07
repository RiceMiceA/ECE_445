# API & State Contract
## AI Nutritious Culinary Assistant

---

## 1. Canonical State Names

| State | Description |
|---|---|
| `idle` | System waiting, nothing active |
| `scanning` | Quest is detecting ingredients |
| `ingredients_confirmed` | User confirmed ingredient list |
| `recipe_ready` | Backend generated recipe, steps loaded |
| `dispensing_step` | ESP32 is dispensing for current step |
| `user_cook_step` | User performing a non-dispense cooking step |
| `complete` | All steps finished |
| `error` | Recoverable error state |
| `manual_override` | Direct hardware control active |

---

## 2. Canonical Spice Strings

Always use exactly these strings (lowercase, no variation):

```
"salt"
"black pepper"
"garlic powder"
```

Container index mapping (fixed):

```
containers[0] = "salt"
containers[1] = "black pepper"
containers[2] = "garlic powder"
```

---

## 3. System State Shape

```json
{
  "demo_state": "idle",
  "candidate_ingredients": [],
  "confirmed_ingredients": [],
  "recipe": null,
  "current_step_index": -1,
  "containers": ["salt", "black pepper", "garlic powder"],
  "container_levels": [100, 100, 100],
  "weight": 0.0,
  "dispense_status": "idle",
  "manual_override": false,
  "last_error": null
}
```

---

## 4. Recipe Shape

```json
{
  "name": "Garlic Herb Chicken",
  "description": "Simple pan-seared chicken with garlic and herbs.",
  "steps": [
    {
      "index": 0,
      "instruction": "Season chicken with salt.",
      "type": "dispense",
      "dispense": {
        "spice": "salt",
        "grams": 3.0
      }
    },
    {
      "index": 1,
      "instruction": "Add black pepper to taste.",
      "type": "dispense",
      "dispense": {
        "spice": "black pepper",
        "grams": 1.5
      }
    },
    {
      "index": 2,
      "instruction": "Sear chicken on medium heat for 6 minutes per side.",
      "type": "cook",
      "dispense": null
    },
    {
      "index": 3,
      "instruction": "Finish with garlic powder and rest for 5 minutes.",
      "type": "dispense",
      "dispense": {
        "spice": "garlic powder",
        "grams": 2.0
      }
    }
  ]
}
```

Step `type` is either `"dispense"` or `"cook"`.

---

## 5. JSON Payloads

### Quest → Backend

**POST /ingredients_confirmed**
```json
{
  "ingredients": ["chicken", "garlic", "olive oil"]
}
```

**POST /candidate_ingredients**  *(Quest pushes live detections)*
```json
{
  "ingredients": ["chicken", "garlic"]
}
```

---

### Backend → Quest (response from GET /current_step)

```json
{
  "step_index": 1,
  "instruction": "Add black pepper to taste.",
  "type": "dispense",
  "dispense": {
    "spice": "black pepper",
    "grams": 1.5
  },
  "total_steps": 4
}
```

---

### Backend → ESP32 (response from GET /pending_command)

```json
{
  "command_id": "cmd_20260325_001",
  "action": "dispense",
  "spice": "black pepper",
  "container_index": 1,
  "target_grams": 1.5
}
```

If no command is pending:
```json
{
  "command_id": null,
  "action": "none"
}
```

---

### ESP32 → Backend

**POST /command_result**
```json
{
  "command_id": "cmd_20260325_001",
  "status": "done",
  "actual_grams": 1.48,
  "weight": 312.4
}
```

**POST /status_update**  *(periodic heartbeat)*
```json
{
  "weight": 312.4,
  "container_levels": [97, 98, 100]
}
```

---

## 6. All Endpoints

| Method | Path | Caller | Description |
|---|---|---|---|
| GET | `/state` | Any | Full system state |
| POST | `/candidate_ingredients` | Quest | Push live detections |
| POST | `/ingredients_confirmed` | Quest / Dashboard | Lock ingredient list |
| POST | `/generate_recipe` | Dashboard | Trigger recipe generation |
| GET | `/current_step` | Quest / Dashboard | Get active step |
| POST | `/advance_step` | Quest / Dashboard | Move to next step |
| POST | `/dispense_step` | Dashboard | Manually trigger dispense |
| GET | `/pending_command` | ESP32 | Poll for dispense command |
| POST | `/command_result` | ESP32 | Report dispense outcome |
| POST | `/status_update` | ESP32 | Heartbeat weight/levels |
| POST | `/set_containers` | Dashboard | Rename containers |
| POST | `/manual_override` | Dashboard | Toggle manual mode |
| POST | `/reset` | Dashboard | Reset to idle |

---

## 7. Rules

1. `demo_state` is the single source of truth — never duplicate state in Unity or ESP32.
2. Quest only POSTs; it never mutates backend state directly beyond ingredient confirmation.
3. ESP32 never generates commands — it only executes and reports.
4. Spice names must always match the canonical list exactly (case-sensitive).
5. `command_id` must be echoed back in `/command_result` for correlation.
