#!/usr/bin/env python3
"""Read-only metadata integrity audit; no recompilation or guest execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tomllib

from section_metadata import MetadataError, validate_patches, validate_sections, verify_images


def read_regular(path, limit):
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_CLOEXEC', 0))
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise MetadataError('input must be a bounded regular file')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            content = stream.read(limit + 1)
        if len(content) > limit:
            raise MetadataError('input exceeds size limit')
        return content
    finally:
        os.close(fd)


def audit(config_path, check_image=False):
    config_path = Path(config_path)
    config_bytes = read_regular(config_path, 1024 * 1024)
    config = tomllib.loads(config_bytes.decode('utf-8'))
    input_config = config.get('input')
    if not isinstance(input_config, dict):
        raise MetadataError('input: expected table')
    def input_path(key):
        value = input_config.get(key)
        if not isinstance(value, str) or not value:
            raise MetadataError(key + ': expected file path')
        return config_path.parent / value
    symbols_bytes = read_regular(input_path('symbols_file_path'), 8 * 1024 * 1024)
    symbols = tomllib.loads(symbols_bytes.decode('utf-8'))
    sections = symbols.get('section')
    functions = validate_sections(sections)
    targets = validate_patches(config, functions)
    result = {
        'metadata_integrity_passed': True,
        'config_sha256': hashlib.sha256(config_bytes).hexdigest(),
        'symbols_sha256': hashlib.sha256(symbols_bytes).hexdigest(),
        'sections': [{'name': s['name'], 'function_entries': len(s['functions'])} for s in sections],
        'function_entries': len(functions),
        'patch_targets': targets,
        'runtime_replacements_required': config.get('patches', {}).get('ignored', []),
        'runtime_implementations_verified': False,
        'images_verified': [],
        'guest_code_executed': False,
    }
    if check_image:
        image = read_regular(input_path('rom_file_path'), 64 * 1024 * 1024)
        result['images_verified'] = verify_images(image, sections)
        result['expanded_image_sha256'] = hashlib.sha256(image).hexdigest()
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'recomp-input/rush2049.toml')
    parser.add_argument('--check-image', action='store_true',
                        help='also verify the four local executable images; never uploads them')
    args = parser.parse_args(argv)
    try:
        result = audit(args.config, args.check_image)
    except (MetadataError, OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        # Do not expose arbitrary TOML contents, ROM bytes, or private local paths.
        reason = str(error) if isinstance(error, MetadataError) else type(error).__name__
        print('Metadata audit failed: ' + reason, file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
