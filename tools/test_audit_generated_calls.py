import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from audit_generated_calls import expected_bodies, inspect_sources, parse_policy, scan_source
from section_metadata import MetadataError
from test_section_metadata import fixture


EMPTY_POLICY = {'reimplemented_funcs': set(), 'ignored_funcs': set(), 'renamed_funcs': set()}
POLL_BODY = 'extern void rush_poll_runtime(uint8_t*); rush_poll_runtime(rdram);'


def sample():
    sections, _, _, config = fixture()
    sections[0]['rom'] = 0x1000
    config['patches']['hook'][0]['text'] = POLL_BODY
    text = '''
#include "recomp.h"
RECOMP_FUNC void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    game_entry(rdram, ctx);
    // fake_comment_call(rdram, ctx);
    const char* note = "fake_string_call(rdram, ctx)";
}
RECOMP_FUNC void game_entry(uint8_t* rdram, recomp_context* ctx) {
    host_call(rdram, ctx);
    extern void fake_declaration(rdram, ctx);
}
RECOMP_FUNC void overlay_a_entry(uint8_t* rdram, recomp_context* ctx) {
    extern void rush_poll_runtime(uint8_t*); rush_poll_runtime(rdram);
    if (1) { overlay_b_entry(rdram, ctx); }
}
RECOMP_FUNC void overlay_b_entry(uint8_t* rdram, recomp_context* ctx) {}
'''
    return sections, config, [('funcs_0.c', text.encode())]


