#!/usr/bin/env python3
"""Reject `= {}` resets of QUuid values; they do not compile with Windows Qt.

On Windows, Qt declares QUuid(const GUID &) and operator=(const GUID &) next to
QUuid's own assignment. GUID is an aggregate, so `id = {}` converts equally
well to QUuid and to GUID and clang-cl/MSVC reject the assignment as ambiguous.
Linux and macOS Qt lack the GUID overloads, so the same line builds there and
fails only deep in a Windows build. Write `id = QUuid()`.

Declarations such as `QUuid id = {}` and default arguments are
copy-initialization, not assignment, and stay valid.

Types are resolved from source text, not by a compiler: struct/class fields,
the nearest earlier declaration of a name in the same file, and element access
into containers of QUuid. When the owner of a field is typed only through
`auto`, every same-named field of a struct visible through the file's quoted
includes is considered, and the reset is reported if any of them is a QUuid.
"""
import argparse
import re
import sys
from pathlib import Path

SUFFIXES = {'.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inc', '.mm'}
IDENT = re.compile(r'[A-Za-z_]\w*')
KEYWORDS = {'return', 'else', 'do', 'case', 'delete', 'new', 'throw', 'goto',
            'co_return', 'co_yield', 'co_await', 'sizeof', 'typename', 'not',
            'and', 'or', 'operator', 'default', 'public', 'private', 'protected',
            'this', 'if', 'while', 'for', 'switch'}
SPECIFIERS = {'static', 'mutable', 'inline', 'constexpr', 'constinit', 'const',
              'volatile', 'thread_local', 'extern', 'friend', 'virtual', 'explicit'}
SEQUENCES = {'std::vector', 'std::array', 'std::deque', 'QList', 'QVector', 'std::span'}
MAPS = {'std::map', 'std::unordered_map', 'QMap', 'QHash'}
POINTERS = {'std::shared_ptr', 'std::unique_ptr', 'std::optional', 'QPointer',
            'QSharedPointer', 'std::weak_ptr'}
RESET = re.compile(r'(?<![=!<>+\-*/%&|^])=\s*\{\s*\}')
UNKNOWN = None


def strip_code(text):
    """Blank comments and literal contents, keeping offsets and line breaks."""
    out = list(text)
    i, n = 0, len(text)

    def blank(start, end):
        for k in range(start, end):
            if out[k] != '\n':
                out[k] = ' '

    while i < n:
        c = text[i]
        word_before = i and (text[i - 1].isalnum() or text[i - 1] == '_')
        if text.startswith('//', i):
            end = text.find('\n', i)
            end = n if end < 0 else end
            blank(i, end)
            i = end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            blank(i, end)
            i = end
        elif text.startswith('R"', i) and not word_before:
            open_paren = text.find('(', i + 2)
            delimiter = text[i + 2:open_paren]
            end = text.find(')' + delimiter + '"', open_paren)
            end = n if end < 0 else end + len(delimiter) + 2
            blank(i + 1, end)
            i = end
        elif c == '"' or (c == "'" and not word_before):
            k = i + 1
            while k < n and text[k] != c and text[k] != '\n':
                k += 2 if text[k] == '\\' else 1
            blank(i + 1, min(k, n))
            i = k + 1
        else:
            i += 1
    return ''.join(out)


def closing(text, start, opening, close):
    """Index after the bracket matching text[start], or len(text)."""
    depth = 0
    for k in range(start, len(text)):
        if text[k] == opening:
            depth += 1
        elif text[k] == close:
            depth -= 1
            if depth == 0:
                return k + 1
    return len(text)


def opening(text, close_index, opening_char, closing_char):
    """Index of the bracket matching text[close_index]."""
    depth = 0
    for k in range(close_index, -1, -1):
        if text[k] == closing_char:
            depth += 1
        elif text[k] == opening_char:
            depth -= 1
            if depth == 0:
                return k
    return 0


def split_top_level(text):
    """Split `A, B<C, D>` at commas outside brackets."""
    parts, depth, current = [], 0, ''
    for c in text:
        if c in '<([{':
            depth += 1
        elif c in '>)]}':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += c
    parts.append(current.strip())
    return parts


