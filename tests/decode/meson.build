err_tests = [
    'err_argset1.decode',
    'err_argset2.decode',
    'err_field1.decode',
    'err_field2.decode',
    'err_field3.decode',
    'err_field4.decode',
    'err_field5.decode',
    'err_field6.decode',
    'err_field7.decode',
    'err_field8.decode',
    'err_field9.decode',
    'err_field10.decode',
    'err_init1.decode',
    'err_init2.decode',
    'err_init3.decode',
    'err_init4.decode',
    'err_overlap1.decode',
    'err_overlap2.decode',
    'err_overlap3.decode',
    'err_overlap4.decode',
    'err_overlap5.decode',
    'err_overlap6.decode',
    'err_overlap7.decode',
    'err_overlap8.decode',
    'err_overlap9.decode',
    'err_pattern_group_empty.decode',
    'err_pattern_group_ident1.decode',
    'err_pattern_group_ident2.decode',
    'err_pattern_group_nest1.decode',
    'err_pattern_group_nest2.decode',
    'err_pattern_group_nest3.decode',
    'err_pattern_group_overlap1.decode',
    'err_width1.decode',
    'err_width2.decode',
    'err_width3.decode',
    'err_width4.decode',
]

succ_tests = [
    'succ_argset_type1.decode',
    'succ_function.decode',
    'succ_ident1.decode',
    'succ_named_field.decode',
    'succ_pattern_group_nest1.decode',
    'succ_pattern_group_nest2.decode',
    'succ_pattern_group_nest3.decode',
    'succ_pattern_group_nest4.decode',
]

suite = 'decodetree'
decodetree = find_program(meson.project_source_root() / 'scripts/decodetree.py')

foreach t: err_tests
    test(fs.replace_suffix(t, ''),
         decodetree, args: ['--output-null', '--test-for-error', files(t)],
         suite: suite)
endforeach

foreach t: succ_tests
    test(fs.replace_suffix(t, ''),
         decodetree, args: ['--output-null', files(t)],
         suite: suite)
endforeach
