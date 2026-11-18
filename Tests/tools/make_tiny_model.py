"""Generate the tiny ONNX decoder + sound map used by the C++ unit tests.

The model mirrors the real exported decoder's interface (input "latent_input"
[batch, LATENT_DIM] -> output "audio_output" [batch, NUM_SAMPLES]) but is just
tanh(z @ W) with deterministic weights, so the tests can recompute the expected
output exactly:  W[i, j] = 0.5 * sin(0.37 * i + 0.011 * j)

Requires: pip install onnx numpy
Usage:    python make_tiny_model.py <output_dir>
"""

import json
import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper

LATENT_DIM = 8
NUM_SAMPLES = 2205  # 50 ms at 44.1 kHz
SAMPLE_RATE = 44100


def weights() -> np.ndarray:
    i = np.arange(LATENT_DIM, dtype=np.float64)[:, None]
    j = np.arange(NUM_SAMPLES, dtype=np.float64)[None, :]
    return (0.5 * np.sin(0.37 * i + 0.011 * j)).astype(np.float32)


def build_model() -> onnx.ModelProto:
    w = helper.make_tensor("W", TensorProto.FLOAT, [LATENT_DIM, NUM_SAMPLES], weights().tobytes(), raw=True)
    graph = helper.make_graph(
        [
            helper.make_node("MatMul", ["latent_input", "W"], ["pre"]),
            helper.make_node("Tanh", ["pre"], ["audio_output"]),
        ],
        "tiny_decoder",
        [helper.make_tensor_value_info("latent_input", TensorProto.FLOAT, ["batch_size", LATENT_DIM])],
        [helper.make_tensor_value_info("audio_output", TensorProto.FLOAT, ["batch_size", NUM_SAMPLES])],
        [w],
    )
    # ONNX Runtime 1.16 reads IR <= 9 / opset <= 19, so pin older versions explicitly.
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 14)], ir_version=8)
    onnx.checker.check_model(model)
    return model


def build_map() -> dict:
    points = [
        {"label": "a", "x": -1.0, "y": -1.0, "latent": [1.0] + [0.0] * (LATENT_DIM - 1)},
        {"label": "b", "x": 1.0, "y": -1.0, "latent": [0.0, 1.0] + [0.0] * (LATENT_DIM - 2)},
        {"label": "c", "x": 0.0, "y": 1.0, "latent": [0.0, 0.0, 1.0] + [0.0] * (LATENT_DIM - 3)},
    ]
    return {"latent_dim": LATENT_DIM, "sample_rate": SAMPLE_RATE, "points": points}


if __name__ == "__main__":
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "data/tiny_model")
    out.mkdir(parents=True, exist_ok=True)
    onnx.save(build_model(), str(out / "latent_decoder.onnx"))
    (out / "sound_map.json").write_text(json.dumps(build_map(), indent=1))
    print(f"wrote {out}")
