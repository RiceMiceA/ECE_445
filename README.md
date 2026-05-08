# NuChef<sup>TM</sup> - AI Culinary Assistant


## Table of Contents
1. [Final Product](#1-final-product)
    - [Photos](#photos)
    - [YouTube Demo](#youtube-demo)
2. [How to use our repo?](#2-how-to-use-our-repo)
    - [Init](#21-init)
    - [Managing the submodule Unity folder](#22-managing-the-submodule-unity-folder)
    - [Network Configuration](#3-network-configuration)
    - [Troubleshooting](#4-troubleshooting)
5. [Third-Party Libraries & Licenses](#5-third-party-libraries--licenses)

## 1. Final Product
### Photos
![alt text](Software/src/images/Images-20260507T234808Z-3-001/Images/IMG_7206.png)
![alt text](Software/src/images/Images-20260507T234808Z-3-001/Images/IMG_9755.png)



### YouTube Demo
[![NuChef Demo](https://img.youtube.com/vi/w1jBzbUWaAI/maxresdefault.jpg)](https://www.youtube.com/watch?v=w1jBzbUWaAI)

## 2. How to use our repo?



### 2.1 Init
#### Freshly Clone 1st time
```bash
git clone --recurse-submodules <repo-url>
```

This does everything:
- clones the parent repo
- initializes submodules
- checks out the correct commits

#### Already cloned repo
```bash
git submodule update --init --recursive
```


### 2.2 Managing the submodule Unity folder
#### 2.2.1 Before Work/Update
#### In parent folder
```bash
git pull
git submodule update --init --recursive
```

#### 2.2.2 Post-work
```bash
cd path/to/submodule
git checkout main
git pull origin main
git add .
git commit -m "Update submodule"
git push origin main

cd /path/to/parent-repo
git add Software/src/Unity-PassthroughCameraApiSamples
git commit -m "Bump submodule"
git push origin main
```


### Verify Health
```bash
git submodule status
```

### Tip
```bash
git config --global submodule.recurse true
# if we wanna track the specific branch
git submodule set-branch --branch tony Software/src/Unity-PassthroughCameraApiSamples
git submodule update --remote
```



## 3. Network Configuration

### Kill previous run
```bash
kill $(lsof -t -i:8000) 
```

### Update IP Address before operation
```bash
Check the current ip address: 
```
When switching networks, update the backend IP (`192.168.x.x`) in these files:


| File                                                                                                                                                                                                                                                                                                       | Line | Variable      |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---- | ------------- |
| [`Software/src/Unity-PassthroughCameraApiSamples/Assets/PassthroughCameraApiSamples/MultiObjectDetection/DetectionManager/Scripts/BackendClient.cs`](Software/src/Unity-PassthroughCameraApiSamples/Assets/PassthroughCameraApiSamples/MultiObjectDetection/DetectionManager/Scripts/BackendClient.cs#L19) | 19   | `m_baseUrl`   |
| [`Software/src/web_app/main/script.js`](Software/src/web_app/main/script.js#L6)                                                                                                                                                                                                                            | 6    | `BACKEND_URL` |

> **Note:** The ESP32 now communicates via **BLE** (not WiFi), so it no longer needs an IP address or network config.

Then start the backend and BLE bridge:
```bash
# Terminal 1 — backend
cd Software/src/web_app
source .env   # loads OPENAI_API_KEY, OPENAI_MODEL, USE_LLM_PLANNER from .env file
uvicorn backend:app --host 0.0.0.0 --port 8000 --reload

# Terminal 2 — BLE bridge (connects to ESP32 over Bluetooth, forwards to backend)
pip install bleak httpx   # first time only
python ble_bridge.py
```


## 4. Troubleshooting

### Upload new ESP32 file
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -20
```

### ESP32 BLE not broadcasting / bridge can't find device
1. **Press the physical RESET button** on the ESP32-S3 board. It will re-advertise as `NuChef-Dispenser` within a few seconds.
2. If the bridge still can't find it, kill and restart `ble_bridge.py`. Linux's BlueZ stack sometimes caches stale BLE data.
3. As a last resort, power-cycle the ESP32 (unplug & replug USB).

### Serial port changed (`/dev/ttyACM0` ↔ `/dev/ttyACM1`)
After a reset or replug the port may shift. Check with:
```bash
ls /dev/ttyACM*
```
Then update `upload_port` and `monitor_port` in `Software/src/esp_driver/platformio.ini` if you need to re-flash.

### Permission denied on `/dev/ttyACM*`
```bash
sudo usermod -aG dialout $USER
# then log out & back in (or reboot) for the group to take effect
```

### Bridge connects but dispense doesn't trigger from the UI
- Make sure the backend (`uvicorn`) **and** `ble_bridge.py` are both running.
- Verify `BACKEND_URL` in `script.js` is `window.location.origin` (not a hardcoded IP).
- Check the bridge terminal for `BLE CMD →` and `write OK` logs when you click **Dispense**.

### Quick sanity check (motor spin from CLI)
```bash
# Direct BLE test — bypasses the backend entirely
python3 -c "
import asyncio
from bleak import BleakClient, BleakScanner
SVC  = '4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a0'
CMD  = '4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a1'
async def test():
    dev = await BleakScanner.find_device_by_name('NuChef-Dispenser', timeout=10)
    async with BleakClient(dev) as c:
        await c.write_gatt_char(CMD, b'spin', response=True)
        await asyncio.sleep(5)
        await c.write_gatt_char(CMD, b'stop', response=True)
asyncio.run(test())
"
```

## 5. Third-Party Libraries & Licenses

### Unity Utility Packages (MIT)
- **Repository:** https://github.com/meta-quest/Unity-UtilityPackages
- **Author:** Meta Platforms, Inc. and affiliates
- **License:** MIT License

> Meta Quest, "Unity-UtilityPackages," GitHub. [Online]. Available: https://github.com/meta-quest/Unity-UtilityPackages. [Accessed: May 7, 2026].

MIT License attribution:
```
Unity Utility Packages
Copyright (c) Meta Platforms, Inc. and affiliates.
Licensed under the MIT License.
https://github.com/meta-quest/Unity-UtilityPackages
```



