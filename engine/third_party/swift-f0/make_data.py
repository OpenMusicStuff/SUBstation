"""Writes engine/src/builtin/SwiftF0Data.h from model.onnx: SwiftF0's weights as C++ arrays, for GigaTune's
High Quality tracker (builtin/SwiftF0.cpp runs the network itself; no ONNX Runtime).

    python make_data.py            (needs numpy and onnx: pip install numpy onnx)

What it reads from the graph (swift-f0 0.3.0; the checks below fail if a later model is laid out otherwise):
- three log-magnitude spectra, frames of 1024, 2048 and 512 samples at 16 kHz every 256 (periodic Hann), each
  through a 128-band filterbank (its weights: rows of (spectrum bin, band, weight)), + 1e-8, natural log;
- eighteen convolutions in graph order (weights [out][in][freq][time], biases, padding, dilation);
- the 95 pitch classes' frequencies and the natural logs the pitch is averaged in.
"""
import os

import numpy as np
import onnx
from onnx import numpy_helper

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'src', 'builtin', 'SwiftF0Data.h')


def literal(v):
    """A float literal that reads back as v exactly (9 significant digits, always with a point or exponent)."""
    text = '%.9g' % v
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


def floats(values, per_line=8):
    values = np.asarray(values, np.float32).ravel()
    lines = []
    for i in range(0, len(values), per_line):
        lines.append('    ' + ' '.join(literal(v) + ',' for v in values[i:i + per_line]))
    return '\n'.join(lines)


def main():
    m = onnx.load(os.path.join(HERE, 'model.onnx'))
    inits = {t.name: numpy_helper.to_array(t) for t in m.graph.initializer}
    convs, scatters, bin_hz, log_hz = [], [], None, None
    for n in m.graph.node:
        attrs = {a.name: onnx.helper.get_attribute_value(a) for a in n.attribute}
        if n.op_type == 'Conv':
            w, b = inits[n.input[1]], inits[n.input[2]]
            if w.ndim == 3:  # a 1-D convolution over time: as 2-D with a frequency extent of 1
                w = w[:, :, None, :]
                pads, dil = [0, attrs['pads'][0], 0, attrs['pads'][1]], [1, attrs['dilations'][0]]
            else:
                pads, dil = list(attrs['pads']), list(attrs['dilations'])
            convs.append((w, b, pads, dil))
        elif n.op_type == 'ScatterElements':
            scatters.append((inits[n.input[1]], inits[n.input[2]]))
        elif n.op_type == 'GreaterOrEqual' and n.input[0] in inits:
            bin_hz = inits[n.input[0]]
        elif n.op_type == 'Gather' and n.input[0] in inits and inits[n.input[0]].shape == (95,):
            log_hz = inits[n.input[0]]
    assert len(convs) == 18 and len(scatters) == 3 and bin_hz is not None and log_hz is not None, 'an unexpected graph'
    assert np.allclose(np.exp(log_hz), bin_hz, rtol=1e-5), 'the pitch classes and their logs disagree'

    parts = ['''#pragma once
// SwiftF0's weights (swift-f0 0.3.0, MIT, Lars Nieradzik: third_party/swift-f0), written by
// third_party/swift-f0/make_data.py from its model.onnx. Do not edit: run the script again.

#include <cstdint>

namespace sub::swiftf0 {

// A filterbank's weight: spectrum bin `bin` into band `band`.
struct FilterTap {
    uint16_t bin;
    uint8_t band;
    float weight;
};

// A convolution over (frequency, time) columns: weights [out][in][freq][time], zero padding above and below in
// frequency (the time extent is centred), dilation in frequency.
struct Conv {
    int out, in, freqTaps, timeTaps, freqDilation, padLow, padHigh;
    const float* weights;
    const float* bias;
};
''']
    for (idx, val), size in zip(scatters, (1024, 2048, 512)):
        rows = idx // 128
        band = idx % 128
        assert rows.max() <= size // 2 and band.max() < 128
        order = np.lexsort((band, rows))
        taps = ',\n'.join('    {%d, %d, %s}' % (rows[i], band[i], literal(val[i])) for i in order)
        parts.append('inline constexpr FilterTap kFilter%d[] = {\n%s,\n};\n' % (size, taps))
    for i, (w, b, pads, dil) in enumerate(convs):
        assert dil[1] == 1 and pads[1] == pads[3] == (w.shape[3] - 1) // 2, 'time padding must be centred'
        parts.append('inline constexpr float kConv%dWeights[] = {\n%s\n};\n' % (i, floats(w)))
        parts.append('inline constexpr float kConv%dBias[] = {\n%s\n};\n' % (i, floats(b)))
    parts.append('inline constexpr Conv kConvs[] = {\n' + ''.join(
        '    {%d, %d, %d, %d, %d, %d, %d, kConv%dWeights, kConv%dBias},\n' % (w.shape[0], w.shape[1], w.shape[2], w.shape[3], dil[0], pads[0], pads[2], i, i)
        for i, (w, b, pads, dil) in enumerate(convs)) + '};\n')
    parts.append('// The pitch classes (Hz) and their natural logs.\ninline constexpr float kClassHz[] = {\n%s\n};\n' % floats(bin_hz))
    parts.append('inline constexpr float kClassLogHz[] = {\n%s\n};\n' % floats(log_hz))
    parts.append('\n}  // namespace sub::swiftf0\n')
    text = '\n'.join(parts)
    with open(OUT, 'w', newline='\r\n') as f:
        f.write(text)
    print('wrote', os.path.normpath(OUT), '(%d convolutions, %d filter taps)' % (len(convs), sum(len(i) for i, _ in scatters)))


if __name__ == '__main__':
    main()
