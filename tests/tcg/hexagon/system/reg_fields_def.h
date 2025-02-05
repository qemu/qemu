/* PTE (aka TLB entry) fields */
DEF_REG_FIELD(PTE_PPD,
    "PPD", 0, 24,
    "Physical page number that the corresponding virtual page maps to.")
DEF_REG_FIELD(PTE_C,
    "C", 24, 4,
    "Cacheability attributes for the page.")
DEF_REG_FIELD(PTE_U,
    "U", 28, 1,
    "User mode permitted.")
DEF_REG_FIELD(PTE_R,
    "R", 29, 1,
    "Read-enable.")
DEF_REG_FIELD(PTE_W,
    "W", 30, 1,
    "Write-enable.")
DEF_REG_FIELD(PTE_X,
    "X", 31, 1,
    "Execute-enable.")
DEF_REG_FIELD(PTE_VPN,
    "VPN", 32, 20,
    "Virtual page number that is matched against the load or store address.")
DEF_REG_FIELD(PTE_ASID,
    "ASID", 52, 7,
    "7-bit address space identifier (tag extender)")
DEF_REG_FIELD(PTE_ATR0,
    "ATR0", 59, 1,
    "General purpose attribute bit kept as an attribute of each cache line.")
DEF_REG_FIELD(PTE_ATR1,
    "ATR1", 60, 1,
    "General purpose attribute bit kept as an attribute of each cache line.")
DEF_REG_FIELD(PTE_PA35,
    "PA35", 61, 1,
    "The Extra Physical bit is the most-significant physical address bit.")
DEF_REG_FIELD(PTE_G,
    "G", 62, 1,
    "Global bit. If set, then the ASID is ignored in the match.")
DEF_REG_FIELD(PTE_V,
    "V", 63, 1,
    "Valid bit. indicates whether this entry should be used for matching.")

/* SSR fields */
DEF_REG_FIELD(SSR_CAUSE,
    "cause", 0, 8,
    "8-bit field that contains the reason for various exception.")
DEF_REG_FIELD(SSR_ASID,
    "asid", 8, 7,
    "7-bit field that contains the Address Space Identifier.")
DEF_REG_FIELD(SSR_UM,
    "um", 16, 1,
    "read-write bit.")
DEF_REG_FIELD(SSR_EX,
    "ex", 17, 1,
    "set when an interrupt or exception is accepted.")
DEF_REG_FIELD(SSR_IE,
    "ie", 18, 1,
    "indicates whether the global interrupt is enabled.")
DEF_REG_FIELD(SSR_GM,
    "gm", 19, 1,
    "Guest mode bit.")
DEF_REG_FIELD(SSR_V0,
    "v0", 20, 1,
    "if BADVA0 register contents are from a valid slot 0 instruction.")
DEF_REG_FIELD(SSR_V1,
     "v1", 21, 1,
    "if BADVA1 register contents are from a valid slot 1 instruction.")
DEF_REG_FIELD(SSR_BVS,
    "bvs", 22, 1,
    "BADVA Selector.")
DEF_REG_FIELD(SSR_CE,
    "ce", 23, 1,
    "grants user or guest read permissions to the PCYCLE register aliases.")
DEF_REG_FIELD(SSR_PE,
    "pe", 24, 1,
    "grants guest read permissions to the PMU register aliases.")
DEF_REG_FIELD(SSR_BP,
    "bp", 25, 1,
    "Internal Bus Priority bit.")
DEF_REG_FIELD(SSR_XA,
    "xa", 27, 3,
    "Extension Active, which control operation of an attached coprocessor.")
DEF_REG_FIELD(SSR_SS,
    "ss", 30, 1,
    "Single Step, which enables single-step exceptions.")
DEF_REG_FIELD(SSR_XE,
    "xe", 31, 1,
    "Coprocessor Enable, which enables use of an attached coprocessor.")
