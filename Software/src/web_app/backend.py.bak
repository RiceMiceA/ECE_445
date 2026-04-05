"""
AI Nutritious Culinary Assistant — Backend Orchestrator
FastAPI state-machine backend for the cooking assistant system.

Run:
    pip install fastapi uvicorn
    uvicorn backend:app --host 0.0.0.0 --port 8000 --reload
"""

from __future__ import annotations

import uuid
import datetime
from copy import deepcopy
from typing import Optional
from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# ---------------------------------------------------------------------------
# App setup
# ---------------------------------------------------------------------------

app = FastAPI(title="AI Cooking Assistant", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Serve the web dashboard at /
app.mount("/ui", StaticFiles(directory="main", html=True), name="frontend")


# ---------------------------------------------------------------------------
# Constants — canonical spice names, fixed container layout
# ---------------------------------------------------------------------------

CONTAINERS: list[str] = ["salt", "black pepper", "garlic powder"]

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
    "dispense_status": "idle",             # idle | dispensing | done | error
    "manual_override": False,
    "last_error": None,
    "pending_command": None,               # command waiting for ESP32
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


def _generate_mock_recipe(ingredients: list[str]) -> dict:
    """
    Placeholder recipe generator.
    Replace this with an LLM API call (e.g. OpenAI) when ready.
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
            "instruction": "Season with salt evenly.",
            "type": "dispense",
            "dispense": {"spice": "salt", "grams": 3.0},
        },
        {
            "index": 2,
            "instruction": "Add black pepper to taste.",
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
            "instruction": "Finish with garlic powder before serving.",
            "type": "dispense",
            "dispense": {"spice": "garlic powder", "grams": 2.0},
        },
    ]
    return {
        "name": "AI Chef's Special",
        "description": f"A recipe generated from: {', '.join(ingredients)}.",
        "steps": steps,
    }


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
    system_state.update({
        "demo_state": "idle",
        "candidate_ingredients": [],
        "confirmed_ingredients": [],
        "recipe": None,
        "current_step_index": -1,
        "dispense_status": "idle",
        "manual_override": False,
        "last_error": None,
        "pending_command": None,
    })
    return {"ok": True, "demo_state": "idle"}


# ---------------------------------------------------------------------------
# Endpoints — ingredient flow
# ---------------------------------------------------------------------------

@app.post("/candidate_ingredients")
def candidate_ingredients(payload: CandidatePayload):
    """Quest pushes live detections here (non-confirmed)."""
    system_state["candidate_ingredients"] = payload.ingredients
    if system_state["demo_state"] == "idle":
        _set_state("scanning")
    return {"ok": True, "candidate_ingredients": system_state["candidate_ingredients"]}


@app.post("/ingredients_confirmed")
def ingredients_confirmed(payload: ConfirmedPayload):
    """Quest or dashboard locks in the ingredient list."""
    if not payload.ingredients:
        raise HTTPException(status_code=400, detail="Ingredient list cannot be empty.")
    system_state["confirmed_ingredients"] = payload.ingredients
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
    """Generate (mock) recipe from confirmed ingredients."""
    ingredients = payload.ingredients or system_state["confirmed_ingredients"]
    if not ingredients:
        raise HTTPException(
            status_code=400,
            detail="No confirmed ingredients. Call /ingredients_confirmed first.",
        )
    recipe = _generate_mock_recipe(ingredients)
    system_state["recipe"] = recipe
    system_state["current_step_index"] = 0
    _set_state("recipe_ready")
    return {
        "ok": True,
        "demo_state": system_state["demo_state"],
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

    # Auto-set state based on step type
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

    # Verify spice is in our containers
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

@app.post("/status_update")
def status_update(payload: StatusUpdatePayload):
    """ESP32 heartbeat — updates weight and container levels."""
    system_state["weight"] = payload.weight
    if payload.container_levels and len(payload.container_levels) == 3:
        system_state["container_levels"] = payload.container_levels
    return {"ok": True}


@app.post("/set_containers")
def set_containers(payload: SetContainersPayload):
    """Rename the three containers (must keep exactly 3)."""
    if len(payload.containers) != 3:
        raise HTTPException(status_code=400, detail="Must provide exactly 3 container names.")
    system_state["containers"] = payload.containers
    return {"ok": True, "containers": system_state["containers"]}


# ---------------------------------------------------------------------------
# Endpoints — manual override
# ---------------------------------------------------------------------------

@app.post("/manual_override")
def manual_override(payload: ManualOverridePayload):
    """Toggle manual override mode."""
    system_state["manual_override"] = payload.active
    if payload.active:
        _set_state("manual_override")
    else:
        _set_state("idle")
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
