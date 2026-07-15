from pathlib import Path

import numpy as np
import pandas as pd

from analyze_finish_table import (
    ROW_COLUMNS,
    corrected_p_value,
    load_raw_data,
    weighted_stats,
)


ROOT = Path(__file__).resolve().parent
OUTPUT_FILE = ROOT / "dest" / "finish_table_corrected_comparison.csv"

EVENTS = (
    "none",
    "at_most_1",
    "at_most_2",
    "at_most_3",
    "at_most_4",
    "fixed_1_edge",
    "fixed_1_middle",
    "fixed_2_edge_edge",
    "fixed_2_edge_middle",
    "fixed_2_middle_middle",
    "fixed_3_edge_edge_middle",
    "fixed_3_edge_middle_middle",
    "fixed_3_middle_middle_middle",
    "fixed_4_edge_edge_middle_middle",
    "fixed_4_edge_middle_middle_middle",
    "cherry",
)

# part-1.ipynb 中考虑舞王选行偏差后的收尾表，原表只保留到 6 位小数。
CORRECTED_PROBABILITIES = {
    ("DE", 9): (
        0.033050, 0.164688, 0.456093, 0.780644, 0.960591,
        0.059700, 0.059162, 0.116250, 0.115072, 0.113924,
        0.233919, 0.231399, 0.228943, 0.481169, 0.475779, 0.463893,
    ),
    ("DE", 19): (
        0.992266, 0.995732, 0.998458, 0.999692, 0.999975,
        0.992964, 0.992956, 0.993938, 0.993927, 0.993916,
        0.995299, 0.995284, 0.995269, 0.997233, 0.997211, 0.998003,
    ),
    ("NE", 9): (
        0.035129, 0.160864, 0.446098, 0.772512, 0.958315,
        0.060614, 0.060051, 0.115433, 0.114186, 0.112972,
        0.230933, 0.228235, 0.225608, 0.476919, 0.471080, 0.456134,
    ),
    ("NE", 19): (
        0.994554, 0.997020, 0.998928, 0.999786, 0.999982,
        0.995051, 0.995045, 0.995742, 0.995733, 0.995724,
        0.996702, 0.996689, 0.996677, 0.998061, 0.998044, 0.998607,
    ),
}

# notebook 中舞王被选中时的红眼单只边际选行概率。
DANCER_ROW_PROBABILITIES = {
    "DE": np.array((0.203928, 0.197381, 0.197381, 0.197381, 0.203928)),
    "NE": np.array((0.204015, 0.197323, 0.197323, 0.197323, 0.204015)),
}

FIXED_MASK_GROUPS = (
    (0b00001, 0b10000),
    (0b00010, 0b00100, 0b01000),
    (0b10001,),
    (0b00011, 0b00101, 0b01001, 0b10010, 0b10100, 0b11000),
    (0b00110, 0b01010, 0b01100),
    (0b10011, 0b10101, 0b11001),
    (0b00111, 0b01011, 0b01101, 0b10110, 0b11010, 0b11100),
    (0b01110,),
    (0b10111, 0b11011, 0b11101),
    (0b01111, 0b11110),
)

OUTPUT_COLUMNS = (
    "scene",
    "wave",
    "event",
    "samples",
    "corrected_probability",
    "iid_corrected_same_count_probability",
    "actual_probability",
    "count_effect",
    "dependence_effect",
    "total_effect",
    "dependence_se",
    "dependence_p_bonf",
    "total_se",
    "total_p_bonf",
)


def event_values(masks: np.ndarray) -> np.ndarray:
    masks = np.asarray(masks, dtype=np.uint32)
    row_count = ((masks[:, None] & (1 << np.arange(5))) != 0).sum(axis=1)
    values = np.zeros((len(masks), len(EVENTS)), dtype=float)
    values[:, 0] = row_count == 0
    for count in range(1, 5):
        values[:, count] = row_count <= count

    for offset, allowed_group in enumerate(FIXED_MASK_GROUPS, start=5):
        allowed = np.asarray(allowed_group, dtype=np.uint32)
        values[:, offset] = ((masks[:, None] | allowed) == allowed).mean(axis=1)

    cherry_masks = np.asarray((0b00111, 0b01110, 0b11100), dtype=np.uint32)
    values[:, 15] = ((masks[:, None] | cherry_masks) == cherry_masks).any(axis=1)
    return values


