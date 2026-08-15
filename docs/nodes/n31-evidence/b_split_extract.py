#!/usr/bin/env python3
"""N31 subagent B evidence tool: static register_one extraction + D4 split.

Subcommands:
  extract <handlers.cpp> <canonical.json>
      Extract every register_one CALL (not the definition) in source order and
      emit a canonical JSON document (name/scope/local_only/description/schema
      literals, verbatim). Prints "<sha256> <op_count>". The canonical bytes are
      the D5 equivalence fingerprint: byte-identical documents before/after the
      D4 decomposition prove the registration sequence (names, scopes,
      local_only flags, descriptions, schemas, order) is unchanged.

  split <handlers.cpp> <output.cpp>
      Perform the D4 decomposition: split register_builtin_ops() into
      per-domain registration units inside the same anonymous namespace, moving
      every register_one call and its attached comments/helpers verbatim and
      preserving the exact registration order. Refuses to write output unless
      the extract fingerprint of the output equals the fingerprint of the input.

Usage: python b_split_extract.py extract|split ...
"""

import hashlib
import json
import re
import sys

# ---------------------------------------------------------------------------
# Scanner: walks C++ source skipping strings/char literals/comments, so the
# JSON inside raw-string schemas (braces, quotes, commas) cannot confuse the
# argument splitting.
# ---------------------------------------------------------------------------

class ScanError(Exception):
    pass


def skip_string(text, i):
    """text[i] opens a normal "..." or '...' literal (after optional raw
    prefix handled by caller). Returns index just past the closing quote."""
    quote = text[i]
    i += 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\":
            i += 2
            continue
        if c == quote:
            return i + 1
        if c == "\n":
            raise ScanError("unterminated string literal")
        i += 1
    raise ScanError("unterminated string literal")


def find_raw_string_end(text, i):
    """text[i:i+2] == 'R"'. Returns index just past the closing delimiter."""
    j = text.find("(", i + 2)
    if j == -1:
        raise ScanError("raw string opening paren not found")
    delim = text[i + 2:j]
    closer = ")" + delim + '"'
    k = text.find(closer, j + 1)
    if k == -1:
        raise ScanError("unterminated raw string literal")
    return k + len(closer)


def skip_line_comment(text, i):
    j = text.find("\n", i)
    return len(text) if j == -1 else j  # leave the newline for the caller


def skip_block_comment(text, i):
    j = text.find("*/", i + 2)
    if j == -1:
        raise ScanError("unterminated block comment")
    return j + 2


def next_code(text, i):
    """Advance past whitespace and comments; return (index, char)."""
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            i = skip_line_comment(text, i)
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i = skip_block_comment(text, i)
        else:
            return i, c
    return i, ""


