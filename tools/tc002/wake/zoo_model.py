"""A small int8 model that runs every op WakeModel supports beyond the trained models, in shapes and
options they do not use: SAME padding with strides and dilation, a depth multiplier, max and average
pooling with partial windows, MEAN with and without keep_dims, broadcast MUL and ADD, per-channel
FULLY_CONNECTED, a reversing STRIDED_SLICE with shrink, SPLIT_V and int8 to uint8 QUANTIZE.
Input [1, 4, 40] int8, output [1, 41] uint8. Written with the flatbuffers package from schema.fbs.
"""

import flatbuffers
import numpy as np

INT32, UINT8, INT8 = 2, 3, 9
ADD, AVERAGE_POOL, CONCATENATION, CONV, DEPTHWISE, FULLY_CONNECTED = 0, 1, 2, 3, 4, 9
LOGISTIC, MAX_POOL, MUL, RESHAPE, MEAN, STRIDED_SLICE, SPLIT_V, QUANTIZE = 14, 17, 18, 22, 40, 45, 102, 114
SAME, VALID, RELU, RELU6 = 0, 1, 1, 3


class Graph:
    def __init__(self):
        self.tensors, self.buffers, self.ops, self.codes = [], [b""], [], []

    def tensor(self, shape, kind=INT8, scale=None, zero=None, data=None, dim=0):
        buffer = 0
        if data is not None:
            self.buffers.append(np.ascontiguousarray(data).tobytes())
            buffer = len(self.buffers) - 1
        scales = None if scale is None else np.atleast_1d(np.asarray(scale, np.float32))
        zeros = None if scale is None else np.broadcast_to(np.asarray(zero, np.int64), scales.shape)
        self.tensors.append((list(shape), kind, buffer, scales, zeros, dim))
        return len(self.tensors) - 1

    def const_i32(self, values):
        values = np.asarray(values, np.int32)
        return self.tensor(values.shape, INT32, data=values)

    def op(self, code, inputs, outputs, options_type=0, options=None):
        if code not in self.codes:
            self.codes.append(code)
        self.ops.append((self.codes.index(code), inputs, outputs, options_type, options or []))


def build(graph, inputs, outputs):
    b = flatbuffers.Builder(4096)

    def ints(values, width=4):
        prepend = {4: b.PrependInt32, 8: b.PrependInt64}[width]
        b.StartVector(width, len(values), width)
        for v in reversed(list(values)):
            prepend(int(v))
        return b.EndVector()

    def floats(values):
        b.StartVector(4, len(values), 4)
        for v in reversed(list(values)):
            b.PrependFloat32(float(v))
        return b.EndVector()

    b.ForceDefaults(True)
    scalar = {"i8": b.PrependInt8Slot, "u8": b.PrependUint8Slot, "i32": b.PrependInt32Slot,
              "u32": b.PrependUint32Slot, "bool": b.PrependBoolSlot}

    def table(fields):
        """fields: (slot, kind, value), kind one of i8 u8 i32 u32 bool, or off for a built offset."""
        b.StartObject(1 + max([slot for slot, _, _ in fields], default=0))
        for slot, kind, value in fields:
            if kind == "off":
                b.PrependUOffsetTRelativeSlot(slot, value, 0)
            else:
                scalar[kind](slot, value, 0)
        return b.EndObject()

    buffers = []
    for data in graph.buffers:
        if data:
            b.StartVector(1, len(data), 16)
            for byte in reversed(data):
                b.PrependUint8(byte)
            vec = b.EndVector()
            buffers.append(table([(0, "off", vec)]))
        else:
            buffers.append(table([]))
    tensors = []
    for i, (shape, kind, buffer, scales, zeros, dim) in enumerate(graph.tensors):
        shape_off = ints(shape)
        name = b.CreateString(f"t{i}")
        quant = None
        if scales is not None:
            scale_off, zero_off = floats(scales), ints(zeros, 8)
            quant = table([(2, "off", scale_off), (3, "off", zero_off), (6, "i32", dim)])
        fields = [(0, "off", shape_off), (1, "i8", kind), (2, "u32", buffer), (3, "off", name)]
        if quant is not None:
            fields.append((4, "off", quant))
        tensors.append(table(fields))
    ops = []
    for code, ins, outs, options_type, options in graph.ops:
        in_off, out_off = ints(ins), ints(outs)
        fields = [(0, "u32", code), (1, "off", in_off), (2, "off", out_off)]
        if options_type:
            resolved = [(slot, kind, ints(value) if kind == "vec" else value) for slot, kind, value in options]
            resolved = [(slot, "off" if kind == "vec" else kind, value) for slot, kind, value in resolved]
            fields += [(3, "u8", options_type), (4, "off", table(resolved))]
        ops.append(table(fields))
    codes = [table([(0, "i8", min(c, 127)), (2, "i32", 1), (3, "i32", c)]) for c in graph.codes]

    def offsets(items):
        b.StartVector(4, len(items), 4)
        for item in reversed(items):
            b.PrependUOffsetTRelative(item)
        return b.EndVector()

    t_vec, o_vec, i_vec, out_vec = offsets(tensors), offsets(ops), ints(inputs), ints(outputs)
    name = b.CreateString("main")
    sub = table([(0, "off", t_vec), (1, "off", i_vec), (2, "off", out_vec), (3, "off", o_vec), (4, "off", name)])
    model = table([(0, "u32", 3), (1, "off", offsets(codes)), (2, "off", offsets([sub])),
                   (3, "off", b.CreateString("wake zoo")), (4, "off", offsets(buffers))])
    b.Finish(model, file_identifier=b"TFL3")
    return bytes(b.Output())


