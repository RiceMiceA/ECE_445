from fastapi import FastAPI
from pydantic import BaseModel
from fastapi.staticfiles import StaticFiles
import requests

app = FastAPI()
app.mount("/main", StaticFiles(directory="main", html=True), name="main")

# --------- Data Models ---------

class ContainerConfig(BaseModel):
    containers: list[str]

class Spice(BaseModel):
    name: str
    grams: float

class Recipe(BaseModel):
    recipe_name: str
    ingredients: list[str]
    spices: list[Spice]

# --------- Memory ---------

system_state = {
    "recipe": None,
    "container_levels": [0, 0, 0],
    "weight": 0.0,
    "dispensing": False,
    "containers": ["Container 1 Not Set", "Container 2 Not Set", "Container 3 Not Set"]
}

ESP_IP = "0.0.0.0"

# --------- Post Endpoints (set variables/state in memory) ---------

# Used by the Meta Quest 3 to post the recipe it has generated
@app.post("/new_recipe")
def new_recipe(recipe: Recipe):
    system_state["recipe"] = recipe
    return {"message": "Recipe received successfully"}

# Used by the ESP32 to update the app with sensor data (IR & Load Cell)
@app.post("/status_update")
def status_update(status: dict):
    system_state["container_levels"] = status.get("container_levels", system_state["container_levels"])
    system_state["weight"] = status.get("weight", system_state["weight"])
    return {"message": "Status updated"}

# Used by frontend to allow user to config the containers
@app.post("/set_containers")
def set_containers(config: ContainerConfig):
    system_state["containers"] = config.containers
    return {"message": "Container configuration updated"}

@app.post("/spin_motor")
def spin_motor():
    r = requests.post(f"http://{ESP_IP}/spin")
    return {"esp_response": r.text}

# --------- Get Endpoints (get data from backend) ---------

# Generic get to send the current state
@app.get("/status")
def get_status():
    return {
        "state": system_state
        }

# Send every spice in the current recipe
@app.get("/spices")
def get_spices():
    if system_state["recipe"] == None:
        return None
    else:
        return system_state["recipe"].spices
    
# Only send the spices that are in the current container
@app.get("/spice_dispense")
def get_spice_dispense():
    dispensable_spices = []
    for s in system_state["recipe"].spices:
        if s.name in system_state["containers"]:
            dispensable_spices.append(s)
    return dispensable_spices

# Send container level info to the frontend
@app.get("/container_info")
def get_container_info():
    return [
        {"id": 1, "spice": system_state["containers"][0], "level_percent": system_state["container_levels"][0]},
        {"id": 2, "spice": system_state["containers"][1], "level_percent": system_state["container_levels"][1]},
        {"id": 3, "spice": system_state["containers"][2], "level_percent": system_state["container_levels"][2]}
    ]

# --------- Internal Functions ---------

def grams_to_steps(int):
    return 0

# --------- Testing JSONs ---------
"""
http://127.0.0.1:8000/docs to get to the fastAPI backend testing page

{
  "recipe_name": "yummy",
  "ingredients": [
    "just one egg"
  ],
  "spices": [
    {
      "name": "pepper",
      "grams": 5
    },
    {
      "name": "cumin",
      "grams": 10
    }
  ]
}


{
  "containers": [
    "salt", 
    "pepper", 
    "paprika"
  ]
}


"""