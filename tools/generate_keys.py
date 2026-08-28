#!/usr/bin/env python3
"""Generate a P-224 key pair for the OpenHaystack / Find My beacon.

Usage:
    pip install cryptography
    python tools/generate_keys.py

Then:
  - Paste the printed Base64 advertisement string into src/main.cpp.
  - Save the printed "Private/Advertisement/Hashed" block as `my-tag.keys`
    and import it into macless-haystack to read the location.

Keep the PRIVATE key secret: anyone with it can decrypt your tag's location.
"""
import base64
import hashlib
from cryptography.hazmat.primitives.asymmetric import ec

priv = ec.generate_private_key(ec.SECP224R1())
d = priv.private_numbers().private_value
x = priv.public_key().public_numbers().x

priv_b = d.to_bytes(28, "big")
pub_b = x.to_bytes(28, "big")          # advertised key = X coordinate (28 bytes)

print("=== macless-haystack key file (save as my-tag.keys) ===")
print("Private key: " + base64.b64encode(priv_b).decode())
print("Advertisement key: " + base64.b64encode(pub_b).decode())
print("Hashed adv key: " + base64.b64encode(hashlib.sha256(pub_b).digest()).decode())
print()
print("=== Paste into src/main.cpp ===")
print(
    'static const char apple_findmy_advertisement_key_base64[] = "'
    + base64.b64encode(pub_b).decode()
    + '";'
)
