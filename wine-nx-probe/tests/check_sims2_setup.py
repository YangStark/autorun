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

# The release's own anadius.cfg sets its language to "invalid" so the game reads
# the locale from a key of its own, and every number the setup takes has to have
# one: without it the game says "open: Invalid handle" and stops.
locales = re.findall(r'\{\s*(\d+), L"([a-z]{2}_[A-Z]{2})"\s*\}', source)
assert len(locales) == 22, len(locales)
numbers = [int(n) for n, _ in locales]
assert numbers == sorted(numbers) and len(set(numbers)) == 22
assert 12 not in numbers and 19 not in numbers      # the two the release skips
assert dict((int(n), l) for n, l in locales)[23] == 'pt_PT'
assert 'Software\\\\Maxis\\\\The Sims 2 Legacy' in source and 'L"Locale"' in source

# The game sits beside the setup, not inside it and not at the root of the
# drive: looking one folder too far up found C: and skipped every pack.
assert source.count('holds_the_game( game )') == 3
assert 'join( game, MAX_PATH, above, L"The Sims 2" )' in source
# A path is joined without doubling the slash, which C:\ would.
assert "if (at && out[at - 1] != '\\\\') wide_append( out, &at, max, L\"\\\\\" );" in source
# And the trailing slash is kept only where it is part of the name.
assert source.count('out[n > 3 ? n - 1 : n] = 0;') == 2

print(f'sims2 setup: {len(folders)} packs, the list and the table in step, the empty place kept')
