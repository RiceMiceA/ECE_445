"""
AI Nutritious Culinary Assistant — Backend Orchestrator
FastAPI state-machine backend for the cooking assistant system.

Run:
    pip install fastapi uvicorn pydantic openai
    export OPENAI_API_KEY="..."
    export OPENAI_MODEL="your-validated-model"
    export USE_LLM_PLANNER=1
    uvicorn backend:app --host 0.0.0.0 --port 8000 --reload
"""

from __future__ import annotations

import json
import os
import uuid
import datetime
from copy import deepcopy
from pathlib import Path
from typing import Literal, Optional

# Auto-load .env file (OPENAI_API_KEY, OPENAI_MODEL, USE_LLM_PLANNER)
_env_path = Path(__file__).resolve().parent / ".env"
if _env_path.exists():
    for _line in _env_path.read_text().splitlines():
        _line = _line.strip()
        if _line and not _line.startswith("#") and "=" in _line:
            _k, _v = _line.split("=", 1)
            os.environ.setdefault(_k.strip(), _v.strip())

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, ValidationError

try:
    from openai import OpenAI
except ImportError:  # pragma: no cover - optional until the team installs it
    OpenAI = None


# ---------------------------------------------------------------------------
# App setup
# ---------------------------------------------------------------------------

app = FastAPI(title="AI Cooking Assistant", version="1.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Serve the web dashboard at /ui
app.mount("/ui", StaticFiles(directory="main", html=True), name="frontend")


# ---------------------------------------------------------------------------
# Constants — canonical spice names, fixed container layout
# ---------------------------------------------------------------------------

CONTAINERS: list[str] = ["salt", "black pepper", "garlic powder"]
VALID_CONTAINER_SET = set(CONTAINERS)

VALID_STATES = {
    "idle",
    "scanning",
    "ingredients_confirmed",
    "recipe_ready",
    "dispensing_step",
    "user_cook_step",
    "complete",
    "error",
    "manual_override",
}

USE_LLM_PLANNER = os.getenv("USE_LLM_PLANNER", "0") == "1"
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")
OPENAI_MODEL = os.getenv("OPENAI_MODEL")

openai_client = OpenAI(api_key=OPENAI_API_KEY) if (OpenAI and OPENAI_API_KEY) else None

RECIPE_PLANNER_INSTRUCTIONS = """
You are NuChefPlanner, a constrained recipe planner for an AR cooking demo.

Goal:
Generate one short, practical, impressive recipe plan for a classroom demo.

Hard rules:
- Use only the confirmed ingredients supplied in the input.
- Use only the loaded spices supplied in the input.
- The only supported spices are salt, black pepper, garlic powder.
- Prefer one-pan or one-bowl recipes.
- If can_generate is true, return 3 to 6 total steps.
- Use at most 3 dispense steps.
- Each instruction must be one short sentence that fits in AR.
- For dispense steps, include a dispense object with spice + grams.
- Each dispense amount must be between 0.2 and 4.0 grams.
- Never invent extra ingredients, sauces, oils, or garnishes.
- Never output motor commands, PWM, timing pulses, step counts, or hardware details.
- If the ingredient set is insufficient, set can_generate=false and return an empty steps list.

Style:
- Optimize for a reliable, visually clean demo.
- Keep instructions short and direct.
""".strip()

RECIPE_PLAN_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["name", "description", "can_generate", "steps"],
    "properties": {
        "name": {"type": "string", "minLength": 1, "maxLength": 60},
        "description": {"type": "string", "minLength": 1, "maxLength": 140},
        "can_generate": {"type": "boolean"},
        "steps": {
            "type": "array",
            "minItems": 0,
            "maxItems": 6,
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["index", "instruction", "type", "dispense"],
                "properties": {
                    "index": {"type": "integer", "minimum": 0, "maximum": 5},
                    "instruction": {"type": "string", "minLength": 1, "maxLength": 140},
                    "type": {"type": "string", "enum": ["cook", "dispense"]},
                    "dispense": {
                        "anyOf": [
                            {"type": "null"},
                            {
                                "type": "object",
                                "additionalProperties": False,
                                "required": ["spice", "grams"],
                                "properties": {
                                    "spice": {
                                        "type": "string",
                                        "enum": ["salt", "black pepper", "garlic powder"],
                                    },
                                    "grams": {
                                        "type": "number",
                                        "minimum": 0.2,
                                        "maximum": 4.0,
                                    },
                                },
                            },
                        ]
                    },
                },
            },
        },
    },
}