def read_type(text, i):
    """Parse `[::]name[<...>](::name[<...>])*` at i; return (type, end) or None."""
    start = i
    if text.startswith('::', i):
        i += 2
    while True:
        match = IDENT.match(text, i)
        if not match:
            return None
        i = match.end()
        while i < len(text) and text[i] in ' \t':
            i += 1
        if i < len(text) and text[i] == '<':
            i = closing(text, i, '<', '>')
        if text.startswith('::', i):
            i += 2
            continue
        return text[start:i], i


class Types:
    def __init__(self, aliases):
        self.aliases = aliases

    def normalize(self, type_text):
        if isinstance(type_text, tuple):
            return type_text
        type_text = re.sub(r'\b(?:const|volatile|struct|class)\b', ' ', type_text)
        type_text = re.sub(r'\s+', '', type_text).rstrip('&*').removeprefix('::')
        return 'QUuid' if type_text in self.aliases else type_text

    def element(self, type_text):
        """Type yielded by operator[] on `type_text`."""
        if isinstance(type_text, tuple):
            return type_text[1]
        match = re.fullmatch(r'([\w:]+)<(.*)>', self.normalize(type_text))
        if not match:
            return UNKNOWN
        arguments = split_top_level(match.group(2))
        if match.group(1) in SEQUENCES:
            return arguments[0]
        if match.group(1) in MAPS and len(arguments) >= 2:
            return arguments[1]
        return UNKNOWN

    def record(self, type_text):
        """Struct name whose fields `.`/`->` on `type_text` reach."""
        type_text = self.normalize(type_text)
        while True:
            match = re.fullmatch(r'([\w:]+)<(.*)>', type_text)
            if not match or match.group(1) not in POINTERS:
                break
            type_text = self.normalize(split_top_level(match.group(2))[0])
        if not re.fullmatch(r'[\w:]+', type_text):
            return UNKNOWN
        return type_text.rsplit('::', 1)[-1]


def declarators(text):
    """Names declared by `a = x, b{y}, c[3]`, excluding function declarations."""
    names = []
    for part in split_top_level(text):
        match = re.match(r'[*&\s]*([A-Za-z_]\w*)\s*(\(|\[|=|\{|:|$)', part)
        if match and match.group(2) != '(' and match.group(1) not in KEYWORDS:
            names.append((match.group(1), match.group(2) == '['))
    return names


def field_declaration(statement):
    """(type, [(name, is_array)]) for a member declaration statement, else None."""
    statement = re.sub(r'^\s*(?:(?:public|private|protected)\s*:)+', '', statement).strip()
    statement = re.sub(r'\[\[[^\]]*\]\]', '', statement).strip()
    while True:
        word = IDENT.match(statement)
        if not word or word.group(0) not in SPECIFIERS:
            break
        statement = statement[word.end():].lstrip()
    if not statement or re.match(r'(?:using|typedef|enum|template|struct|class|union|friend)\b|~', statement):
        return None
    parsed = read_type(statement, 0)
    if not parsed or parsed[0] in KEYWORDS:
        return None
    type_text, end = parsed
    pointer = re.match(r'[\s*&]*', statement[end:]).group(0)
    names = declarators(statement[end + len(pointer):])
    return (type_text + pointer.strip(), names) if names else None


def struct_fields(code):
    """{struct name: {field: type}} for every struct/class body in `code`."""
    structs = {}
    for match in re.finditer(r'(?<!enum )\b(?:struct|class)\s+(?:alignas\([^)]*\)\s*)?([A-Za-z_]\w*)\b[^;{()=]*\{', code):
        fields = structs.setdefault(match.group(1), {})
        body_start = match.end() - 1
        body_end = closing(code, body_start, '{', '}') - 1
        statement, depth = '', 0
        for k in range(body_start + 1, body_end):
            c = code[k]
            if c == '{':
                if depth == 0:
                    statement += '{}'
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0 and not re.match(r'\s*[;,=)]', code[k + 1:k + 8]):
                    # A member function body ends its statement without a semicolon.
                    statement = ''
            elif depth == 0 and c == ';':
                declaration = field_declaration(statement)
                if declaration:
                    type_text, names = declaration
                    for name, is_array in names:
                        fields[name] = ('array', type_text) if is_array else type_text
                statement = ''
            elif depth == 0:
                statement += c
    return structs


DECLARED_TYPE = re.compile(
    r'(?:(?<=[\s(,;{}<])|^)((?:::)?[A-Za-z_][\w:]*\s*(?:<[^;{}()]*>)?)[\s*&]*$')