def find_calls(text):
    """Yield (start, end, args) for every register_one(...) call.
    start = offset of 'register_one', end = offset past the terminating ';'.
    args = list of verbatim top-level argument texts."""
    calls = []
    n = len(text)
    ident_re = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    i = 0
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i = skip_line_comment(text, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i = skip_block_comment(text, i)
            continue
        if c in "\"'":
            i = skip_string(text, i)
            continue
        if c == "R" and text[i:i + 2] == 'R"':
            i = find_raw_string_end(text, i)
            continue
        if c.isalpha() or c == "_":
            m = ident_re.match(text, i)
            ident = m.group(0)
            if ident == "register_one":
                j, nc = next_code(text, m.end())
                if nc == "(":
                    # Exclude the definition: 'void register_one('
                    k, _ = prev_code(text, i)
                    prev_txt = text[k:i].strip()
                    if prev_txt.endswith("void"):
                        i = m.end()
                        continue
                    end, args = parse_call(text, j)
                    calls.append((i, end, args))
                    i = end
                    continue
            i = m.end()
            continue
        i += 1
    return calls


def prev_code(text, i):
    """Index of the previous non-whitespace char before i (comments not
    stripped; sufficient for the 'void' definition check)."""
    j = i - 1
    while j >= 0 and text[j] in " \t\r\n":
        j -= 1
    k = j
    while k >= 0 and (text[k].isalnum() or text[k] == "_"):
        k -= 1
    return k + 1, None


def parse_call(text, open_paren):
    """Parse one register_one( ... ); call starting at its '('."""
    n = len(text)
    depth = 0
    args = []
    cur_start = open_paren + 1
    i = open_paren
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i = skip_line_comment(text, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i = skip_block_comment(text, i)
            continue
        if c in "\"'":
            i = skip_string(text, i)
            continue
        if c == "R" and text[i:i + 2] == 'R"':
            i = find_raw_string_end(text, i)
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if c == ")" and depth == 0:
                args.append(text[cur_start:i])
                # expect optional whitespace + ';'
                j, nc = next_code(text, i + 1)
                if nc == ";":
                    return j + 1, args
                if nc == "":
                    return len(text), args
                raise ScanError("register_one call not terminated by ';'")
        elif c == "," and depth == 1:
            args.append(text[cur_start:i])
            cur_start = i + 1
        i += 1
    raise ScanError("unterminated register_one call")


# ---------------------------------------------------------------------------
# Canonical extraction
# ---------------------------------------------------------------------------

NAME_RE = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"\s*$')
SCOPE_RE = re.compile(r"^\s*Scope::(Read|Write|Admin)\s*$")


def decode_name(lit):
    m = NAME_RE.match(lit)
    if not m:
        raise ScanError("first register_one argument is not a string literal: %r" % lit[:60])
    return (
        m.group(1)
        .replace('\\"', '"')
        .replace("\\\\", "\\")
    )


def extract_records(text):
    records = []
    for start, end, args in find_calls(text):
        if len(args) < 3:
            raise ScanError("register_one call with < 3 args at offset %d" % start)
        name = decode_name(args[0])
        m = SCOPE_RE.match(args[1])
        if not m:
            raise ScanError("bad scope arg for %s: %r" % (name, args[1][:40]))
        scope = m.group(1)
        local_only = False
        description = "<default>"
        schema = "<default>"
        if len(args) >= 4:
            t = args[3].strip()
            if t not in ("true", "false"):
                raise ScanError("bad local_only arg for %s: %r" % (name, t[:40]))
            local_only = t == "true"
        if len(args) >= 5:
            description = args[3 + 1].strip()
        if len(args) >= 6:
            schema = args[3 + 2].strip()
        if len(args) > 6:
            raise ScanError("too many register_one args for %s" % name)
        records.append(
            {
                "name": name,
                "scope": scope,
                "local_only": local_only,
                "description": description,
                "schema": schema,
            }
        )
    return records


def canonical_bytes(text):
    records = extract_records(text)
    names = [r["name"] for r in records]
    if len(set(names)) != len(names):
        dupes = sorted({n for n in names if names.count(n) > 1})
        raise ScanError("duplicate op names: %s" % dupes)
    doc = {"op_count": len(records), "ops": records}
    return (json.dumps(doc, ensure_ascii=True, separators=(",", ":"), sort_keys=False) + "\n").encode("utf-8"), records


def cmd_extract(src, out_path):
    with open(src, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    data, records = canonical_bytes(text)
    with open(out_path, "wb") as f:
        f.write(data)
    print("%s %d" % (hashlib.sha256(data).hexdigest(), len(records)))


# ---------------------------------------------------------------------------
# D4 split
# ---------------------------------------------------------------------------

SECTIONS = [
    # (function name, one-line doc, first op name of the section)
    ("register_system_ops", "system / health ops", "get_health"),
    ("register_pages_ops", "page CRUD core ops", "put_page"),
    ("register_search_ops", "search / think ops", "search"),
    ("register_pages_lifecycle_ops", "page lifecycle: capture, link reads, delete/restore/purge, versions", "capture"),
    ("register_sources_ops", "source registry ops", "sources_list"),
    ("register_knowledge_ops", "facts / trajectory / skills / doctor / gbrain query alias ops", "list_facts"),
    ("register_graph_ops", "link/tag graph and graph analytics ops", "add_link"),
    ("register_workspace_ops", "brain workspace context ops", "list_brains"),
    ("register_jobs_ops", "N12 job submission / monitoring ops", "submit_job"),
    ("register_cycle_sync_ops", "N12 dream cycle + N13 sync / traversal ops", "run_dream"),
    ("register_jobs_control_ops", "job control: retry / pause / resume / progress / snapshot", "retry_job"),
    ("register_maintenance_ops", "remediation / fact hygiene / slug resolution / recall ops", "doctor_remediate"),
    ("register_code_ops", "N16 code-intel ops", "code_def"),
    ("register_chronicle_ops", "N15 link sources / ingest log / chronicle / timeline ops", "list_link_sources"),
    ("register_jobs_messaging_ops", "N17 job replay and messaging ops", "replay_job"),
    ("register_agent_context_ops", "N19 identity / volunteer context / timeline ops", "get_brain_identity"),
    ("register_schema_ops", "N20 schema pack + ontology ops", "list_schema_packs"),
    ("register_takes_ops", "N21 takes / calibration ops", "takes_list"),
    ("register_code_traversal_ops", "N22 code traversal ops", "code_callees"),
    ("register_chronicle_history_ops", "N23 chronicle history ops", "chronicle_on_this_day"),
    ("register_files_ops", "N24 file storage ops", "file_upload"),
    ("register_schema_deep_ops", "N25 schema / ontology deep-read ops", "schema_lint"),
    ("register_agent_ops", "N26 agent / advisor / onboard / skillopt ops", "submit_agent"),
    ("register_raw_ops", "N27 raw data / transcripts / salience / image ops", "put_raw_data"),
    ("register_schema_mutations_ops", "N28 schema mutation op", "schema_apply_mutations"),
]

FUNC_HEADER = """void register_builtin_ops() {"""
ANON_END = "}  // namespace"


def itemize_body(text, body_start, body_end):
    """Split the function body (offsets between '{' and matching '}') into
    items: ('call', start, end, name), ('attach', start, end) for comments and
    top-level helper defs (they attach to the NEXT call), ('blank', ...)."""
    items = []
    i = body_start
    n = body_end
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            # blank run
            j = i
            while j < n and text[j] in " \t\r\n":
                j += 1
            items.append(("blank", i, j))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = skip_line_comment(text, i)
            items.append(("attach", i, j))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = skip_block_comment(text, i)
            items.append(("attach", i, j))
            i = j
            continue
        if c in "\"'":
            raise ScanError("unexpected top-level string literal at %d" % i)
        if c == "R" and text[i:i + 2] == 'R"':
            raise ScanError("unexpected top-level raw string at %d" % i)
        # identifier or other token
        import re as _re
        m = _re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:n])
        if not m:
            raise ScanError("unexpected top-level token %r at %d" % (c, i))
        ident = m.group(0)
        if ident == "register_one":
            j, nc = next_code(text, i + len(ident))
            if nc != "(":
                raise ScanError("top-level register_one without '(' at %d" % i)
            end, args = parse_call(text, j)
            name = decode_name(args[0])
            items.append(("call", i, end, name))
            i = end
            continue
        if ident == "auto":
            # top-level helper definition: consume to ';' at depth 0
            depth = 0
            j = i
            while j < n:
                cj = text[j]
                if cj == "/" and j + 1 < n and text[j + 1] == "/":
                    j = skip_line_comment(text, j)
                    continue
                if cj == "/" and j + 1 < n and text[j + 1] == "*":
                    j = skip_block_comment(text, j)
                    continue
                if cj in "\"'":
                    j = skip_string(text, j)
                    continue
                if cj == "R" and text[j:j + 2] == 'R"':
                    j = find_raw_string_end(text, j)
                    continue
                if cj in "([{":
                    depth += 1
                elif cj in ")]}":
                    depth -= 1
                elif cj == ";" and depth == 0:
                    items.append(("attach", i, j + 1))
                    j += 1
                    break
                j += 1
            else:
                raise ScanError("unterminated top-level helper at %d" % i)
            i = j
            continue
        raise ScanError("unexpected top-level identifier %r at %d" % (ident, i))
    return items


def trim_blanks(items):
    while items and items[0][0] == "blank":
        items.pop(0)
    while items and items[-1][0] == "blank":
        items.pop()
    return items


def cmd_split(src, out_path):
    with open(src, "r", encoding="utf-8", newline="") as f:
        text = f.read()

    before_bytes, before_records = canonical_bytes(text)

    hdr = text.find(FUNC_HEADER)
    if hdr == -1:
        raise ScanError("register_builtin_ops header not found")
    # must be at the start of a line and preceded only by whitespace on its line
    line_start = text.rfind("\n", 0, hdr) + 1
    if text[line_start:hdr].strip():
        raise ScanError("register_builtin_ops header not at line start")
    open_brace = hdr + len(FUNC_HEADER) - 1
    if text[open_brace] != "{":
        raise ScanError("register_builtin_ops body brace not found")

    # find matching close brace for the function body
    depth = 0
    i = open_brace
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i = skip_line_comment(text, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i = skip_block_comment(text, i)
            continue
        if c in "\"'":
            i = skip_string(text, i)
            continue
        if c == "R" and text[i:i + 2] == 'R"':
            i = find_raw_string_end(text, i)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    if i >= n:
        raise ScanError("register_builtin_ops close brace not found")
    body_start = open_brace + 1
    body_end = i

    items = itemize_body(text, body_start, body_end)

    call_names = [it[3] for it in items if it[0] == "call"]
    if call_names != [r["name"] for r in before_records]:
        raise ScanError("itemizer call sequence != extraction sequence")

    # validate section boundaries: first ops present, in order, no dupes
    idx = {name: k for k, name in enumerate(call_names)}
    bounds = []
    for _, _, first in SECTIONS:
        if first not in idx:
            raise ScanError("section boundary op missing: %s" % first)
        bounds.append(idx[first])
    if bounds != sorted(bounds) or len(set(bounds)) != len(bounds):
        raise ScanError("section boundaries out of order")
    if bounds[0] != 0:
        raise ScanError("first section must start at the first op")

    # assign items to sections. Comments, helper defs and blank runs all
    # attach FORWARD to the next call, so every section body preserves the
    # original interior spacing verbatim; seam blanks are trimmed later.
    sections = [[] for _ in SECTIONS]
    pending = []
    sec_of_call = {}
    call_no = 0
    for it in items:
        if it[0] == "call":
            s = 0
            for k in range(len(SECTIONS) - 1, -1, -1):
                if call_no >= bounds[k]:
                    s = k
                    break
            sec_of_call[call_no] = s
            sections[s].extend(pending)
            pending = []
            sections[s].append(it)
            call_no += 1
        else:
            pending.append(it)
    # trailing blanks/comments after the last call are dropped (seam blanks);
    # anything else is an error.
    if any(it[0] == "attach" for it in pending):
        raise ScanError("trailing attach items after last call")

    counts = [sum(1 for it in sec if it[0] == "call") for sec in sections]
    if sum(counts) != len(call_names):
        raise ScanError("section op count mismatch")

    # rebuild file
    out = []
    out.append(text[:line_start])  # everything before the function line
    # insert section functions INSIDE the anonymous namespace: find the
    # anonymous-namespace close that immediately precedes the function.
    # text[:line_start] currently ends with ANON_END + "\n\n" (blank line).
    prefix = text[:line_start]
    p = prefix.rstrip("\n")
    idx_anon = p.rfind("\n" + ANON_END)
    if idx_anon == -1 or not p[idx_anon + 1:].strip() == ANON_END.strip():
        raise ScanError("anonymous namespace close not found before function")
    keep = p[: idx_anon + 1]  # up to but not including the ANON_END line start
    out = []
    out.append(keep)  # includes trailing newline of previous line
    for k, (fn, doc, first) in enumerate(SECTIONS):
        body_items = trim_blanks(list(sections[k]))
        body = "".join(text[it[1]:it[2]] for it in body_items)
        body = body.strip("\n")
        # the trimmed seam blank run carried the first line's 2-space indent;
        # restore it so every moved line keeps its original indentation
        body = "  " + body
        if not body.endswith(");"):
            raise ScanError("section %s body does not end with a call" % fn)
        out.append(
            "// N31 D4: %s (%d) — moved verbatim from register_builtin_ops;\n"
            "// registration order preserved.\n"
            "void %s() {\n%s\n}\n\n" % (doc, counts[k], fn, body)
        )
    out.append(ANON_END + "\n\n")
    out.append(
        "// N31 D4: builtin registration decomposed into per-domain units in\n"
        "// the anonymous namespace above. The call sequence preserves the\n"
        "// exact historical registration order (ops are never reordered\n"
        "// across units); equivalence evidence:\n"
        "// docs/nodes/n31-evidence/EQUIVALENCE.json.\n"
    )
    out.append(FUNC_HEADER + "\n")
    for fn, _, _ in SECTIONS:
        out.append("  %s();\n" % fn)
    out.append("}\n")
    # tail after the original function close brace (blank line + namespace
    # close); normalize to exactly one blank line.
    tail = text[body_end + 1:].lstrip("\n")
    out.append("\n" + tail)
    new_text = "".join(out)

    after_bytes, after_records = canonical_bytes(new_text)
    if after_bytes != before_bytes:
        raise ScanError("FINGERPRINT MISMATCH after split — refusing to write")

    with open(out_path, "w", encoding="utf-8", newline="") as f:
        f.write(new_text)
    sys.stderr.write(
        "split ok: %d ops in %d units, fingerprint %s\n"
        % (len(after_records), len(SECTIONS), hashlib.sha256(after_bytes).hexdigest())
    )
    for (fn, doc, first), cnt in zip(SECTIONS, counts):
        sys.stderr.write("  %-34s %3d ops (from %s)\n" % (fn, cnt, first))


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("extract", "split"):
        sys.stderr.write(__doc__)
        return 2
    if sys.argv[1] == "extract":
        cmd_extract(sys.argv[2], sys.argv[3])
    else:
        cmd_split(sys.argv[2], sys.argv[3])
    return 0


if __name__ == "__main__":
    sys.exit(main())
