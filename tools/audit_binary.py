"""Inspect a Windows release DLL for embedded paths, PII and debug references.

Standard-library only. Optional --denylist accepts a private JSON list of strings
to reject in addition to the generic checks. Reports never include those values.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def inspect_pe(data):
    if data[:2] != b'MZ':
        raise ValueError('Expected a Windows PE binary')
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    coff = pe + 4
    machine, count = struct.unpack_from('<HH', data, coff)
    symbols, symbol_count = struct.unpack_from('<II', data, coff + 8)
    optional = coff + 20
    optional_size = struct.unpack_from('<H', data, coff + 16)[0]
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected an x64 PE32+ binary')
    sections = []
    for index in range(count):
        offset = optional + optional_size + 40 * index
        size, address, raw_size, raw = struct.unpack_from('<IIII', data, offset + 8)
        sections.append((address, max(size, raw_size), raw))

    def offset(address):
        for start, size, raw in sections:
            if start <= address < start + size:
                return raw + address - start
        raise ValueError('Unmapped PE virtual address')

    directories = optional + 112
    address, size = struct.unpack_from('<II', data, directories + 6 * 8)
    debug_types = []
    if address:
        if size % 28:
            raise ValueError('Malformed PE debug directory')
        for entry in range(offset(address), offset(address) + size, 28):
            debug_types.append(struct.unpack_from('<I', data, entry + 12)[0])
    address, _ = struct.unpack_from('<II', data, directories + 8)
    imports = []
    if address:
        entry = offset(address)
        while any(struct.unpack_from('<IIIII', data, entry)):
            name = offset(struct.unpack_from('<I', data, entry + 12)[0])
            imports.append(data[name:data.index(0, name)].decode('ascii'))
            entry += 20
    return {'architecture': 'windows-x64', 'debugDirectoryTypes': debug_types,
            'hasCodeViewDebugRecord': 2 in debug_types,
            'hasCoffSymbolTable': bool(symbols or symbol_count), 'imports': imports}


def audit(path, denylist=()):
    data = Path(path).read_bytes()
    pe = inspect_pe(data)
    findings = []
    strings = [(m.start(), m.group().decode('ascii'))
               for m in re.finditer(rb'[\x20-\x7e]{4,}', data)]
    for encoding, pattern in [('utf-16le', rb'(?:[\x20-\x7e]\0){4,}'),
                              ('utf-16be', rb'(?:\0[\x20-\x7e]){4,}')]:
        strings.extend((m.start(), m.group().decode(encoding))
                       for m in re.finditer(pattern, data))
    patterns = {
        'absolute-or-home-path': r'(?i)(?:(?<![a-z0-9])[a-z]:[\\/]|[\\/](?:users|home|documents|appdata)[\\/])',
        'email-address': r'(?i)[a-z0-9_.+\-]+@[a-z0-9.\-]+\.[a-z]{2,}',
        'machine-name': r'(?i)\b(?:DESKTOP|LAPTOP)-[a-z0-9]{5,}\b',
        'pdb-reference': r'(?i)\.pdb\b',
        'credential-pattern': r'(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)',
    }
    for location, text in strings:
        for reason, pattern in patterns.items():
            if re.search(pattern, text):
                findings.append({'kind': reason, 'offset': location})
    for index, value in enumerate(denylist):
        if not isinstance(value, str) or not value:
            raise ValueError('Denylist must contain nonempty strings')
        for encoding in ('utf-8', 'utf-16le', 'utf-16be'):
            needles = {value.encode(encoding).lower(), value.casefold().encode(encoding)}
            if any(needle in data.lower() for needle in needles):
                findings.append({'kind': 'private-denylist', 'index': index,
                                 'encoding': encoding})
    if pe['hasCodeViewDebugRecord']:
        findings.append({'kind': 'codeview-debug-record'})
    if pe['hasCoffSymbolTable']:
        findings.append({'kind': 'coff-symbol-table'})
    return {'binary': Path(path).name, 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), **pe,
            'scannedEncodings': ['ASCII/UTF-8', 'UTF-16LE', 'UTF-16BE'],
            'privateDenylistValuesChecked': len(denylist),
            'findings': findings, 'passed': not findings,
            'scope': 'Embedded printable strings, supplied private values, PE imports and debug metadata. Intentional public project URLs may remain. This is a documented inspection, not a guarantee that arbitrary PII can always be recognized.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--denylist', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    denied = json.loads(args.denylist.read_text(encoding='utf-8')) if args.denylist else []
    if not isinstance(denied, list):
        parser.error('Denylist must be a JSON list')
    result = audit(args.binary, denied)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
