/**
 * AI Cooking Assistant — Dashboard Script
 * Talks to FastAPI backend at BACKEND_URL.
 */

const BACKEND_URL = window.location.origin;   // same origin as the UI
// const BACKEND_URL = "http://192.168.197.86:8000";   // Triangle
// const BACKEND_URL_ESP = "http://192.168.4.28:8000";    // 508 E John
const POLL_MS = 2000;
const DEFAULT_CONTAINER_POSITIONS = ["left", "middle", "right"];

// ── Helpers ──────────────────────────────────────────────────────────────────

async function api(path, method = "GET", body = null) {
    const opts = {
        method,
        headers: { "Content-Type": "application/json" },
    };
    if (body !== null) opts.body = JSON.stringify(body);
    const res = await fetch(BACKEND_URL + path, opts);
    if (!res.ok) {
        const err = await res.json().catch(() => ({ detail: res.statusText }));
        throw new Error(err.detail || res.statusText);
    }
    return res.json();
}

function toast(msg, isError = false) {
    const el = document.getElementById("last-error");
    el.textContent = isError ? `⚠ ${msg}` : msg;
    el.style.color = isError ? "#ff6b6b" : "#aaffaa";
    setTimeout(() => { el.textContent = ""; }, 5000);
}

// ── State rendering ───────────────────────────────────────────────────────────

const STATE_COLORS = {
    idle: "idle",
    scanning: "scanning",
    ingredients_confirmed: "confirmed",
    recipe_ready: "ready",
    dispensing_step: "dispensing",
    user_cook_step: "cook",
    complete: "complete",
    error: "error",
    manual_override: "manual",
};

function renderState(state) {
    const badge = document.getElementById("state-badge");
    badge.textContent = state.demo_state.replace(/_/g, " ").toUpperCase();
    badge.className = "badge " + (STATE_COLORS[state.demo_state] || "idle");

    // Candidates
    renderList("candidate-list", state.candidate_ingredients, "None detected");

    // Confirmed
    renderList("confirmed-list", state.confirmed_ingredients, "None confirmed");

    // Weight (pulse when value changes)
    const wEl = document.getElementById("weight-display");
    const newW = state.weight.toFixed(2) + " g";
    if (wEl.textContent !== newW) {
        wEl.textContent = newW;
        wEl.classList.add("pulse");
        setTimeout(() => wEl.classList.remove("pulse"), 600);
    }

    // Dispense status
    const ds = document.getElementById("dispense-status");
    ds.textContent = state.dispense_status;
    ds.className = "status-chip " + state.dispense_status;

    // Containers
    renderContainers(
        state.containers,
        state.container_levels,
        state.container_positions || DEFAULT_CONTAINER_POSITIONS
    );

    // Fill container edit fields if empty
    ["c0", "c1", "c2"].forEach((id, i) => {
        const el = document.getElementById(id);
        el.placeholder = `${(state.container_positions || DEFAULT_CONTAINER_POSITIONS)[i]} slot`;
        if (!el.value) el.value = state.containers[i] || "";
    });

    // Pending command
    const cmdEl = document.getElementById("pending-command");
    cmdEl.textContent = state.pending_command
        ? JSON.stringify(state.pending_command, null, 2)
        : "none";

    // Error
    if (state.last_error) toast(state.last_error, true);

    // Recipe
    if (state.recipe) {
        renderRecipe(state.recipe, state.current_step_index, state.demo_state);
    }

    // Button states
    const hasRecipe = !!state.recipe;
    const step = hasRecipe
        ? state.recipe.steps[state.current_step_index]
        : null;
    const isDispenseStep = step && step.action === "season";
    const notComplete = state.demo_state !== "complete";

    document.getElementById("btn-dispense").disabled =
        !isDispenseStep || state.dispense_status === "dispensing";
    document.getElementById("btn-advance").disabled =
        !hasRecipe || !notComplete;
}

function renderList(id, items, emptyMsg) {
    const ul = document.getElementById(id);
    if (!items || items.length === 0) {
        ul.innerHTML = `<li class="empty">${emptyMsg}</li>`;
    } else {
        ul.innerHTML = items.map((i) => `<li>${i}</li>`).join("");
    }
}

