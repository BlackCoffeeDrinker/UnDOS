#!/usr/bin/env python3
# undos_idl.py
#
# Thin entry point for the UnDOS IDL compiler. The actual implementation
# (tokenizer, parser, AST, and code generators) lives in the `idlgen`
# package alongside this script (see tools/idlgen/).

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from idlgen.cli import main

if __name__ == "__main__":
    main()
