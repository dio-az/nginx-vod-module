#!/usr/bin/env python3

import json
from pathlib import Path

PACKAGES = {
	'ffmpeg': 'ffmpeg.org/releases',
	'nginx': 'nginx.org/download',
}

matrix_path = Path(__file__).parents[1] / '.github/matrix.json'
matrix = json.loads(matrix_path.read_text())

installs = {
	(row['ffmpeg-version'], row['arch'])
	for row in matrix
	if row.get('ffmpeg-version') not in (None, 'distro')
}

# Min supported version for glibc backward compatibility
min_version = min(row['ubuntu-version'] for row in matrix)

ffmpeg = [
	{'ffmpeg-version': version, 'ubuntu-version': min_version, 'arch': arch}
	for version, arch in sorted(installs)
]

tarballs = {('nginx', row['nginx-version']) for row in matrix}
tarballs |= {('ffmpeg', row['ffmpeg-version']) for row in ffmpeg}

source = [
	{'package': package, 'version': version, 'url': PACKAGES[package]}
	for package, version in sorted(tarballs)
]

for name, rows in ('source', source), ('ffmpeg', ffmpeg), ('build', matrix):
	print(f'{name}={json.dumps({'include': rows})}')
