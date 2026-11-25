"""Parse MinGW SDK headers (C mode COM definitions) for the Monac COM shim."""
import re
import sys

SEED_D2D = [
    "IUnknown",
    "ID2D1Factory",
    "ID2D1Resource",
    "ID2D1RenderTarget",
    "ID2D1HwndRenderTarget",
    "ID2D1DCRenderTarget",
    "ID2D1Brush",
    "ID2D1SolidColorBrush",
    "ID2D1LinearGradientBrush",
    "ID2D1RadialGradientBrush",
    "ID2D1BitmapBrush",
    "ID2D1Image",
    "ID2D1Bitmap",
    "ID2D1GradientStopCollection",
    "ID2D1StrokeStyle",
    "ID2D1Geometry",
    "ID2D1PathGeometry",
    "ID2D1GeometrySink",
]

SEED_DWRITE = [
    "IUnknown",
    "IDWriteFactory",
    "IDWriteTextFormat",
    "IDWriteTextLayout",
    "IDWriteInlineObject",
    "IDWritePixelSnapping",
    "IDWriteTextRenderer",
    "IDWriteFontCollection",
]


def strip_comments(text):
    """Remove /* */ and // comments while preserving newlines."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def find_vtbl_blocks(text):
    """Return {interface: body_text} for every `typedef struct <I>Vtbl { ... } <I>Vtbl;`."""
    blocks = {}
    for m in re.finditer(r"typedef\s+struct\s+(\w+?)Vtbl\s*\{", text):
        name = m.group(1)
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        blocks[name] = text[start : i - 1]
    return blocks


RE_STDMETHOD_ = re.compile(
    r"STDMETHOD_?\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)\s*\((.*)\)\s*PURE\s*$", re.S
)
RE_PTRMETHOD = re.compile(
    r"^(.*?)\(\s*STDMETHODCALLTYPE\s*\*\s*(\w+)\s*\)\s*\((.*)\)\s*$", re.S
)


def split_decls(body):
    """Split a vtable body into top-level `;`-terminated declarations."""
    parts = []
    depth = 0
    cur = []
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == ";" and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


RE_IFACE_MARKER = re.compile(r"^\s*(BEGIN_INTERFACE|END_INTERFACE)\b\s*")


def parse_vtbl(interface, body, warn):
    """Return (base_name_or_None, [(ret, name, args, is_static), ...])."""
    base = None
    methods = []
    for decl in split_decls(body):
        d = " ".join(decl.split())
        while True:
            stripped = RE_IFACE_MARKER.sub("", d)
            if stripped == d:
                break
            d = stripped
        if not d:
            continue
        mb = re.fullmatch(r"(\w+)Vtbl\s+Base", d)
        if mb:
            base = mb.group(1)
            continue
        m = RE_STDMETHOD_.match(d)
        if m:
            ret, name, args = m.group(1), m.group(2), m.group(3)
            if name is None:
                name, ret = ret, "HRESULT"
            methods.append((ret, name, args, False))
            continue
        m = RE_PTRMETHOD.match(d)
        if m:
            ret, name, args = m.group(1).strip(), m.group(2), m.group(3)
            methods.append((ret, name, args, False))
            continue
        warn("unparsed declaration in %sVtbl: %r" % (interface, d[:120]))
    return base, methods


def load_header(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    return strip_comments(raw)


def collect_interface(blocks, name, warn, seen=None):
    """Resolve the flat method list of `name` (base chain first)."""
    if seen is None:
        seen = set()
    if name in seen:
        return []
    seen.add(name)
    if name == "IUnknown":
        return [
            ("HRESULT", "QueryInterface", "void *This, REFIID riid, void **ppvObject", False),
            ("ULONG", "AddRef", "void *This", False),
            ("ULONG", "Release", "void *This", False),
        ]
    body = blocks.get(name)
    if body is None:
        raise KeyError("no vtable block for %s" % name)
    base, methods = parse_vtbl(name, body, warn)
    out = []
    if base:
        out.extend(collect_interface(blocks, base, warn, seen))
    out.extend(methods)
    return out


if __name__ == "__main__":
    def _warn(msg):
        print("WARN: " + msg, file=sys.stderr)

    blocks = {}
    for path in sys.argv[2:]:
        blocks.update(find_vtbl_blocks(load_header(path)))
    tbl = collect_interface(blocks, sys.argv[1], _warn)
    print("%s (%d)" % (sys.argv[1], len(tbl)))
    for ret, name, args, _ in tbl:
        print("    %-34s %s" % (name, ret))
