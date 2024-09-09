crypto_ss.add(genh)
crypto_ss.add(files(
  'afsplit.c',
  'akcipher.c',
  'block-luks.c',
  'block-qcow.c',
  'block.c',
  'cipher.c',
  'der.c',
  'hash.c',
  'hmac.c',
  'ivgen-essiv.c',
  'ivgen-plain.c',
  'ivgen-plain64.c',
  'ivgen.c',
  'pbkdf.c',
  'secret_common.c',
  'secret.c',
  'tlscreds.c',
  'tlscredsanon.c',
  'tlscredspsk.c',
  'tlscredsx509.c',
  'tlssession.c',
  'rsakey.c',
))

if gnutls.found()
  crypto_ss.add(files('x509-utils.c'))
endif

if nettle.found()
  crypto_ss.add(nettle, files('hash-nettle.c', 'hmac-nettle.c', 'pbkdf-nettle.c'))
  if hogweed.found()
    crypto_ss.add(gmp, hogweed)
  endif
  if xts == 'private'
    crypto_ss.add(files('xts.c'))
  endif
elif gcrypt.found()
  crypto_ss.add(gcrypt, files('hash-gcrypt.c', 'hmac-gcrypt.c', 'pbkdf-gcrypt.c'))
elif gnutls_crypto.found()
  crypto_ss.add(gnutls, files('hash-gnutls.c', 'hmac-gnutls.c', 'pbkdf-gnutls.c'))
else
  crypto_ss.add(files('hash-glib.c', 'hmac-glib.c', 'pbkdf-stub.c'))
endif

if have_keyring
  crypto_ss.add(files('secret_keyring.c'))
endif
if have_afalg
  crypto_ss.add(if_true: files('afalg.c', 'cipher-afalg.c', 'hash-afalg.c'))
endif

system_ss.add(when: gnutls, if_true: files('tls-cipher-suites.c'))

util_ss.add(files(
  'aes.c',
  'clmul.c',
  'init.c',
  'sm4.c',
))
if gnutls.found()
  util_ss.add(gnutls)
endif

if gcrypt.found()
  util_ss.add(gcrypt, files('random-gcrypt.c'))
elif gnutls.found()
  util_ss.add(gnutls, files('random-gnutls.c'))
elif get_option('rng_none')
  util_ss.add(files('random-none.c'))
else
  util_ss.add(files('random-platform.c'))
endif

