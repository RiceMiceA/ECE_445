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

    // Confirmed — show per-label counts from ingredient_instances (accumulates markers).
    renderConfirmedList("confirmed-list", state.ingredient_instances, state.confirmed_ingredients);

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

/**
 * Renders the confirmed ingredient list with per-label counts and average weights.
 * Uses ingredient_instances (array of {label, weight_g, is_measured, display_name})
 * which preserves duplicates. Falls back to plain confirmed_ingredients if instances
 * is empty (e.g. before X is pressed).
 */
function renderConfirmedList(id, instances, fallbackLabels) {
    const ul = document.getElementById(id);
    if (!instances || instances.length === 0) {
        if (!fallbackLabels || fallbackLabels.length === 0) {
            ul.innerHTML = `<li class="empty">None confirmed</li>`;
        } else {
            ul.innerHTML = fallbackLabels.map((l) => `<li>${l}</li>`).join("");
        }
        return;
    }
    // Tally per-label counts and total measured weight.
    const map = new Map(); // label -> {count, measured, totalW}
    for (const inst of instances) {
        const label = inst.label || inst;
        if (!map.has(label)) map.set(label, { count: 0, measured: 0, totalW: 0 });
        const e = map.get(label);
        e.count++;
        if (inst.is_measured) { e.measured++; e.totalW += inst.weight_g || 0; }
    }
    ul.innerHTML = [...map.entries()].map(([label, e]) => {
        const countBadge = e.count > 1
            ? `<span class="ing-count">×${e.count}</span>` : "";
        const weightNote = e.measured > 0
            ? `<span class="ing-weight">${(e.totalW / e.measured).toFixed(1)}g avg</span>` : "";
        return `<li>${label}${countBadge}${weightNote}</li>`;
    }).join("");
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

// ── Motor calibration ───────────────────────────────────────────────────────

document.querySelectorAll(".motor-step-btn").forEach((btn) => {
    btn.addEventListener("click", async () => {
        const motor = parseInt(btn.dataset.motor, 10);
        const dir = parseInt(btn.dataset.dir, 10);
        const stepsInput = document.getElementById(`motor-steps-${motor}`);
        const steps = dir * Math.abs(parseInt(stepsInput.value, 10) || 10);
        try {
            await api("/motor_step", "POST", { motor, steps });
            toast(`Motor ${motor}: ${steps > 0 ? "+" : ""}${steps} steps queued.`);
        } catch (e) {
            toast(e.message, true);
        }
    });
});

document.getElementById("btn-motor-reset-pos").addEventListener("click", async () => {
    try {
        await api("/motor_reset_position", "POST");
        toast("Motor positions reset to zero.");
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

// ── Vision R&V HUD polling (every 500 ms) ─────────────────────────────────────

const RV_POLL_MS = 500;
let rvPollDotOn = true;

function passClass(flag) {
    if (flag === null || flag === undefined) return "";
    return flag ? "pass" : "fail";
}
function passLabel(flag, label) {
    if (flag === null || flag === undefined) return label + ": —";
    return label + ": " + (flag ? "PASS ✓" : "FAIL ✗");
}
function fmtMs(v) {
    return v != null ? v.toFixed(1) + " ms" : "—";
}

function renderRvState(rv) {
    // Last vision frame
    const lf = rv.last_vision_frame;
    if (lf) {
        document.getElementById("rv-frame-id").textContent    = lf.frame_id ?? "—";
        document.getElementById("rv-fps").textContent         = lf.fps != null ? lf.fps.toFixed(1) : "—";
        document.getElementById("rv-infer-ms").textContent    = lf.inference_ms != null ? lf.inference_ms.toFixed(0) + " ms" : "—";
        const dets = lf.detections || [];
        const capPass = dets.length <= 20;
        document.getElementById("rv-det-count").textContent   = dets.length + " / 20" + (capPass ? " ✓" : " ✗");
        document.getElementById("rv-det-count").style.color   = capPass ? "var(--accent2)" : "var(--danger)";

        // Detection table
        const tbody = document.getElementById("rv-det-body");
        if (dets.length === 0) {
            tbody.innerHTML = '<tr><td colspan="4" class="empty">—</td></tr>';
        } else {
            tbody.innerHTML = dets.map((d, i) => {
                const bb = d.bbox_xywh ? d.bbox_xywh.map(v => Math.round(v)).join(", ") : "—";
                return `<tr>
                    <td>${i + 1}</td>
                    <td>${d.label}</td>
                    <td>${d.confidence.toFixed(2)}</td>
                    <td>[${bb}]</td>
                </tr>`;
            }).join("");
        }
    }
    document.getElementById("rv-frame-count").textContent = rv.vision_frame_count ?? "—";

    // Staleness indicator — color-codes how fresh the last frame is.
    const staleEl = document.getElementById("rv-staleness");
    if (rv.vision_last_receive_ms) {
        const ageS = (Date.now() - rv.vision_last_receive_ms) / 1000;
        staleEl.textContent = ageS < 1 ? "<1 s ago" : ageS.toFixed(1) + " s ago";
        staleEl.style.color = ageS < 2 ? "var(--accent2)" : ageS < 5 ? "#f5a623" : "var(--danger)";
    } else {
        staleEl.textContent = "—";
        staleEl.style.color = "inherit";
    }

    // Planner
    document.getElementById("rv-demo-state").textContent  = rv.demo_state || "—";
    document.getElementById("rv-candidates").textContent  =
        rv.candidate_ingredients?.length ? rv.candidate_ingredients.join(", ") : "(none)";
    document.getElementById("rv-confirmed").textContent   =
        rv.confirmed_ingredients?.length ? rv.confirmed_ingredients.join(", ") : "(none)";
    const step = rv.current_step;
    document.getElementById("rv-step").textContent = step
        ? `[${step.index}] ${step.action} — ${step.display || ""}` : "—";

    // Comm / latency
    document.getElementById("rv-evt-count").textContent = rv.rv_event_count ?? "—";
    document.getElementById("rv-p50").textContent        = fmtMs(rv.rv_latency_p50_ms);
    document.getElementById("rv-p95").textContent        = fmtMs(rv.rv_latency_p95_ms);
    document.getElementById("rv-max").textContent        = fmtMs(rv.rv_latency_max_ms);
    document.getElementById("rv-dups").textContent       = rv.rv_duplicate_count ?? "—";
    document.getElementById("rv-weight").textContent     =
        rv.weight != null ? rv.weight.toFixed(2) + " g" : "—";

    // PASS/FAIL badges
    function setBadge(id, flag, label) {
        const el = document.getElementById(id);
        el.textContent  = passLabel(flag, label);
        el.className    = "rv-badge " + passClass(flag);
    }
    setBadge("rv-badge-det",   rv.pass_detection_cap, "Det ≤ 20");
    setBadge("rv-badge-p95",   rv.pass_latency_p95,   "p95 ≤ 150 ms");
    setBadge("rv-badge-drops", rv.pass_no_dropped,    "No drops");
}

async function pollRv() {
    try {
        const rv = await api("/rv_state");
        renderRvState(rv);
        rvPollDotOn = !rvPollDotOn;
        document.getElementById("rv-poll-dot").style.opacity = rvPollDotOn ? "1" : "0.3";
    } catch (e) {
        console.warn("RV poll error:", e.message);
    }
}

pollRv();
setInterval(pollRv, RV_POLL_MS);
