import sys, struct, os

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.spv> <output.hpp>")
    sys.exit(1)

inp = sys.argv[1]
out = sys.argv[2]
var = os.path.splitext(os.path.basename(out))[0].replace('.', '_')
with open(inp, 'rb') as f:
    data = f.read()

words = struct.iter_unpack('<I', data)
values = [f"0x{w[0]:08x}" for w in words]

with open(out, 'w') as f:
    f.write('#pragma once\n')
    f.write('#include <cstddef>\n')
    f.write('const uint32_t ' + var + '[] = {\n    ')
    for i, v in enumerate(values):
        if i and i % 8 == 0:
            f.write('\n    ')
        f.write(v)
        if i != len(values) - 1:
            f.write(', ')
    f.write('\n};\n')
    f.write(f'const size_t {var}_size = {len(data)};\n')
