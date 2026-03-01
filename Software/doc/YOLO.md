# ECE 445

Links:

- Review on CNN: Basic CNN Knowledge Check
    - Tensor operation through torch.
    - Loss Function;
    - Score Function;
    - Optimization using SGD.
- YOLO Paper: https://arxiv.org/abs/1506.02640
- Fine-tune YOLO Model: https://docs.ultralytics.com/guides/model-testing/
- Citation: 1: https://www.nature.com/articles/s41598-025-15755-6
- From 2022 and relatable: https://arxiv.org/abs/2203.06721
- From 2023: https://pmc.ncbi.nlm.nih.gov/articles/PMC10607895/
- Dataset: https://universe.roboflow.com/huyifei/tft-id
- 

## YOLO

- S*S grids, B bounding boxes, C number of classes, 5-number predictions: (x, y, $\omega$, h, confidence).
- Therefore all the predictions are in a S*S*(B*5+C) tensor.

## Performance Table

| **Model Name** | **Dataset** | **Accuracy (%)** | **Precision (%)** | **Recall (%)** | **F1 Score** | **Inference Time (ms)** | **Notes** |
| --- | --- | --- | --- | --- | --- | --- | --- |
| YOLOv11  |  |  |  |  |  |  |  |
| YOLOv11-FT |  |  |  |  |  |  |  |
| CNN Custom |  |  |  |  |  |  |  |
|  |  |  |  |  |  |  |  |