# ---------------------------------------------------------------------------
# System state — single source of truth
# ---------------------------------------------------------------------------

system_state: dict = {
    "demo_state": "idle",
    "candidate_ingredients": [],
    "confirmed_ingredients": [],
    "recipe": None,
    "current_step_index": -1,
    "containers": deepcopy(CONTAINERS),
    "container_levels": [100, 100, 100],   # percentage remaining
    "weight": 0.0,
    "dispense_status": "idle",            # idle | dispensing | done | error
    "manual_override": False,
    "resume_state": "idle",
    "last_error": None,
    "pending_command": None,               # command waiting for ESP32
    "planner_mode": "llm" if USE_LLM_PLANNER else "mock",
    "last_planner_status": None,
    "last_dispense_actual_grams": None,
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_command_id() -> str:
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"cmd_{ts}_{uuid.uuid4().hex[:6]}"



def _set_state(new_state: str) -> None:
    if new_state not in VALID_STATES:
        raise ValueError(f"Unknown state: {new_state}")
    system_state["demo_state"] = new_state



def _current_step() -> Optional[dict]:
    recipe = system_state.get("recipe")
    if not recipe:
        return None
    idx = system_state["current_step_index"]
    steps = recipe.get("steps", [])
    if idx < 0 or idx >= len(steps):
        return None
    return steps[idx]



def _normalize_ingredients(ingredients: list[str]) -> list[str]:
    cleaned: list[str] = []
    seen: set[str] = set()
    for item in ingredients:
        value = " ".join(item.strip().lower().split())
        if value and value not in seen:
            cleaned.append(value)
            seen.add(value)
    return cleaned



def _generate_mock_recipe(ingredients: list[str]) -> dict:
    """
    Placeholder recipe generator.
    Keeps the backend demoable if the LLM planner is unavailable.
    """
    steps = [
        {
            "index": 0,
            "instruction": f"Prepare your ingredients: {', '.join(ingredients)}.",
            "type": "cook",
            "dispense": None,
        },
        {
            "index": 1,
            "instruction": "Dispense salt into the bowl.",
            "type": "dispense",
            "dispense": {"spice": "salt", "grams": 3.0},
        },
        {
            "index": 2,
            "instruction": "Dispense black pepper into the bowl.",
            "type": "dispense",
            "dispense": {"spice": "black pepper", "grams": 1.5},
        },
        {
            "index": 3,
            "instruction": "Cook on medium heat for 8 minutes.",
            "type": "cook",
            "dispense": None,
        },
        {
            "index": 4,
            "instruction": "Dispense garlic powder before serving.",
            "type": "dispense",
            "dispense": {"spice": "garlic powder", "grams": 2.0},
        },
    ]
    return {
        "name": "AI Chef's Special",
        "description": f"A recipe generated from: {', '.join(ingredients)}.",
        "steps": steps,
    }


# LLM recipe planner
class DispenseSpec(BaseModel):
    spice: Literal["salt", "black pepper", "garlic powder"]
    grams: float = Field(ge=0.2, le=4.0)

    class Config:
        extra = "forbid"


class RecipeStep(BaseModel):
    index: int = Field(ge=0, le=5)
    instruction: str = Field(min_length=1, max_length=140)
    type: Literal["cook", "dispense"]
    dispense: Optional[DispenseSpec] = None

    class Config:
        extra = "forbid"


class RecipePlan(BaseModel):
    name: str = Field(min_length=1, max_length=60)
    description: str = Field(min_length=1, max_length=140)
    can_generate: bool
    steps: list[RecipeStep] = Field(default_factory=list)

    class Config:
        extra = "forbid"



def _postprocess_recipe_plan(plan: RecipePlan, confirmed_ingredients: list[str]) -> dict:
    if not plan.can_generate:
        raise ValueError("Planner declined recipe generation.")

    if not (3 <= len(plan.steps) <= 6):
        raise ValueError(f"Expected 3-6 steps, got {len(plan.steps)}")

    cleaned_steps: list[dict] = []
    dispense_count = 0

    for i, step in enumerate(plan.steps):
        instruction = " ".join(step.instruction.strip().split())
        if not instruction:
            raise ValueError("Empty instruction returned by planner.")

        if step.type == "dispense":
            if step.dispense is None:
                raise ValueError("Dispense step missing dispense payload.")
            spice = step.dispense.spice
            if spice not in system_state["containers"]:
                raise ValueError(f"Planner used unloaded spice: {spice}")
            grams = round(float(step.dispense.grams), 1)
            dispense_count += 1
            cleaned_steps.append(
                {
                    "index": i,
                    "instruction": instruction,
                    "type": "dispense",
                    "dispense": {"spice": spice, "grams": grams},
                }
            )
        else:
            cleaned_steps.append(
                {
                    "index": i,
                    "instruction": instruction,
                    "type": "cook",
                    "dispense": None,
                }
            )

    if dispense_count > 3:
        raise ValueError("Too many dispense steps.")

    return {
        "name": plan.name.strip(),
        "description": plan.description.strip(),
        "ingredients": list(confirmed_ingredients),
        "steps": cleaned_steps,
    }


# Actual calling openai api.
def _generate_llm_recipe(ingredients: list[str]) -> dict:
    if not USE_LLM_PLANNER:
        system_state["last_planner_status"] = "llm_disabled_fallback_to_mock"
        return _generate_mock_recipe(ingredients)

    if openai_client is None:
        system_state["last_planner_status"] = "openai_sdk_or_api_key_missing_fallback_to_mock"
        return _generate_mock_recipe(ingredients)

    if not OPENAI_MODEL:
        system_state["last_planner_status"] = "openai_model_missing_fallback_to_mock"
        return _generate_mock_recipe(ingredients)

    payload = {
        "confirmed_ingredients": ingredients,
        "loaded_spices": system_state["containers"],
        "demo_goal": "short reliable classroom demo",
    }

    try:
        response = openai_client.responses.create(
            model=OPENAI_MODEL,
            instructions=RECIPE_PLANNER_INSTRUCTIONS,
            input=json.dumps(payload),
            text={
                "format": {
                    "type": "json_schema",
                    "name": "recipe_plan",
                    "schema": RECIPE_PLAN_SCHEMA,
                    "strict": True,
                }
            },
        )

        raw_text = response.output_text
        if not raw_text:
            raise ValueError("Planner returned empty output_text")

        plan_dict = json.loads(raw_text)
        plan = RecipePlan(**plan_dict)
        cleaned = _postprocess_recipe_plan(plan, ingredients)
        system_state["last_planner_status"] = "llm_success"
        return cleaned

    except (json.JSONDecodeError, ValidationError, ValueError) as exc:
        system_state["last_planner_status"] = f"llm_validation_failed: {exc}"
        system_state["last_error"] = f"LLM planner validation failed: {exc}"
        return _generate_mock_recipe(ingredients)
    except Exception as exc:  # pragma: no cover - runtime/API dependent
        system_state["last_planner_status"] = f"llm_request_failed: {exc}"
        system_state["last_error"] = f"OpenAI request failed: {exc}"
        return _generate_mock_recipe(ingredients)


# ---------------------------------------------------------------------------
# Pydantic request models
# ---------------------------------------------------------------------------


class CandidatePayload(BaseModel):
    ingredients: list[str]


class ConfirmedPayload(BaseModel):
    ingredients: list[str]


class GenerateRecipePayload(BaseModel):
    ingredients: Optional[list[str]] = None   # defaults to confirmed list


class SetContainersPayload(BaseModel):
    containers: list[str]   # must be exactly 3 items


class StatusUpdatePayload(BaseModel):
    weight: float
    container_levels: Optional[list[int]] = None


class CommandResultPayload(BaseModel):
    command_id: str
    status: str          # "done" | "error"
    actual_grams: Optional[float] = None
    weight: Optional[float] = None


class ManualOverridePayload(BaseModel):
    active: bool


class ManualDispensePayload(BaseModel):
    spice: str
    grams: float


# ---------------------------------------------------------------------------
# Endpoints — general
# ---------------------------------------------------------------------------


@app.get("/state")
def get_state():
    """Return the full system state."""
    return system_state


@app.post("/reset")
def reset():
    """Reset system to idle. Clears all transient state."""
    system_state.update(
        {
            "demo_state": "idle",
            "candidate_ingredients": [],
            "confirmed_ingredients": [],
            "recipe": None,
            "current_step_index": -1,
            "dispense_status": "idle",
            "manual_override": False,
            "resume_state": "idle",
            "last_error": None,
            "pending_command": None,
            "last_planner_status": None,
            "last_dispense_actual_grams": None,
        }
    )
    return {"ok": True, "demo_state": "idle"}


# ---------------------------------------------------------------------------
# Endpoints — ingredient flow
# ---------------------------------------------------------------------------


@app.post("/candidate_ingredients")
def candidate_ingredients(payload: CandidatePayload):
    """Quest pushes live detections here (non-confirmed)."""
    system_state["candidate_ingredients"] = _normalize_ingredients(payload.ingredients)
    if system_state["demo_state"] == "idle":
        _set_state("scanning")
    return {"ok": True, "candidate_ingredients": system_state["candidate_ingredients"]}


@app.post("/ingredients_confirmed")
def ingredients_confirmed(payload: ConfirmedPayload):
    """Quest or dashboard locks in the ingredient list."""
    confirmed = _normalize_ingredients(payload.ingredients)
    if not confirmed:
        raise HTTPException(status_code=400, detail="Ingredient list cannot be empty.")
    system_state["confirmed_ingredients"] = confirmed
    system_state["candidate_ingredients"] = []
    _set_state("ingredients_confirmed")
    return {
        "ok": True,
        "demo_state": system_state["demo_state"],
        "confirmed_ingredients": system_state["confirmed_ingredients"],
    }


# ---------------------------------------------------------------------------
# Endpoints — recipe
# ---------------------------------------------------------------------------


@app.post("/generate_recipe")
def generate_recipe(payload: GenerateRecipePayload):
    """Generate recipe from confirmed ingredients using LLM if enabled."""
    ingredients = _normalize_ingredients(payload.ingredients or system_state["confirmed_ingredients"])
    if not ingredients:
        raise HTTPException(
            status_code=400,
            detail="No confirmed ingredients. Call /ingredients_confirmed first.",
        )

    recipe = _generate_llm_recipe(ingredients)
    system_state["recipe"] = recipe
    system_state["current_step_index"] = 0
    _set_state("recipe_ready")
    return {
        "ok": True,
        "demo_state": system_state["demo_state"],
        "planner_mode": system_state["planner_mode"],
        "planner_status": system_state["last_planner_status"],
        "recipe": recipe,
    }


# ---------------------------------------------------------------------------
# Endpoints — step sequencing
# ---------------------------------------------------------------------------


@app.get("/current_step")
def current_step():
    """Return the current active recipe step."""
    step = _current_step()
    if step is None:
        return {"step": None, "demo_state": system_state["demo_state"]}
    recipe = system_state["recipe"]
    return {
        "step_index": step["index"],
        "instruction": step["instruction"],
        "type": step["type"],
        "dispense": step.get("dispense"),
        "total_steps": len(recipe["steps"]),
        "demo_state": system_state["demo_state"],
    }


@app.post("/advance_step")
def advance_step():
    """Advance to the next recipe step."""
    recipe = system_state.get("recipe")
    if not recipe:
        raise HTTPException(status_code=400, detail="No recipe loaded.")

    steps = recipe["steps"]
    idx = system_state["current_step_index"]

    if idx >= len(steps) - 1:
        _set_state("complete")
        system_state["current_step_index"] = len(steps)
        return {"ok": True, "demo_state": "complete", "step": None}

    system_state["current_step_index"] = idx + 1
    new_step = _current_step()

    if new_step["type"] == "dispense":
        _set_state("dispensing_step")
    else:
        _set_state("user_cook_step")

    return {
        "ok": True,
        "demo_state": system_state["demo_state"],
        "step": new_step,
    }


# ---------------------------------------------------------------------------
# Endpoints — dispense
# ---------------------------------------------------------------------------


@app.post("/dispense_step")
def dispense_step():
    """
    Issue a dispense command for the current step.
    Writes the pending_command; ESP32 picks it up via GET /pending_command.
    """
    step = _current_step()
    if step is None:
        raise HTTPException(status_code=400, detail="No active step.")
    if step["type"] != "dispense":
        raise HTTPException(status_code=400, detail="Current step is not a dispense step.")

    dispense = step["dispense"]
    spice = dispense["spice"]

    if spice not in system_state["containers"]:
        raise HTTPException(
            status_code=400,
            detail=f"Spice '{spice}' not in containers: {system_state['containers']}",
        )

    container_index = system_state["containers"].index(spice)
    command = {
        "command_id": _make_command_id(),
        "action": "dispense",
        "spice": spice,
        "container_index": container_index,
        "target_grams": dispense["grams"],
    }
    system_state["pending_command"] = command
    system_state["dispense_status"] = "dispensing"
    _set_state("dispensing_step")

    return {"ok": True, "command": command}


@app.get("/pending_command")
def pending_command():
    """ESP32 polls here to receive dispense commands."""
    cmd = system_state.get("pending_command")
    if cmd:
        return cmd
    return {"command_id": None, "action": "none"}


@app.post("/command_result")
def command_result(payload: CommandResultPayload):
    """ESP32 reports the outcome of a dispense command."""
    pending = system_state.get("pending_command")
    if pending and pending["command_id"] != payload.command_id:
        raise HTTPException(status_code=400, detail="command_id mismatch.")

    system_state["pending_command"] = None

    if payload.weight is not None:
        system_state["weight"] = payload.weight
    if payload.actual_grams is not None:
        system_state["last_dispense_actual_grams"] = payload.actual_grams

    if payload.status == "done":
        system_state["dispense_status"] = "done"
    else:
        system_state["dispense_status"] = "error"
        system_state["last_error"] = f"Dispense failed for cmd {payload.command_id}"
        _set_state("error")

    return {"ok": True, "dispense_status": system_state["dispense_status"]}


# ---------------------------------------------------------------------------
# Endpoints — hardware status
# ---------------------------------------------------------------------------


@app.post("/tare")
def tare_scale():
    """Send a tare command to the ESP32 load cell via BLE bridge."""
    command = {
        "command_id": _make_command_id(),
        "action": "tare",
    }
    system_state["pending_command"] = command
    system_state["weight"] = 0.0
    return {"ok": True, "command": command}


@app.post("/status_update")
def status_update(payload: StatusUpdatePayload):
    """ESP32 heartbeat — updates weight and container levels."""
    system_state["weight"] = payload.weight
    if payload.container_levels and len(payload.container_levels) == 3:
        system_state["container_levels"] = payload.container_levels
    return {"ok": True}


@app.post("/set_containers")
def set_containers(payload: SetContainersPayload):
    """Set the 3 spice slots using a permutation of the canonical spice names."""
    if len(payload.containers) != 3:
        raise HTTPException(status_code=400, detail="Must provide exactly 3 container names.")

    cleaned = [" ".join(item.strip().lower().split()) for item in payload.containers]
    if set(cleaned) != VALID_CONTAINER_SET:
        raise HTTPException(
            status_code=400,
            detail=f"Containers must be a permutation of {CONTAINERS}.",
        )

    system_state["containers"] = cleaned
    return {"ok": True, "containers": system_state["containers"]}


# ---------------------------------------------------------------------------
# Endpoints — manual override
# ---------------------------------------------------------------------------


@app.post("/manual_override")
def manual_override(payload: ManualOverridePayload):
    """Toggle manual override mode."""
    system_state["manual_override"] = payload.active
    if payload.active:
        system_state["resume_state"] = system_state["demo_state"]
        _set_state("manual_override")
    else:
        _set_state(system_state.get("resume_state") or "idle")
    return {"ok": True, "manual_override": system_state["manual_override"]}


@app.post("/manual_dispense")
def manual_dispense(payload: ManualDispensePayload):
    """Directly command a dispense in manual override mode."""
    if not system_state["manual_override"]:
        raise HTTPException(status_code=403, detail="Manual override is not active.")
    if payload.spice not in system_state["containers"]:
        raise HTTPException(
            status_code=400,
            detail=f"Spice '{payload.spice}' not in containers.",
        )
    container_index = system_state["containers"].index(payload.spice)
    command = {
        "command_id": _make_command_id(),
        "action": "dispense",
        "spice": payload.spice,
        "container_index": container_index,
        "target_grams": payload.grams,
    }
    system_state["pending_command"] = command
    system_state["dispense_status"] = "dispensing"
    return {"ok": True, "command": command}