function renderContainers(containers, levels, positions) {
    const el = document.getElementById("containers-info");
    el.innerHTML = containers
        .map(
            (name, i) => `
      <div class="container-row">
                <span class="container-name">${positions[i]} · ${name}</span>
        <div class="level-bar-wrap">
          <div class="level-bar" style="width:${levels[i] || 0}%"></div>
        </div>
        <span class="level-pct">${levels[i] || 0}%</span>
      </div>`
        )
        .join("");
}

function renderRecipe(recipe, currentIdx, demoState) {
    document.getElementById("recipe-name").textContent = recipe.name;
    document.getElementById("recipe-desc").textContent = recipe.description;

    const container = document.getElementById("steps-container");
    container.innerHTML = recipe.steps
        .map((step) => {
            let cls = "step";
            if (step.index < currentIdx) cls += " done";
            else if (step.index === currentIdx) cls += " active";

            const dispenseTag =
                step.action === "season" && step.dispense
                    ? `<span class="dispense-tag">${step.dispense.spice} · ${step.dispense.grams}g</span>`
                    : "";

            return `
        <div class="${cls}">
          <span class="step-num">${step.index + 1}</span>
          <span class="step-action-badge">${step.action}</span>
          <span class="step-text">${step.display_text}</span>
          ${dispenseTag}
        </div>`;
        })
        .join("");
}

// ── Button handlers ───────────────────────────────────────────────────────────

document.getElementById("btn-tare").addEventListener("click", async () => {
    try {
        await api("/tare", "POST");
        toast("Tare command sent — scale zeroed.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-reset").addEventListener("click", async () => {
    try {
        await api("/reset", "POST");
        document.getElementById("recipe-name").textContent = "—";
        document.getElementById("recipe-desc").textContent = "";
        document.getElementById("steps-container").innerHTML =
            '<p class="empty">No recipe loaded</p>';
        toast("System reset.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-confirm").addEventListener("click", async () => {
    const raw = document.getElementById("ingredient-input").value;
    const ingredients = raw
        .split(",")
        .map((s) => s.trim())
        .filter(Boolean);
    if (!ingredients.length) {
        toast("Enter at least one ingredient.", true);
        return;
    }
    try {
        await api("/ingredients_confirmed", "POST", { ingredients });
        toast("Ingredients confirmed.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-generate").addEventListener("click", async () => {
    try {
        await api("/generate_recipe", "POST", {});
        toast("Recipe generated!");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-dispense").addEventListener("click", async () => {
    try {
        await api("/dispense_step", "POST");
        toast("Dispense command sent to ESP32.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-advance").addEventListener("click", async () => {
    try {
        const res = await api("/advance_step", "POST");
        if (res.demo_state === "complete") toast("Recipe complete! 🎉");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-set-containers").addEventListener("click", async () => {
    const containers = ["c0", "c1", "c2"].map(
        (id) => document.getElementById(id).value.trim()
    );
    if (containers.some((c) => !c)) {
        toast("All three container names required.", true);
        return;
    }
    try {
        await api("/set_containers", "POST", { containers });
        toast("Containers updated.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("manual-toggle").addEventListener("change", async (e) => {
    const active = e.target.checked;
    try {
        await api("/manual_override", "POST", { active });
        document.getElementById("manual-controls").classList.toggle("hidden", !active);
        toast(active ? "Manual override ON." : "Manual override OFF.");
    } catch (e) {
        toast(e.message, true);
    }
});

document.getElementById("btn-manual-dispense").addEventListener("click", async () => {
    const spice = document.getElementById("manual-spice").value;
    const grams = parseFloat(document.getElementById("manual-grams").value);
    if (!spice || isNaN(grams) || grams <= 0) {
        toast("Select spice and enter valid grams.", true);
        return;
    }
    try {
        await api("/manual_dispense", "POST", { spice, grams });
        toast(`Manual dispense: ${grams}g of ${spice} sent.`);
    } catch (e) {
        toast(e.message, true);
    }
});

// ── Polling loop ──────────────────────────────────────────────────────────────

let pollIndicatorOn = true;

async function poll() {
    try {
        const state = await api("/state");
        renderState(state);
        pollIndicatorOn = !pollIndicatorOn;
        document.getElementById("poll-indicator").style.opacity = pollIndicatorOn
            ? "1"
            : "0.3";
    } catch (e) {
        console.warn("Poll error:", e.message);
    }
}

poll(); // immediate first fetch
setInterval(poll, POLL_MS);
