# NuChef — R&V Results

This folder holds output from the R&V measurement tools.

Generated files:
- `latency_burst.csv` / `latency_burst_summary.txt` — from `tools/latency_burst_test.py`
- `connection_soak.csv` / `connection_soak_summary.txt` — from `tools/connection_soak_test.py`
- `vision_val_summary.txt` — manual YOLO validation metrics entry
- `confusion_matrix.png` — YOLO validation confusion matrix
- `sample_predictions/` — annotated prediction images

Generate with:
    python tools/latency_burst_test.py --url http://<backend>:8000
    python tools/connection_soak_test.py --url http://<backend>:8000 --duration-min 30
