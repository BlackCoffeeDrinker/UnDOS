#!/usr/bin/env python3
"""Generate the kernel component dependency graph (Mermaid) from the sources.

An edge A --> B means a source file of component A includes a public header of
component B (`kernel/<folder>/...` under B's `include/`). The kernel core
(`Kernel/Source/src`) is shown as CORE. Edge labels are include-site counts.

The script also reads each component's CMakeLists.txt and reports includes of a
component whose `<Name>_Interface` the includer does not link (CORE links every
component, so it is exempt).

Usage:
  Scripts/components_graph.py            # print the Mermaid block
  Scripts/components_graph.py --update   # rewrite the graph in Kernel/Components/README.md
  Scripts/components_graph.py --check    # exit 1 if the README graph is stale
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = ROOT / 'Kernel/Components'
CORE = ROOT / 'Kernel/Source/src'
README = COMPONENTS / 'README.md'
BEGIN = '<!-- BEGIN GENERATED GRAPH (Scripts/components_graph.py --update) -->'
END = '<!-- END GENERATED GRAPH -->'
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
SOURCES = ('.cpp', '.hpp', '.h', '.c')


def components():
    return sorted(p for p in COMPONENTS.iterdir() if (p / 'CMakeLists.txt').is_file())


def header_owners(comps):
    """Map `kernel/<folder>/` prefix -> component name."""
    owners = {}
    for comp in comps:
        inc = comp / 'include/kernel'
        if inc.is_dir():
            for folder in inc.iterdir():
                if folder.is_dir():
                    owners[f'kernel/{folder.name}/'] = comp.name
    return owners


def linked_interfaces(comp):
    text = (comp / 'CMakeLists.txt').read_text()
    return set(re.findall(r'(\w+)_Interface', text)) - {comp.name}


def scan():
    comps = components()
    owners = header_owners(comps)
    roots = [(c.name, c) for c in comps] + [('CORE', CORE)]
    edges = defaultdict(int)
    for name, root in roots:
        for path in root.rglob('*'):
            if path.suffix not in SOURCES:
                continue
            for inc in INCLUDE.findall(path.read_text(errors='replace')):
                for prefix, owner in owners.items():
                    # Kernel/API shares some folder names (kernel/gfx/), so the
                    # header must really live in the owner's include/ folder.
                    if (inc.startswith(prefix) and owner != name
                            and (COMPONENTS / owner / 'include' / inc).is_file()):
                        edges[(name, owner)] += 1
    links = {c.name: linked_interfaces(c) for c in comps}
    unlinked = sorted((a, b) for (a, b) in edges if a != 'CORE' and b not in links.get(a, set()))
    return [c.name for c in comps], edges, unlinked


def mermaid(names, edges):
    lines = ['```mermaid', 'flowchart TD']
    lines.append('  CORE["Kernel core<br/>Kernel/Source"]')
    for n in names:
        lines.append(f'  {n}["{n}"]')
    for (a, b), count in sorted(edges.items()):
        if a == 'CORE':
            continue
        lines.append(f'  {a} -->|{count}| {b}')
    # CORE includes are summarised with dotted edges to keep the graph readable.
    for (a, b), _ in sorted(edges.items()):
        if a == 'CORE':
            lines.append(f'  CORE -.-> {b}')
    lines.append('```')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--update', action='store_true', help='rewrite the graph in Kernel/Components/README.md')
    ap.add_argument('--check', action='store_true', help='fail if the README graph is out of date')
    args = ap.parse_args()

    names, edges, unlinked = scan()
    graph = mermaid(names, edges)
    for a, b in unlinked:
        print(f'warning: {a} includes kernel headers of {b} but does not link {b}_Interface', file=sys.stderr)

    if args.update or args.check:
        text = README.read_text()
        if BEGIN not in text or END not in text:
            sys.exit(f'{README}: graph markers not found')
        head, rest = text.split(BEGIN, 1)
        _, tail = rest.split(END, 1)
        new = f'{head}{BEGIN}\n{graph}\n{END}{tail}'
        if args.check:
            if new != text:
                sys.exit(f'{README}: graph is stale; run Scripts/components_graph.py --update')
            return
        README.write_text(new)
        print(f'updated {README.relative_to(ROOT)}')
    else:
        print(graph)


if __name__ == '__main__':
    main()
