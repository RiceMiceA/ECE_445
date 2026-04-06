async function setContainers() {
    console.log("Button clicked!");
    const c1 = document.getElementById("container1").value.trim();
    const c2 = document.getElementById("container2").value.trim();
    const c3 = document.getElementById("container3").value.trim();

    // if (!c1 || !c2 || !c3) {
    //     alert("All three containers must have a spice.");
    //     return;
    // }

    const payload = {
        containers: [c1, c2, c3]
    };

    const response = await fetch("/set_containers", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify(payload)
    });

    const data = await response.json();
    alert(data.message);

    loadContainers();
}


async function loadContainers(){
    console.log("Containers Loaded!");
    const response = await fetch("/container_info");
    const containers = await response.json();

    const containerDiv = document.getElementById("containers");
    containerDiv.innerHTML = "";

    containers.forEach(c => {
        const card = document.createElement("div");
        card.className = "container-card";

        card.innerHTML = `
            <h3>${c.spice} (${c.level_percent}%)</h3>
            <div class="progress-bar">
                <div class="progress-fill" style="width: ${c.level_percent}%"></div>
            </div>
        `;

        containerDiv.appendChild(card);
    });
}

async function spinMotor(){
    const ip = document.getElementById("IP_Input").value;
    
    const payload = {
        ip: ip
    };

    const response = await fetch("/spin_motor", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify(payload)
    });

    const data = await response.json();
    console.log(data);
}


loadContainers();
// Auto-refresh every 2 seconds
setInterval(loadContainers, 2000);
document.getElementById("setContainersBtn").addEventListener("click", setContainers);
document.getElementById("spinMotor").addEventListener("click", spinMotor);
