# C standard enforcement

Run the project gate from the repository root:

```sh
python tools/c_standard/check.py --root .
python -m unittest discover -s tests/c_standard -p "test_*.py"
```

The gate requires clang-format **20.1.8** and the repository's `.clang-format`.
Install the pinned requirements, put that executable on PATH, set
`BARR_CLANG_FORMAT` to its path, or supply `--format <executable>`. A missing or
different formatter fails the gate. `--skip-format` is a lexical diagnostic
option and is not the build/CI gate. `--json` provides machine-readable findings.

Use the project formatting command when editing C files:

```sh
python tools/c_standard/format.py --root .
# Or format only named files, relative to the repository root:
python tools/c_standard/format.py --root . firmware/src/runtime/runtime.c
```

It uses the same executable selection and required version as the checker. It
applies clang-format, then changes only the leading spaces on switch-owned
`break` lines to align them with their case labels (BARR-C 8.3.a). clang-format
20 has no independent setting for that alignment. Nested loop breaks retain
their indentation, and nested switches use their own case labels. Calling
clang-format directly is insufficient for modules containing switches.

The source of the coding rules is the project's copy of BARR-C:2018. This tool
checks a defined subset; a clean result is **not proof of full conformance**.
Compiler diagnostics, behavioral tests and a review of the remaining rules are
required. The checker does not rewrite files or grant deviations.

## Scope and checks

Discovery includes new, unstaged `.c` and `.h` files throughout the repository.
Vendor `third_party`, build outputs and dependency/environment directories are
excluded. Symlink directories and files are not followed. Documentation C files
and project templates are included. The list of excluded directory names is
explicit in `check.py`; it must not be expanded to hide project source.

The lexer distinguishes comments, character/string literals, preprocessing
directives and code after escaped-newline splicing. Its structural checks run
without expanding macros. The C compiler remains responsible for valid syntax.
Macro replacement bodies receive token, naming and language-profile checks;
their expressions and control structures require review. A named function-like
macro rationale does not exempt its body from the remaining source rules.

| Area | Checks performed by this tool |
| --- | --- |
| Text | 80-character lines; no tabs, CR or prohibited control characters; no trailing whitespace; final end-of-file comment and blank line. |
| Formatting | Byte comparison with pinned clang-format plus the switch-break alignment pass. |
| Comments | Forbidden nested comment delimiters and backslashes in comment text; unterminated literals/comments. |
| Control flow | Braces for control bodies; no assignment in conditional/loop tests; final else for else-if chains; switch default; documented empty loop; literal infinite while loops. |
| Expressions | Parentheses around compound logical operands, respecting logical precedence; literals and conventional uppercase constants on the left of equality comparisons. |
| Tokens | `auto`, `register`, `short`, `long`; references to forbidden jump-library functions. |
| Modules | Lowercase filenames; standard-header collisions; uniqueness of the first eight basename characters; paired source/header names; source includes its own header; relative includes; no included C implementation; conventional header guards. |
| Names | Direct function declarations: lowercase, length, reserved names, public module prefix. Recognized typedef names: lowercase `_t` and public module prefix. Macro names: uppercase. |
| Variables | Ordinary explicit declarators: 3–31 characters, lowercase, no leading underscore/reserved names; global, explicit pointer/pointer-to-pointer, Boolean and recognizable handle prefixes; no multi-variable declarations; no header storage. |
| Macros | Function-like macros require a named, specific rationale in the deviation manifest. |
| Language profile | Selected C11 keywords and headers, atomic names, and atomic operations are rejected outside the two exact retained atomic-profile paths. Other C11 keywords/headers remain rejected inside those paths. |

The variable scanner recognizes explicit built-in and `_t` declarations, common
atomic types, and the pinned kernel ABI types. It checks direct pointers and
function pointers. It does **not** infer pointer aliases supplied by other
headers, macro-generated declarations, the meaning of integer Boolean flags, or
the signedness/value range of expressions. These remain review/compiler tasks.

