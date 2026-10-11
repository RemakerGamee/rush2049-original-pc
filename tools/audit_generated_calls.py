#!/usr/bin/env python3
"""Read-only generated-body/call audit, separate from executable link acceptance."""
import argparse
import bisect
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import tomllib

from audit_metadata import read_regular
from section_metadata import MetadataError, validate_sections, validate_patches
from oc_support.save_path_audit import (
    _mask_comments_and_literals, _function_regions, _is_local_function_declaration,
)


PINNED_POLICY_BLOB = '9c8f27d1aac03aca863972bc676baf71907237a8'
POLICY_NAMES = ('reimplemented_funcs', 'ignored_funcs', 'renamed_funcs')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(\s*rdram\s*,\s*ctx\s*\)')
POLL = re.compile(r'\brush_poll_runtime\s*\(\s*rdram\s*\)')
CALL_START = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
GENERATED_SIGNATURE = re.compile(
    r'RECOMP_FUNC\s+void\s+[A-Za-z_]\w*\s*\(\s*uint8_t\s*\*\s*rdram\s*,'
    r'\s*recomp_context\s*\*\s*ctx\s*\)\s*\Z')


def parse_policy(data, require_pin=True):
    blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if require_pin and blob != PINNED_POLICY_BLOB:
        raise MetadataError('recompiler symbol policy differs from pinned source')
    source = data.decode('utf-8')
    result = {}
    for name in POLICY_NAMES:
        pattern = r'N64Recomp::' + name + r'\s*\{(.*?)\};'
        matches = re.findall(pattern, source, re.S)
        if len(matches) != 1:
            raise MetadataError('recompiler policy table missing or ambiguous: ' + name)
        # String values form the policy. Remove comments before reading strings.
        body = re.sub(r'/\*.*?\*/|//[^\n]*', '', matches[0], flags=re.S)
        values = re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"', body)
        # These are upstream unordered_set initializers. Repeated names are
        # harmless and occur in the pinned ignored list; retain set semantics.
        result[name] = set(values)
    return result, blob


def expected_bodies(sections, config, policy):
    """Mirror the pinned symbol-file route's naming/selection, not recompilation."""
    functions = validate_sections(sections)
    validate_patches(config, functions)
    patches = config.get('patches', {})
    if patches.get('stubs'):
        raise MetadataError('stubbed guest bodies require a separate review')
    entrypoint = config['input']['entrypoint']
    boot = next(s for s in sections if s['name'] == 'boot')
    entry = next(f for f in functions.values() if f['section'] == 'boot' and f['vram'] == entrypoint)
    if boot['rom'] + entry['vram'] - boot['vram'] != 0x1000:
        raise MetadataError('entrypoint: pinned generator requires function ROM offset 0x1000')
    selected = {}
    suppressed = []
    generated_names = set()
    for original, function in functions.items():
        name = original
        ignored = False
        if original in policy['reimplemented_funcs'] or original in policy['ignored_funcs']:
            name += '_recomp'
            ignored = True
        elif original in policy['renamed_funcs']:
            name += '_recomp'
        if function['vram'] == entrypoint and function['section'] == 'boot':
            name = 'recomp_entrypoint'
        if name in patches.get('ignored', []):
            ignored = True
        if name in patches.get('renamed', []):
            name += '_recomp'
        if name in generated_names:
            raise MetadataError('generated name collision: ' + name)
        generated_names.add(name)
        if ignored:
            suppressed.append({'metadata_name': original, 'generated_name': name,
                               'section': function['section']})
        else:
            selected[name] = original
    # The generator resolves explicit policies after its automatic renames.
    automatically_renamed = set.union(*policy.values())
    automatic_names = {n + '_recomp' if n in automatically_renamed else n for n in functions}
    automatic_names.discard(next(n for n, f in functions.items()
                                if f['section'] == 'boot' and f['vram'] == entrypoint))
    automatic_names.add('recomp_entrypoint')
    for kind in ('ignored', 'renamed'):
        if any(n not in automatic_names for n in patches.get(kind, [])):
            raise MetadataError(kind + ': target was automatically renamed by recompiler')
    return selected, suppressed


def scan_source(data, filename):
    if b'\0' in data:
        raise MetadataError('NUL byte in generated source')
    source = data.decode('utf-8')
    # C line splicing occurs before comments are removed. Without this check a
    # continued // comment can look like an executable poll on its next line.
    # Alternate directive/backslash spellings also need actual preprocessing.
    # GCC also splices a backslash separated from newline by whitespace.
    if re.search(r'\\[ \t\v\f]*\r?\n|\?\?[=/]', source):
        raise MetadataError('spliced or trigraph source requires compiler preprocessing')
    masked = _mask_comments_and_literals(source)
    # Only includes are expected in plain generator output. Macros can erase a
    # required call, and pragmas can restore macro definitions. Do not strip
    # these and then mistake their unprocessed operands for executable calls.
    for directive in re.finditer(r'(?m)^[^\S\n]*(#|%:)[^\S\n]*([A-Za-z_]\w*)?', masked):
        if directive[1] != '#' or directive[2] != 'include':
            raise MetadataError('generated directive requires compiler preprocessing')
    masked = re.sub(r'(?m)^\s*#[^\n]*', lambda m: ' ' * len(m[0]), masked)
    regions = _function_regions(masked)
    if len(list(re.finditer(r'\bRECOMP_FUNC\b', masked))) != len(regions):
        raise MetadataError('incomplete or non-definition RECOMP_FUNC declaration')
    if any(not GENERATED_SIGNATURE.fullmatch(masked[r['start']:r['body_start'] - 1]) for r in regions):
        raise MetadataError('generated function signature differs from recompiler output')
    lines = [0] + [m.end() for m in re.finditer('\n', source)]
    calls = []
    polls = {}
    for region in regions:
        name = region['name']
        polls[name] = sum(not _is_local_function_declaration(
            masked, CALL_START.match(masked, match.start()), region)
            for match in POLL.finditer(masked, region['body_start'], region['body_end']))
        for match in CALL.finditer(masked, region['body_start'], region['body_end']):
            # This helper expects a match ending at the opening parenthesis.
            opening_match = CALL_START.match(masked, match.start())
            if _is_local_function_declaration(masked, opening_match, region):
                continue
            calls.append({'caller': name, 'target': match[1], 'file': filename,
                          'line': bisect.bisect_right(lines, match.start())})
    return [r['name'] for r in regions], calls, polls


