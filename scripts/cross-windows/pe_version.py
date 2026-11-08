#!/usr/bin/env python3
"""Read the VERSIONINFO resource of a Windows PE file on any host."""
import json
import struct
import sys
from pathlib import Path

RT_VERSION = 16
FIXED_FILE_INFO = 0xFEEF04BD


def version_block(data):
    """Bytes of the first RT_VERSION resource."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    section_count, = struct.unpack_from('<H', data, pe + 6)
    optional_size, = struct.unpack_from('<H', data, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from('<H', data, optional)
    directories = optional + (112 if magic == 0x20b else 96)
    resources, _ = struct.unpack_from('<II', data, directories + 2 * 8)
    sections = [struct.unpack_from('<IIII', data, optional + optional_size + 40 * i + 8)
                for i in range(section_count)]

    def offset(rva):
        for virtual_size, address, raw_size, raw in sections:
            if address <= rva < address + max(virtual_size, raw_size):
                return rva - address + raw
        raise ValueError(f'RVA {rva:#x} is outside every section')

    if not resources:
        raise ValueError('no resource directory')
    root = offset(resources)

    def entries(directory):
        named, ids = struct.unpack_from('<HH', data, directory + 12)
        return [struct.unpack_from('<II', data, directory + 16 + 8 * i) for i in range(named + ids)]

    for identifier, target in entries(root):
        if identifier == RT_VERSION:
            names = entries(root + (target & 0x7fffffff))
            languages = entries(root + (names[0][1] & 0x7fffffff))
            data_rva, size = struct.unpack_from('<II', data, root + languages[0][1])
            start = offset(data_rva)
            return data[start:start + size]
    raise ValueError('no VERSIONINFO resource')


def read_version(path):
    """{'file_version': [a, b, c, d], 'product_version': [...], 'strings': {...}}."""
    block = version_block(Path(path).read_bytes())

    def align(offset):
        return (offset + 3) & ~3

    def node(offset):
        length, value_length, kind = struct.unpack_from('<HHH', block, offset)
        end = offset + 6
        while block[end:end + 2] != b'\0\0':
            end += 2
        return length, value_length, kind, block[offset + 6:end].decode('utf-16-le'), align(end + 2)

    def children(offset):
        length, value_length, kind, _, value = node(offset)
        # Binary values are sized in bytes; containers have no value.
        child = align(value + (value_length if kind == 0 else 0))
        while child < offset + length:
            child_length, = struct.unpack_from('<H', block, child)
            if not child_length:
                break
            yield child
            child = align(child + child_length)

    _, value_length, _, key, value = node(0)
    signature, = struct.unpack_from('<I', block, value)
    if key != 'VS_VERSION_INFO' or value_length < 52 or signature != FIXED_FILE_INFO:
        raise ValueError('malformed VERSIONINFO resource')
    file_ms, file_ls, product_ms, product_ls = struct.unpack_from('<IIII', block, value + 8)
    strings = {}
    for info in children(0):
        if node(info)[3] != 'StringFileInfo':
            continue
        for table in children(info):
            for string in children(table):
                length, _, _, name, start = node(string)
                text = block[start:string + length].decode('utf-16-le')
                strings[name] = text.split('\0', 1)[0]
    return {'file_version': [file_ms >> 16, file_ms & 0xffff, file_ls >> 16, file_ls & 0xffff],
            'product_version': [product_ms >> 16, product_ms & 0xffff,
                                product_ls >> 16, product_ls & 0xffff],
            'strings': strings}


if __name__ == '__main__':
    print(json.dumps(read_version(sys.argv[1]), indent=2))
