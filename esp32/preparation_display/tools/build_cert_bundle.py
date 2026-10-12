"""Extract subject/SPKI DER in ESP-IDF 4.x's compact certificate bundle format.

No third-party Python dependencies. Input is the checked-in Mozilla CA PEM file.
Format reference: Espressif gen_crt_bundle.py (ESP-IDF v4.4.7).
Certificates remain trusted exactly as in the input; this is not a TLS verifier.
"""
from pathlib import Path
import base64
import re
import struct


def der_value(data, start, limit):
    """Return tag, content offset, and end for one definite-length DER value."""
    if start + 2 > limit:
        raise ValueError("Truncated DER header")
    tag = data[start]
    length = data[start + 1]
    content = start + 2
    if length & 0x80:
        size = length & 0x7f
        if not 1 <= size <= 4 or content + size > limit:
            raise ValueError("Invalid DER length")
        length = int.from_bytes(data[content:content + size], "big")
        content += size
    end = content + length
    if end > limit:
        raise ValueError("Truncated DER value")
    return tag, content, end


def certificate_parts(cert):
    tag, content, end = der_value(cert, 0, len(cert))
    if tag != 0x30 or end != len(cert):
        raise ValueError("Expected X.509 certificate sequence")
    tag, tbs_content, tbs_end = der_value(cert, content, end)
    if tag != 0x30:
        raise ValueError("Expected TBSCertificate sequence")
    offset = tbs_content
    tag, _, next_offset = der_value(cert, offset, tbs_end)
    if tag == 0xa0:  # Optional explicit version precedes serial number.
        offset = next_offset
    # serialNumber, signature, issuer, validity, subject, subjectPublicKeyInfo
    parts = []
    for expected_tag in (0x02, 0x30, 0x30, 0x30, 0x30, 0x30):
        tag, _, next_offset = der_value(cert, offset, tbs_end)
        if tag != expected_tag:
            raise ValueError("Unexpected X.509 field")
        parts.append(cert[offset:next_offset])
        offset = next_offset
    return parts[4], parts[5]


def build(pem):
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----", pem, re.S)
    if not blocks or len(blocks) > 65535:
        raise ValueError("Invalid CA certificate count")
    entries = [certificate_parts(base64.b64decode(re.sub(r"\s+", "", block), validate=True)) for block in blocks]
    entries.sort(key=lambda entry: entry[0])
    output = bytearray(struct.pack(">H", len(entries)))
    for subject, key in entries:
        output.extend(struct.pack(">HH", len(subject), len(key)))
        output.extend(subject)
        output.extend(key)
    return bytes(output), len(entries)


if __name__ == "__main__":
    project = Path(__file__).resolve().parents[1]
    source = project / "data/weather_roots.pem"
    target = project / "data/weather_roots.crt_bundle"
    bundle, count = build(source.read_text(encoding="ascii"))
    target.write_bytes(bundle)
    print(f"Built {count} roots: PEM {source.stat().st_size} -> bundle {len(bundle)} bytes")
