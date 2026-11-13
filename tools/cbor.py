"""Deterministic CBOR (RFC 8949) for pime repo tooling.

Encodes definite-length maps with canonical key ordering (shorter encoded keys
first, then bytewise), so chain output is reproducible and diffable. Decoding
is intentionally small: it only needs to read the frames the tools produce and
the frames the test cases describe.

No third-party dependencies.
"""

from __future__ import annotations


class CBORError(ValueError):
    pass


def _head(major: int, value: int) -> bytes:
    if value < 0:
        raise CBORError("negative head value")
    prefix = major << 5
    if value < 24:
        return bytes([prefix | value])
    if value <= 0xFF:
        return bytes([prefix | 24, value])
    if value <= 0xFFFF:
        return bytes([prefix | 25]) + value.to_bytes(2, "big")
    if value <= 0xFFFFFFFF:
        return bytes([prefix | 26]) + value.to_bytes(4, "big")
    if value <= 0xFFFFFFFFFFFFFFFF:
        return bytes([prefix | 27]) + value.to_bytes(8, "big")
    raise CBORError("integer out of range")


def dumps(value) -> bytes:
    if value is None:
        return b"\xf6"
    if value is True:
        return b"\xf5"
    if value is False:
        return b"\xf4"
    if isinstance(value, int):
        if value >= 0:
            return _head(0, value)
        return _head(1, -1 - value)
    if isinstance(value, float):
        raise CBORError("floats are not part of the pime data model")
    if isinstance(value, bytes):
        return _head(2, len(value)) + value
    if isinstance(value, str):
        data = value.encode("utf-8")
        return _head(3, len(data)) + data
    if isinstance(value, (list, tuple)):
        out = bytearray(_head(4, len(value)))
        for item in value:
            out += dumps(item)
        return bytes(out)
    if isinstance(value, dict):
        encoded = []
        for key, item in value.items():
            if not isinstance(key, str):
                raise CBORError("map keys must be text")
            encoded.append((dumps(key), dumps(item)))
        encoded.sort(key=lambda pair: (len(pair[0]), pair[0]))
        out = bytearray(_head(5, len(encoded)))
        for key_bytes, value_bytes in encoded:
            out += key_bytes
            out += value_bytes
        return bytes(out)
    raise CBORError(f"unsupported type: {type(value).__name__}")


def _read_head(data: bytes, pos: int):
    if pos >= len(data):
        raise CBORError("truncated head")
    byte = data[pos]
    pos += 1
    major = byte >> 5
    info = byte & 0x1F
    if info < 24:
        return major, info, pos
    sizes = {24: 1, 25: 2, 26: 4, 27: 8}
    if info not in sizes:
        raise CBORError("indefinite length is not supported")
    size = sizes[info]
    if pos + size > len(data):
        raise CBORError("truncated argument")
    value = int.from_bytes(data[pos : pos + size], "big")
    return major, value, pos + size


def loads(data: bytes, pos: int = 0):
    value, end = _decode(data, pos)
    return value


def _decode(data: bytes, pos: int):
    major, arg, pos = _read_head(data, pos)
    if major == 0:
        return arg, pos
    if major == 1:
        return -1 - arg, pos
    if major == 2:
        if pos + arg > len(data):
            raise CBORError("truncated bytes")
        return data[pos : pos + arg], pos + arg
    if major == 3:
        if pos + arg > len(data):
            raise CBORError("truncated text")
        return data[pos : pos + arg].decode("utf-8"), pos + arg
    if major == 4:
        items = []
        for _ in range(arg):
            item, pos = _decode(data, pos)
            items.append(item)
        return items, pos
    if major == 5:
        result = {}
        for _ in range(arg):
            key, pos = _decode(data, pos)
            if not isinstance(key, str):
                raise CBORError("map key is not text")
            item, pos = _decode(data, pos)
            result[key] = item
        return result, pos
    if major == 6:
        tagged, pos = _decode(data, pos)
        return tagged, pos
    if major == 7:
        if arg == 20:
            return False, pos
        if arg == 21:
            return True, pos
        if arg == 22:
            return None, pos
        return arg, pos
    raise CBORError(f"unsupported major type {major}")


def to_hex(data: bytes) -> str:
    return data.hex()


def from_hex(text: str) -> bytes:
    return bytes.fromhex("".join(text.split()))
