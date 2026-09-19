#!/usr/bin/env python3
"""The Sims 2 setup (wine-nx-probe/tools/sims2_setup.c), which writes what the
release's "Instalar Registros" batch file writes -- from inside the card, where
the paths are the card's.

The game reads EPsInstalled by position, so the list and the table of packs
have to say the same thing in the same order; an empty place in the list is a
place, and losing it moves every pack after it."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = (root / 'wine-nx-probe/tools/sims2_setup.c').read_text()

table = re.findall(r'\{\s*L"(Sims2[A-Z0-9]*\.exe)",\s*L"(\w+)"\s*\}', source)
assert len(table) == 17, len(table)
assert table[0] == ('Sims2.exe', 'Base')

declaration = source[source.index('eps_installed[]'):]
declaration = declaration[:declaration.index(';')]
listed = ''.join(re.findall(r'L"([^"]*)"', declaration)).split(',')
assert len(listed) == 17, listed          # sixteen packs and the empty place
assert listed.count('') == 1, listed
assert listed[11] == '', listed           # where the release's own list has it

# The list is the table without the base game, in the same order.
assert [name for name in listed if name] == [exe for exe, _ in table[1:]]
# No folder named twice, or two packs would share one.
folders = [folder for _, folder in table]
assert len(folders) == len(set(folders)) == 17

# The base game and the newest expansion are the two that carry Game Registry.
assert 'if (!i || i + 1 == sizeof(packs) / sizeof(packs[0]))' in source
# A pack that is not there gets no key, rather than one pointing at nothing.
assert 'if (!folder_exists( folder ))' in source and 'continue;' in source

print(f'sims2 setup: {len(folders)} packs, the list and the table in step, the empty place kept')
