# ECE_445
For Sp26 ECE445 Project - Griffin, Jackson, Tony

## 1. Init
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


## 2. Managing the submodule Unity folder
### 2.1 Before Work/Update
#### In parent folder
```bash
git pull
git submodule update --init --recursive
```

### 2.2 Post-work
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
When switching networks, update the backend IP (`192.168.x.x`) in these files:

| File | Line | Variable |
|------|------|----------|
| [`Software/src/Unity-PassthroughCameraApiSamples/Assets/PassthroughCameraApiSamples/MultiObjectDetection/DetectionManager/Scripts/BackendClient.cs`](Software/src/Unity-PassthroughCameraApiSamples/Assets/PassthroughCameraApiSamples/MultiObjectDetection/DetectionManager/Scripts/BackendClient.cs#L19) | 19 | `m_baseUrl` |
| [`Software/src/esp_driver/src/main.cpp`](Software/src/esp_driver/src/main.cpp#L29) | 29 | `BACKEND` |
| [`Software/src/web_app/main/script.js`](Software/src/web_app/main/script.js#L6) | 6 | `BACKEND_URL` |

Then start the backend on all interfaces:
```bash
uvicorn backend:app --host 0.0.0.0 --port 8000 --reload
```


# State Machine
$$ IDLE \rightarrow SCANNING \rightarrow INGREDIENTS_CONFIRMED \rightarrow RECIPE_READY \rightarrow DISPENSING_STEP \rightarrow USER_COOK_STEP \rightarrow COMPLETE $$