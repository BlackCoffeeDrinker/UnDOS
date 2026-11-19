#!/usr/bin/env python3
"""Kernel component dependency report.

Groups every source under Kernel/Source/src and Kernel/Components/<Name> into a
component, then reports:
  * include edges between groups (A -> B: a file of A includes a header of B),
  * cycles between groups,
  * shared declaration headers under Kernel/API/include (kernel/kobject/*,
    kernel/device.hpp, kernel/scheduler.hpp, ...) form the API group and create
    no component edge,
  * private-header violations: a file outside Kernel/Components/<Name> that
    reaches a header under <Name>/src (exit code 1 if any),
  * with --objects <build-dir>: symbol edges from `nm` on the object files
    (coupling through extern declarations that includes do not show).

usage: kernel_deps.py [--root DIR] [--verbose] [--objects BUILD_DIR] [--nm NM]
"""
import argparse
import collections
import os
import pathlib
import re
import subprocess
import sys

# Groups for files still in Kernel/Source/src, and for public kernel/ headers.
GROUPS = [
    # Wait/wake interface used by OB, MM and PS; PS implements it (wait_interface.cpp).
    ('WAIT', r'^sched_wait\.hpp$'),
    ('MM', r'^(vmm|vmm_owner_lifecycle|memory/|guard/)|^kernel/(memory|mm)/'),
    ('PS', r'^(scheduler/|sched_|time\.|kernel/ps/|process|user_process|wait|task_stats|percpu|smp|syscalls/(CreateProcess|GetStdHandle))'
           r'|^kernel/sched/|^kernel/(scheduler|kobject/K(Process|Thread)Object)|^(Scheduler|CpuContext|ScheduleNext|ReadyQueue)\.'),
    ('OB', r'^(object_manager|event_broadcaster|dispatch)'
           r'|^kernel/(ob/|object_manager|dispatch|KObject|kobject/K(Dispatcher|Directory|Event))'),
    ('IO', r'^(io\.|vfs|syscalls/IO|dma_manager)|^kernel/(vfs|kobject/KIOObject)'),
    ('PNP', r'^(pnp_|configuration_manager|resource_manager)'
            r'|^kernel/(configuration_manager|device|pnp/|kobject/K(Device|Driver)Object)'),
    # legacy_isa_enum loads drivers (KE_DRIVER_Load) and reports PDOs to PnP.
    ('LDR', r'^(elf_|driver_loader|subsystem_loader|legacy_isa|module/)|^kernel/(elf|loader/)'),
    # konsole.cpp (KE_PrintBuffer, KE_RUNTIME_Panic) is used by everything: CORE.
    ('TTY', r'^(tty/|syscalls/TTY|syscalls/DEBUG)|^kernel/tty/'),
    ('INP', r'^input/|^kernel/input'),
    ('PROF', r'^profiler/|^kernel/profiler'),
    ('BOOT', r'^initial/|^kernel/initial/'),
]
# Kernel/Components/<folder> -> group name (public headers live in include/kernel/<lower>/).
COMPONENT_GROUPS = {'GFX': 'GFX', 'User': 'USER', 'Profiler': 'PROF', 'Input': 'INP', 'Initial': 'BOOT',
                    'TTY': 'TTY', 'PnP': 'PNP', 'Loader': 'LDR', 'Executive': 'EXEC', 'IO': 'IO'}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def group_of_path(rel):
    for name, pattern in GROUPS:
        if re.search(pattern, rel):
            return name
    return None


def group_of_include(inc, root):
    if (root / 'Kernel/API/include' / inc).is_file():
        return 'API'
    for folder, name in COMPONENT_GROUPS.items():
        if inc.startswith(f'kernel/{folder.lower()}/') or inc.startswith(f'{folder.lower()}/'):
            return name
    return group_of_path(inc)


def collect(root):
    """Returns {abs path: (group, component folder or None, path relative to its src/include root)}."""
    files = {}
    src = root / 'Kernel/Source/src'
    for path in src.rglob('*'):
        if path.suffix in ('.cpp', '.hpp', '.h'):
            rel = path.relative_to(src).as_posix()
            files[path] = (group_of_path(rel) or 'CORE', None, rel)
    for comp in (root / 'Kernel/Components').iterdir():
        if not comp.is_dir():
            continue
        name = COMPONENT_GROUPS.get(comp.name, comp.name.upper())
        for path in comp.rglob('*'):
            if path.suffix in ('.cpp', '.hpp', '.h'):
                rel = path.relative_to(comp).as_posix()
                group = name
                if comp.name == 'Executive':
                    # Keep the OB/PS/MM/WAIT split visible inside Executive.
                    inner = rel.split('/', 1)[1] if '/' in rel else rel
                    group = group_of_path(inner) or 'PS'
                files[path] = (group, comp.name, rel)
    return files


