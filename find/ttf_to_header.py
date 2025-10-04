import sys, os


if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input> <output>")
    sys.exit(1)

inp, out = sys.argv[1], sys.argv[2]
var = os.path.splitext(os.path.basename(out))[0].replace('.', '_')
with open(inp, 'rb') as f:
    data = f.read()

values = [f"0x{b:02x}" for b in data]

with open(out, 'w') as f:
    f.write('#pragma once\n')
    f.write('#include <cstddef>\n')
    f.write('#include <cstdint>\n')
    f.write(f'const unsigned char {var}[] = {{\n    ')
    for i, v in enumerate(values):
        if i and i % 12 == 0:
            f.write('\n    ')
        f.write(v)
        if i != len(values) - 1:
            f.write(', ')
    f.write('\n};\n')
    f.write(f'constexpr size_t {var}_size = {len(data)};\n')
