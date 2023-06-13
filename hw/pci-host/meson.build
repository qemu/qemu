pci_ss = ss.source_set()
pci_ss.add(when: 'CONFIG_PAM', if_true: files('pam.c'))
pci_ss.add(when: 'CONFIG_PCI_BONITO', if_true: files('bonito.c'))
pci_ss.add(when: 'CONFIG_GT64120', if_true: files('gt64120.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_DESIGNWARE', if_true: files('designware.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_GENERIC_BRIDGE', if_true: files('gpex.c'))
pci_ss.add(when: ['CONFIG_PCI_EXPRESS_GENERIC_BRIDGE', 'CONFIG_ACPI'], if_true: files('gpex-acpi.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_Q35', if_true: files('q35.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_XILINX', if_true: files('xilinx-pcie.c'))
pci_ss.add(when: 'CONFIG_PCI_I440FX', if_true: files('i440fx.c'))
pci_ss.add(when: 'CONFIG_PCI_SABRE', if_true: files('sabre.c'))
pci_ss.add(when: 'CONFIG_XEN_IGD_PASSTHROUGH', if_true: files('xen_igd_pt.c'))
pci_ss.add(when: 'CONFIG_REMOTE_PCIHOST', if_true: files('remote.c'))
pci_ss.add(when: 'CONFIG_SH_PCI', if_true: files('sh_pci.c'))

# PPC devices
pci_ss.add(when: 'CONFIG_RAVEN_PCI', if_true: files('raven.c'))
pci_ss.add(when: 'CONFIG_GRACKLE_PCI', if_true: files('grackle.c'))
# NewWorld PowerMac
pci_ss.add(when: 'CONFIG_UNIN_PCI', if_true: files('uninorth.c'))
# PowerPC E500 boards
pci_ss.add(when: 'CONFIG_PPCE500_PCI', if_true: files('ppce500.c'))
# Pegasos2
pci_ss.add(when: 'CONFIG_MV64361', if_true: files('mv64361.c'))

# ARM devices
pci_ss.add(when: 'CONFIG_VERSATILE_PCI', if_true: files('versatile.c'))

# HPPA devices
pci_ss.add(when: 'CONFIG_DINO', if_true: files('dino.c'))

system_ss.add_all(when: 'CONFIG_PCI', if_true: pci_ss)

specific_ss.add(when: 'CONFIG_PCI_POWERNV', if_true: files(
  'pnv_phb3.c',
  'pnv_phb3_msi.c',
  'pnv_phb3_pbcq.c',
  'pnv_phb4.c',
  'pnv_phb4_pec.c',
  'pnv_phb.c',
))
