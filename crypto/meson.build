crypto_ss.add(genh)
crypto_ss.add(files(
  'afsplit.c',
  'block-luks.c',
  'block-qcow.c',
  'block.c',
  'cipher.c',
  'desrfb.c',
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
))

if 'CONFIG_NETTLE' in config_host
  crypto_ss.add(files('hash-nettle.c', 'hmac-nettle.c', 'pbkdf-nettle.c'))
elif 'CONFIG_GCRYPT' in config_host
  crypto_ss.add(files('hash-gcrypt.c', 'pbkdf-gcrypt.c'))
  if 'CONFIG_GCRYPT_HMAC' in config_host
    crypto_ss.add(files('hmac-gcrypt.c'))
  else
    crypto_ss.add(files('hmac-glib.c'))
  endif
else
  crypto_ss.add(files('hash-glib.c', 'hmac-glib.c', 'pbkdf-stub.c'))
endif

crypto_ss.add(when: 'CONFIG_SECRET_KEYRING', if_true: files('secret_keyring.c'))
crypto_ss.add(when: 'CONFIG_QEMU_PRIVATE_XTS', if_true: files('xts.c'))
crypto_ss.add(when: 'CONFIG_AF_ALG', if_true: files('afalg.c', 'cipher-afalg.c', 'hash-afalg.c'))
crypto_ss.add(when: 'CONFIG_GNUTLS', if_true: files('tls-cipher-suites.c'))

if 'CONFIG_NETTLE' in config_host
  crypto_ss.add(nettle)
elif 'CONFIG_GCRYPT' in config_host
  crypto_ss.add(gcrypt)
endif

if 'CONFIG_GNUTLS' in config_host
  crypto_ss.add(gnutls)
endif


util_ss.add(files('aes.c'))
util_ss.add(files('init.c'))

if 'CONFIG_GCRYPT' in config_host
  util_ss.add(files('random-gcrypt.c'))
elif 'CONFIG_GNUTLS' in config_host
  util_ss.add(files('random-gnutls.c'))
elif 'CONFIG_RNG_NONE' in config_host
  util_ss.add(files('random-none.c'))
else
  util_ss.add(files('random-platform.c'))
endif

if 'CONFIG_GCRYPT' in config_host
  util_ss.add(gcrypt)
endif

if 'CONFIG_GNUTLS' in config_host
  util_ss.add(gnutls)
endif
