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