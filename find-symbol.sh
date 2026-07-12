#!/usr/bin/env sh

if [ -z "$1" ]; then
    echo "Usage: $0 <symbol>"
    exit 1
fi

SYMBOL="$1"
FOUND=0

# Find all .o files and check if they contain the symbol
for obj_file in $(find . -name '*.o'); do
    if nm -C "$obj_file" 2>/dev/null | grep -q "$SYMBOL"; then
        echo "Found '$SYMBOL' in $obj_file"
        echo "----------------------------------------"
        readelf -r "$obj_file"
        objdump -r -C "$obj_file"
        echo "----------------------------------------"
        echo ""
        FOUND=1
    fi
done

if [ $FOUND -eq 0 ]; then
    echo "$SYMBOL was not found"
fi


