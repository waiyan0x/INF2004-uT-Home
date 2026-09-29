"""Regression tests for lexical blind spots and exception fail-closed behavior."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("c_standard_check", ROOT / "tools/c_standard/check.py")
assert SPEC and SPEC.loader
CHECK = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECK
SPEC.loader.exec_module(CHECK)


def complete(body: str) -> str:
    return body.rstrip() + "\n\n/*** end of file ***/\n\n"


def rules(source: str, filename: str = "sample.c", atomic_allowed: bool = False) -> set[str]:
    return {finding.rule for finding in CHECK.check_source(filename, complete(source), ["sample"], atomic_allowed)}


class LexicalTests(unittest.TestCase):
    def test_literal_and_comment_words_are_not_keywords(self):
        self.assertEqual(set(), rules('''
/* A long explanation mentioning auto and register is prose. */
static const char *gp_message = "goto continue short long abort()";
static const char *gp_quote = "escaped \\\" long";
static int g_character = '\\'';
'''))

    def test_escaped_newline_cannot_hide_keyword(self):
        findings = CHECK.check_source("sample.c", complete("void sample (void)\n{\n    au\\\nto int value;\n    return;\n}"))
        violation = next(finding for finding in findings if finding.rule == "1.7.a")
        self.assertEqual(3, violation.line)

    def test_spliced_comment_remains_comment(self):
        tokens = CHECK.lex("/\\\n* goto register *\\\n/\nint value;")
        self.assertEqual("comment", tokens[0].kind)
        self.assertEqual("/* goto register */", tokens[0].value)
        self.assertEqual(set(), rules("/\\\n* goto register *\\\n/\nint g_value;"))

    def test_backslash_and_nested_comment_tokens_rejected(self):
        self.assertIn("2.1.b", rules("/* Nested // syntax. */"))
        self.assertIn("2.1.b", rules("// A Windows path C:\\temp."))

    def test_unterminated_literal_and_comment(self):
        self.assertIn("project.lexical", rules('char *p_text = "oops\n'))
        self.assertIn("project.lexical", {item.rule for item in CHECK.check_source("sample.c", "/* missing")})

    def test_tabs_crlf_width_and_final_marker(self):
        found = CHECK.check_source("sample.c", "\t" + "x" * 81 + "\r\n")
        self.assertTrue({"3.5.a", "3.6", "1.2.a", "3.3.c"} <= {item.rule for item in found})

    def test_include_filename_not_interpreted_as_code(self):
        self.assertEqual(set(), rules("#include <setjmp.h>\n#include <long.h>"))

    def test_macro_code_is_checked(self):
        self.assertIn("5.2.b", rules("#define LENGTH ((long) 4)"))
        self.assertIn("8.5.b", rules("#define TERMINATE exit(1)"))

    def test_number_lexer_keeps_exponent_signs(self):
        tokens = CHECK.lex("1e-3 + 0x1p+4 - 2U")
        self.assertEqual(["1e-3", "+", "0x1p+4", "-", "2U"], [token.value for token in tokens])


class StructuralTests(unittest.TestCase):
    def test_unbraced_controls(self):
        source = "void sample (void)\n{\n    if (ready) work();\n    while (busy);\n}"
        self.assertIn("1.3.a", rules(source))

    def test_do_while_tail_is_not_a_missing_body(self):
        self.assertEqual(set(), rules("void sample (void)\n{\n    do\n    {\n        work();\n    } while (busy);\n}"))

    def test_assignment_in_nested_test(self):
        self.assertIn("8.2.c", rules("void sample (void)\n{\n    if ((value = load()) > 0)\n    {\n        use();\n    }\n}"))

    def test_for_initialization_and_increment_are_allowed(self):
        self.assertEqual(set(), rules("void sample (void)\n{\n    for (int index = 0; index < LIMIT; ++index)\n    {\n        work();\n    }\n}"))

    def test_assignment_in_for_test_is_rejected(self):
        self.assertIn("8.4.b", rules("void sample (void)\n{\n    for (index = 0; index = LIMIT; ++index)\n    {\n        work();\n    }\n}"))

    def test_empty_loop_requires_comment_inside(self):
        self.assertIn("8.4.d", rules("void sample (void)\n{\n    while (busy)\n    {\n    }\n}"))
        self.assertNotIn("8.4.d", rules("void sample (void)\n{\n    while (busy)\n    {\n        /* Hardware clears the pending flag. */\n    }\n}"))

    def test_else_if_chain_requires_final_else(self):
        source = "void sample (void)\n{\n    if (first)\n    {\n        run();\n    }\n    else if (second)\n    {\n        stop();\n    }\n}"
        self.assertIn("8.2.d", rules(source))
        source = source[:-1] + "    else\n    {\n        wait();\n    }\n}"
        self.assertNotIn("8.2.d", rules(source))

    def test_nested_switch_default_does_not_satisfy_outer_switch(self):
        source = "void sample (void)\n{\n    switch (outer)\n    {\n        case 0:\n            switch (inner)\n            {\n                default:\n                    break;\n            }\n            break;\n    }\n}"
        self.assertEqual(1, sum(item.rule == "8.3.b" for item in CHECK.check_source("sample.c", complete(source))))

    def test_call_is_not_treated_as_function_definition(self):
        self.assertNotIn("6.1.e", rules("void sample (void)\n{\n    SDK_ABI();\n}"))

    def test_function_name_rules(self):
        found = rules("void _Bad (void)\n{\n    return;\n}")
        self.assertTrue({"6.1.c", "6.1.e"} <= found)
        self.assertIn("6.1.d", rules("void " + "x" * 32 + " (void);"))
        self.assertIn("6.1.b", rules("void memcpy (void);"))

    def test_typedefs_include_function_pointer_names(self):
        self.assertEqual({"project.pointer-declarator"}, rules("typedef void (*callback_t)(void);"))
        self.assertIn("5.1.a", rules("typedef void (*CALLBACK)(void);"))
        self.assertIn("5.1.a", rules("typedef struct\n{\n    int value;\n} Wrong;"))
        self.assertNotIn("5.1.a", rules("typedef struct\n{\n    void (*p_step)(void);\n} sample_t;"))

    def test_stringizing_token_is_not_a_new_directive(self):
        source = "#define CHECK(expression) \\\n+    check((expression), #expression)"
        self.assertNotIn("3.2.d", rules(source))

    def test_guard_and_include_checks(self):
        self.assertIn("4.2.b", rules("#pragma once", "sample.h"))
        self.assertNotIn("4.2.b", rules("#ifndef SAMPLE_H\n#define SAMPLE_H\n#endif", "sample.h"))
        self.assertIn("4.3.d", rules('#include "C:/private/file.h"'))
        self.assertIn("4.3.f", rules('#include "implementation.c"'))


class ExpressionTests(unittest.TestCase):
    def test_compound_logical_operands_require_own_parentheses(self):
        self.assertIn("1.4.b", rules("void sample (void)\n{\n    if (value > 0 && other > 0)\n    {\n        run();\n    }\n}"))
        self.assertNotIn("1.4.b", rules("void sample (void)\n{\n    if ((value > 0) && (other > 0))\n    {\n        run();\n    }\n}"))

    def test_function_call_member_access_and_not_need_wrapping(self):
        for expression in ("ready() && enabled", "p_data->ready && enabled", "!ready && enabled"):
            self.assertIn("1.4.b", rules(f"void sample (void)\n{{\n    use({expression});\n}}"))
        self.assertNotIn("1.4.b", rules("void sample (void)\n{\n    use((ready()) && enabled);\n}"))

    def test_logical_precedence_checked_without_false_chain_violations(self):
        self.assertIn("1.4.b", rules("void sample (void)\n{\n    use(first || second && third);\n}"))
        self.assertNotIn("1.4.b", rules("void sample (void)\n{\n    use(first && second && third);\n}"))

    def test_constants_left_equality_only(self):
        for expression in ("value == 0", "p_data == NULL", "value == MODE_READY", "value == (-1)"):
            self.assertIn("8.6.a", rules(f"void sample (void)\n{{\n    use({expression});\n}}"))
        for expression in ("0 == value", "NULL == p_data", "first == second", "value != 0"):
            self.assertNotIn("8.6.a", rules(f"void sample (void)\n{{\n    use({expression});\n}}"))

    def test_equality_does_not_cross_argument_or_condition_boundary(self):
        self.assertNotIn("8.6.a", rules("void sample (void)\n{\n    use(0 == value, 3);\n    if ((0 == value) && ready)\n    {\n        run();\n    }\n}"))


class DeclarationTests(unittest.TestCase):
    def test_parameter_and_struct_pointer_prefixes(self):
        self.assertEqual(set(), rules("typedef struct\n{\n    char *p_name;\n} sample_t;\nvoid sample (char *p_text, int amount);"))
        self.assertIn("7.1.k", rules("void sample (char *text);"))
        self.assertIn("7.1.l", rules("void sample (char **text);"))

    def test_global_and_combined_prefixes(self):
        self.assertEqual(set(), rules("static bool gb_busy;\nstatic char *gp_text;\nstatic bool *gpb_ready;\nstatic ID gh_task;"))
        self.assertIn("7.1.j", rules("static int count;"))
        self.assertIn("7.1.o", rules("static bool g_busy;"))
        self.assertEqual(set(), rules("static atomic_int gh_owner;", atomic_allowed=True))

    def test_local_boolean_and_handle_prefixes(self):
        self.assertEqual(set(), rules("void sample (void)\n{\n    bool b_ready = false;\n    ID h_task = 0;\n}"))
        self.assertIn("7.1.m", rules("void sample (bool ready);"))
        self.assertIn("7.1.n", rules("void sample (ID task);"))

    def test_array_storage_and_array_parameter_are_distinct(self):
        self.assertEqual(set(), rules("static char g_text[LENGTH];\nvoid sample (char p_text[LENGTH]);"))
        self.assertIn("7.1.k", rules("void sample (char text[LENGTH]);"))

    def test_explicit_function_pointer_declaration(self):
        self.assertEqual({"project.pointer-declarator"}, rules("static void (*gp_callback)(void *p_context);"))
        self.assertIn("7.1.k", rules("void sample (void (*callback)(void));"))

    def test_multi_declarator_is_not_function_parameter_comma(self):
        self.assertEqual(set(), rules("void sample (int first, int second);"))
        self.assertIn("8.1.a", rules("static int g_first, g_second;"))
        self.assertIn("8.1.a", rules("void sample (void)\n{\n    int first = call(1, 2), second = 3;\n}"))

    def test_assignment_call_and_cast_are_not_declarations(self):
        self.assertEqual(set(), rules("void sample (void)\n{\n    output = call(value);\n    output = (uint32_t) value;\n    use(&value);\n}"))

    def test_names_reserved_short_and_uppercase(self):
        self.assertIn("7.1.e", rules("void sample (int id);"))
        self.assertIn("7.1.f", rules("void sample (int Value);"))
        self.assertIn("7.1.a", rules("void sample (int private);"))


class DeviationTests(unittest.TestCase):
    def setUp(self):
        self.source = complete("typedef int ID;")
        self.findings = CHECK.check_source("tkernel.h", self.source)
        self.entry = {"id": "KERNEL-ABI-ID", "path": "tkernel.h", "rule": "5.1.a",
                      "symbol": "ID", "reason": "Required by the kernel API mirror.",
                      "authority": "Pinned kernel's public ID typedef."}

    def apply(self, entries):
        return CHECK.apply_deviations(self.findings, entries, Path("."), {"tkernel.h": self.source})

    def test_exact_symbol_only_suppresses_matching_rule(self):
        remaining, errors = self.apply([self.entry])
        self.assertEqual([], errors)
        self.assertNotIn("5.1.a", {item.rule for item in remaining})
        self.assertIn("4.2.b", {item.rule for item in remaining})

    def test_stale_and_wildcard_exceptions_fail(self):
        entry = dict(self.entry, symbol="REMOVED")
        self.assertTrue(self.apply([entry])[1])
        entry = dict(self.entry, path="*.h")
        self.assertTrue(self.apply([entry])[1])

    def test_empty_reason_or_missing_anchor_fails(self):
        self.assertTrue(self.apply([dict(self.entry, reason="")])[1])
        entry = dict(self.entry)
        del entry["symbol"]
        self.assertTrue(self.apply([entry])[1])

    def test_source_anchor_does_not_survive_changed_line(self):
        entry = dict(self.entry, source_line="typedef int OLD;")
        self.assertTrue(self.apply([entry])[1])

    def test_new_files_in_scope_vendor_and_build_out_of_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("firmware/new.c", "tests/new.h", "firmware/third_party/lib.c",
                             "build/main.c", "docs/example.c", "build-debug/generated.c"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual({"firmware/new.c", "tests/new.h", "docs/example.c"},
                             {path.relative_to(root).as_posix() for path in CHECK.source_files(root)})

    def test_formatter_missing_fails_closed(self):
        self.assertTrue(CHECK.check_format("unlikely-to-exist-clang-format", [], "20.1.8"))

    def test_wrong_formatter_version_fails_closed(self):
        with patch.object(CHECK.subprocess, "run") as run:
            run.return_value.stdout = "clang-format version 19.1.0"
            self.assertTrue(CHECK.check_format("clang-format", [], "20.1.8"))


class ModuleTests(unittest.TestCase):
    def test_paired_header_and_own_include(self):
        sources = {"src/sample.c": '#include "api/sample.h"\n', "api/sample.h": ""}
        self.assertEqual([], CHECK.check_modules(sources))
        self.assertEqual({"4.2.a", "4.3.c"}, {finding.rule for finding in CHECK.check_modules({"sample.c": ""})})

    def test_eight_character_module_collision(self):
        sources = {"runtime_first.h": "", "runtime_second.h": ""}
        self.assertEqual({"4.1.b"}, {finding.rule for finding in CHECK.check_modules(sources)})

    def test_shared_public_module_prefix(self):
        source = complete("int rt_port_init (void);")
        self.assertIn("6.1.i", {finding.rule for finding in CHECK.check_source("hostport.c", source)})
        self.assertNotIn("6.1.i", {finding.rule for finding in CHECK.check_source("hostport.c", source, ["rt_port_"])})

    def test_header_cannot_allocate_storage(self):
        source = "#ifndef SAMPLE_H\n#define SAMPLE_H\nstatic int g_storage;\n#endif"
        self.assertIn("4.2.c.ii", rules(source, "sample.h"))
        self.assertNotIn("4.2.c.ii", rules(source.replace("static", "extern"), "sample.h"))


class FormatPolicyTests(unittest.TestCase):
    def test_function_signature_typedef_and_pointer_preserve_spacing(self):
        self.assertEqual(set(), rules("typedef int sample_callback_t (void * p_context);\nstatic sample_callback_t * gp_callback;"))
        self.assertIn("project.pointer-declarator", rules("void sample (int (*p_callback)(void));"))
        self.assertNotIn("project.pointer-declarator", rules("void sample (void)\n{\n    (*p_callback)();\n}"))

    def test_switch_break_is_aligned_with_its_case(self):
        source = "switch (value)\n{\ncase 0:\n    work();\n    break;\ndefault:\n    break;\n}\n"
        expected = source.replace("    break;", "break;")
        self.assertEqual(expected, CHECK.canonical_format(source))
        self.assertEqual(expected, CHECK.canonical_format(expected))
        self.assertIn("8.3.a", rules(source))
        self.assertNotIn("8.3.a", rules(expected))

    def test_loop_breaks_remain_at_their_original_indentation(self):
        source = "switch (value)\n{\ncase 0:\n    while (busy)\n    {\n        break;\n    }\n    do\n    {\n        break;\n    } while (busy);\n    for (;;)\n    {\n        break;\n    }\n    break;\ndefault:\n    break;\n}\n"
        expected = source.replace("\n    break;", "\nbreak;")
        self.assertEqual(expected, CHECK.canonical_format(source))

    def test_nested_switch_uses_its_own_labels(self):
        source = "switch (outer)\n{\ncase 0:\n    switch (inner)\n    {\n    case 1:\n        break;\n    default:\n        break;\n    }\n    break;\ndefault:\n    break;\n}\n"
        expected = "switch (outer)\n{\ncase 0:\n    switch (inner)\n    {\n    case 1:\n    break;\n    default:\n    break;\n    }\nbreak;\ndefault:\nbreak;\n}\n"
        self.assertEqual(expected, CHECK.canonical_format(source))

    def test_braced_case_and_conditional_switch_exit(self):
        source = "switch (value)\n{\ncase 0:\n{\n    if (ready)\n    {\n        break;\n    }\n    break;\n}\ndefault:\n    break;\n}\n"
        expected = source.replace("        break;", "break;").replace("    break;", "break;")
        self.assertEqual(expected, CHECK.canonical_format(source))

    def test_comments_strings_and_macro_bodies_are_unchanged(self):
        source = '/* switch (x) { case 0: break; } */\nconst char * p_text = "switch break";\n#define BLOCK switch (x) { case 0: break; }\n'
        self.assertEqual(source, CHECK.canonical_format(source))


class LanguageProfileTests(unittest.TestCase):
    def test_atomic_constructs_require_the_explicit_profile(self):
        source = "#include <stdatomic.h>\nstatic atomic_int g_value;"
        self.assertIn("1.1.a", rules(source))
        self.assertNotIn("1.1.a", rules(source, atomic_allowed=True))
        self.assertNotIn("1.1.a", rules('/* atomic_int is discussed here. */\nstatic char *gp_text = "_Atomic";'))

    def test_atomic_profile_does_not_enable_other_c11_features(self):
        source = '#include <threads.h>\n_Static_assert(1, "test");'
        self.assertIn("1.1.a", rules(source, atomic_allowed=True))
        self.assertIn("1.1.a", rules('#define ASSERT_LAYOUT _Static_assert(1, "layout")', atomic_allowed=True))


if __name__ == "__main__":
    unittest.main()