def zoo_model(seed=5):
    rng = np.random.default_rng(seed)
    g = Graph()

    def weights(shape):
        return rng.integers(-127, 128, size=shape).astype(np.int8)

    def bias(n, low=-2000, high=2000):
        return rng.integers(low, high, size=n).astype(np.int32)

    x = g.tensor([1, 4, 40], scale=0.10196079, zero=-128)
    a = g.tensor([1, 4, 10, 4], scale=0.10196079, zero=-128)
    g.op(RESHAPE, [x, g.const_i32([1, 4, 10, 4])], [a], 17, [(0, "vec", [1, 4, 10, 4])])

    conv_scales = rng.uniform(0.002, 0.006, 6).astype(np.float32)
    b_ = g.tensor([1, 4, 5, 6], scale=0.4, zero=-128)
    g.op(CONV, [a, g.tensor([6, 3, 3, 4], scale=conv_scales, zero=0, data=weights([6, 3, 3, 4])),
                g.tensor([6], INT32, scale=conv_scales * np.float32(0.10196079), zero=0, data=bias(6))], [b_],
         1, [(0, "i8", SAME), (1, "i32", 2), (2, "i32", 1), (3, "i8", RELU6), (4, "i32", 1), (5, "i32", 2)])

    dw_scales = rng.uniform(0.004, 0.01, 12).astype(np.float32)
    c = g.tensor([1, 2, 3, 12], scale=0.3, zero=-100)
    g.op(DEPTHWISE, [b_, g.tensor([1, 3, 2, 12], scale=dw_scales, zero=0, data=weights([1, 3, 2, 12]), dim=3),
                     g.tensor([12], INT32, scale=dw_scales * np.float32(0.4), zero=0, data=bias(12, -500, 500))], [c],
         2, [(0, "i8", VALID), (1, "i32", 1), (2, "i32", 1), (3, "i32", 2), (4, "i8", RELU), (5, "i32", 2),
             (6, "i32", 1)])

    d = g.tensor([1, 2, 3, 12], scale=0.3, zero=-100)
    g.op(MAX_POOL, [c], [d], 5, [(0, "i8", SAME), (1, "i32", 1), (2, "i32", 1), (3, "i32", 2), (4, "i32", 2)])
    e = g.tensor([1, 1, 2, 12], scale=0.3, zero=-100)
    g.op(AVERAGE_POOL, [c], [e], 5, [(0, "i8", SAME), (1, "i32", 2), (2, "i32", 2), (3, "i32", 3), (4, "i32", 2)])

    f = g.tensor([1, 1, 3, 12], scale=0.2, zero=-90)
    g.op(MEAN, [d, g.const_i32([1])], [f], 27, [(0, "bool", True)])
    h = g.tensor([1, 1, 3, 12], scale=0.25, zero=3)
    g.op(MUL, [f, g.tensor([12], scale=0.02, zero=5, data=weights([12]))], [h], 21, [])
    e2 = g.tensor([1, 1, 1, 12], scale=0.17, zero=-60)
    g.op(MEAN, [e, g.const_i32([-2])], [e2], 27, [(0, "bool", True)])
    i = g.tensor([1, 1, 3, 12], scale=0.35, zero=-20)
    g.op(ADD, [h, e2], [i], 11, [(0, "i8", 0)])
    j = g.tensor([1, 12], scale=0.3, zero=-10)
    g.op(MEAN, [i, g.const_i32([2, 1])], [j], 27, [(0, "bool", False)])

    fc_scales = rng.uniform(0.002, 0.005, 5).astype(np.float32)
    k = g.tensor([1, 5], scale=0.3, zero=-100)
    g.op(FULLY_CONNECTED, [j, g.tensor([5, 12], scale=fc_scales, zero=0, data=weights([5, 12])),
                           g.tensor([5], INT32, scale=fc_scales * np.float32(0.3), zero=0, data=bias(5))], [k],
         8, [(0, "i8", 0)])

    s = g.tensor([2, 3, 6], scale=0.3, zero=-100)
    g.op(STRIDED_SLICE, [c, g.const_i32([0, 1, 2, 11]), g.const_i32([1, 0, 0, 0]), g.const_i32([1, -1, -1, -2])],
         [s], 32, [(0, "i32", 0), (1, "i32", 0b1110), (4, "i32", 0b0001)])
    r = g.tensor([1, 36], scale=0.3, zero=-100)
    g.op(RESHAPE, [s, g.const_i32([1, 36])], [r], 17, [(0, "vec", [1, 36])])
    m = g.tensor([1, 41], scale=0.3, zero=-100)
    g.op(CONCATENATION, [k, r], [m], 10, [(0, "i32", -1)])

    lo = g.tensor([1, 41], scale=1 / 256, zero=-128)
    g.op(LOGISTIC, [m], [lo])
    p1, p2 = g.tensor([1, 21], scale=1 / 256, zero=-128), g.tensor([1, 20], scale=1 / 256, zero=-128)
    g.op(SPLIT_V, [lo, g.const_i32([-1, 20]), g.tensor([], INT32, data=np.asarray([1], np.int32))], [p1, p2],
         79, [(0, "i32", 2)])
    q = g.tensor([1, 41], scale=1 / 256, zero=-128)
    g.op(CONCATENATION, [p2, p1], [q], 10, [(0, "i32", 1)])
    out = g.tensor([1, 41], UINT8, scale=1 / 200, zero=0)
    g.op(QUANTIZE, [q], [out])
    return build(g, [x], [out])
