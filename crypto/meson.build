crypto_ss = ss.source_set()
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

if 'CONFIG_GCRYPT' in config_host
  wo_nettle = files('hash-gcrypt.c', 'pbkdf-gcrypt.c')
else
  wo_nettle = files('hash-glib.c', 'pbkdf-stub.c')
endif
if 'CONFIG_GCRYPT_HMAC' not in config_host
  wo_nettle += files('hmac-glib.c')
endif
crypto_ss.add(when: [nettle, 'CONFIG_NETTLE'],
             if_true: files('hash-nettle.c', 'hmac-nettle.c', 'pbkdf-nettle.c'),
             if_false: wo_nettle)

crypto_ss.add(when: 'CONFIG_SECRET_KEYRING', if_true: files('secret_keyring.c'))
crypto_ss.add(when: 'CONFIG_QEMU_PRIVATE_XTS', if_true: files('xts.c'))
crypto_ss.add(when: 'CONFIG_GCRYPT_HMAC', if_true: files('hmac-gcrypt.c'))
crypto_ss.add(when: 'CONFIG_AF_ALG', if_true: files('afalg.c', 'cipher-afalg.c', 'hash-afalg.c'))
crypto_ss.add(when: 'CONFIG_GNUTLS', if_true: files('tls-cipher-suites.c'))

crypto_ss = crypto_ss.apply(config_host, strict: false)
libcrypto = static_library('crypto', crypto_ss.sources() + genh,
                           dependencies: [crypto_ss.dependencies()],
                           name_suffix: 'fa',
                           build_by_default: false)

crypto = declare_dependency(link_whole: libcrypto,
                            dependencies: [authz, qom])

util_ss.add(files('aes.c'))
util_ss.add(files('init.c'))
if 'CONFIG_GCRYPT' in config_host
  util_ss.add(files('random-gcrypt.c'))
elif 'CONFIG_GNUTLS' in config_host
  util_ss.add(files('random-gnutls.c'), gnutls)
elif 'CONFIG_RNG_NONE' in config_host
  util_ss.add(files('random-none.c'))
else
  util_ss.add(files('random-platform.c'))
endif
