gen_features = executable('gen-features', 'gen-features.c', native: true,
                          build_by_default: false)

gen_features_h = custom_target('gen-features.h',
                               output: 'gen-features.h',
                               capture: true,
                               command: gen_features)

specific_ss.add(gen_features_h)
