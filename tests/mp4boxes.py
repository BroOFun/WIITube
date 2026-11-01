"""Print the top-level MP4 box layout of a file (first few boxes), e.g. ftyp moov sidx moof mdat."""
import struct
import sys

for path in sys.argv[1:]:
    boxes = []
    with open(path, "rb") as f:
        while len(boxes) < 12:
            head = f.read(8)
            if len(head) < 8:
                break
            size, kind = struct.unpack(">I4s", head)
            if size == 1:
                size = struct.unpack(">Q", f.read(8))[0]
                f.seek(size - 16, 1)
            elif size == 0:
                boxes.append(kind.decode("latin-1") + "(to end)")
                break
            else:
                f.seek(size - 8, 1)
            boxes.append(f"{kind.decode('latin-1')}({size})")
    print(f"{path}: {' '.join(boxes)}")
