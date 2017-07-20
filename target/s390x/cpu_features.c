/*
 * CPU features/facilities for s390x
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "cpu_features.h"
#include "gen-features.h"

#define FEAT_INIT(_name, _type, _bit, _desc) \
    {                                                \
        .name = _name,                               \
        .type = _type,                               \
        .bit = _bit,                                 \
        .desc = _desc,                               \
    }

/* indexed by feature number for easy lookup */
static const S390FeatDef s390_features[] = {
    FEAT_INIT("esan3", S390_FEAT_TYPE_STFL, 0, "Instructions marked as n3"),
    FEAT_INIT("zarch", S390_FEAT_TYPE_STFL, 1, "z/Architecture architectural mode"),
    FEAT_INIT("dateh", S390_FEAT_TYPE_STFL, 3, "DAT-enhancement facility"),
    FEAT_INIT("idtes", S390_FEAT_TYPE_STFL, 4, "IDTE selective TLB segment-table clearing"),
    FEAT_INIT("idter", S390_FEAT_TYPE_STFL, 5, "IDTE selective TLB region-table clearing"),
    FEAT_INIT("asnlxr", S390_FEAT_TYPE_STFL, 6, "ASN-and-LX reuse facility"),
    FEAT_INIT("stfle", S390_FEAT_TYPE_STFL, 7, "Store-facility-list-extended facility"),
    FEAT_INIT("edat", S390_FEAT_TYPE_STFL, 8, "Enhanced-DAT facility"),
    FEAT_INIT("srs", S390_FEAT_TYPE_STFL, 9, "Sense-running-status facility"),
    FEAT_INIT("csske", S390_FEAT_TYPE_STFL, 10, "Conditional-SSKE facility"),
    FEAT_INIT("ctop", S390_FEAT_TYPE_STFL, 11, "Configuration-topology facility"),
    FEAT_INIT("ipter", S390_FEAT_TYPE_STFL, 13, "IPTE-range facility"),
    FEAT_INIT("nonqks", S390_FEAT_TYPE_STFL, 14, "Nonquiescing key-setting facility"),
    FEAT_INIT("etf2", S390_FEAT_TYPE_STFL, 16, "Extended-translation facility 2"),
    FEAT_INIT("msa-base", S390_FEAT_TYPE_STFL, 17, "Message-security-assist facility (excluding subfunctions)"),
    FEAT_INIT("ldisp", S390_FEAT_TYPE_STFL, 18, "Long-displacement facility"),
    FEAT_INIT("ldisphp", S390_FEAT_TYPE_STFL, 19, "Long-displacement facility has high performance"),
    FEAT_INIT("hfpm", S390_FEAT_TYPE_STFL, 20, "HFP-multiply-add/subtract facility"),
    FEAT_INIT("eimm", S390_FEAT_TYPE_STFL, 21, "Extended-immediate facility"),
    FEAT_INIT("etf3", S390_FEAT_TYPE_STFL, 22, "Extended-translation facility 3"),
    FEAT_INIT("hfpue", S390_FEAT_TYPE_STFL, 23, "HFP-unnormalized-extension facility"),
    FEAT_INIT("etf2eh", S390_FEAT_TYPE_STFL, 24, "ETF2-enhancement facility"),
    FEAT_INIT("stckf", S390_FEAT_TYPE_STFL, 25, "Store-clock-fast facility"),
    FEAT_INIT("parseh", S390_FEAT_TYPE_STFL, 26, "Parsing-enhancement facility"),
    FEAT_INIT("mvcos", S390_FEAT_TYPE_STFL, 27, "Move-with-optional-specification facility"),
    FEAT_INIT("tods-base", S390_FEAT_TYPE_STFL, 28, "TOD-clock-steering facility (excluding subfunctions)"),
    FEAT_INIT("etf3eh", S390_FEAT_TYPE_STFL, 30, "ETF3-enhancement facility"),
    FEAT_INIT("ectg", S390_FEAT_TYPE_STFL, 31, "Extract-CPU-time facility"),
    FEAT_INIT("csst", S390_FEAT_TYPE_STFL, 32, "Compare-and-swap-and-store facility"),
    FEAT_INIT("csst2", S390_FEAT_TYPE_STFL, 33, "Compare-and-swap-and-store facility 2"),
    FEAT_INIT("ginste", S390_FEAT_TYPE_STFL, 34, "General-instructions-extension facility"),
    FEAT_INIT("exrl", S390_FEAT_TYPE_STFL, 35, "Execute-extensions facility"),
    FEAT_INIT("emon", S390_FEAT_TYPE_STFL, 36, "Enhanced-monitor facility"),
    FEAT_INIT("fpe", S390_FEAT_TYPE_STFL, 37, "Floating-point extension facility"),
    FEAT_INIT("opc", S390_FEAT_TYPE_STFL, 38, "Order Preserving Compression facility"),
    FEAT_INIT("sprogp", S390_FEAT_TYPE_STFL, 40, "Set-program-parameters facility"),
    FEAT_INIT("fpseh", S390_FEAT_TYPE_STFL, 41, "Floating-point-support-enhancement facilities"),
    FEAT_INIT("dfp", S390_FEAT_TYPE_STFL, 42, "DFP (decimal-floating-point) facility"),
    FEAT_INIT("dfphp", S390_FEAT_TYPE_STFL, 43, "DFP (decimal-floating-point) facility has high performance"),
    FEAT_INIT("pfpo", S390_FEAT_TYPE_STFL, 44, "PFPO instruction"),
    FEAT_INIT("stfle45", S390_FEAT_TYPE_STFL, 45, "Various facilities introduced with z196"),
    FEAT_INIT("cmpsceh", S390_FEAT_TYPE_STFL, 47, "CMPSC-enhancement facility"),
    FEAT_INIT("dfpzc", S390_FEAT_TYPE_STFL, 48, "Decimal-floating-point zoned-conversion facility"),
    FEAT_INIT("stfle49", S390_FEAT_TYPE_STFL, 49, "Various facilities introduced with zEC12"),
    FEAT_INIT("cte", S390_FEAT_TYPE_STFL, 50, "Constrained transactional-execution facility"),
    FEAT_INIT("ltlbc", S390_FEAT_TYPE_STFL, 51, "Local-TLB-clearing facility"),
    FEAT_INIT("iacc2", S390_FEAT_TYPE_STFL, 52, "Interlocked-access facility 2"),
    FEAT_INIT("stfle53", S390_FEAT_TYPE_STFL, 53, "Various facilities introduced with z13"),
    FEAT_INIT("eec", S390_FEAT_TYPE_STFL, 54, "Entropy encoding compression facility"),
    FEAT_INIT("msa5-base", S390_FEAT_TYPE_STFL, 57, "Message-security-assist-extension-5 facility (excluding subfunctions)"),
    FEAT_INIT("minste2", S390_FEAT_TYPE_STFL, 58, "Miscellaneous-instruction-extensions facility 2"),
    FEAT_INIT("sema", S390_FEAT_TYPE_STFL, 59, "Semaphore-assist facility"),
    FEAT_INIT("tsi", S390_FEAT_TYPE_STFL, 60, "Time-slice Instrumentation facility"),
    FEAT_INIT("ri", S390_FEAT_TYPE_STFL, 64, "CPU runtime-instrumentation facility"),
    FEAT_INIT("zpci", S390_FEAT_TYPE_STFL, 69, "z/PCI facility"),
    FEAT_INIT("aen", S390_FEAT_TYPE_STFL, 71, "General-purpose-adapter-event-notification facility"),
    FEAT_INIT("ais", S390_FEAT_TYPE_STFL, 72, "General-purpose-adapter-interruption-suppression facility"),
    FEAT_INIT("te", S390_FEAT_TYPE_STFL, 73, "Transactional-execution facility"),
    FEAT_INIT("sthyi", S390_FEAT_TYPE_STFL, 74, "Store-hypervisor-information facility"),
    FEAT_INIT("aefsi", S390_FEAT_TYPE_STFL, 75, "Access-exception-fetch/store-indication facility"),
    FEAT_INIT("msa3-base", S390_FEAT_TYPE_STFL, 76, "Message-security-assist-extension-3 facility (excluding subfunctions)"),
    FEAT_INIT("msa4-base", S390_FEAT_TYPE_STFL, 77, "Message-security-assist-extension-4 facility (excluding subfunctions)"),
    FEAT_INIT("edat2", S390_FEAT_TYPE_STFL, 78, "Enhanced-DAT facility 2"),
    FEAT_INIT("dfppc", S390_FEAT_TYPE_STFL, 80, "Decimal-floating-point packed-conversion facility"),
    FEAT_INIT("vx", S390_FEAT_TYPE_STFL, 129, "Vector facility"),
    FEAT_INIT("iep", S390_FEAT_TYPE_STFL, 130, "Instruction-execution-protection facility"),
    FEAT_INIT("sea_esop2", S390_FEAT_TYPE_STFL, 131, "Side-effect-access facility and Enhanced-suppression-on-protection facility 2"),
    FEAT_INIT("gs", S390_FEAT_TYPE_STFL, 133, "Guarded-storage facility"),
    FEAT_INIT("vxpd", S390_FEAT_TYPE_STFL, 134, "Vector packed decimal facility"),
    FEAT_INIT("vxeh", S390_FEAT_TYPE_STFL, 135, "Vector enhancements facility"),
    FEAT_INIT("mepoch", S390_FEAT_TYPE_STFL, 139, "Multiple-epoch facility"),
    FEAT_INIT("tpei", S390_FEAT_TYPE_STFL, 144, "Test-pending-external-interruption facility"),
    FEAT_INIT("irbm", S390_FEAT_TYPE_STFL, 145, "Insert-reference-bits-multiple facility"),
    FEAT_INIT("msa8-base", S390_FEAT_TYPE_STFL, 146, "Message-security-assist-extension-8 facility (excluding subfunctions)"),
    FEAT_INIT("cmmnt", S390_FEAT_TYPE_STFL, 147, "CMM: ESSA-enhancement (no translate) facility"),

    /* SCLP SCCB Byte 80 - 98  (bit numbers relative to byte-80) */
    FEAT_INIT("gsls", S390_FEAT_TYPE_SCLP_CONF_CHAR, 40, "SIE: Guest-storage-limit-suppression facility"),
    FEAT_INIT("esop", S390_FEAT_TYPE_SCLP_CONF_CHAR, 46, "Enhanced-suppression-on-protection facility"),
    FEAT_INIT("hpma2", S390_FEAT_TYPE_SCLP_CONF_CHAR, 90, "Host page management assist 2 Facility"), /* 91-2 */
    FEAT_INIT("kss", S390_FEAT_TYPE_SCLP_CONF_CHAR, 151, "SIE: Keyless-subset facility"),  /* 98-7 */

    /* SCLP SCCB Byte 116 - 119 (bit numbers relative to byte-116) */
    FEAT_INIT("64bscao", S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT, 0, "SIE: 64-bit-SCAO facility"),
    FEAT_INIT("cmma", S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT, 1, "SIE: Collaborative-memory-management assist"),
    FEAT_INIT("pfmfi", S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT, 9, "SIE: PFMF interpretation facility"),
    FEAT_INIT("ibs", S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT, 10, "SIE: Interlock-and-broadcast-suppression facility"),

    FEAT_INIT("sief2", S390_FEAT_TYPE_SCLP_CPU, 4, "SIE: interception format 2 (Virtual SIE)"),
    FEAT_INIT("skey", S390_FEAT_TYPE_SCLP_CPU, 5, "SIE: Storage-key facility"),
    FEAT_INIT("gpereh", S390_FEAT_TYPE_SCLP_CPU, 10, "SIE: Guest-PER enhancement facility"),
    FEAT_INIT("siif", S390_FEAT_TYPE_SCLP_CPU, 11, "SIE: Shared IPTE-interlock facility"),
    FEAT_INIT("sigpif", S390_FEAT_TYPE_SCLP_CPU, 12, "SIE: SIGP interpretation facility"),
    FEAT_INIT("ib", S390_FEAT_TYPE_SCLP_CPU, 42, "SIE: Intervention bypass facility"),
    FEAT_INIT("cei", S390_FEAT_TYPE_SCLP_CPU, 43, "SIE: Conditional-external-interception facility"),

    FEAT_INIT("dateh2", S390_FEAT_TYPE_MISC, 0, "DAT-enhancement facility 2"),
    FEAT_INIT("cmm", S390_FEAT_TYPE_MISC, 0, "Collaborative-memory-management facility"),

    FEAT_INIT("plo-cl", S390_FEAT_TYPE_PLO, 0, "PLO Compare and load (32 bit in general registers)"),
    FEAT_INIT("plo-clg", S390_FEAT_TYPE_PLO, 1, "PLO Compare and load (64 bit in parameter list)"),
    FEAT_INIT("plo-clgr", S390_FEAT_TYPE_PLO, 2, "PLO Compare and load (32 bit in general registers)"),
    FEAT_INIT("plo-clx", S390_FEAT_TYPE_PLO, 3, "PLO Compare and load (128 bit in parameter list)"),
    FEAT_INIT("plo-cs", S390_FEAT_TYPE_PLO, 4, "PLO Compare and swap (32 bit in general registers)"),
    FEAT_INIT("plo-csg", S390_FEAT_TYPE_PLO, 5, "PLO Compare and swap (64 bit in parameter list)"),
    FEAT_INIT("plo-csgr", S390_FEAT_TYPE_PLO, 6, "PLO Compare and swap (32 bit in general registers)"),
    FEAT_INIT("plo-csx", S390_FEAT_TYPE_PLO, 7, "PLO Compare and swap (128 bit in parameter list)"),
    FEAT_INIT("plo-dcs", S390_FEAT_TYPE_PLO, 8, "PLO Double compare and swap (32 bit in general registers)"),
    FEAT_INIT("plo-dcsg", S390_FEAT_TYPE_PLO, 9, "PLO Double compare and swap (64 bit in parameter list)"),
    FEAT_INIT("plo-dcsgr", S390_FEAT_TYPE_PLO, 10, "PLO Double compare and swap (32 bit in general registers)"),
    FEAT_INIT("plo-dcsx", S390_FEAT_TYPE_PLO, 11, "PLO Double compare and swap (128 bit in parameter list)"),
    FEAT_INIT("plo-csst", S390_FEAT_TYPE_PLO, 12, "PLO Compare and swap and store (32 bit in general registers)"),
    FEAT_INIT("plo-csstg", S390_FEAT_TYPE_PLO, 13, "PLO Compare and swap and store (64 bit in parameter list)"),
    FEAT_INIT("plo-csstgr", S390_FEAT_TYPE_PLO, 14, "PLO Compare and swap and store (32 bit in general registers)"),
    FEAT_INIT("plo-csstx", S390_FEAT_TYPE_PLO, 15, "PLO Compare and swap and store (128 bit in parameter list)"),
    FEAT_INIT("plo-csdst", S390_FEAT_TYPE_PLO, 16, "PLO Compare and swap and double store (32 bit in general registers)"),
    FEAT_INIT("plo-csdstg", S390_FEAT_TYPE_PLO, 17, "PLO Compare and swap and double store (64 bit in parameter list)"),
    FEAT_INIT("plo-csdstgr", S390_FEAT_TYPE_PLO, 18, "PLO Compare and swap and double store (32 bit in general registers)"),
    FEAT_INIT("plo-csdstx", S390_FEAT_TYPE_PLO, 19, "PLO Compare and swap and double store (128 bit in parameter list)"),
    FEAT_INIT("plo-cstst", S390_FEAT_TYPE_PLO, 20, "PLO Compare and swap and triple store (32 bit in general registers)"),
    FEAT_INIT("plo-cststg", S390_FEAT_TYPE_PLO, 21, "PLO Compare and swap and triple store (64 bit in parameter list)"),
    FEAT_INIT("plo-cststgr", S390_FEAT_TYPE_PLO, 22, "PLO Compare and swap and triple store (32 bit in general registers)"),
    FEAT_INIT("plo-cststx", S390_FEAT_TYPE_PLO, 23, "PLO Compare and swap and triple store (128 bit in parameter list)"),

    FEAT_INIT("ptff-qto", S390_FEAT_TYPE_PTFF, 1, "PTFF Query TOD Offset"),
    FEAT_INIT("ptff-qsi", S390_FEAT_TYPE_PTFF, 2, "PTFF Query Steering Information"),
    FEAT_INIT("ptff-qpc", S390_FEAT_TYPE_PTFF, 3, "PTFF Query Physical Clock"),
    FEAT_INIT("ptff-qui", S390_FEAT_TYPE_PTFF, 4, "PTFF Query UTC Information"),
    FEAT_INIT("ptff-qtou", S390_FEAT_TYPE_PTFF, 5, "PTFF Query TOD Offset User"),
    FEAT_INIT("ptff-sto", S390_FEAT_TYPE_PTFF, 65, "PTFF Set TOD Offset"),
    FEAT_INIT("ptff-stou", S390_FEAT_TYPE_PTFF, 69, "PTFF Set TOD Offset User"),

    FEAT_INIT("kmac-dea", S390_FEAT_TYPE_KMAC, 1, "KMAC DEA"),
    FEAT_INIT("kmac-tdea-128", S390_FEAT_TYPE_KMAC, 2, "KMAC TDEA-128"),
    FEAT_INIT("kmac-tdea-192", S390_FEAT_TYPE_KMAC, 3, "KMAC TDEA-192"),
    FEAT_INIT("kmac-edea", S390_FEAT_TYPE_KMAC, 9, "KMAC Encrypted-DEA"),
    FEAT_INIT("kmac-etdea-128", S390_FEAT_TYPE_KMAC, 10, "KMAC Encrypted-TDEA-128"),
    FEAT_INIT("kmac-etdea-192", S390_FEAT_TYPE_KMAC, 11, "KMAC Encrypted-TDEA-192"),
    FEAT_INIT("kmac-aes-128", S390_FEAT_TYPE_KMAC, 18, "KMAC AES-128"),
    FEAT_INIT("kmac-aes-192", S390_FEAT_TYPE_KMAC, 19, "KMAC AES-192"),
    FEAT_INIT("kmac-aes-256", S390_FEAT_TYPE_KMAC, 20, "KMAC AES-256"),
    FEAT_INIT("kmac-eaes-128", S390_FEAT_TYPE_KMAC, 26, "KMAC Encrypted-AES-128"),
    FEAT_INIT("kmac-eaes-192", S390_FEAT_TYPE_KMAC, 27, "KMAC Encrypted-AES-192"),
    FEAT_INIT("kmac-eaes-256", S390_FEAT_TYPE_KMAC, 28, "KMAC Encrypted-AES-256"),

    FEAT_INIT("kmc-dea", S390_FEAT_TYPE_KMC, 1, "KMC DEA"),
    FEAT_INIT("kmc-tdea-128", S390_FEAT_TYPE_KMC, 2, "KMC TDEA-128"),
    FEAT_INIT("kmc-tdea-192", S390_FEAT_TYPE_KMC, 3, "KMC TDEA-192"),
    FEAT_INIT("kmc-edea", S390_FEAT_TYPE_KMC, 9, "KMC Encrypted-DEA"),
    FEAT_INIT("kmc-etdea-128", S390_FEAT_TYPE_KMC, 10, "KMC Encrypted-TDEA-128"),
    FEAT_INIT("kmc-etdea-192", S390_FEAT_TYPE_KMC, 11, "KMC Encrypted-TDEA-192"),
    FEAT_INIT("kmc-aes-128", S390_FEAT_TYPE_KMC, 18, "KMC AES-128"),
    FEAT_INIT("kmc-aes-192", S390_FEAT_TYPE_KMC, 19, "KMC AES-192"),
    FEAT_INIT("kmc-aes-256", S390_FEAT_TYPE_KMC, 20, "KMC AES-256"),
    FEAT_INIT("kmc-eaes-128", S390_FEAT_TYPE_KMC, 26, "KMC Encrypted-AES-128"),
    FEAT_INIT("kmc-eaes-192", S390_FEAT_TYPE_KMC, 27, "KMC Encrypted-AES-192"),
    FEAT_INIT("kmc-eaes-256", S390_FEAT_TYPE_KMC, 28, "KMC Encrypted-AES-256"),
    FEAT_INIT("kmc-prng", S390_FEAT_TYPE_KMC, 67, "KMC PRNG"),

    FEAT_INIT("km-dea", S390_FEAT_TYPE_KM, 1, "KM DEA"),
    FEAT_INIT("km-tdea-128", S390_FEAT_TYPE_KM, 2, "KM TDEA-128"),
    FEAT_INIT("km-tdea-192", S390_FEAT_TYPE_KM, 3, "KM TDEA-192"),
    FEAT_INIT("km-edea", S390_FEAT_TYPE_KM, 9, "KM Encrypted-DEA"),
    FEAT_INIT("km-etdea-128", S390_FEAT_TYPE_KM, 10, "KM Encrypted-TDEA-128"),
    FEAT_INIT("km-etdea-192", S390_FEAT_TYPE_KM, 11, "KM Encrypted-TDEA-192"),
    FEAT_INIT("km-aes-128", S390_FEAT_TYPE_KM, 18, "KM AES-128"),
    FEAT_INIT("km-aes-192", S390_FEAT_TYPE_KM, 19, "KM AES-192"),
    FEAT_INIT("km-aes-256", S390_FEAT_TYPE_KM, 20, "KM AES-256"),
    FEAT_INIT("km-eaes-128", S390_FEAT_TYPE_KM, 26, "KM Encrypted-AES-128"),
    FEAT_INIT("km-eaes-192", S390_FEAT_TYPE_KM, 27, "KM Encrypted-AES-192"),
    FEAT_INIT("km-eaes-256", S390_FEAT_TYPE_KM, 28, "KM Encrypted-AES-256"),
    FEAT_INIT("km-xts-aes-128", S390_FEAT_TYPE_KM, 50, "KM XTS-AES-128"),
    FEAT_INIT("km-xts-aes-256", S390_FEAT_TYPE_KM, 52, "KM XTS-AES-256"),
    FEAT_INIT("km-xts-eaes-128", S390_FEAT_TYPE_KM, 58, "KM XTS-Encrypted-AES-128"),
    FEAT_INIT("km-xts-eaes-256", S390_FEAT_TYPE_KM, 60, "KM XTS-Encrypted-AES-256"),

    FEAT_INIT("kimd-sha-1", S390_FEAT_TYPE_KIMD, 1, "KIMD SHA-1"),
    FEAT_INIT("kimd-sha-256", S390_FEAT_TYPE_KIMD, 2, "KIMD SHA-256"),
    FEAT_INIT("kimd-sha-512", S390_FEAT_TYPE_KIMD, 3, "KIMD SHA-512"),
    FEAT_INIT("kimd-sha3-224", S390_FEAT_TYPE_KIMD, 32, "KIMD SHA3-224"),
    FEAT_INIT("kimd-sha3-256", S390_FEAT_TYPE_KIMD, 33, "KIMD SHA3-256"),
    FEAT_INIT("kimd-sha3-384", S390_FEAT_TYPE_KIMD, 34, "KIMD SHA3-384"),
    FEAT_INIT("kimd-sha3-512", S390_FEAT_TYPE_KIMD, 35, "KIMD SHA3-512"),
    FEAT_INIT("kimd-shake-128", S390_FEAT_TYPE_KIMD, 36, "KIMD SHAKE-128"),
    FEAT_INIT("kimd-shake-256", S390_FEAT_TYPE_KIMD, 37, "KIMD SHAKE-256"),
    FEAT_INIT("kimd-ghash", S390_FEAT_TYPE_KIMD, 65, "KIMD GHASH"),

    FEAT_INIT("klmd-sha-1", S390_FEAT_TYPE_KLMD, 1, "KLMD SHA-1"),
    FEAT_INIT("klmd-sha-256", S390_FEAT_TYPE_KLMD, 2, "KLMD SHA-256"),
    FEAT_INIT("klmd-sha-512", S390_FEAT_TYPE_KLMD, 3, "KLMD SHA-512"),
    FEAT_INIT("klmd-sha3-224", S390_FEAT_TYPE_KLMD, 32, "KLMD SHA3-224"),
    FEAT_INIT("klmd-sha3-256", S390_FEAT_TYPE_KLMD, 33, "KLMD SHA3-256"),
    FEAT_INIT("klmd-sha3-384", S390_FEAT_TYPE_KLMD, 34, "KLMD SHA3-384"),
    FEAT_INIT("klmd-sha3-512", S390_FEAT_TYPE_KLMD, 35, "KLMD SHA3-512"),
    FEAT_INIT("klmd-shake-128", S390_FEAT_TYPE_KLMD, 36, "KLMD SHAKE-128"),
    FEAT_INIT("klmd-shake-256", S390_FEAT_TYPE_KLMD, 37, "KLMD SHAKE-256"),

    FEAT_INIT("pckmo-edea", S390_FEAT_TYPE_PCKMO, 1, "PCKMO Encrypted-DEA-Key"),
    FEAT_INIT("pckmo-etdea-128", S390_FEAT_TYPE_PCKMO, 2, "PCKMO Encrypted-TDEA-128-Key"),
    FEAT_INIT("pckmo-etdea-192", S390_FEAT_TYPE_PCKMO, 3, "PCKMO Encrypted-TDEA-192-Key"),
    FEAT_INIT("pckmo-aes-128", S390_FEAT_TYPE_PCKMO, 18, "PCKMO Encrypted-AES-128-Key"),
    FEAT_INIT("pckmo-aes-192", S390_FEAT_TYPE_PCKMO, 19, "PCKMO Encrypted-AES-192-Key"),
    FEAT_INIT("pckmo-aes-256", S390_FEAT_TYPE_PCKMO, 20, "PCKMO Encrypted-AES-256-Key"),

    FEAT_INIT("kmctr-dea", S390_FEAT_TYPE_KMCTR, 1, "KMCTR DEA"),
    FEAT_INIT("kmctr-tdea-128", S390_FEAT_TYPE_KMCTR, 2, "KMCTR TDEA-128"),
    FEAT_INIT("kmctr-tdea-192", S390_FEAT_TYPE_KMCTR, 3, "KMCTR TDEA-192"),
    FEAT_INIT("kmctr-edea", S390_FEAT_TYPE_KMCTR, 9, "KMCTR Encrypted-DEA"),
    FEAT_INIT("kmctr-etdea-128", S390_FEAT_TYPE_KMCTR, 10, "KMCTR Encrypted-TDEA-128"),
    FEAT_INIT("kmctr-etdea-192", S390_FEAT_TYPE_KMCTR, 11, "KMCTR Encrypted-TDEA-192"),
    FEAT_INIT("kmctr-aes-128", S390_FEAT_TYPE_KMCTR, 18, "KMCTR AES-128"),
    FEAT_INIT("kmctr-aes-192", S390_FEAT_TYPE_KMCTR, 19, "KMCTR AES-192"),
    FEAT_INIT("kmctr-aes-256", S390_FEAT_TYPE_KMCTR, 20, "KMCTR AES-256"),
    FEAT_INIT("kmctr-eaes-128", S390_FEAT_TYPE_KMCTR, 26, "KMCTR Encrypted-AES-128"),
    FEAT_INIT("kmctr-eaes-192", S390_FEAT_TYPE_KMCTR, 27, "KMCTR Encrypted-AES-192"),
    FEAT_INIT("kmctr-eaes-256", S390_FEAT_TYPE_KMCTR, 28, "KMCTR Encrypted-AES-256"),

    FEAT_INIT("kmf-dea", S390_FEAT_TYPE_KMF, 1, "KMF DEA"),
    FEAT_INIT("kmf-tdea-128", S390_FEAT_TYPE_KMF, 2, "KMF TDEA-128"),
    FEAT_INIT("kmf-tdea-192", S390_FEAT_TYPE_KMF, 3, "KMF TDEA-192"),
    FEAT_INIT("kmf-edea", S390_FEAT_TYPE_KMF, 9, "KMF Encrypted-DEA"),
    FEAT_INIT("kmf-etdea-128", S390_FEAT_TYPE_KMF, 10, "KMF Encrypted-TDEA-128"),
    FEAT_INIT("kmf-etdea-192", S390_FEAT_TYPE_KMF, 11, "KMF Encrypted-TDEA-192"),
    FEAT_INIT("kmf-aes-128", S390_FEAT_TYPE_KMF, 18, "KMF AES-128"),
    FEAT_INIT("kmf-aes-192", S390_FEAT_TYPE_KMF, 19, "KMF AES-192"),
    FEAT_INIT("kmf-aes-256", S390_FEAT_TYPE_KMF, 20, "KMF AES-256"),
    FEAT_INIT("kmf-eaes-128", S390_FEAT_TYPE_KMF, 26, "KMF Encrypted-AES-128"),
    FEAT_INIT("kmf-eaes-192", S390_FEAT_TYPE_KMF, 27, "KMF Encrypted-AES-192"),
    FEAT_INIT("kmf-eaes-256", S390_FEAT_TYPE_KMF, 28, "KMF Encrypted-AES-256"),

    FEAT_INIT("kmo-dea", S390_FEAT_TYPE_KMO, 1, "KMO DEA"),
    FEAT_INIT("kmo-tdea-128", S390_FEAT_TYPE_KMO, 2, "KMO TDEA-128"),
    FEAT_INIT("kmo-tdea-192", S390_FEAT_TYPE_KMO, 3, "KMO TDEA-192"),
    FEAT_INIT("kmo-edea", S390_FEAT_TYPE_KMO, 9, "KMO Encrypted-DEA"),
    FEAT_INIT("kmo-etdea-128", S390_FEAT_TYPE_KMO, 10, "KMO Encrypted-TDEA-128"),
    FEAT_INIT("kmo-etdea-192", S390_FEAT_TYPE_KMO, 11, "KMO Encrypted-TDEA-192"),
    FEAT_INIT("kmo-aes-128", S390_FEAT_TYPE_KMO, 18, "KMO AES-128"),
    FEAT_INIT("kmo-aes-192", S390_FEAT_TYPE_KMO, 19, "KMO AES-192"),
    FEAT_INIT("kmo-aes-256", S390_FEAT_TYPE_KMO, 20, "KMO AES-256"),
    FEAT_INIT("kmo-eaes-128", S390_FEAT_TYPE_KMO, 26, "KMO Encrypted-AES-128"),
    FEAT_INIT("kmo-eaes-192", S390_FEAT_TYPE_KMO, 27, "KMO Encrypted-AES-192"),
    FEAT_INIT("kmo-eaes-256", S390_FEAT_TYPE_KMO, 28, "KMO Encrypted-AES-256"),

    FEAT_INIT("pcc-cmac-dea", S390_FEAT_TYPE_PCC, 1, "PCC Compute-Last-Block-CMAC-Using-DEA"),
    FEAT_INIT("pcc-cmac-tdea-128", S390_FEAT_TYPE_PCC, 2, "PCC Compute-Last-Block-CMAC-Using-TDEA-128"),
    FEAT_INIT("pcc-cmac-tdea-192", S390_FEAT_TYPE_PCC, 3, "PCC Compute-Last-Block-CMAC-Using-TDEA-192"),
    FEAT_INIT("pcc-cmac-edea", S390_FEAT_TYPE_PCC, 9, "PCC Compute-Last-Block-CMAC-Using-Encrypted-DEA"),
    FEAT_INIT("pcc-cmac-etdea-128", S390_FEAT_TYPE_PCC, 10, "PCC Compute-Last-Block-CMAC-Using-Encrypted-TDEA-128"),
    FEAT_INIT("pcc-cmac-etdea-192", S390_FEAT_TYPE_PCC, 11, "PCC Compute-Last-Block-CMAC-Using-EncryptedTDEA-192"),
    FEAT_INIT("pcc-cmac-aes-128", S390_FEAT_TYPE_PCC, 18, "PCC Compute-Last-Block-CMAC-Using-AES-128"),
    FEAT_INIT("pcc-cmac-aes-192", S390_FEAT_TYPE_PCC, 19, "PCC Compute-Last-Block-CMAC-Using-AES-192"),
    FEAT_INIT("pcc-cmac-eaes-256", S390_FEAT_TYPE_PCC, 20, "PCC Compute-Last-Block-CMAC-Using-AES-256"),
    FEAT_INIT("pcc-cmac-eaes-128", S390_FEAT_TYPE_PCC, 26, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-128"),
    FEAT_INIT("pcc-cmac-eaes-192", S390_FEAT_TYPE_PCC, 27, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-192"),
    FEAT_INIT("pcc-cmac-eaes-256", S390_FEAT_TYPE_PCC, 28, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-256"),
    FEAT_INIT("pcc-xts-aes-128", S390_FEAT_TYPE_PCC, 50, "PCC Compute-XTS-Parameter-Using-AES-128"),
    FEAT_INIT("pcc-xts-aes-256", S390_FEAT_TYPE_PCC, 52, "PCC Compute-XTS-Parameter-Using-AES-256"),
    FEAT_INIT("pcc-xts-eaes-128", S390_FEAT_TYPE_PCC, 58, "PCC Compute-XTS-Parameter-Using-Encrypted-AES-128"),
    FEAT_INIT("pcc-xts-eaes-256", S390_FEAT_TYPE_PCC, 60, "PCC Compute-XTS-Parameter-Using-Encrypted-AES-256"),

    FEAT_INIT("ppno-sha-512-drng", S390_FEAT_TYPE_PPNO, 3, "PPNO SHA-512-DRNG"),
    FEAT_INIT("prno-trng-qrtcr", S390_FEAT_TYPE_PPNO, 112, "PRNO TRNG-Query-Raw-to-Conditioned-Ratio"),
    FEAT_INIT("prno-trng", S390_FEAT_TYPE_PPNO, 114, "PRNO TRNG"),

    FEAT_INIT("kma-gcm-aes-128", S390_FEAT_TYPE_KMA, 18, "KMA GCM-AES-128"),
    FEAT_INIT("kma-gcm-aes-192", S390_FEAT_TYPE_KMA, 19, "KMA GCM-AES-192"),
    FEAT_INIT("kma-gcm-aes-256", S390_FEAT_TYPE_KMA, 20, "KMA GCM-AES-256"),
    FEAT_INIT("kma-gcm-eaes-128", S390_FEAT_TYPE_KMA, 26, "KMA GCM-Encrypted-AES-128"),
    FEAT_INIT("kma-gcm-eaes-192", S390_FEAT_TYPE_KMA, 27, "KMA GCM-Encrypted-AES-192"),
    FEAT_INIT("kma-gcm-eaes-256", S390_FEAT_TYPE_KMA, 28, "KMA GCM-Encrypted-AES-256"),
};

const S390FeatDef *s390_feat_def(S390Feat feat)
{
    return &s390_features[feat];
}

S390Feat s390_feat_by_type_and_bit(S390FeatType type, int bit)
{
    S390Feat feat;

    for (feat = 0; feat < ARRAY_SIZE(s390_features); feat++) {
        if (s390_features[feat].type == type &&
            s390_features[feat].bit == bit) {
            return feat;
        }
    }
    return S390_FEAT_MAX;
}

void s390_init_feat_bitmap(const S390FeatInit init, S390FeatBitmap bitmap)
{
    int i, j;

    for (i = 0; i < (S390_FEAT_MAX / 64 + 1); i++) {
        if (init[i]) {
            for (j = 0; j < 64; j++) {
                if (init[i] & 1ULL << j) {
                    set_bit(i * 64 + j, bitmap);
                }
            }
        }
    }
}

void s390_fill_feat_block(const S390FeatBitmap features, S390FeatType type,
                          uint8_t *data)
{
    S390Feat feat;
    int bit_nr;

    switch (type) {
    case S390_FEAT_TYPE_STFL:
        if (test_bit(S390_FEAT_ZARCH, features)) {
            /* Features that are always active */
            set_be_bit(2, data);   /* z/Architecture */
            set_be_bit(138, data); /* Configuration-z-architectural-mode */
        }
        break;
    case S390_FEAT_TYPE_PTFF:
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KMC:
    case S390_FEAT_TYPE_KM:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_KMCTR:
    case S390_FEAT_TYPE_KMF:
    case S390_FEAT_TYPE_KMO:
    case S390_FEAT_TYPE_PCC:
    case S390_FEAT_TYPE_PPNO:
    case S390_FEAT_TYPE_KMA:
        set_be_bit(0, data); /* query is always available */
        break;
    default:
        break;
    };

    feat = find_first_bit(features, S390_FEAT_MAX);
    while (feat < S390_FEAT_MAX) {
        if (s390_features[feat].type == type) {
            bit_nr = s390_features[feat].bit;
            /* big endian on uint8_t array */
            set_be_bit(bit_nr, data);
        }
        feat = find_next_bit(features, S390_FEAT_MAX, feat + 1);
    }
}

void s390_add_from_feat_block(S390FeatBitmap features, S390FeatType type,
                              uint8_t *data)
{
    int nr_bits, le_bit;

    switch (type) {
    case S390_FEAT_TYPE_STFL:
       nr_bits = 2048;
       break;
    case S390_FEAT_TYPE_PLO:
       nr_bits = 256;
       break;
    default:
       /* all cpu subfunctions have 128 bit */
       nr_bits = 128;
    };

    le_bit = find_first_bit((unsigned long *) data, nr_bits);
    while (le_bit < nr_bits) {
        /* convert the bit number to a big endian bit nr */
        S390Feat feat = s390_feat_by_type_and_bit(type, BE_BIT_NR(le_bit));
        /* ignore unknown bits */
        if (feat < S390_FEAT_MAX) {
            set_bit(feat, features);
        }
        le_bit = find_next_bit((unsigned long *) data, nr_bits, le_bit + 1);
    }
}

void s390_feat_bitmap_to_ascii(const S390FeatBitmap features, void *opaque,
                               void (*fn)(const char *name, void *opaque))
{
    S390FeatBitmap bitmap, tmp;
    S390FeatGroup group;
    S390Feat feat;

    bitmap_copy(bitmap, features, S390_FEAT_MAX);

    /* process whole groups first */
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);

        bitmap_and(tmp, bitmap, def->feat, S390_FEAT_MAX);
        if (bitmap_equal(tmp, def->feat, S390_FEAT_MAX)) {
            bitmap_andnot(bitmap, bitmap, def->feat, S390_FEAT_MAX);
            fn(def->name, opaque);
        }
    }

    /* report leftovers as separate features */
    feat = find_first_bit(bitmap, S390_FEAT_MAX);
    while (feat < S390_FEAT_MAX) {
        fn(s390_feat_def(feat)->name, opaque);
        feat = find_next_bit(bitmap, S390_FEAT_MAX, feat + 1);
    };
}

#define FEAT_GROUP_INIT(_name, _group, _desc)        \
    {                                                \
        .name = _name,                               \
        .desc = _desc,                               \
        .init = { S390_FEAT_GROUP_LIST_ ## _group }, \
    }

/* indexed by feature group number for easy lookup */
static S390FeatGroupDef s390_feature_groups[] = {
    FEAT_GROUP_INIT("plo", PLO, "Perform-locked-operation facility"),
    FEAT_GROUP_INIT("tods", TOD_CLOCK_STEERING, "Tod-clock-steering facility"),
    FEAT_GROUP_INIT("gen13ptff", GEN13_PTFF, "PTFF enhancements introduced with z13"),
    FEAT_GROUP_INIT("msa", MSA, "Message-security-assist facility"),
    FEAT_GROUP_INIT("msa1", MSA_EXT_1, "Message-security-assist-extension 1 facility"),
    FEAT_GROUP_INIT("msa2", MSA_EXT_2, "Message-security-assist-extension 2 facility"),
    FEAT_GROUP_INIT("msa3", MSA_EXT_3, "Message-security-assist-extension 3 facility"),
    FEAT_GROUP_INIT("msa4", MSA_EXT_4, "Message-security-assist-extension 4 facility"),
    FEAT_GROUP_INIT("msa5", MSA_EXT_5, "Message-security-assist-extension 5 facility"),
    FEAT_GROUP_INIT("msa6", MSA_EXT_6, "Message-security-assist-extension 6 facility"),
    FEAT_GROUP_INIT("msa7", MSA_EXT_7, "Message-security-assist-extension 7 facility"),
    FEAT_GROUP_INIT("msa8", MSA_EXT_8, "Message-security-assist-extension 8 facility"),
};

const S390FeatGroupDef *s390_feat_group_def(S390FeatGroup group)
{
    return &s390_feature_groups[group];
}

static void init_groups(void)
{
    int i;

    /* init all bitmaps from gnerated data initially */
    for (i = 0; i < ARRAY_SIZE(s390_feature_groups); i++) {
        s390_init_feat_bitmap(s390_feature_groups[i].init,
                              s390_feature_groups[i].feat);
    }
}

type_init(init_groups)
