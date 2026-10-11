import copy
import hashlib
import unittest
from pathlib import Path
import tomllib

from section_metadata import MetadataError, validate_sections, validate_patches, verify_images


def fixture():
    sections = []
    image = b'0123456789abcdef'
    hashes = {}
    for index, name in enumerate(('boot', 'game', 'overlay_a', 'overlay_b')):
        base = 0x80000400 if index == 0 else 0x80300000 + (4 if index == 1 else 0)
        section = {'name': name, 'rom': index * 4, 'vram': base, 'size': 4,
                   'functions': [{'name': name + '_entry', 'vram': base, 'size': 4}]}
        sections.append(section)
        hashes[name] = hashlib.sha256(image[index * 4:index * 4 + 4]).hexdigest()
    config = {'input': {'entrypoint': 0x80000400}, 'patches': {
        'instruction': [{'func': 'game_entry', 'vram': sections[1]['vram'], 'value': 0}],
        'hook': [{'func': 'overlay_a_entry', 'before_vram': sections[2]['vram'], 'text': 'poll();'}]}}
    return sections, image, hashes, config


class MetadataTests(unittest.TestCase):
    def test_complete_images_checked_in_actual_order(self):
        sections, image, hashes, _ = fixture()
        self.assertEqual(verify_images(image, sections[::-1], hashes), list(hashes)[::-1])

    def test_missing_duplicate_extra_sections_fail(self):
        sections, image, hashes, _ = fixture()
        for changed in (sections[:2], sections + [sections[0]],
                        [{**s, 'name': 'extra'} if s['name'] == 'game' else s for s in sections]):
            with self.subTest(changed=changed), self.assertRaises(MetadataError):
                verify_images(image, changed, hashes)

    def test_corrupt_or_truncated_image_fails(self):
        sections, image, hashes, _ = fixture()
        for changed in (image[:-1], b'x' + image[1:]):
            with self.assertRaises(MetadataError):
                verify_images(changed, sections, hashes)

    def test_invalid_section_ranges_fail(self):
        sections, _, _, _ = fixture()
        for key, value in (('rom', -4), ('rom', True), ('size', 0), ('size', 3),
                           ('vram', 0xFFFFFFFC), ('rom', 0xFFFFFFFC)):
            changed = copy.deepcopy(sections)
            changed[0][key] = value
            if value == 0xFFFFFFFC:
                changed[0]['size'] = 8
            with self.subTest(key=key, value=value), self.assertRaises(MetadataError):
                validate_sections(changed)

    def test_overlapping_image_ranges_fail(self):
        sections, _, _, _ = fixture()
        sections[1]['rom'] = 0
        with self.assertRaises(MetadataError):
            validate_sections(sections)

    def test_duplicate_function_name_and_entry_fail(self):
        sections, _, _, _ = fixture()
        for same_name in (True, False):
            changed = copy.deepcopy(sections)
            duplicate = dict(changed[0]['functions'][0])
            if not same_name:
                duplicate['name'] = 'second_name'
            changed[0]['functions'].append(duplicate)
            with self.assertRaises(MetadataError):
                validate_sections(changed)
        sections[1]['functions'][0]['name'] = sections[0]['functions'][0]['name']
        with self.assertRaises(MetadataError):
            validate_sections(sections)

    def test_invalid_function_ranges_and_names_fail(self):
        sections, _, _, _ = fixture()
        for key, value in (('vram', 0), ('vram', 0x80000401), ('size', 8),
                           ('size', False), ('name', 'bad-name')):
            changed = copy.deepcopy(sections)
            changed[0]['functions'][0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(MetadataError):
                validate_sections(changed)

    def test_shared_tails_and_overlay_vram_are_supported(self):
        sections, _, _, _ = fixture()
        sections[0]['size'] = 16
        sections[0]['rom'] = 64
        sections[0]['functions'][0]['size'] = 16
        sections[0]['functions'].append({'name': 'shared_tail_entry', 'vram': 0x80000404, 'size': 12})
        self.assertEqual(len(validate_sections(sections)), 5)

    def test_unknown_or_outside_patch_target_fails(self):
        sections, _, _, config = fixture()
        functions = validate_sections(sections)
        for kind, field, value in (('hook', 'func', 'missing'),
                                    ('hook', 'before_vram', sections[2]['vram'] + 4),
                                    ('instruction', 'vram', 1), ('instruction', 'value', -1),
                                    ('hook', 'text', ''), ('instruction', 'value', True)):
            changed = copy.deepcopy(config)
            changed['patches'][kind][0][field] = value
            with self.subTest(kind=kind, field=field), self.assertRaises(MetadataError):
                validate_patches(changed, functions)

    def test_duplicate_patch_and_policy_conflicts_fail(self):
        sections, _, _, config = fixture()
        functions = validate_sections(sections)
        for kind in ('hook', 'instruction'):
            changed = copy.deepcopy(config)
            changed['patches'][kind] *= 2
            with self.assertRaises(MetadataError):
                validate_patches(changed, functions)
        for policies in ({'ignored': ['missing']}, {'renamed': ['game_entry'] * 2},
                         {'ignored': ['game_entry']},
                         {'ignored': ['boot_entry'], 'renamed': ['boot_entry']}):
            changed = copy.deepcopy(config)
            changed['patches'].update(policies)
            with self.assertRaises(MetadataError):
                validate_patches(changed, functions)

    def test_entrypoint_must_be_unique_boot_entry(self):
        sections, _, _, config = fixture()
        for entry in (0, True, sections[1]['vram']):
            changed = copy.deepcopy(config)
            changed['input']['entrypoint'] = entry
            with self.assertRaises(MetadataError):
                validate_patches(changed, validate_sections(sections))

    def test_real_checked_in_metadata(self):
        root = Path(__file__).resolve().parents[1]
        sections = tomllib.loads((root / 'recomp-input/symbols.toml').read_text())['section']
        config = tomllib.loads((root / 'recomp-input/rush2049.toml').read_text())
        functions = validate_sections(sections)
        self.assertEqual(len(functions), 2136)
        self.assertEqual(len(validate_patches(config, functions)), 21)


if __name__ == '__main__':
    unittest.main()
