#!/usr/bin/env python3
"""Source guard regressions; pure Python, never launches a Windows process."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("guard", ROOT / "tools/check_win_pointer.py")
guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guard)


class PointerGuardTest(unittest.TestCase):
    def test_every_hazard_requires_gate(self):
        names = ("SC_MOVE", "SC_SIZE", "SC_KEYMENU", "SetCursorPos", "SetPhysicalCursorPos",
                 "ClipCursor", "mouse_event", "SendInput", "WM_NCLBUTTONDOWN", "DragDetect",
                 "TrackPopupMenu", "TrackPopupMenuEx", "BlockInput", "SetCapture",
                 "SetForegroundWindow", "SwitchDesktop")
        for name in names:
            with self.subTest(name=name):
                self.assertTrue(guard.violations(f"void f(void) {{ use({name}); }}"))
                self.assertFalse(guard.violations(
                    f'void f(void) {{ LIVE_ONLY_NATIVE_LOOP("native"); use({name}); }}'))

    def test_gate_must_be_first_unconditional_statement(self):
        for prefix in ('int x=0;', 'if (live)', '#if 0\n', ''):
            code = f'void f(void) {{ {prefix} LIVE_ONLY_NATIVE_LOOP("native"); use(SC_SIZE); }}'
            self.assertEqual(bool(guard.violations(code)), bool(prefix))
        self.assertTrue(guard.violations(
            'void f(void) { { LIVE_ONLY_NATIVE_LOOP("nested"); use(SC_MOVE); } }'))

    def test_scope_does_not_leak_to_next_function_or_global(self):
        code = ('void f(void) { LIVE_ONLY_NATIVE_LOOP("native",0); use(SC_MOVE); }\n'
                'void g(void) { use(SC_SIZE); }\nint command=SC_KEYMENU;')
        self.assertEqual([name for _, name in guard.violations(code)], ["SC_SIZE", "SC_KEYMENU"])

    def test_comments_and_strings_are_not_operations(self):
        self.assertFalse(guard.violations('/* SC_MOVE */ // SendInput\nvoid f(void) { '
                                          'puts("SC_SIZE \\\" ClipCursor"); }'))
        self.assertTrue(guard.violations('#define MOVE SC_MOVE\n'))

    def test_alt_space_and_safe_caption_commands(self):
        self.assertTrue(guard.violations('void f(void) { PostMessageW(h,WM_SYSKEYDOWN,VK_SPACE,0); }'))
        self.assertTrue(guard.violations('void f(void) { PostMessageW(h,WM_SYSCHAR,VK_SPACE,0); }'))
        self.assertTrue(guard.violations('void f(void) { PostMessageW(h,WM_SYSKEYUP,VK_MENU,0); }'))
        for name in ("SC_MAXIMIZE", "SC_RESTORE", "SC_MINIMIZE", "SC_CLOSE"):
            self.assertFalse(guard.violations(f'void f(void) {{ SendMessageW(h,WM_SYSCOMMAND,{name},0); }}'))

    def test_header_resize_regression(self):
        # Incoming editor_resize_cases.h has this unguarded native size entry.
        code = ('static int resize_cases(editor *e) {\n'
                ' T(test_isolation_query()!=TEST_ISOLATION_LIVE);\n'
                ' T(PostMessageW(hwnd,WM_SYSCOMMAND,SC_SIZE|WMSZ_RIGHT,0));\n}')
        self.assertEqual(guard.violations(code), [(3, "SC_SIZE")])

    def test_gate_cannot_execute_input_as_hidden_return_value(self):
        self.assertTrue(guard.violations(
            'int f(void) { LIVE_ONLY_NATIVE_LOOP("native",SendInput(0,0,0)); }'))

    def test_gate_definition_must_keep_the_native_query_and_skip(self):
        self.assertTrue(guard.gate_is_valid(guard.GATE_DEFINITION))
        self.assertFalse(guard.gate_is_valid('/* ' + guard.GATE_DEFINITION + ' */'))
        self.assertFalse(guard.gate_is_valid(guard.GATE_DEFINITION.replace('test_isolation_query()', 'requested_mode')))
        self.assertFalse(guard.gate_is_valid(guard.GATE_DEFINITION + '\n#undef LIVE_ONLY_NATIVE_LOOP'))


if __name__ == "__main__":
    unittest.main()