`goto` and `continue` avoidance is a preferred practice in BARR-C, not an
unconditional prohibition. Their use requires code review, including the
standard's restrictions on a goto's destination. Multiple return statements are
not rejected: the standard permits them when they improve readability.

`policy.json` records the formatter version and the narrowly named modules that
implement a shared public API. These module-prefix associations are part of the
project’s module design, not a file-wide exemption from the other checks.

Declare callback signatures as function types, then declare their pointers:

```c
typedef int module_callback_t (void * p_context);
module_callback_t * p_callback;
```

This retains the callback ABI while allowing both declaration-pointer spacing
and tight grouping parentheses. New inline parenthesized pointer declarations
are rejected by `project.pointer-declarator`; pointer casts at an external ABI
boundary still require their nearby explanation and review. The formatter does
not rewrite declarations into typedefs.

## Required review beyond the tool

Reviewers must apply the complete source standard, including these areas that a
lexical checker or formatter cannot establish:

- The supported language/compiler profile, extensions and ABI requirements;
  appropriate `static`, `const`, atomic/volatile use and synchronization.
- Each cast's nearby explanation of type, range and alignment safety; explicit
  evaluation order beyond the checked logical operators; macro-expanded
  expressions and constant expressions the lexical checker cannot identify.
- Meaningful identifiers and documented abbreviations; correct prefixes hidden
  by typedefs; task and ISR naming; interrupt registration/handling.
- Useful module/function documentation, assumptions and design references;
  no commented-out code; debug-only instrumentation guards.
- Public/private interface placement, prototypes, include necessity, module
  organization and private functions' linkage.
- Fixed-width requirements, signed/unsigned mixing, bitwise operands, structure
  and wire layout, Boolean conversion and floating-point handling if introduced.
- Initialization before use, pointer initialization, one-statement lines,
  justified nesting and function length, loop bounds and nonliteral named
  constants on the left of equality. Compiler warnings and tests supplement
  this review.
- Any necessary parameterized macro: complete/operand parentheses, single
  evaluation and no transfer of control. A recorded macro rationale does not
  remove the need to inspect its implementation.

## Narrow deviations and external interfaces

`deviations.json` is version controlled. Each entry requires a unique `id`, an
exact checked `path`, one `rule`, an exact `symbol` or `source_line` anchor, a
specific `reason`, and its `authority`. For example, an upstream kernel header
can require an uppercase typedef in the faithful host-test ABI mirror. In that
case cite the pinned upstream declaration as the authority. This records an
external contract; it must not falsely claim that a project manager approved a
general deviation. Any discretionary departure still requires the approval
process stated in the source standard.

No wildcard file exemptions or blanket per-file suppressions are supported.
Unanchored, malformed, duplicate-ID and stale entries fail the gate. Changing
the manifest or module-prefix policy is a code-review event. Keep explanations
beside the affected interface too, so reviewers can understand it without
decoding a linter configuration.

The existing runtime's C11 atomics preserve synchronization semantics that C99
volatile objects would not provide. The user approved the existing profile in
the two exact `atomic_profile_paths` and the 49 existing kernel-interface
exceptions on 2026-09-28, recorded under the effective Git name Neoweikang.
See [the sign-off](AUDIT.md#compatibility-sign-off-and-limits) and
[scoped approval record](approval-20260928.json). The record binds the approved
exception IDs and definitions with a canonical payload hash; the CHECK macro
rationale is outside this deviation approval. New or altered exceptions,
expanded paths and additional C11 features need separate review and approval.

The `atomic_profile_approval` and `compatibility_approval` metadata link to that
documentary record. The checker does not authenticate approval metadata or
validate the approval hash. Reviewers must compare the scope when changing
policy or deviations; future entries do not inherit this approval.
The tool neither validates thread synchronization nor certifies target behavior.