CHAIN = re.compile(r'[A-Za-z_]\w*(?:\s*(?:\.|->|::)\s*[A-Za-z_]\w*|\[[^\[\]]*\]|\([^()]*\))*')


def local_declaration(code, name, before):
    """Nearest declaration of `name` before offset `before`.

    Returns ('type', type), ('auto', (initializer, offset)) or None.
    """
    found = None
    for match in re.finditer(r'\b' + re.escape(name) + r'\b(?=\s*[=;,){\[:])', code[:before]):
        prefix = code[max(0, match.start() - 200):match.start()]
        declaration = DECLARED_TYPE.search(prefix)
        if not declaration:
            continue
        type_text = re.sub(r'^const\b\s*', '', declaration.group(1).strip())
        head = IDENT.match(type_text.removeprefix('::'))
        if not head or head.group(0) in KEYWORDS or head.group(0) in SPECIFIERS:
            continue
        rest = code[match.end():match.end() + 300]
        if type_text == 'auto':
            initializer = re.match(r'\s*=\s*(?:std::move\()?\s*', rest)
            chain = CHAIN.match(rest, initializer.end()) if initializer else None
            found = ('auto', (chain.group(0), match.end()) if chain else None)
        elif rest.lstrip().startswith('['):
            found = ('type', ('array', type_text))
        else:
            found = ('type', type_text)
    return found


def lvalue_before(code, end):
    """(start, stop) of the assigned expression ending before offset `end`."""
    k = end
    while k > 0 and code[k - 1] in ' \t\n':
        k -= 1
    stop = k
    while k > 0:
        c = code[k - 1]
        if c == ']':
            k = opening(code, k - 1, '[', ']')
        elif c == ')':
            k = opening(code, k - 1, '(', ')')
            if not (k > 0 and (code[k - 1].isalnum() or code[k - 1] == '_')):
                return None
        elif c.isalnum() or c == '_':
            while k > 0 and (code[k - 1].isalnum() or code[k - 1] == '_'):
                k -= 1
            j = k
            while j > 0 and code[j - 1] in ' \t\n':
                j -= 1
            if code[j - 2:j] in ('->', '::'):
                k = j - 2
            elif j > 0 and code[j - 1] == '.':
                k = j - 1
            else:
                return k, stop
            while k > 0 and code[k - 1] in ' \t\n':
                k -= 1
        else:
            return None
    return k, stop


def is_initializer(code, start):
    """True when a type precedes the expression, i.e. this declares it."""
    k = start
    while k > 0 and code[k - 1] in ' \t\n':
        k -= 1
    if k == 0:
        return False
    if code[k - 1] == '>':
        return code[k - 2:k] != '->'
    if code[k - 1] in '*&':
        return code[k - 2:k] != '&&'
    if code[k - 1].isalnum() or code[k - 1] == '_':
        return re.search(r'[A-Za-z_]\w*$', code[:k]).group(0) not in KEYWORDS
    return False


def steps(expression):
    """Split `a.b[0]->c().d` into ('a', [('.', 'b'), ('[]',), ...])."""
    base = IDENT.match(expression)
    if not base:
        return None
    result, i = [], base.end()
    while i < len(expression):
        if expression[i] == '[':
            i = closing(expression, i, '[', ']')
            result.append(('[]',))
        elif expression[i] == '(':
            i = closing(expression, i, '(', ')')
            result.append(('()',))
        else:
            match = re.match(r'(\.|->|::)([A-Za-z_]\w*)', expression[i:])
            if not match:
                return None
            result.append(('.', match.group(2)))
            i += match.end()
    return base.group(0), result


