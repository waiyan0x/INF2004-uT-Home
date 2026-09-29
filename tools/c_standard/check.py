#!/usr/bin/env python3
"""Check the mechanically enforceable part of the project's C standard.

This is a lexical checker, not a C compiler or a claim of complete BARR-C
conformance.  Compiler warnings and the review checklist remain mandatory.
No source is rewritten by this command.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import subprocess
import sys


EXCLUDED_DIRS = {
    ".git", ".venv", "venv", "__pycache__", "node_modules", "third_party",
    "build", "dist",
}
C_KEYWORDS = set("""auto break case char const continue default do double else
enum extern float for goto if inline int long register restrict return short
signed sizeof static struct switch typedef union unsigned void volatile while
_Bool _Complex _Imaginary _Alignas _Alignof _Atomic _Generic _Noreturn
_Static_assert _Thread_local""".split())
RESERVED_NAMES = set("""alignas alignof and and_eq asm bitand bitor bool catch
class compl concept const_cast consteval constexpr constinit co_await co_return
co_yield decltype delete dynamic_cast explicit export false friend interrupt
mutable namespace new noexcept not not_eq nullptr operator or or_eq private
protected public reinterpret_cast requires static_assert static_cast template
this thread_local throw true try typeid typename using virtual wchar_t xor
xor_eq errno""".split()) | C_KEYWORDS
STANDARD_HEADERS = set("""assert complex ctype errno fenv float inttypes iso646
limits locale math setjmp signal stdalign stdarg stdatomic stdbool stddef stdint
stdio stdlib stdnoreturn string tgmath threads time uchar wchar wctype""".split())
STANDARD_FUNCTIONS = set("""abort abs atof atoi atol atoll bsearch calloc clearerr
clock fclose feof ferror fflush fgetc fgetpos fgets fopen fprintf fputc fputs
fread free freopen fscanf fseek fsetpos ftell fwrite getc getchar getenv gets
isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace
isupper isxdigit labs llabs longjmp malloc memchr memcmp memcpy memmove memset
perror printf putc putchar puts qsort rand realloc remove rename rewind scanf
setbuf setjmp setvbuf snprintf sprintf srand sscanf strcat strchr strcmp
strcoll strcpy strcspn strerror strlen strncat strncmp strncpy strpbrk strrchr
strspn strstr strtok strtol strtoll strtoul strtoull strxfrm time tmpfile tmpnam
tolower toupper ungetc vfprintf vprintf vsnprintf vsprintf""".split())
FORBIDDEN = {
    "auto": "1.7.a", "register": "1.7.b", "short": "5.2.b", "long": "5.2.b",
}
JUMPS = {"abort", "exit", "setjmp", "longjmp"}
ASSIGNMENTS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
OPERATORS = sorted({
    "...", ">>=", "<<=", "->", "++", "--", "<<", ">>", "<=", ">=", "==",
    "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##",
}, key=len, reverse=True)
TYPE_WORDS = {"void", "char", "int", "float", "double", "signed", "unsigned",
              "short", "long", "bool", "_Bool", "_Complex", "_Imaginary",
              "atomic_bool", "atomic_int", "atomic_uint", "atomic_flag"}
TYPE_QUALIFIERS = {"const", "volatile", "restrict", "static", "extern", "inline",
                   "register", "auto", "typedef", "_Atomic", "_Noreturn"}
KERNEL_TYPES = {"ID", "INT", "ER", "TMO", "SZ", "FP", "SYSTIM", "T_RSYS",
                "T_RTSK", "T_CTSK", "UINT", "UW", "VP", "UB", "PRI"}
C11_KEYWORDS = {"_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
                "_Static_assert", "_Thread_local"}
C11_HEADERS = {"stdalign.h", "stdatomic.h", "stdnoreturn.h", "threads.h", "uchar.h"}


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    start: int
    end: int
    line: int
    column: int
    directive: bool = False


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    column: int
    rule: str
    message: str
    symbol: str = ""


def lex(source: str) -> list[Token]:
    """Tokenize after C line splicing, retaining original source locations.

    Comments and literals remain individual tokens.  In particular, forbidden
    words inside them cannot become code findings, and escaped newlines cannot
    hide keywords or manufacture a second, inconsistent comment interpretation.
    Preprocessor lines are marked, but not expanded.
    """
    logical = []
    locations = []
    offset = 0
    line = 1
    column = 1
    while offset < len(source):
        width = 0
        if source.startswith("\\\r\n", offset):
            width = 3
        elif source.startswith("\\\n", offset):
            width = 2
        if width:
            offset += width
            line += 1
            column = 1
        else:
            character = source[offset]
            logical.append(character)
            locations.append((offset, line, column))
            offset += 1
            if character == "\n":
                line += 1
                column = 1
            else:
                column += 1
    text = "".join(logical)
    result = []
    index = 0
    directive = False
    beginning = True
    while index < len(text):
        character = text[index]
        if character.isspace():
            if character == "\n":
                directive = False
                beginning = True
            index += 1
            continue
        start = index
        kind = "punctuation"
        if character == "#" and beginning:
            directive = True
            kind = "directive_start"
        if text.startswith("//", index):
            kind = "comment"
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
        elif text.startswith("/*", index):
            kind = "comment"
            end = text.find("*/", index + 2)
            if end < 0:
                kind = "invalid"
                index = len(text)
            else:
                index = end + 2
        else:
            literal = re.match(r"(?:u8|[LuU])?([\"'])", text[index:])
            if literal:
                kind = "literal"
                quote = literal.group(1)
                index += literal.end()
                while index < len(text) and text[index] != quote:
                    if text[index] == "\\":
                        index += 2
                    elif text[index] == "\n":
                        kind = "invalid"
                        break
                    else:
                        index += 1
                if index >= len(text):
                    kind = "invalid"
                elif text[index] == quote:
                    index += 1
            elif character.isalpha() or character == "_":
                kind = "identifier"
                index += 1
                while index < len(text) and (text[index].isalnum() or text[index] == "_"):
                    index += 1
            elif character.isdigit() or (character == "." and index + 1 < len(text)
                                         and text[index + 1].isdigit()):
                kind = "number"
                index += 1
                while index < len(text):
                    current = text[index]
                    if current.isalnum() or current in "_.":
                        index += 1
                    elif current in "+-" and text[index - 1] in "eEpP":
                        index += 1
                    else:
                        break
            else:
                operator = next((op for op in OPERATORS if text.startswith(op, index)), None)
                index += len(operator) if operator else 1
        position, token_line, token_column = locations[start]
        end_position = locations[min(index, len(text)) - 1][0] + 1
        result.append(Token(kind, text[start:index], position, end_position,
                            token_line, token_column, directive))
        if kind != "comment":
            beginning = False
        elif "\n" in text[start:index]:
            beginning = True
            directive = False
    return result


def matching(tokens: list[Token]) -> dict[int, int]:
    result = {}
    stack = []
    close_to_open = {")": "(", "]": "[", "}": "{"}
    for index, token in enumerate(tokens):
        if token.value in {"(", "[", "{"}:
            stack.append(index)
        elif token.value in close_to_open:
            if stack and tokens[stack[-1]].value == close_to_open[token.value]:
                opening = stack.pop()
                result[opening] = index
                result[index] = opening
    return result


def declarations(tokens: list[Token], pairs: dict[int, int]):
    """Recognize ordinary C declarators without inventing a semantic type model.

    Returns explicit declarations only.  Pointer aliases from another module,
    macro-generated declarations and expressions' signedness require compiler
    analysis/review; this scanner intentionally does not guess about those.
    """
    depth = 0
    parentheses = 0
    emitted = set()
    for index, token in enumerate(tokens):
        previous = tokens[index - 1].value if index else None
        declaration_start = previous in {None, ";", "{", "}", "(", ","}
        if declaration_start:
            cursor = index
            types = []
            while cursor < len(tokens):
                word = tokens[cursor].value
                if word in TYPE_QUALIFIERS or word in TYPE_WORDS or word in KERNEL_TYPES or word.endswith("_t"):
                    types.append(word)
                    cursor += 1
                    if word == "_Atomic" and cursor < len(tokens) and tokens[cursor].value == "(" and cursor in pairs:
                        types.extend(entry.value for entry in tokens[cursor + 1:pairs[cursor]])
                        cursor = pairs[cursor] + 1
                elif word in {"struct", "union", "enum"}:
                    types.append(word)
                    cursor += 1
                    if cursor < len(tokens) and tokens[cursor].kind == "identifier":
                        types.append(tokens[cursor].value)
                        cursor += 1
                    if cursor < len(tokens) and tokens[cursor].value == "{" and cursor in pairs:
                        cursor = pairs[cursor] + 1
                else:
                    break
            # A storage qualifier alone does not establish a declaration type.
            typed = any(word not in TYPE_QUALIFIERS for word in types)
            if typed and "typedef" not in types:
                parameter = parentheses > 0 and not (previous == "(" and index > 1 and tokens[index - 2].value == "for")
                global_variable = depth == 0 and parentheses == 0
                while cursor < len(tokens):
                    pointer_depth = 0
                    indirect_function = False
                    while cursor < len(tokens) and tokens[cursor].value in {"*", "const", "volatile", "restrict"}:
                        pointer_depth += tokens[cursor].value == "*"
                        cursor += 1
                    if cursor + 2 < len(tokens) and tokens[cursor].value == "(" and tokens[cursor + 1].value == "*":
                        indirect_function = True
                        cursor += 1
                        while cursor < len(tokens) and tokens[cursor].value in {"*", "const", "volatile", "restrict"}:
                            pointer_depth += tokens[cursor].value == "*"
                            cursor += 1
                    if cursor >= len(tokens) or tokens[cursor].kind != "identifier" or tokens[cursor].value in RESERVED_NAMES:
                        # C++ reserved names are still valid C identifiers and
                        # must be reported, rather than skipped, below.
                        if cursor >= len(tokens) or tokens[cursor].kind != "identifier" or tokens[cursor].value in C_KEYWORDS:
                            break
                    name_index = cursor
                    name = tokens[cursor]
                    cursor += 1
                    if indirect_function:
                        if cursor >= len(tokens) or tokens[cursor].value != ")":
                            break
                        cursor += 1
                    function = cursor < len(tokens) and tokens[cursor].value == "(" and not indirect_function
                    array = cursor < len(tokens) and tokens[cursor].value == "["
                    if function:
                        break
                    if not function and name_index not in emitted:
                        emitted.add(name_index)
                        yield name, types, max(pointer_depth, int(array and parameter)), global_variable, None
                    # Skip declarator suffixes and initializer subexpressions.
                    while cursor < len(tokens) and tokens[cursor].value not in {";", ",", ")", "}"}:
                        if tokens[cursor].value in {"(", "[", "{"} and cursor in pairs:
                            cursor = pairs[cursor] + 1
                        else:
                            cursor += 1
                    if parameter or cursor >= len(tokens) or tokens[cursor].value != ",":
                        break
                    yield name, types, pointer_depth, global_variable, tokens[cursor]
                    cursor += 1
        if token.value == "{":
            depth += 1
        elif token.value == "}":
            depth -= 1
        elif token.value == "(":
            parentheses += 1
        elif token.value == ")":
            parentheses -= 1


def expression_operand(tokens: list[Token], pairs: dict[int, int],
                       operator: int, direction: int) -> list[Token]:
    """Find a logical/equality operand, respecting grouping and precedence."""
    stop = ASSIGNMENTS | {";", ",", "?", ":", "{", "}", "return", "case"}
    value = tokens[operator].value
    stop |= {"||"}
    if value in {"&&", "=="}:
        stop.add("&&")
    if value == "==":
        stop |= {"&", "^", "|", "==", "!="}
    cursor = operator + direction
    while 0 <= cursor < len(tokens):
        current = tokens[cursor].value
        if current in stop:
            break
        if direction < 0:
            if current in {")", "]"} and cursor in pairs:
                cursor = pairs[cursor] - 1
                continue
            if current in {"(", "["}:
                break
        else:
            if current in {"(", "["} and cursor in pairs:
                cursor = pairs[cursor] + 1
                continue
            if current in {")", "]"}:
                break
        cursor += direction
    return tokens[cursor + 1:operator] if direction < 0 else tokens[operator + 1:cursor]


def parenthesized(operand: list[Token]) -> bool:
    return bool(operand and operand[0].value == "(" and
                matching(operand).get(0) == len(operand) - 1)


def constant_operand(operand: list[Token]) -> bool:
    while parenthesized(operand):
        operand = operand[1:-1]
    if len(operand) == 2 and operand[0].value in {"+", "-"}:
        operand = operand[1:]
    if len(operand) != 1:
        return False
    token = operand[0]
    return (token.kind in {"number", "literal"}
            or token.value in {"true", "false", "NULL"}
            or bool(re.fullmatch(r"[A-Z][A-Z0-9_]*", token.value)))


def switch_breaks(source: str) -> list[tuple[Token, Token]]:
    """Pair each switch-owned break with its case, excluding loop breaks."""
    tokens = [token for token in lex(source) if token.kind not in {"comment", "invalid"}
              and not token.directive]
    pairs = matching(tokens)
    regions = []
    for index, token in enumerate(tokens):
        if token.value not in {"switch", "while", "for", "do"}:
            continue
        body = index + 1
        if token.value != "do":
            if body not in pairs or tokens[body].value != "(":
                continue
            body = pairs[body] + 1
        if body in pairs and tokens[body].value == "{":
            regions.append((body, pairs[body], token.value))
    result = []
    for index, token in enumerate(tokens):
        if token.value != "break":
            continue
        owners = [region for region in regions if region[0] < index < region[1]]
        if not owners:
            continue
        owner = max(owners)
        if owner[2] != "switch":
            continue
        label = None
        for position in range(owner[0] + 1, index):
            if tokens[position].value not in {"case", "default"}:
                continue
            switches = [region for region in regions if region[2] == "switch"
                        and region[0] < position < region[1]]
            if switches and max(switches) == owner:
                label = tokens[position]
        if label is not None:
            result.append((token, label))
    return result


def canonical_format(source: str) -> str:
    """Only align switch-owned break lines after clang-format has run.

    No expressions, declarations, braces, comments or other whitespace are
    rewritten here.  This fills clang-format 20's lack of case-break alignment.
    """
    edits = []
    for token, label in switch_breaks(source):
        beginning = source.rfind("\n", 0, token.start) + 1
        prefix = source[beginning:token.start]
        if not prefix.strip():
            edits.append((beginning, token.start, " " * (label.column - 1)))
    for beginning, end, replacement in reversed(edits):
        source = source[:beginning] + replacement + source[end:]
    return source


def check_source(path: str, source: str,
                 public_prefixes: list[str] | None = None,
                 atomic_allowed: bool = False) -> list[Finding]:
    findings = []
    public_prefixes = public_prefixes or [Path(path).stem + "_"]

    def add(rule: str, message: str, token: Token | None = None,
            line: int = 1, column: int = 1, symbol: str = "") -> None:
        findings.append(Finding(path, token.line if token else line,
                                token.column if token else column,
                                rule, message, symbol))

    filename = Path(path).name
    if not re.fullmatch(r"[a-z][a-z0-9_]*\.[ch]", filename):
        add("4.1.a", "Use a lowercase module filename.", symbol=filename)
    if Path(path).stem in STANDARD_HEADERS:
        add("4.1.c", "Module name overlaps a standard library header.", symbol=Path(path).stem)
    for number, line in enumerate(source.split("\n"), 1):
        if len(line) > 80:
            add("1.2.a", f"Line contains {len(line)} characters; maximum is 80.", line=number)
        if "\t" in line:
            add("3.5.a", "Replace tabs with spaces.", line=number, column=line.index("\t") + 1)
        for column, character in enumerate(line, 1):
            if (ord(character) < 32 and character not in "\t\f") or ord(character) == 127:
                add("3.6", "Use LF line endings and no forbidden control characters.",
                    line=number, column=column)
        if line.rstrip(" \t") != line:
            add("project.whitespace", "Remove trailing whitespace.", line=number)
    if not source.endswith("\n"):
        add("3.3.c", "Terminate the file with a newline.")

    all_tokens = lex(source)
    comments = [token for token in all_tokens if token.kind == "comment"]
    for token in all_tokens:
        if token.kind == "invalid":
            add("project.lexical", "Unterminated comment or literal.", token)
        if token.kind == "comment":
            body = token.value[2:-2] if token.value.startswith("/*") else token.value[2:]
            if any(part in body for part in ("/*", "//", "\\")):
                add("2.1.b", "Comment contains a nested comment token or backslash.", token)
        if token.kind == "identifier" and not token.directive:
            if token.value in FORBIDDEN:
                add(FORBIDDEN[token.value], f"Avoid the token '{token.value}'.", token, symbol=token.value)
            if token.value in JUMPS:
                add("8.5.b", f"Do not use {token.value}().", token, symbol=token.value)
            atomic = token.value == "_Atomic" or token.value.startswith(("atomic_", "ATOMIC_", "memory_order_"))
            if ((atomic and not atomic_allowed)
                    or (token.value in C11_KEYWORDS and not (atomic and atomic_allowed))):
                add("1.1.a", "C11 constructs are restricted to the documented atomic profile.", token, symbol=token.value)
    if not all_tokens or all_tokens[-1].kind != "comment" or not re.search(
            r"\bend\s+of\s+file\b", all_tokens[-1].value, re.IGNORECASE):
        add("3.3.c", "End the source with an end-of-file comment and a blank line.")
    elif not source[all_tokens[-1].end:].endswith("\n\n"):
        add("3.3.c", "Leave a blank line after the end-of-file comment.", all_tokens[-1])

    directives: list[list[Token]] = []
    for token in all_tokens:
        if token.kind == "comment":
            continue
        if token.directive:
            if token.kind == "directive_start":
                directives.append([])
            if directives:
                directives[-1].append(token)
    for directive in directives:
        if len(directive) < 2:
            continue
        if directive[0].column != 1:
            add("3.2.d", "Place preprocessor '#' at column one.", directive[0])
        action = directive[1].value
        if action == "define" and len(directive) >= 3:
            name = directive[2]
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name.value):
                add("6.1.f", "Macro names must be uppercase.", name, symbol=name.value)
            if name.value in C_KEYWORDS:
                add("1.1.d", "Do not redefine C keywords.", name, symbol=name.value)
            adjacent = (len(directive) >= 4 and not re.sub(
                r"\\\r?\n", "", source[name.end:directive[3].start]))
            if len(directive) >= 4 and directive[3].value == "(" and adjacent:
                add("6.3.a", "A function-like macro needs a specific documented deviation.",
                    name, symbol=name.value)
            for token in directive[3:]:
                if token.kind == "identifier" and token.value in FORBIDDEN:
                    add(FORBIDDEN[token.value], f"Avoid the token '{token.value}'.",
                        token, symbol=token.value)
                if token.kind == "identifier" and token.value in JUMPS:
                    add("8.5.b", f"Do not use {token.value}().", token, symbol=token.value)
                atomic = token.value == "_Atomic" or token.value.startswith(("atomic_", "ATOMIC_", "memory_order_"))
                if token.kind == "identifier" and ((atomic and not atomic_allowed)
                        or (token.value in C11_KEYWORDS and not (atomic and atomic_allowed))):
                    add("1.1.a", "C11 constructs are restricted to the documented atomic profile.", token, symbol=token.value)
        if action == "include" and len(directive) >= 3:
            include = "".join(token.value for token in directive[2:]).strip('<>"')
            if include in C11_HEADERS and not (include == "stdatomic.h" and atomic_allowed):
                add("1.1.a", "C11 headers are outside this module's language profile.", directive[2], symbol=include)
            if include.startswith(("/", "\\")) or re.match(r"[A-Za-z]:", include):
                add("4.3.d", "Include paths must be relative.", directive[2], symbol=include)
            if include.endswith(".c"):
                add("4.3.f", "Do not include a C source file.", directive[2], symbol=include)
    if path.endswith(".h"):
        guard_ok = (len(directives) >= 3 and len(directives[0]) == 3
                    and directives[0][1].value == "ifndef"
                    and len(directives[1]) == 3 and directives[1][1].value == "define"
                    and directives[0][2].value == directives[1][2].value
                    and directives[-1][1].value == "endif")
        if not guard_ok:
            add("4.2.b", "Wrap the header in matching #ifndef / #define / #endif guards.")

    tokens = [token for token in all_tokens if token.kind not in {"comment", "invalid"}
              and not token.directive]
    pairs = matching(tokens)
    for token, label in switch_breaks(source):
        if token.column != label.column:
            add("8.3.a", "Align a switch-owned break with its case label.", token)
    for index, token in enumerate(tokens):
        if token.value in {"&&", "||"}:
            for direction in (-1, 1):
                operand = expression_operand(tokens, pairs, index, direction)
                single = len(operand) == 1 and operand[0].kind in {"identifier", "number", "literal"}
                if operand and not single and not parenthesized(operand):
                    add("1.4.b", "Parenthesize each compound logical operand.", token)
        elif token.value == "==":
            left = expression_operand(tokens, pairs, index, -1)
            right = expression_operand(tokens, pairs, index, 1)
            if constant_operand(right) and not constant_operand(left):
                add("8.6.a", "Place the constant on the left of ==.", token)
    for name, types, pointer_depth, global_variable, comma in declarations(tokens, pairs):
        if comma is not None:
            add("8.1.a", "Declare one variable at a time.", comma, symbol=name.value)
            continue
        if path.endswith(".h") and global_variable and "extern" not in types:
            add("4.2.c.ii", "Do not allocate variable storage in a header.", name, symbol=name.value)
        name_position = next(position for position, token in enumerate(tokens) if token.start == name.start)
        prior = name_position - 1
        while prior >= 0 and tokens[prior].value in {"*", "const", "volatile", "restrict"}:
            prior -= 1
        if pointer_depth and prior >= 0 and tokens[prior].value == "(" and tokens[name_position - 1].value == "*":
            add("project.pointer-declarator", "Use a named function/array signature type and an explicit pointer declaration.", name, symbol=name.value)
        if name.value in RESERVED_NAMES:
            add("7.1.a", "Variable name is reserved by C or C++.", name, symbol=name.value)
        if name.value == "errno":
            add("7.1.b", "Do not shadow the standard errno object.", name, symbol=name.value)
        if name.value.startswith("_"):
            add("7.1.c", "Variable names must not start with an underscore.", name, symbol=name.value)
        if len(name.value) > 31:
            add("7.1.d", "Variable names must be at most 31 characters.", name, symbol=name.value)
        if len(name.value) < 3:
            add("7.1.e", "Variable names must have at least three characters.", name, symbol=name.value)
        if any(character.isupper() for character in name.value):
            add("7.1.f", "Variable names must be lowercase.", name, symbol=name.value)
        boolean = any(word in {"bool", "_Bool", "atomic_bool"} for word in types)
        handle = any(word == "ID" or word.endswith(("_id_t", "_handle_t")) for word in types)
        pointer_depth = max(pointer_depth, int("FP" in types))
        prefix = "g" if global_variable else ""
        prefix += "pp" if pointer_depth > 1 else ("p" if pointer_depth else "")
        prefix += "b" if boolean else ("h" if handle else "")
        accepted = [prefix + "_"]
        if prefix and not boolean and not handle:
            # Integer handles and Boolean semantics may be documented by name
            # even when the storage type is a plain or atomic integer.
            accepted += [prefix + "b_", prefix + "h_"]
        if prefix and not name.value.startswith(tuple(accepted)):
            rule = "7.1.o" if len(prefix) > 1 and prefix != "pp" else {
                "g": "7.1.j", "p": "7.1.k", "pp": "7.1.l", "b": "7.1.m", "h": "7.1.n",
            }[prefix]
            add(rule, f"This declaration requires the '{prefix}_' naming prefix.", name, symbol=name.value)
    controls = []
    for index, token in enumerate(tokens):
        value = token.value
        if value not in {"if", "for", "while", "switch", "else", "do"}:
            continue
        condition_start = None
        condition_end = None
        if value in {"if", "for", "while", "switch"}:
            if index + 1 >= len(tokens) or tokens[index + 1].value != "(":
                continue  # The C compiler diagnoses malformed syntax.
            condition_start = index + 1
            condition_end = pairs.get(condition_start)
            if condition_end is None:
                continue
            body = condition_end + 1
        else:
            body = index + 1
        # The final 'while' in a do/while belongs to the preceding do block.
        do_tail = (value == "while" and index > 0 and tokens[index - 1].value == "}"
                   and index - 1 in pairs and pairs[index - 1] > 0
                   and tokens[pairs[index - 1] - 1].value == "do")
        if body >= len(tokens):
            continue
        if value == "else" and tokens[body].value == "if":
            continue
        if not do_tail and tokens[body].value != "{":
            add("1.3.a", f"Enclose the {value} body in braces.", token)
        if condition_start is not None and condition_end is not None:
            condition = tokens[condition_start + 1:condition_end]
            if value == "for":
                separators = [position for position in range(condition_start + 1, condition_end)
                              if tokens[position].value == ";"]
                if len(separators) == 2:
                    condition = tokens[separators[0] + 1:separators[1]]
            if value in {"if", "for", "while"}:
                for assignment in condition:
                    if assignment.value in ASSIGNMENTS or (value != "for" and assignment.value in {"++", "--"}):
                        add("8.2.c" if value == "if" else "8.4.b",
                            "Do not assign or modify values in a controlling test.", assignment)
            if value == "while" and [entry.value for entry in condition] in (["1"], ["true"]):
                add("8.4.c", "Use for (;;) for an infinite loop.", token)
        if tokens[body].value == "{" and body in pairs:
            end = pairs[body]
            controls.append((index, value, body, end))
            if value in {"for", "while", "do"} and end == body + 1:
                if not any(tokens[body].end <= comment.start < tokens[end].start for comment in comments):
                    add("8.4.d", "Explain the intentionally empty loop with a comment inside its braces.", token)
            if value == "switch":
                depth = 0
                found = False
                for member in tokens[body + 1:end]:
                    if member.value == "{":
                        depth += 1
                    elif member.value == "}":
                        depth -= 1
                    elif member.value == "default" and depth == 0:
                        found = True
                if not found:
                    add("8.3.b", "Provide a default block in every switch.", token)

    for index, kind, body, end in controls:
        if kind != "if" or (index > 0 and tokens[index - 1].value == "else"):
            continue
        after = end + 1
        chained = False
        while after + 1 < len(tokens) and tokens[after].value == "else" and tokens[after + 1].value == "if":
            chained = True
            following = next((entry for entry in controls if entry[0] == after + 1), None)
            if following is None:
                break
            after = following[3] + 1
        if chained and (after >= len(tokens) or tokens[after].value != "else"):
            add("8.2.d", "Finish the else-if chain with an else block.", tokens[index])

    # Typedef declarators, including function-pointer typedefs.
    for index, token in enumerate(tokens):
        if token.value != "typedef":
            continue
        end = index + 1
        while end < len(tokens) and tokens[end].value != ";":
            if tokens[end].value in {"{", "(", "["} and end in pairs:
                end = pairs[end] + 1
            else:
                end += 1
        declaration = tokens[index + 1:end]
        aliases = [declaration[-1]] if declaration else []
        if not any(entry.value == "{" for entry in declaration):
            for position in range(len(declaration) - 3):
                if [entry.value for entry in declaration[position:position + 2]] == ["(", "*"]:
                    aliases = [declaration[position + 2]]
                    add("project.pointer-declarator", "Define the function signature first, then typedef its pointer if the ABI requires it.", aliases[0], symbol=aliases[0].value)
                    break
            else:
                for position, entry in enumerate(declaration[:-1]):
                    if entry.kind == "identifier" and declaration[position + 1].value == "(" and entry.value not in TYPE_WORDS:
                        aliases = [entry]
                        break
        for alias in aliases:
            if alias.kind == "identifier" and not re.fullmatch(r"[a-z][a-z0-9_]*_t", alias.value):
                add("5.1.a", "Name project types in lowercase with a _t suffix.", alias, symbol=alias.value)
            if alias.kind == "identifier" and path.endswith(".h") and not alias.value.startswith(tuple(public_prefixes)):
                add("5.1.c", "Prefix a public type with its logical module name.", alias, symbol=alias.value)

    # Direct file-scope function declarations/definitions.  Calls and indirect
    # function-pointer declarations are deliberately not inferred as functions.
    depth = 0
    declaration_start = 0
    for index, token in enumerate(tokens):
        if depth == 0 and token.kind == "identifier" and index + 1 < len(tokens) and tokens[index + 1].value == "(":
            close = pairs.get(index + 1)
            prefix = tokens[declaration_start:index]
            if close is not None and close + 1 < len(tokens) and tokens[close + 1].value in {";", "{"} and prefix:
                if not any(entry.value in {"=", "typedef"} for entry in prefix) and token.value not in C_KEYWORDS:
                    if token.value.startswith("_"):
                        add("6.1.c", "Function names must not start with an underscore.", token, symbol=token.value)
                    if len(token.value) > 31:
                        add("6.1.d", "Function names must be at most 31 characters.", token, symbol=token.value)
                    if any(character.isupper() for character in token.value):
                        add("6.1.e", "Function names must be lowercase.", token, symbol=token.value)
                    if token.value in RESERVED_NAMES:
                        add("6.1.a", "Function name is reserved by C or C++.", token, symbol=token.value)
                    if token.value in STANDARD_FUNCTIONS:
                        add("6.1.b", "Do not redefine a standard library function.", token, symbol=token.value)
                    if not any(entry.value == "static" for entry in prefix) and not token.value.startswith(tuple(public_prefixes)):
                        if token.value != "main" or not path.startswith("tests/"):
                            add("6.1.i", "Prefix a public function with its logical module name.", token, symbol=token.value)
                    if token.value == "main" and "main" not in Path(path).stem:
                        add("4.1.d", "A main() translation unit must have 'main' in its filename.", token, symbol="main")
        if token.value == "{":
            depth += 1
        elif token.value == "}":
            depth -= 1
            if depth == 0:
                declaration_start = index + 1
        elif token.value == ";" and depth == 0:
            declaration_start = index + 1
    return findings


def check_modules(sources: dict[str, str]) -> list[Finding]:
    """Check file relationships after the entire source tree is known."""
    findings = []
    modules: dict[str, list[str]] = {}
    for path in sources:
        modules.setdefault(Path(path).stem, []).append(path)
    prefixes: dict[str, set[str]] = {}
    for name in modules:
        prefixes.setdefault(name[:8], set()).add(name)
    for names in prefixes.values():
        if len(names) > 1:
            for name in sorted(names):
                findings.append(Finding(sorted(modules[name])[0], 1, 1, "4.1.b",
                                        "Module names must be unique in their first eight characters.", name))
    for path, source in sources.items():
        if not path.endswith(".c"):
            continue
        stem = Path(path).stem
        headers = [name for name in modules[stem] if name.endswith(".h")]
        if len(headers) != 1:
            findings.append(Finding(path, 1, 1, "4.2.a",
                                    "Provide exactly one paired header with the same module basename.", stem))
        tokens = lex(source)
        has_header = False
        for index, token in enumerate(tokens):
            if token.directive and token.value == "include" and index + 1 < len(tokens):
                included = tokens[index + 1]
                if included.kind == "literal":
                    has_header |= Path(included.value.strip('"').replace("\\", "/")).name == stem + ".h"
                elif included.value == "<":
                    end = index + 2
                    name = ""
                    while end < len(tokens) and tokens[end].value != ">":
                        name += tokens[end].value
                        end += 1
                    has_header |= Path(name).name == stem + ".h"
        if not has_header:
            findings.append(Finding(path, 1, 1, "4.3.c",
                                    "Include the module's own paired header.", stem))
    return findings


def source_files(root: Path) -> list[Path]:
    """Discover new files too, without relying on Git's staging state."""
    result = []
    for directory, subdirs, files in os.walk(root, followlinks=False):
        subdirs[:] = sorted(name for name in subdirs
                           if name not in EXCLUDED_DIRS and not name.startswith("build-")
                           and not (Path(directory) / name).is_symlink())
        for filename in sorted(files):
            path = Path(directory) / filename
            if path.suffix.lower() in {".c", ".h"} and not path.is_symlink():
                result.append(path)
    return result