def find_cycles(edges):
    graph = collections.defaultdict(set)
    for a, b in edges:
        graph[a].add(b)
    # Tarjan's strongly connected components.
    index, low, stack, on, sccs, counter = {}, {}, [], set(), [], [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in graph[v]:
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            scc = []
            while True:
                w = stack.pop()
                on.discard(w)
                scc.append(w)
                if w == v:
                    break
            if len(scc) > 1:
                sccs.append(sorted(scc))

    for v in list(graph):
        if v not in index:
            visit(v)
    return sccs


def symbol_edges(files, build, nm):
    """Maps object files back to sources by name and cross-references nm symbols."""
    by_name = collections.defaultdict(list)
    for path, (group, comp, rel) in files.items():
        if path.suffix == '.cpp':
            by_name[path.name].append((rel if comp else 'src/' + rel, group))
    defined, undefined = {}, collections.defaultdict(set)
    for obj in pathlib.Path(build).rglob('*.o*'):
        if obj.suffix not in ('.o', '.obj'):
            continue
        if '/Kernel/Source/' not in obj.as_posix() and '/Kernel/Components/' not in obj.as_posix():
            continue
        candidates = by_name.get(obj.stem, [])
        # CMake names objects <target>.dir/src/<path under src>.o(bj)
        matches = [g for p, g in candidates
                   if obj.as_posix().endswith('.dir/' + p + obj.suffix)]
        if not matches:
            continue
        group = matches[0]
        out = subprocess.run([nm, '-P', str(obj)], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 2:
                continue
            sym, kind = parts[0], parts[1]
            if kind == 'U':
                undefined[group].add(sym)
            elif kind in 'TDBRVW':
                defined.setdefault(sym, group)
    edges = collections.defaultdict(set)
    for group, syms in undefined.items():
        for sym in syms:
            owner = defined.get(sym)
            if owner and owner != group:
                edges[(group, owner)].add(sym)
    return edges


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', default=pathlib.Path(__file__).resolve().parents[1], type=pathlib.Path)
    parser.add_argument('--verbose', '-v', action='store_true', help='list every include site')
    parser.add_argument('--objects', help='build directory with kernel object files for nm')
    parser.add_argument('--nm', default='nm')
    args = parser.parse_args()
    root = args.root.resolve()
    files = collect(root)
    basenames = collections.defaultdict(list)
    for path in files:
        basenames[path.name].append(path)

    edges = collections.defaultdict(set)
    violations = []
    for path, (group, comp, rel) in sorted(files.items()):
        for inc in INCLUDE.findall(path.read_text(errors='ignore')):
            local = (path.parent / inc).resolve()
            target_group = None
            if local in files:
                target_group, target_comp, target_rel = files[local]
                if target_comp and target_comp != comp and target_rel.startswith('src/'):
                    violations.append(f'{path.relative_to(root)} -> {inc}')
            else:
                target_group = group_of_include(inc, root)
                if not target_group and '/' not in inc and len(basenames.get(inc, [])) == 1:
                    target_group = files[basenames[inc][0]][0]
            if target_group and target_group not in (group, 'API'):
                edges[(group, target_group)].add(f'{path.relative_to(root)} -> {inc}')

    print('== Include edges (A -> B: A includes B; count = include sites)')
    for (a, b), sites in sorted(edges.items()):
        print(f'{a} -> {b} ({len(sites)})')
        if args.verbose:
            for site in sorted(sites):
                print(f'    {site}')
    print('\n== Cycles (strongly connected groups, CORE excluded)')
    cycles = find_cycles([e for e in edges if 'CORE' not in e])
    for scc in cycles:
        print('  ' + ' <-> '.join(scc))
    if not cycles:
        print('  none')
    print('\n== Direct mutual edges')
    for a, b in sorted(e for e in edges if e[0] < e[1] and (e[1], e[0]) in edges and 'CORE' not in e):
        print(f'  {a} <-> {b}')

    if args.objects:
        print('\n== Symbol edges from nm (A -> B: A references a symbol B defines)')
        for (a, b), syms in sorted(symbol_edges(files, args.objects, args.nm).items()):
            print(f'{a} -> {b} ({len(syms)})')
            if args.verbose:
                for sym in sorted(syms)[:20]:
                    print(f'    {sym}')

    print('\n== Cross-component private includes')
    for v in violations:
        print(f'  {v}')
    if not violations:
        print('  none')
    return 1 if violations else 0


if __name__ == '__main__':
    sys.exit(main())