class GeneratedAuditTests(unittest.TestCase):
    def test_pinned_generator_entry_rom_offset_required(self):
        sections, config, sources = sample()
        sections[0]['rom'] = 0x2000
        with self.assertRaisesRegex(MetadataError, 'ROM offset 0x1000'):
            inspect_sources(sources, sections, config, EMPTY_POLICY)
        # Selection concerns the function's ROM position, not only the section.
        sections[0]['rom'] = 0xFFC
        sections[0]['vram'] -= 4
        sections[0]['size'] = 8
        self.assertTrue(inspect_sources(sources, sections, config, EMPTY_POLICY)['generated_body_integrity_passed'])
    def test_complete_synthetic_set_and_external_call_classification(self):
        sections, config, sources = sample()
        result = inspect_sources(sources, sections, config, EMPTY_POLICY)
        self.assertTrue(result['generated_body_integrity_passed'])
        self.assertEqual(result['external_direct_call_targets'], ['host_call'])
        self.assertEqual(len(result['direct_calls']), 3)
        self.assertFalse(result['runtime_implementations_verified'])
        self.assertFalse(result['indirect_calls_verified'])
        self.assertFalse(result['guest_code_executed'])
        self.assertFalse(result['hook_instruction_placement_verified'])
        self.assertFalse(result['instruction_patch_values_verified'])

    def test_missing_body_is_not_satisfied_by_prototype_or_comment(self):
        sections, config, sources = sample()
        body = sources[0][1].replace(b'RECOMP_FUNC void overlay_b_entry(uint8_t* rdram, recomp_context* ctx) {}',
                                    b'void overlay_b_entry(uint8_t*, recomp_context*); // RECOMP_FUNC void overlay_b_entry() {}')
        result = inspect_sources([('funcs_0.c', body)], sections, config, EMPTY_POLICY)
        self.assertFalse(result['generated_body_integrity_passed'])
        self.assertEqual(result['missing_bodies'], ['overlay_b_entry'])

    def test_duplicate_body_across_files_fails(self):
        sections, config, sources = sample()
        sources.append(('funcs_1.c', b'RECOMP_FUNC void game_entry(uint8_t* rdram, recomp_context* ctx) {}'))
        result = inspect_sources(sources, sections, config, EMPTY_POLICY)
        self.assertEqual(result['duplicate_bodies'], ['game_entry'])
        self.assertFalse(result['generated_body_integrity_passed'])

    def test_missing_and_insufficient_polls_fail(self):
        sections, config, sources = sample()
        for comment_only in (False, True):
            body = sources[0][1].replace(b'rush_poll_runtime(rdram);',
                                        b'/* rush_poll_runtime(rdram); */' if comment_only else b'')
            result = inspect_sources([('funcs_0.c', body)], sections, config, EMPTY_POLICY)
            self.assertEqual(result['missing_runtime_polls'][0]['found'], 0)
            self.assertFalse(result['generated_body_integrity_passed'])
        config['patches']['hook'].append({**config['patches']['hook'][0],
                                         'before_vram': config['patches']['hook'][0]['before_vram'] + 4})
        sections[2]['size'] = sections[2]['functions'][0]['size'] = 8
        sections[3]['rom'] = 16
        result = inspect_sources(sources, sections, config, EMPTY_POLICY)
        self.assertEqual(result['missing_runtime_polls'][0]['required'], 2)
        self.assertEqual(result['missing_runtime_polls'][0]['found'], 1)

    def test_suppressed_body_detected_and_selected_rename_applied(self):
        sections, config, sources = sample()
        policy = copy.deepcopy(EMPTY_POLICY)
        policy['reimplemented_funcs'].add('game_entry')
        expected, suppressed = expected_bodies(sections, config, policy)
        self.assertNotIn('game_entry_recomp', expected)
        self.assertEqual(suppressed[0]['generated_name'], 'game_entry_recomp')
        sources = [(n, s.replace(b'game_entry', b'game_entry_recomp')) for n, s in sources]
        result = inspect_sources(sources, sections, config, policy)
        self.assertEqual(result['unexpected_suppressed_bodies'], ['game_entry_recomp'])
        self.assertFalse(result['generated_body_integrity_passed'])
        policy = copy.deepcopy(EMPTY_POLICY)
        policy['renamed_funcs'].add('game_entry')
        result = inspect_sources(sources, sections, config, policy)
        self.assertTrue(result['generated_body_integrity_passed'])

    def test_explicit_renaming_and_ignoring(self):
        sections, config, sources = sample()
        config['patches'].update(renamed=['game_entry'], ignored=['overlay_b_entry'])
        body = sources[0][1].replace(b'game_entry', b'game_entry_recomp')
        body = body.replace(b'RECOMP_FUNC void overlay_b_entry(uint8_t* rdram, recomp_context* ctx) {}', b'')
        result = inspect_sources([('funcs_0.c', body)], sections, config, EMPTY_POLICY)
        self.assertTrue(result['generated_body_integrity_passed'])
        self.assertEqual(result['external_direct_call_targets'], ['host_call', 'overlay_b_entry'])

    def test_policies_resolve_after_automatic_rename(self):
        sections, config, _ = sample()
        config['patches']['renamed'] = ['game_entry']
        policy = copy.deepcopy(EMPTY_POLICY)
        policy['renamed_funcs'].add('game_entry')
        with self.assertRaises(MetadataError):
            expected_bodies(sections, config, policy)

    def test_generated_name_collisions_fail(self):
        sections, config, _ = sample()
        config['patches']['renamed'] = ['game_entry']
        sections[3]['functions'][0]['name'] = 'game_entry_recomp'
        with self.assertRaises(MetadataError):
            expected_bodies(sections, config, EMPTY_POLICY)

    def test_preprocessor_and_incomplete_definitions_refused(self):
        for source in (b'#if 0\nRECOMP_FUNC void fake() {}\n#endif',
                       b'RECOMP_FUNC void fake(uint8_t*, recomp_context*);',
                       b'RECOMP_FUNC void fake() {'):
            with self.assertRaises(MetadataError):
                scan_source(source, 'funcs_0.c')

    def test_line_locations_and_nested_calls(self):
        text = b'\nRECOMP_FUNC void test(uint8_t* rdram, recomp_context* ctx) {\n if (1) {\n  return direct(rdram,\n ctx);\n }\n}\n'
        definitions, calls, _ = scan_source(text, 'funcs_9.c')
        self.assertEqual(definitions, ['test'])
        self.assertEqual(calls, [{'caller': 'test', 'target': 'direct', 'file': 'funcs_9.c', 'line': 4}])

    def test_unknown_hook_body_and_stubs_refused(self):
        sections, config, sources = sample()
        config['patches']['hook'][0]['text'] = 'unrelated();'
        with self.assertRaises(MetadataError):
            inspect_sources(sources, sections, config, EMPTY_POLICY)
        config['patches']['hook'][0]['text'] = POLL_BODY
        config['patches']['stubs'] = ['game_entry']
        with self.assertRaises(MetadataError):
            inspect_sources(sources, sections, config, EMPTY_POLICY)

    def test_poll_prototype_cannot_satisfy_required_call(self):
        sections, config, sources = sample()
        body = sources[0][1].replace(b'rush_poll_runtime(rdram);', b'extern void rush_poll_runtime(rdram);')
        result = inspect_sources([('funcs_0.c', body)], sections, config, EMPTY_POLICY)
        self.assertEqual(result['missing_runtime_polls'][0]['found'], 0)

    def test_macro_cannot_turn_required_poll_into_noop(self):
        sections, config, sources = sample()
        for directive in (b'#define rush_poll_runtime(rdram) ((void)0)\n',
                          b'\f#\vdefine rush_poll_runtime(rdram) ((void)0)\n',
                          b'#undef rush_poll_runtime\n',
                          b'#pragma pop_macro("rush_poll_runtime")\n',
                          b'%:define rush_poll_runtime(rdram) ((void)0)\n',
                          b'??=define rush_poll_runtime(rdram) ((void)0)\n'):
            with self.subTest(directive=directive), self.assertRaises(MetadataError):
                inspect_sources([('funcs_0.c', directive + sources[0][1])],
                                sections, config, EMPTY_POLICY)

    def test_spliced_comment_cannot_supply_a_poll(self):
        sections, config, sources = sample()
        for continuation in (b'\\\n', b'\\\r\n', b'??/\n', b'\\ \n', b'\\\t\n'):
            body = sources[0][1].replace(b'rush_poll_runtime(rdram);',
                                        b'// disabled ' + continuation + b'rush_poll_runtime(rdram);')
            with self.subTest(continuation=continuation), self.assertRaises(MetadataError):
                inspect_sources([('funcs_0.c', body)], sections, config, EMPTY_POLICY)

    def test_plain_include_and_comments_remain_supported(self):
        text = (b'#include "recomp.h"\n#include <stdint.h>\n'
                b'// #define harmless comment\n/* #pragma harmless comment */\n'
                b'RECOMP_FUNC void test(uint8_t* rdram, recomp_context* ctx) {}')
        self.assertEqual(scan_source(text, 'funcs_0.c')[0], ['test'])

    def test_wrong_signature_and_binary_content_refused(self):
        for text in (b'RECOMP_FUNC void wrong() {}',
                     b'RECOMP_FUNC int wrong(uint8_t* rdram, recomp_context* ctx) {}',
                     b'\0RECOMP_FUNC void wrong(uint8_t* rdram, recomp_context* ctx) {}'):
            with self.assertRaises(MetadataError):
                scan_source(text, 'funcs_0.c')

    def test_policy_pin_and_table_parser(self):
        data = b'''const std::unordered_set<std::string> N64Recomp::reimplemented_funcs {"sdk"};
const std::unordered_set<std::string> N64Recomp::ignored_funcs { /* "fake" */ "ignored" };
const std::unordered_set<std::string> N64Recomp::renamed_funcs {"rename"};'''
        with self.assertRaises(MetadataError):
            parse_policy(data)
        policy, _ = parse_policy(data, require_pin=False)
        self.assertEqual(policy['ignored_funcs'], {'ignored'})
        repeated, _ = parse_policy(data.replace(b'"rename"', b'"rename", "rename"'), require_pin=False)
        self.assertEqual(repeated['renamed_funcs'], {'rename'})

    def test_no_sources_is_not_success(self):
        sections, config, _ = sample()
        with self.assertRaises(MetadataError):
            inspect_sources([], sections, config, EMPTY_POLICY)

    def test_cli_fails_missing_inputs_without_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('audit_generated_calls.py')),
                                     '--root', directory], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn(directory, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