def apply_deviations(findings: list[Finding], entries: list[dict], root: Path,
                     sources: dict[str, str]) -> tuple[list[Finding], list[str]]:
    """Accept only named, exact-source exceptions; reject stale exemptions."""
    errors = []
    ids = set()
    valid = []
    for entry in entries:
        required = {"id", "path", "rule", "reason", "authority"}
        if not isinstance(entry, dict) or not required <= entry.keys():
            errors.append("Each deviation requires id, path, rule, reason and authority.")
            continue
        if not all(isinstance(entry[key], str) and entry[key].strip() for key in required):
            errors.append("Deviation metadata must contain nonempty strings.")
            continue
        identity = entry["id"]
        if identity in ids:
            errors.append(f"Duplicate deviation id: {identity}")
        ids.add(identity)
        path = entry["path"]
        if path not in sources or any(character in path for character in "*?[]"):
            errors.append(f"{identity}: deviation must name an exact checked source path.")
            continue
        if not entry.get("symbol") and not entry.get("source_line"):
            errors.append(f"{identity}: require an exact symbol or source_line anchor.")
            continue
        valid.append(entry)
    suppressed = Counter()
    remaining = []
    for finding in findings:
        matched = False
        for entry in valid:
            if entry["path"] != finding.path or entry["rule"] != finding.rule:
                continue
            if entry.get("symbol") and entry["symbol"] != finding.symbol:
                continue
            if entry.get("source_line"):
                lines = sources[finding.path].splitlines()
                if not 0 < finding.line <= len(lines) or lines[finding.line - 1] != entry["source_line"]:
                    continue
            matched = True
            suppressed[entry["id"]] += 1
        if not matched:
            remaining.append(finding)
    for entry in valid:
        if not suppressed[entry["id"]]:
            errors.append(f"{entry['id']}: stale deviation matches no current finding.")
    return remaining, errors


