#!/usr/bin/env python3
#
# Libu2f-emu setup directory generator for USB U2F key emulation.
#
# Copyright (c) 2020 César Belley <cesar.belley@lse.epita.fr>
# Written by César Belley <cesar.belley@lse.epita.fr>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or, at your option, any later version.  See the COPYING file in
# the top-level directory.

import sys
import os
from random import randint
from typing import Tuple

from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding, \
    NoEncryption, PrivateFormat, PublicFormat
from OpenSSL import crypto


def write_setup_dir(dirpath: str, privkey_pem: bytes, cert_pem: bytes,
                    entropy: bytes, counter: int) -> None:
    """
    Write the setup directory.

    Args:
        dirpath: The directory path.
        key_pem: The private key PEM.
        cert_pem: The certificate PEM.
        entropy: The 48 bytes of entropy.
        counter: The counter value.
    """
    # Directory
    os.mkdir(dirpath)

    # Private key
    with open(f'{dirpath}/private-key.pem', 'bw') as f:
        f.write(privkey_pem)

    # Certificate
    with open(f'{dirpath}/certificate.pem', 'bw') as f:
        f.write(cert_pem)

    # Entropy
    with open(f'{dirpath}/entropy', 'wb') as f:
        f.write(entropy)

    # Counter
    with open(f'{dirpath}/counter', 'w') as f:
        f.write(f'{str(counter)}\n')


def generate_ec_key_pair() -> Tuple[str, str]:
    """
    Generate an ec key pair.

    Returns:
        The private and public key PEM.
    """
    # Key generation
    privkey = ec.generate_private_key(ec.SECP256R1, default_backend())
    pubkey = privkey.public_key()

    # PEM serialization
    privkey_pem = privkey.private_bytes(encoding=Encoding.PEM,
                                        format=PrivateFormat.TraditionalOpenSSL,
                                        encryption_algorithm=NoEncryption())
    pubkey_pem = pubkey.public_bytes(encoding=Encoding.PEM,
                                     format=PublicFormat.SubjectPublicKeyInfo)
    return privkey_pem, pubkey_pem


def generate_certificate(privkey_pem: str, pubkey_pem: str) -> str:
    """
    Generate a x509 certificate from a key pair.

    Args:
        privkey_pem: The private key PEM.
        pubkey_pem: The public key PEM.

    Returns:
        The certificate PEM.
    """
    # Convert key pair
    privkey = crypto.load_privatekey(crypto.FILETYPE_PEM, privkey_pem)
    pubkey = crypto.load_publickey(crypto.FILETYPE_PEM, pubkey_pem)

    # New x509v3 certificate
    cert = crypto.X509()
    cert.set_version(0x2)

    # Serial number
    cert.set_serial_number(randint(1, 2 ** 64))

    # Before / After
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(4 * (365 * 24 * 60 * 60))

    # Public key
    cert.set_pubkey(pubkey)

    # Subject name and issueer
    cert.get_subject().CN = "U2F emulated"
    cert.set_issuer(cert.get_subject())

    # Extensions
    cert.add_extensions([
        crypto.X509Extension(b"subjectKeyIdentifier",
                             False, b"hash", subject=cert),
    ])
    cert.add_extensions([
        crypto.X509Extension(b"authorityKeyIdentifier",
                             False, b"keyid:always", issuer=cert),
    ])
    cert.add_extensions([
        crypto.X509Extension(b"basicConstraints", True, b"CA:TRUE")
    ])

    # Signature
    cert.sign(privkey, 'sha256')

    return crypto.dump_certificate(crypto.FILETYPE_PEM, cert)


def generate_setup_dir(dirpath: str) -> None:
    """
    Generates the setup directory.

    Args:
        dirpath: The directory path.
    """
    # Key pair
    privkey_pem, pubkey_pem = generate_ec_key_pair()

    # Certificate
    certificate_pem = generate_certificate(privkey_pem, pubkey_pem)

    # Entropy
    entropy = os.urandom(48)

    # Counter
    counter = 0

    # Write
    write_setup_dir(dirpath, privkey_pem, certificate_pem, entropy, counter)


def main() -> None:
    """
    Main function
    """
    # Dir path
    if len(sys.argv) != 2:
        sys.stderr.write(f'Usage: {sys.argv[0]} <setup_dir>\n')
        exit(2)
    dirpath = sys.argv[1]

    # Dir non existence
    if os.path.exists(dirpath):
        sys.stderr.write(f'Directory: {dirpath} already exists.\n')
        exit(1)

    generate_setup_dir(dirpath)


if __name__ == '__main__':
    main()
