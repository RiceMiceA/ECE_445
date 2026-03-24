# ECE_445
For Sp26 ECE445 Project - Griffin, Jackson, Tony




## Managing the submodule Unity folder
For pull and fetch from remote
### Freshly Clone 1st time
```bash
git clone --recurse-submodules <repo-url>
```

This does everything:
- clones the parent repo
- initializes submodules
- checks out the correct commits

### Already cloned repo
```bash
git submodule update --init --recursive
```

### Modification towards Submodule
```bash
cd path/to/submodule
git checkout main
git pull origin main
git add .
git commit -m "Update submodule"
git push origin main

cd /path/to/parent-repo
git add path/to/submodule
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
```