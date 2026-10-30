"""Extract one generated dispatcher unchanged for a hosted boundary test."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
name = sys.argv[2]
start = source.index(f"static kstd::error_code Dispatch_{name}(")
end = source.index("\n}\n", start) + len("\n}\n")
pathlib.Path(sys.argv[3]).write_text(source[start:end])
