"""Regenerate Content/Models/crowd_policy.onnx (requires numpy and onnx).

The network is hand-built, not trained. Its output is the goal direction
(observation features 0-1) plus a bounded perturbation from a randomly
initialized 64-256-256-2 MLP. The MLP gives every decision a realistic
inference cost and makes agents wander instead of walking in straight lines.
"""
import pathlib
import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

OBS, HIDDEN, ACT = 64, 256, 2
rng = np.random.default_rng(301)


def he(rows, cols):
    return (rng.standard_normal((rows, cols)) * np.sqrt(2.0 / rows)).astype(np.float32)


skip = np.zeros((OBS, ACT), np.float32)
skip[0, 0] = skip[1, 1] = 1.0
weights = {
    "w1": he(OBS, HIDDEN), "b1": np.zeros(HIDDEN, np.float32),
    "w2": he(HIDDEN, HIDDEN), "b2": np.zeros(HIDDEN, np.float32),
    "w3": he(HIDDEN, ACT) * 0.5, "b3": np.zeros(ACT, np.float32),
    "skip": skip, "scale": np.array([0.6], np.float32),
}
nodes = [
    helper.make_node("Gemm", ["obs", "w1", "b1"], ["h1"]),
    helper.make_node("Relu", ["h1"], ["r1"]),
    helper.make_node("Gemm", ["r1", "w2", "b2"], ["h2"]),
    helper.make_node("Relu", ["h2"], ["r2"]),
    helper.make_node("Gemm", ["r2", "w3", "b3"], ["h3"]),
    helper.make_node("Tanh", ["h3"], ["t3"]),
    helper.make_node("Mul", ["t3", "scale"], ["wander"]),
    helper.make_node("MatMul", ["obs", "skip"], ["goal"]),
    helper.make_node("Add", ["goal", "wander"], ["action"]),
]
graph = helper.make_graph(
    nodes, "crowd_policy",
    [helper.make_tensor_value_info("obs", TensorProto.FLOAT, ["batch", OBS])],
    [helper.make_tensor_value_info("action", TensorProto.FLOAT, ["batch", ACT])],
    [numpy_helper.from_array(value, name) for name, value in weights.items()],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], producer_name="BackgroundNPCs")
model.ir_version = 8
onnx.checker.check_model(model)
output = pathlib.Path(__file__).resolve().parents[1] / "Content/Models/crowd_policy.onnx"
onnx.save(model, output)
print(output)
