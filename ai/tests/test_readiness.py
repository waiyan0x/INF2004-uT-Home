"""Startup creation is distinct from runtime creation, never a qualification."""
from pathlib import Path
import tempfile
import unittest

from ai.static_readiness import inspect_source, report, KNOWN_PATHS


class ReadinessTests(unittest.TestCase):
    def inspect(self, worker, parser='void parse(void) {}'):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in KNOWN_PATHS:
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(worker if name.endswith('tkport.c') else parser if name.endswith('cfgjson.c') else '')
            return inspect_source(root)

    def test_boot_creation_is_retained_as_evidence_requirement(self):
        result = self.inspect('int rt_port_init (void) { for (;;) { tk_cre_tsk(&task); } }')
        self.assertEqual(result['findings'], [])
        self.assertEqual(len(result['startup_calls_requiring_runtime_evidence']), 1)
        self.assertFalse(report()['capture_allowed'])
        self.assertIn('missing_reviewed_execution_context', report()['blockers'])

    def test_creations_elsewhere_deletion_and_allocator_are_blockers(self):
        boot = 'int rt_port_init (void) { tk_cre_tsk(&task); }\n'
        for call in ('tk_cre_tsk(&other);', 'tk_cre_sem(&sem);', 'tk_del_tsk(handle);'):
            result = self.inspect(boot+'int prepare(void) {'+call+'}')
            self.assertEqual(len(result['findings']), 1, call)
        result = self.inspect(boot, 'void parse(void) { malloc(10); free(ptr); }')
        self.assertTrue(result['findings'])

    def test_comments_strings_missing_or_extra_boot_site_do_not_hide_calls(self):
        boot = 'int rt_port_init (void) { tk_cre_tsk(&task); }\n'
        self.assertFalse(self.inspect(boot+'/* tk_del_tsk(x); */')['findings'])
        for source in ('/* '+boot+' */', 'int rt_port_init(void) { tk_cre_tsk(&a); tk_cre_tsk(&b); }'):
            self.assertTrue(self.inspect(source)['findings'])