def inspect_sources(sources, sections, config, policy):
    expected, suppressed = expected_bodies(sections, config, policy)
    definitions = Counter()
    calls = []
    polls = Counter()
    inventory = []
    for filename, data in sources:
        names, found_calls, found_polls = scan_source(data, filename)
        definitions.update(names)
        calls.extend(found_calls)
        polls.update(found_polls)
        inventory.append({'file': filename, 'sha256': hashlib.sha256(data).hexdigest()})
    if not inventory:
        raise MetadataError('no generated funcs_*.c sources found')
    missing = sorted(set(expected) - set(definitions))
    duplicate = sorted(n for n, count in definitions.items() if count != 1)
    unexpected_suppressed = sorted({s['generated_name'] for s in suppressed} & set(definitions))
    hook_requirements = Counter()
    for hook in config.get('patches', {}).get('hook', []):
        # Only the maintained runtime-poll body is audited; reject other bodies
        # rather than imply they have been checked by a string search.
        if hook.get('text') != 'extern void rush_poll_runtime(uint8_t*); rush_poll_runtime(rdram);':
            raise MetadataError('unsupported hook body for generated preservation audit')
        generated_name = next((n for n, original in expected.items() if original == hook['func']), None)
        if generated_name is None:
            raise MetadataError('hook has no selected generated body')
        hook_requirements[generated_name] += 1
    missing_polls = [{'function': n, 'required': count, 'found': polls[n]}
                     for n, count in sorted(hook_requirements.items()) if polls[n] < count]
    external = sorted({call['target'] for call in calls} - set(definitions))
    passed = not (missing or duplicate or unexpected_suppressed or missing_polls)
    return {
        'generated_body_integrity_passed': passed,
        'expected_bodies': len(expected), 'observed_unique_bodies': len(definitions),
        'missing_bodies': missing, 'duplicate_bodies': duplicate,
        'unexpected_suppressed_bodies': unexpected_suppressed,
        'additional_generated_bodies': sorted(set(definitions) - set(expected) - set(unexpected_suppressed)),
        'missing_runtime_polls': missing_polls,
        'suppressed_metadata_functions': suppressed,
        'direct_calls': sorted(calls, key=lambda c: (c['file'], c['line'], c['target'])),
        'external_direct_call_targets': external,
        'source_inventory': inventory,
        'runtime_implementations_verified': False,
        'compiler_preprocessing_verified': False,
        'indirect_calls_verified': False,
        'hook_instruction_placement_verified': False,
        'instruction_patch_values_verified': False,
        'guest_code_executed': False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument('--root', type=Path, default=root)
    args = parser.parse_args(argv)
    try:
        config_bytes = read_regular(args.root / 'recomp-input/rush2049.toml', 1024 * 1024)
        config = tomllib.loads(config_bytes.decode('utf-8'))
        symbols_path = args.root / 'recomp-input' / config['input']['symbols_file_path']
        symbols_bytes = read_regular(symbols_path, 8 * 1024 * 1024)
        sections = tomllib.loads(symbols_bytes.decode('utf-8'))['section']
        policy, blob = parse_policy(read_regular(args.root / 'N64Recomp/src/symbol_lists.cpp', 1024 * 1024))
        source_dir = args.root / 'recomp-input' / config['input']['output_func_path']
        paths = sorted(source_dir.glob('funcs_*.c'))
        if len(paths) > 4096:
            raise MetadataError('too many generated source files')
        sources = []
        total = 0
        for path in paths:
            data = read_regular(path, 16 * 1024 * 1024)
            total += len(data)
            if total > 256 * 1024 * 1024:
                raise MetadataError('generated source bundle exceeds size limit')
            sources.append((path.name, data))
        result = inspect_sources(sources, sections, config, policy)
        result.update(config_sha256=hashlib.sha256(config_bytes).hexdigest(),
                      symbols_sha256=hashlib.sha256(symbols_bytes).hexdigest(), policy_blob=blob)
    except (MetadataError, OSError, UnicodeError, tomllib.TOMLDecodeError, KeyError, TypeError) as error:
        reason = str(error) if isinstance(error, MetadataError) else type(error).__name__
        print('Generated-call audit failed: ' + reason, file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result['generated_body_integrity_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
