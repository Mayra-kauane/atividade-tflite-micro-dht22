from __future__ import annotations

import csv
import json
import math
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATASET = ROOT / "data" / "dht_comfort_dataset.csv"
MODELS_DIR = ROOT / "models"
GENERATED_DIR = ROOT / "main" / "generated"
HEADER = GENERATED_DIR / "comfort_model.h"
MODEL_JSON = MODELS_DIR / "comfort_model.json"
METRICS_JSON = MODELS_DIR / "metrics.json"


def read_dataset() -> list[dict[str, float | str]]:
    rows: list[dict[str, float | str]] = []
    with DATASET.open(newline="", encoding="utf-8") as csv_file:
        for row in csv.DictReader(csv_file):
            rows.append(
                {
                    "temperature_c": float(row["temperature_c"]),
                    "humidity_percent": float(row["humidity_percent"]),
                    "comfort": row["comfort"],
                }
            )
    return rows


def train_centroids(rows: list[dict[str, float | str]]) -> list[dict[str, float | str | int]]:
    groups: dict[str, list[dict[str, float | str]]] = defaultdict(list)
    for row in rows:
        groups[str(row["comfort"])].append(row)

    centroids: list[dict[str, float | str | int]] = []
    for label in sorted(groups):
        samples = groups[label]
        centroids.append(
            {
                "label": label,
                "temperature_c": sum(float(sample["temperature_c"]) for sample in samples) / len(samples),
                "humidity_percent": sum(float(sample["humidity_percent"]) for sample in samples) / len(samples),
                "samples": len(samples),
            }
        )
    return centroids


def classify(
    temperature_c: float,
    humidity_percent: float,
    centroids: list[dict[str, float | str | int]],
) -> str:
    best_label = str(centroids[0]["label"])
    best_distance = math.inf

    for centroid in centroids:
        temp_delta = temperature_c - float(centroid["temperature_c"])
        humidity_delta = (humidity_percent - float(centroid["humidity_percent"])) / 5.0
        distance = temp_delta * temp_delta + humidity_delta * humidity_delta
        if distance < best_distance:
            best_distance = distance
            best_label = str(centroid["label"])

    return best_label


def evaluate(
    rows: list[dict[str, float | str]],
    centroids: list[dict[str, float | str | int]],
) -> dict[str, object]:
    labels = [str(centroid["label"]) for centroid in centroids]
    confusion = {label: {predicted: 0 for predicted in labels} for label in labels}
    correct = 0

    for row in rows:
        expected = str(row["comfort"])
        predicted = classify(
            float(row["temperature_c"]),
            float(row["humidity_percent"]),
            centroids,
        )
        confusion[expected][predicted] += 1
        if predicted == expected:
            correct += 1

    return {
        "accuracy": correct / len(rows),
        "correct": correct,
        "total": len(rows),
        "confusion_matrix": confusion,
    }


def write_header(
    rows: list[dict[str, float | str]],
    centroids: list[dict[str, float | str | int]],
) -> None:
    lines = [
        "#pragma once",
        "",
        "struct ComfortCentroid {",
        "    const char *label;",
        "    float temperature_c;",
        "    float humidity_percent;",
        "};",
        "",
        "static constexpr ComfortCentroid kComfortCentroids[] = {",
    ]

    for centroid in centroids:
        lines.append(
            f'    {{"{centroid["label"]}", '
            f'{float(centroid["temperature_c"]):.2f}f, '
            f'{float(centroid["humidity_percent"]):.2f}f}},'
        )

    lines.extend(
        [
            "};",
            "",
            f"static constexpr int kComfortDatasetRows = {len(rows)};",
            f"static constexpr int kComfortClassCount = {len(centroids)};",
            "",
        ]
    )

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    HEADER.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    rows = read_dataset()
    centroids = train_centroids(rows)
    metrics = evaluate(rows, centroids)

    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    write_header(rows, centroids)

    model = {
        "type": "nearest_centroid",
        "inputs": ["temperature_c", "humidity_percent"],
        "output": "comfort",
        "distance": "squared_euclidean_with_humidity_scaled_by_5",
        "centroids": centroids,
    }
    MODEL_JSON.write_text(json.dumps(model, indent=2, ensure_ascii=False), encoding="utf-8")
    METRICS_JSON.write_text(
        json.dumps(
            {
                "hello_world": {
                    "model": "int8 TensorFlow Lite Micro sine regression model embedded in main/model.cc",
                    "runtime": "esp-tflite-micro",
                },
                "dht_comfort": {
                    "dataset": str(DATASET.relative_to(ROOT)),
                    "generated_header": str(HEADER.relative_to(ROOT)),
                    "model_file": str(MODEL_JSON.relative_to(ROOT)),
                    **metrics,
                },
            },
            indent=2,
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )

    print(f"Dataset: {DATASET.relative_to(ROOT)}")
    print(f"Amostras: {len(rows)}")
    for centroid in centroids:
        print(
            f"{centroid['label']}: "
            f"temperatura={float(centroid['temperature_c']):.2f}, "
            f"umidade={float(centroid['humidity_percent']):.2f}, "
            f"amostras={centroid['samples']}"
        )
    print(f"Acuracia no dataset: {metrics['accuracy']:.2%}")
    print(f"Modelo gerado: {MODEL_JSON.relative_to(ROOT)}")
    print(f"Metricas geradas: {METRICS_JSON.relative_to(ROOT)}")
    print(f"Header gerado: {HEADER.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
