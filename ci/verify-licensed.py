"""Fail packaging if the built product lacks its production licensing code."""
from pathlib import Path
import sys
build = Path(sys.argv[1])
cache = (build / 'CMakeCache.txt').read_text()
assert 'PARADYNEQ_ENABLE_LICENSING:BOOL=ON' in cache, 'Licensing is not enabled'
assert len(sys.argv) > 2, 'Expected product binaries'
for name in sys.argv[2:]:
    data = Path(name).read_bytes()
    for marker in [b'Unlock ParaDyn EQ', b'Could not read a stable machine identifier', b'paradyn-eq']:
        assert marker in data or marker.decode().encode('utf-16-le') in data, f'Missing licensing marker {marker!r} in {name}'
    print(f'Licensed product verified: {name}')
