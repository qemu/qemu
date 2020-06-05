/*
 * We adjust linker script modification to place all of the stuff that needs to
 * persist across fuzzing runs into a contiguous section of memory. Then, it is
 * easy to re-map the counter-related memory as shared.
 */

SECTIONS
{
  .data.fuzz_start : ALIGN(4K)
  {
      __FUZZ_COUNTERS_START = .;
      __start___sancov_cntrs = .;
      *(_*sancov_cntrs);
      __stop___sancov_cntrs = .;

      /* Lowest stack counter */
      *(__sancov_lowest_stack);
  }
  .data.fuzz_ordered :
  {
      /*
       * Coverage counters. They're not necessary for fuzzing, but are useful
       * for analyzing the fuzzing performance
       */
      __start___llvm_prf_cnts = .;
      *(*llvm_prf_cnts);
      __stop___llvm_prf_cnts = .;

      /* Internal Libfuzzer TracePC object which contains the ValueProfileMap */
      FuzzerTracePC*(.bss*);
      /*
       * In case the above line fails, explicitly specify the (mangled) name of
       * the object we care about
       */
       *(.bss._ZN6fuzzer3TPCE);
  }
  .data.fuzz_end : ALIGN(4K)
  {
      __FUZZ_COUNTERS_END = .;
  }
}
/*
 * Don't overwrite the SECTIONS in the default linker script. Instead insert the
 * above into the default script
 */
INSERT AFTER .data;