def check_format(executable: str, paths: list[Path], version: str) -> list[str]:
    errors = []
    try:
        result = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exception:
        return [f"Cannot run clang-format: {exception}"]
    if not re.search(r"\bversion\s+" + re.escape(version) + r"\b", result.stdout):
        return [f"Expected clang-format {version}; found {result.stdout.strip()}."]
    for path in paths:
        result = subprocess.run([executable, "--style=file", "--fallback-style=none", str(path)],
                                capture_output=True)
        if result.returncode:
            errors.append(f"{path}: clang-format failed: {result.stderr.decode(errors='replace').strip()}")
        elif canonical_format(result.stdout.decode("utf-8")).encode("utf-8") != path.read_bytes():
            errors.append(f"{path}: formatting differs; run tools/c_standard/format.py.")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--format", "--clang-format", dest="clang_format",
                        default=os.environ.get("BARR_CLANG_FORMAT", "clang-format"))
    parser.add_argument("--skip-format", action="store_true", help="Diagnostic use only; does not run the full gate.")
    parser.add_argument("--json", action="store_true", help="Write findings as JSON.")
    arguments = parser.parse_args(argv)
    root = arguments.root.resolve()
    manifest = root / "tools/c_standard/deviations.json"
    policy_path = root / "tools/c_standard/policy.json"
    errors = []
    try:
        policy = json.loads(policy_path.read_text(encoding="utf-8"))
        deviations = json.loads(manifest.read_text(encoding="utf-8"))
        if policy.get("schema_version") != 1 or deviations.get("schema_version") != 1:
            raise ValueError("Unsupported C standard policy/manifest version.")
        entries = deviations["deviations"]
        if not isinstance(entries, list):
            raise ValueError("deviations must be a list.")
        if not isinstance(policy.get("clang_format_version"), str):
            raise ValueError("clang_format_version must be a version string.")
        atomic_paths = policy.get("atomic_profile_paths", [])
        if (not isinstance(atomic_paths, list)
                or any(not isinstance(path, str) or not path.endswith(".c")
                       or any(character in path for character in "*?[]")
                       for path in atomic_paths)):
            raise ValueError("Atomic profile entries must be exact C source paths.")
        for path, prefixes in policy.get("module_prefixes", {}).items():
            if (not isinstance(path, str) or not isinstance(prefixes, list) or not prefixes
                    or not all(isinstance(prefix, str) and re.fullmatch(r"[a-z][a-z0-9_]*_", prefix)
                               for prefix in prefixes)):
                raise ValueError("Module prefixes must be nonempty lists of module_name_ strings.")
    except (OSError, ValueError, KeyError, AttributeError) as exception:
        print(f"C standard policy could not be read: {exception}", file=sys.stderr)
        return 2
    paths = source_files(root)
    if not paths:
        errors.append("No first-party C source files were found.")
    sources = {}
    findings = []
    for path in paths:
        relative = path.relative_to(root).as_posix()
        try:
            source = path.read_bytes().decode("utf-8")
        except (OSError, UnicodeDecodeError) as exception:
            errors.append(f"{relative}: cannot read UTF-8 source: {exception}")
            continue
        sources[relative] = source
        findings.extend(check_source(relative, source, policy.get("module_prefixes", {}).get(relative),
                                     relative in policy.get("atomic_profile_paths", [])))
    findings.extend(check_modules(sources))
    findings, exception_errors = apply_deviations(findings, entries, root, sources)
    errors.extend(exception_errors)
    if not arguments.skip_format:
        if not (root / ".clang-format").is_file():
            errors.append("The project .clang-format file is missing.")
        else:
            errors.extend(check_format(arguments.clang_format, paths, policy["clang_format_version"]))
    if arguments.json:
        print(json.dumps({"files_checked": len(paths), "full_gate": not arguments.skip_format,
                          "findings": [finding.__dict__ for finding in findings], "errors": errors}, indent=2))
    else:
        for finding in findings:
            print(f"{finding.path}:{finding.line}:{finding.column}: {finding.rule}: {finding.message}")
        for error in errors:
            print(f"error: {error}")
        label = "lexical diagnostic pass" if arguments.skip_format else "C standard gate"
        print(f"{label}: {len(paths)} files, {len(findings)} findings, {len(errors)} tool/policy errors.")
    return 1 if findings or errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
