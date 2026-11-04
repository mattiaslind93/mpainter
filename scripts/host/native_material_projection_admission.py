"""Pure fixture/predecessor admission for the explicit BJ runtime wrapper.

No Blender, Qt, application caches, source loading or rendering is involved.
Runtime still checks actual imported tile/owner/parameter/GPU state separately.
"""
import hashlib
import json
from pathlib import Path

KINDS = ('UV', 'Object', 'Generated', 'uniform')
FILES = ('owned-projection.blend', 'owned.obj', 'owned.mtl',
         'owned-uniform.png', 'owned-height.exr', 'fixture.json')

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require(okay, message):
    if not okay:
        raise ValueError(message)

def validate_fixture(directory, kind, generator_sha256):
    require(kind in KINDS, 'Unsupported projection fixture kind')
    directory = Path(directory)
    for name in FILES:
        require((directory / name).is_file(), 'Missing required fixture file: ' + name)
    fixture = json.loads((directory / 'fixture.json').read_text())
    require(fixture.get('format') == 'bts-native-projection-fixture' and
            fixture.get('version') == 1, 'Wrong projection fixture manifest')
    require(fixture.get('generator_sha256') == generator_sha256,
            'Fixture generator source hash differs')
    require(fixture.get('source_blend_sha256') == sha(directory / 'owned-projection.blend'),
            'Fixture blend bytes differ from generated manifest')
    packed = fixture.get('uniform_packed_image', {})
    require(packed.get('filepath') == '//owned-uniform.png' and
            packed.get('source') == 'FILE' and packed.get('verified_reopen') is True and
            packed.get('packed_paths') == ['//owned-uniform.png'] and
            packed.get('packed') is True and packed.get('packed_bytes', 0) > 0,
            'Uniform fixture image is not packed with the required relative filename')
    require(fixture.get('tiles') == [1001, 1002, 1011] and
            fixture.get('owner_names') == ['BJ Owner A', 'BJ Owner B'],
            'Wrong declared fixture tile/owner membership')
    material = 'BJ Uniform bitmap' if kind == 'uniform' else 'BJ Native ' + kind
    require(fixture.get('uniform_material') == material if kind == 'uniform'
            else material in fixture.get('native_materials', []),
            'Requested kind is absent from fixture manifest')
    return fixture, {name: sha(directory / name) for name in FILES}

def validate_predecessor(receipt, proof, kind, binary_sha256, source_hashes):
    require(receipt.get('actual_exit') == 0 and receipt.get('clean_exit') is True,
            'Warm predecessor did not exit successfully and cleanly')
    require(receipt.get('phase') == 'warm' and receipt.get('kind') == kind,
            'Wrong warm predecessor phase/kind')
    require(receipt.get('binary_sha256') == binary_sha256 and
            receipt.get('source_hashes') == source_hashes,
            'Warm/cold binary or source guard differs')
    require(proof.get('failure') == '' and proof.get('phase') == 'warm' and
            proof.get('kind') == kind, 'Wrong/failed warm proof')
    checkpoints = proof.get('checkpoints', [])
    require(len(checkpoints) == 14 and
            {(row.get('policy'), row.get('mode')) for row in checkpoints} ==
            {(policy, mode) for policy in range(2) for mode in range(7)},
            'Warm predecessor lacks the complete unique policy/mode matrix')


def validate_cold_frame_progression(proof):
    """Actual ordinary frames, with independent data admitted before any draw."""
    rows = proof['checkpoints']
    if len(rows) != 14:
        raise ValueError('Cold proof needs all 14 checkpoints')
    total = 0
    for row in rows:
        frames = row.get('normal_eevee_frames')
        if type(frames) is not int or frames < 8:
            raise ValueError('Cold checkpoint requires at least eight actual normal frames')
        if row['data'].get('explicit_eevee_frames_before_validation') != 0:
            raise ValueError('Cold independent data must precede the first explicit draw')
        total += frames
    if proof.get('normal_eevee_frames') != total:
        raise ValueError('Cold actual frame sum is inconsistent')
