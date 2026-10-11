"""Validate section identity and metadata without running guest code."""
import hashlib
import re


IMAGE_HASHES = {
    'boot': 'fdccac846e1c2bbc431860f4d76d69f2820fccee4f809a98cb495d8dd0499482',
    'game': 'bf7da3fa6283428a97372250cd4076d15e9eae10f9d5709c0387fe0742d43a1d',
    'overlay_a': '0d6702c320df84cc6cbc3c5967dde08d44b6e476e110667fe2a43dc2dd536667',
    'overlay_b': 'b55fc2d1b22eb1ebdf01286a69a181b496da7b45ff7aec888ff74b7db748e7cd',
}
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z_0-9]*\Z')


class MetadataError(ValueError):
    pass


def _integer(value, label, minimum=0, maximum=0xFFFFFFFF, aligned=False):
    if type(value) is not int or not minimum <= value <= maximum:
        raise MetadataError(f'{label}: expected integer in [{minimum}, {maximum}]')
    if aligned and value % 4:
        raise MetadataError(f'{label}: expected instruction alignment')
    return value


def validate_sections(sections, expected_names=tuple(IMAGE_HASHES), image_size=None):
    """Require all sections once; allow shared function tails and overlay VRAM."""
    if not isinstance(sections, list) or not sections:
        raise MetadataError('section: expected nonempty array')
    names = [s.get('name') if isinstance(s, dict) else None for s in sections]
    if any(not isinstance(n, str) for n in names) or len(set(names)) != len(names):
        raise MetadataError('section: missing or duplicate names')
    if set(names) != set(expected_names):
        raise MetadataError('section: expected exactly ' + ', '.join(expected_names))
    by_function = {}
    rom_ranges = []
    for section in sections:
        label = section['name']
        offset = _integer(section.get('rom'), label + '.rom', aligned=True)
        base = _integer(section.get('vram'), label + '.vram', aligned=True)
        size = _integer(section.get('size'), label + '.size', minimum=4, aligned=True)
        if base + size > 0x100000000 or offset + size > 0x100000000:
            raise MetadataError(label + ': section range overflows 32 bits')
        if image_size is not None and offset + size > image_size:
            raise MetadataError(label + ': section exceeds expanded image')
        rom_ranges.append((offset, offset + size, label))
        functions = section.get('functions')
        if not isinstance(functions, list) or not functions:
            raise MetadataError(label + ': expected nonempty functions array')
        starts = set()
        for function in functions:
            if not isinstance(function, dict):
                raise MetadataError(label + ': expected function table')
            name = function.get('name')
            if not isinstance(name, str) or not IDENTIFIER.fullmatch(name):
                raise MetadataError(label + ': invalid function identifier')
            address = _integer(function.get('vram'), name + '.vram', aligned=True)
            length = _integer(function.get('size'), name + '.size', minimum=4, aligned=True)
            if address < base or address + length > base + size:
                raise MetadataError(name + ': function exceeds section')
            if name in by_function or address in starts:
                raise MetadataError(name + ': duplicate name or section entry address')
            starts.add(address)
            by_function[name] = {'section': label, **function}
    ranges = sorted(rom_ranges)
    for previous, current in zip(ranges, ranges[1:]):
        if previous[1] > current[0]:
            raise MetadataError(f'{previous[2]}/{current[2]}: overlapping image ranges')
    return by_function


def verify_images(image, sections, expected_hashes=None):
    """Return only image names actually checked, after validating the whole set."""
    expected = IMAGE_HASHES if expected_hashes is None else expected_hashes
    validate_sections(sections, expected_names=tuple(expected), image_size=len(image))
    verified = []
    for section in sections:
        data = image[section['rom']:section['rom'] + section['size']]
        if hashlib.sha256(data).hexdigest() != expected[section['name']]:
            raise MetadataError(section['name'] + ': executable image hash differs')
        verified.append(section['name'])
    return verified


def validate_patches(config, functions):
    """Check configured targets; this does not prove replacement implementations."""
    if not isinstance(config, dict) or not isinstance(config.get('input'), dict):
        raise MetadataError('input: expected table')
    entry = _integer(config['input'].get('entrypoint'), 'entrypoint', aligned=True)
    entries = [f for f in functions.values() if f['vram'] == entry]
    if len(entries) != 1 or entries[0]['section'] != 'boot':
        raise MetadataError('entrypoint: expected unique boot function entry')
    patches = config.get('patches', {})
    if not isinstance(patches, dict):
        raise MetadataError('patches: expected table')
    policies = {}
    for policy in ('ignored', 'renamed'):
        values = patches.get(policy, [])
        if not isinstance(values, list) or any(not isinstance(n, str) for n in values):
            raise MetadataError(policy + ': expected names array')
        if len(set(values)) != len(values) or any(n not in functions for n in values):
            raise MetadataError(policy + ': duplicate or unknown function')
        policies[policy] = set(values)
    if policies['ignored'] & policies['renamed']:
        raise MetadataError('patches: function cannot be both ignored and renamed')
    targets = []
    for kind, address_key in (('instruction', 'vram'), ('hook', 'before_vram')):
        records = patches.get(kind, [])
        if not isinstance(records, list):
            raise MetadataError(kind + ': expected array')
        seen = set()
        for record in records:
            if not isinstance(record, dict):
                raise MetadataError(kind + ': expected table')
            name = record.get('func')
            if not isinstance(name, str) or name not in functions:
                raise MetadataError(kind + ': unknown function')
            function = functions[name]
            address = _integer(record.get(address_key), kind + '.' + address_key, aligned=True)
            if not function['vram'] <= address < function['vram'] + function['size']:
                raise MetadataError(name + ': patch address outside function')
            if name in policies['ignored']:
                raise MetadataError(name + ': patch targets ignored function')
            key = (name, address)
            if key in seen:
                raise MetadataError(name + ': duplicate ' + kind + ' target')
            seen.add(key)
            if kind == 'instruction':
                _integer(record.get('value'), name + '.value')
            elif not isinstance(record.get('text'), str) or not record['text'].strip():
                raise MetadataError(name + ': empty hook body')
            targets.append({'kind': kind, 'function': name, 'section': function['section'],
                            'address': f'0x{address:08x}'})
    return targets
