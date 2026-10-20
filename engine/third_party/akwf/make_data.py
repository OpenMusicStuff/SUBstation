"""Writes engine/src/builtin/SnarlTablesData.h from waves/: the AKWF single cycles Snarl's wavetables are built
from (builtin/SnarlTables.cpp), as 16-bit samples, a table's in the order listed here (dark to bright).

    python make_data.py            (Python's standard library only)

Why these waves: VERSION.txt.
"""
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'src', 'builtin', 'SnarlTablesData.h')

# Each table: its name in Snarl's Table list, the AKWF folder, and the waves' numbers, dark to bright.
TABLES = [
    ('Voice', 'AKWF_hvoice',
     ['0034', '0094', '0102', '0101', '0036', '0015', '0066', '0048',
      '0073', '0021', '0050', '0056', '0041', '0059', '0062', '0058']),
    ('FM', 'AKWF_fmsynth',
     ['0045', '0109', '0024', '0105', '0010', '0086', '0054', '0015',
      '0059', '0033', '0030', '0016', '0027', '0034', '0036', '0002']),
    ('Hollow', 'AKWF_overtone',
     ['0013', '0004', '0005', '0043', '0026', '0027', '0028', '0018',
      '0016', '0002', '0039', '0042', '0031', '0032', '0014', '0035']),
    ('Dirty', 'AKWF_distorted',
     ['0015', '0042', '0019', '0009', '0007', '0026', '0041', '0033',
      '0043', '0035', '0037', '0036', '0008', '0025', '0031', '0045']),
]
LENGTH = 600


def samples(path):
    with wave.open(path) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, path
        frames = w.readframes(w.getnframes())
    values = list(struct.unpack('<%dh' % (len(frames) // 2), frames))
    assert len(values) == LENGTH, (path, len(values))
    return values


def main():
    out = [
        '#pragma once',
        '// The single cycles Snarl\'s wavetables are built from: Adventure Kid Waveforms (AKWF) by Kristoffer Ekstrand,',
        '// CC0 1.0 (third_party/akwf), their 16-bit samples as they are. Written by third_party/akwf/make_data.py:',
        '// do not edit, run the script again.',
        '',
        '#include <cstdint>',
        '',
        'namespace sub::snarl::akwf {',
        '',
        'inline constexpr int kLength = %d;  // samples a cycle' % LENGTH,
        'inline constexpr int kWaves = %d;   // a table\'s' % len(TABLES[0][2]),
        '',
        '// A table\'s waves: its name, then each wave\'s file and samples, dark to bright.',
        'struct Table {',
        '    const char* name;',
        '    const char* files[kWaves];',
        '    int16_t samples[kWaves][kLength];',
        '};',
        '',
        'inline constexpr Table kTables[] = {',
    ]
    for name, folder, numbers in TABLES:
        assert len(numbers) == len(TABLES[0][2])
        files = ['%s_%s.wav' % (folder, n) for n in numbers]
        out.append('    {"%s",' % name)
        out.append('     {')
        for i in range(0, len(files), 4):
            out.append('         ' + ' '.join('"%s",' % f for f in files[i:i + 4]))
        out.append('     },')
        out.append('     {')
        for f in files:
            values = samples(os.path.join(HERE, 'waves', folder, f))
            out.append('         {')
            for i in range(0, LENGTH, 12):
                out.append('             ' + ' '.join('%d,' % v for v in values[i:i + 12]))
            out.append('         },')
        out.append('     }},')
    out += ['};', '', '}  // namespace sub::snarl::akwf', '']
    with open(OUT, 'w', newline='\n') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
