from math import comb, sqrt
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import norm


ROOT = Path(__file__).resolve().parent
INPUT_FILE = ROOT / "dest" / "finish_table_raw.csv"
OUTPUT_FILE = ROOT / "dest" / "finish_table_analysis.csv"

EVENTS = (
    "none",
    "at_most_1",
    "at_most_2",
    "at_most_3",
    "at_most_4",
    "fixed_1",
    "fixed_2",
    "fixed_3",
    "fixed_4",
    "cherry",
)

OLD_PROBABILITIES = {
    ("DE", 9): (
        0.033050, 0.164680, 0.456064, 0.780609, 0.960578,
        0.059376, 0.114840, 0.231897, 0.478996, 0.466012,
    ),
    ("DE", 19): (
        0.992266, 0.995732, 0.998458, 0.999692, 0.999975,
        0.992959, 0.993925, 0.995287, 0.997225, 0.998010,
    ),
    ("NE", 9): (
        0.035129, 0.160855, 0.446066, 0.772473, 0.958299,
        0.060275, 0.113941, 0.228769, 0.474564, 0.458425,
    ),
    ("NE", 19): (
        0.994554, 0.997020, 0.998928, 0.999786, 0.999982,
        0.995047, 0.995731, 0.996692, 0.998054, 0.998613,
    ),
}

ROW_COLUMNS = [f"giga_row_{row}" for row in range(1, 6)]
OUTPUT_COLUMNS = (
    "scene",
    "wave",
    "event",
    "samples",
    "old_probability",
    "iid_same_count_probability",
    "actual_probability",
    "count_effect",
    "row_effect",
    "total_effect",
    "row_se",
    "row_p_bonf",
    "total_se",
    "total_p_bonf",
)


def event_values(masks: np.ndarray) -> np.ndarray:
    masks = np.asarray(masks, dtype=np.uint32)
    occupied = (masks[:, None] & (1 << np.arange(5))) != 0
    row_count = occupied.sum(axis=1)
    values = np.zeros((len(masks), len(EVENTS)), dtype=float)
    values[:, 0] = row_count == 0

    for k in range(1, 5):
        values[:, k] = row_count <= k
        fixed = np.zeros(len(masks), dtype=float)
        valid = row_count <= k
        fixed[valid] = [
            comb(5 - int(count), k - int(count)) / comb(5, k)
            for count in row_count[valid]
        ]
        values[:, 4 + k] = fixed

    values[:, 9] = (
        ((masks | 0b00111) == 0b00111)
        | ((masks | 0b01110) == 0b01110)
        | ((masks | 0b11100) == 0b11100)
    )
    return values


def make_iid_table() -> np.ndarray:
    mask_probabilities = np.zeros((51, 32), dtype=float)
    mask_probabilities[0, 0] = 1.0
    for count in range(1, 51):
        for mask in range(32):
            for row in range(5):
                mask_probabilities[count, mask | (1 << row)] += (
                    mask_probabilities[count - 1, mask] / 5
                )
    return mask_probabilities @ event_values(np.arange(32))


def weighted_stats(values: np.ndarray, weights: np.ndarray) -> tuple[float, float]:
    sample_count = int(weights.sum())
    total = float(np.dot(values, weights))
    mean = total / sample_count
    if sample_count < 2:
        return mean, 0.0
    square_total = float(np.dot(values * values, weights))
    variance = max(0.0, (square_total - total * total / sample_count) / (sample_count - 1))
    return mean, sqrt(variance / sample_count)


def corrected_p_value(effect: float, se: float, comparisons: int) -> float:
    if se == 0:
        return 1.0 if effect == 0 else 0.0
    return min(1.0, comparisons * 2 * norm.sf(abs(effect / se)))


def load_raw_data() -> pd.DataFrame:
    data = pd.read_csv(INPUT_FILE)
    required = {"scene", "wave", "dancer_selected", "frequency", *ROW_COLUMNS}
    missing = required.difference(data.columns)
    if missing:
        raise ValueError(f"raw CSV is missing columns: {sorted(missing)}")
    numeric = ["wave", "dancer_selected", "frequency", *ROW_COLUMNS]
    if data[numeric].isna().any().any() or (data[["frequency", *ROW_COLUMNS]] < 0).any().any():
        raise ValueError("raw CSV contains invalid counts")
    if not data["dancer_selected"].isin((0, 1)).all():
        raise ValueError("dancer_selected must be 0 or 1")
    return data


def analyze(data: pd.DataFrame) -> pd.DataFrame:
    iid_table = make_iid_table()
    output = []

    for (scene, wave), old_values in OLD_PROBABILITIES.items():
        group = data[(data["scene"] == scene) & (data["wave"] == wave)]
        if group.empty:
            raise ValueError(f"raw CSV has no data for {scene} w{wave}")

        rows = group[ROW_COLUMNS].to_numpy(dtype=np.uint32)
        weights = group["frequency"].to_numpy(dtype=np.float64)
        sample_count = int(weights.sum())
        giga_count = rows.sum(axis=1)
        masks = ((rows > 0) * (1 << np.arange(5))).sum(axis=1)
        actual_values = event_values(masks)
        iid_values = iid_table[giga_count]

        for event_index, event in enumerate(EVENTS):
            actual, total_se = weighted_stats(actual_values[:, event_index], weights)
            iid_mean, _ = weighted_stats(iid_values[:, event_index], weights)
            row_effect, row_se = weighted_stats(
                actual_values[:, event_index] - iid_values[:, event_index], weights
            )
            total_effect = actual - old_values[event_index]
            output.append({
                "scene": scene,
                "wave": wave,
                "event": event,
                "samples": sample_count,
                "old_probability": old_values[event_index],
                "iid_same_count_probability": iid_mean,
                "actual_probability": actual,
                "count_effect": iid_mean - old_values[event_index],
                "row_effect": row_effect,
                "total_effect": total_effect,
                "row_se": row_se,
                "row_p_bonf": corrected_p_value(row_effect, row_se, len(EVENTS)),
                "total_se": total_se,
                "total_p_bonf": corrected_p_value(total_effect, total_se, len(EVENTS)),
            })

        edge_count = rows[:, 0] + rows[:, 4]
        total_giga = float(np.dot(giga_count, weights))
        if total_giga == 0:
            print(f"{scene} w{wave} edge=n/a p=1.0000")
        else:
            edge_share = float(np.dot(edge_count, weights)) / total_giga
            edge_effect, edge_se = weighted_stats(edge_count - 0.4 * giga_count, weights)
            edge_p = corrected_p_value(edge_effect, edge_se, len(OLD_PROBABILITIES))
            print(f"{scene} w{wave} edge={100 * edge_share:.4f}% p={edge_p:.4f}")

    return pd.DataFrame(output, columns=OUTPUT_COLUMNS)


def main() -> None:
    analysis = analyze(load_raw_data())
    analysis.to_csv(OUTPUT_FILE, index=False, float_format="%.10g")
    print(f"saved {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