def make_iid_table(row_probabilities: np.ndarray) -> np.ndarray:
    probabilities = np.asarray(row_probabilities, dtype=float)
    probabilities /= probabilities.sum()
    masks = np.zeros((51, 32), dtype=float)
    masks[0, 0] = 1.0
    for count in range(1, 51):
        for mask in range(32):
            for row, probability in enumerate(probabilities):
                masks[count, mask | (1 << row)] += masks[count - 1, mask] * probability
    return masks @ event_values(np.arange(32, dtype=np.uint32))


def analyze(data: pd.DataFrame) -> pd.DataFrame:
    uniform_table = make_iid_table(np.full(5, 0.2))
    dancer_tables = {
        scene: make_iid_table(probabilities)
        for scene, probabilities in DANCER_ROW_PROBABILITIES.items()
    }
    output = []

    for (scene, wave), corrected_values in CORRECTED_PROBABILITIES.items():
        group = data[(data["scene"] == scene) & (data["wave"] == wave)]
        if group.empty:
            raise ValueError(f"raw CSV has no data for {scene} w{wave}")

        rows = group[ROW_COLUMNS].to_numpy(dtype=np.uint32)
        weights = group["frequency"].to_numpy(dtype=np.float64)
        dancer_selected = group["dancer_selected"].to_numpy(dtype=bool)
        sample_count = int(weights.sum())
        giga_count = rows.sum(axis=1)
        if (giga_count > 50).any():
            raise ValueError(f"raw CSV contains more than 50 gigas in {scene} w{wave}")

        actual_masks = ((rows > 0) * (1 << np.arange(5))).sum(axis=1)
        actual_values = event_values(actual_masks)
        iid_values = uniform_table[giga_count].copy()
        iid_values[dancer_selected] = dancer_tables[scene][giga_count[dancer_selected]]

        group_rows = []
        for event_index, event in enumerate(EVENTS):
            actual, total_se = weighted_stats(actual_values[:, event_index], weights)
            iid_mean, _ = weighted_stats(iid_values[:, event_index], weights)
            dependence_effect, dependence_se = weighted_stats(
                actual_values[:, event_index] - iid_values[:, event_index], weights
            )
            corrected = corrected_values[event_index]
            total_effect = actual - corrected
            row = {
                "scene": scene,
                "wave": wave,
                "event": event,
                "samples": sample_count,
                "corrected_probability": corrected,
                "iid_corrected_same_count_probability": iid_mean,
                "actual_probability": actual,
                "count_effect": iid_mean - corrected,
                "dependence_effect": dependence_effect,
                "total_effect": total_effect,
                "dependence_se": dependence_se,
                "dependence_p_bonf": corrected_p_value(
                    dependence_effect, dependence_se, len(EVENTS)
                ),
                "total_se": total_se,
                "total_p_bonf": corrected_p_value(total_effect, total_se, len(EVENTS)),
            }
            output.append(row)
            group_rows.append(row)

        max_total = max(group_rows, key=lambda row: abs(row["total_effect"]))
        max_dependence = max(group_rows, key=lambda row: abs(row["dependence_effect"]))
        print(
            f"{scene} w{wave}: max total {max_total['event']}="
            f"{max_total['total_effect']:+.6%}; max dependence "
            f"{max_dependence['event']}={max_dependence['dependence_effect']:+.6%}"
        )

    return pd.DataFrame(output, columns=OUTPUT_COLUMNS)


def main() -> None:
    analysis = analyze(load_raw_data())
    analysis.to_csv(OUTPUT_FILE, index=False, float_format="%.10g")
    print(f"saved {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