class Checker:
    def __init__(self, sources):
        self.sources = sources
        aliases = {'QUuid'}
        for code in sources.values():
            aliases.update(re.findall(r'\busing\s+([A-Za-z_]\w*)\s*=\s*(?:::)?QUuid\s*;', code))
            aliases.update(re.findall(r'\btypedef\s+(?:::)?QUuid\s+([A-Za-z_]\w*)\s*;', code))
        self.types = Types(aliases)
        self.structs = {path: struct_fields(code) for path, code in sources.items()}
        self.by_name = {}
        for path in sources:
            self.by_name.setdefault(path.name, []).append(path)
        self.includes = {path: self.quoted_includes(path) for path in sources}
        self.visible_cache = {}

    def quoted_includes(self, path):
        found = []
        raw = path.read_text(encoding='utf-8', errors='replace')
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', raw, re.M):
            target = (path.parent / include).resolve()
            if target in self.sources:
                found.append(target)
                continue
            found.extend(candidate for candidate in self.by_name.get(Path(include).name, [])
                         if candidate.as_posix().endswith(include.lstrip('./')))
        return found

    def closure(self, roots):
        seen, pending = set(), list(roots)
        while pending:
            current = pending.pop()
            if current not in seen:
                seen.add(current)
                pending.extend(self.includes.get(current, []))
        return seen

    def visible(self, path):
        """Files whose structs `path` can name, nearest first.

        A header or .inc sees its own includes; failing that, whatever the
        files that include it see; failing that, every scanned file.
        """
        if path not in self.visible_cache:
            includers = {path}
            while True:
                more = {f for f, targets in self.includes.items()
                        if includers.intersection(targets)} - includers
                if not more:
                    break
                includers |= more
            self.visible_cache[path] = (self.closure([path]), self.closure(includers),
                                        set(self.sources))
        return self.visible_cache[path]

    def field_types(self, path, owner, name):
        """Possible types of `owner.name`; owner UNKNOWN means any struct."""
        for files in self.visible(path):
            found = {fields[name] for visible in files
                     for struct, fields in self.structs[visible].items()
                     if (owner is UNKNOWN or struct == owner) and name in fields}
            if found:
                return found
        if owner is not UNKNOWN:
            return self.field_types(path, UNKNOWN, name)
        return {UNKNOWN}

    def resolve(self, path, expression, offset, depth=0):
        parsed = steps(expression)
        if not parsed or depth > 4:
            return {UNKNOWN}
        base, chain = parsed
        declaration = local_declaration(self.sources[path], base, offset)
        if chain and chain[0] == ('()',):
            current = {UNKNOWN}
        elif declaration is None:
            # Not declared earlier in this file: a field of the enclosing class.
            current = self.field_types(path, UNKNOWN, base)
        elif declaration[0] == 'auto':
            current = (self.resolve(path, *declaration[1], depth + 1)
                       if declaration[1] else {UNKNOWN})
        else:
            current = {declaration[1]}
        for index, step in enumerate(chain):
            if step == ('()',):
                continue
            if step == ('[]',):
                current = {UNKNOWN if t is UNKNOWN else self.types.element(t) for t in current}
                continue
            if index + 1 < len(chain) and chain[index + 1] == ('()',):
                if step[1] in ('front', 'back', 'at'):
                    current = {UNKNOWN if t is UNKNOWN else self.types.element(t) for t in current}
                else:
                    # Other method results are not resolved.
                    current = {UNKNOWN}
                continue
            resolved = set()
            for owner in current:
                record = (UNKNOWN if owner is UNKNOWN or isinstance(owner, tuple)
                          else self.types.record(owner))
                resolved |= self.field_types(path, record, step[1])
            current = resolved
        return {UNKNOWN if t is UNKNOWN else self.types.normalize(t) for t in current}

    def findings(self):
        for path, code in self.sources.items():
            for match in RESET.finditer(code):
                found = lvalue_before(code, match.start())
                if not found:
                    continue
                start, stop = found
                if start == stop or is_initializer(code, start):
                    continue
                expression = re.sub(r'\s+', '', code[start:stop])
                # Reported even when the name is ambiguous: the suggested
                # `QUuid()` fails to compile if the value was something else.
                if 'QUuid' in self.resolve(path, expression, start):
                    yield path, code.count('\n', 0, start) + 1, expression


def source_files(roots):
    for root in roots:
        if root.is_file():
            yield root.resolve()
            continue
        for path in sorted(root.rglob('*')):
            if path.suffix in SUFFIXES and path.is_file() and 'third_party' not in path.parts:
                yield path.resolve()


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('roots', nargs='*', type=Path, default=[repo / 'native'],
                        help='source files or directories (default: native/)')
    args = parser.parse_args()
    sources = {}
    for path in source_files(args.roots):
        sources[path] = strip_code(path.read_text(encoding='utf-8', errors='replace'))
    findings = sorted(Checker(sources).findings())
    for path, line, expression in findings:
        print(f'{path}:{line}: `{expression} = {{}}` is ambiguous with Windows Qt '
              f'(QUuid vs GUID); write `{expression} = QUuid()`')
    if findings:
        return 1
    print(f'Windows QUuid reset check passed: {len(sources)} files, '
          'no `= {}` assignments to QUuid values.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
