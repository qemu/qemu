/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Test programs for various Xen versions that QEMU supports.  */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION == 41100
  #undef XC_WANT_COMPAT_DEVICEMODEL_API
  #define __XEN_TOOLS__
  #include <xendevicemodel.h>
  #include <xenforeignmemory.h>
  int main(void) {
    xendevicemodel_handle *xd;
    xenforeignmemory_handle *xfmem;

    xd = xendevicemodel_open(0, 0);
    xendevicemodel_pin_memory_cacheattr(xd, 0, 0, 0, 0);

    xfmem = xenforeignmemory_open(0, 0);
    xenforeignmemory_map_resource(xfmem, 0, 0, 0, 0, 0, NULL, 0, 0);

    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 41000
  #undef XC_WANT_COMPAT_MAP_FOREIGN_API
  #include <xenforeignmemory.h>
  #include <xentoolcore.h>
  int main(void) {
    xenforeignmemory_handle *xfmem;

    xfmem = xenforeignmemory_open(0, 0);
    xenforeignmemory_map2(xfmem, 0, 0, 0, 0, 0, 0, 0);
    xentoolcore_restrict_all(0);

    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40900
  #undef XC_WANT_COMPAT_DEVICEMODEL_API
  #define __XEN_TOOLS__
  #include <xendevicemodel.h>
  int main(void) {
    xendevicemodel_handle *xd;

    xd = xendevicemodel_open(0, 0);
    xendevicemodel_close(xd);

    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40800
  /*
   * If we have stable libs the we don't want the libxc compat
   * layers, regardless of what CFLAGS we may have been given.
   *
   * Also, check if xengnttab_grant_copy_segment_t is defined and
   * grant copy operation is implemented.
   */
  #undef XC_WANT_COMPAT_EVTCHN_API
  #undef XC_WANT_COMPAT_GNTTAB_API
  #undef XC_WANT_COMPAT_MAP_FOREIGN_API
  #include <xenctrl.h>
  #include <xenstore.h>
  #include <xenevtchn.h>
  #include <xengnttab.h>
  #include <xenforeignmemory.h>
  #include <stdint.h>
  #include <xen/hvm/hvm_info_table.h>
  #if !defined(HVM_MAX_VCPUS)
  # error HVM_MAX_VCPUS not defined
  #endif
  int main(void) {
    xc_interface *xc = NULL;
    xenforeignmemory_handle *xfmem;
    xenevtchn_handle *xe;
    xengnttab_handle *xg;
    xengnttab_grant_copy_segment_t* seg = NULL;

    xs_daemon_open();

    xc = xc_interface_open(0, 0, 0);
    xc_hvm_set_mem_type(0, 0, HVMMEM_ram_ro, 0, 0);
    xc_domain_add_to_physmap(0, 0, XENMAPSPACE_gmfn, 0, 0);
    xc_hvm_inject_msi(xc, 0, 0xf0000000, 0x00000000);
    xc_hvm_create_ioreq_server(xc, 0, HVM_IOREQSRV_BUFIOREQ_ATOMIC, NULL);

    xfmem = xenforeignmemory_open(0, 0);
    xenforeignmemory_map(xfmem, 0, 0, 0, 0, 0);

    xe = xenevtchn_open(0, 0);
    xenevtchn_fd(xe);

    xg = xengnttab_open(0, 0);
    xengnttab_grant_copy(xg, 0, seg);

    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40701
  /*
   * If we have stable libs the we don't want the libxc compat
   * layers, regardless of what CFLAGS we may have been given.
   */
  #undef XC_WANT_COMPAT_EVTCHN_API
  #undef XC_WANT_COMPAT_GNTTAB_API
  #undef XC_WANT_COMPAT_MAP_FOREIGN_API
  #include <xenctrl.h>
  #include <xenstore.h>
  #include <xenevtchn.h>
  #include <xengnttab.h>
  #include <xenforeignmemory.h>
  #include <stdint.h>
  #include <xen/hvm/hvm_info_table.h>
  #if !defined(HVM_MAX_VCPUS)
  # error HVM_MAX_VCPUS not defined
  #endif
  int main(void) {
    xc_interface *xc = NULL;
    xenforeignmemory_handle *xfmem;
    xenevtchn_handle *xe;
    xengnttab_handle *xg;

    xs_daemon_open();

    xc = xc_interface_open(0, 0, 0);
    xc_hvm_set_mem_type(0, 0, HVMMEM_ram_ro, 0, 0);
    xc_domain_add_to_physmap(0, 0, XENMAPSPACE_gmfn, 0, 0);
    xc_hvm_inject_msi(xc, 0, 0xf0000000, 0x00000000);
    xc_hvm_create_ioreq_server(xc, 0, HVM_IOREQSRV_BUFIOREQ_ATOMIC, NULL);

    xfmem = xenforeignmemory_open(0, 0);
    xenforeignmemory_map(xfmem, 0, 0, 0, 0, 0);

    xe = xenevtchn_open(0, 0);
    xenevtchn_fd(xe);

    xg = xengnttab_open(0, 0);
    xengnttab_map_grant_ref(xg, 0, 0, 0);

    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40600
  #include <xenctrl.h>
  #include <xenstore.h>
  #include <stdint.h>
  #include <xen/hvm/hvm_info_table.h>
  #if !defined(HVM_MAX_VCPUS)
  # error HVM_MAX_VCPUS not defined
  #endif
  int main(void) {
    xc_interface *xc;
    xs_daemon_open();
    xc = xc_interface_open(0, 0, 0);
    xc_hvm_set_mem_type(0, 0, HVMMEM_ram_ro, 0, 0);
    xc_gnttab_open(NULL, 0);
    xc_domain_add_to_physmap(0, 0, XENMAPSPACE_gmfn, 0, 0);
    xc_hvm_inject_msi(xc, 0, 0xf0000000, 0x00000000);
    xc_hvm_create_ioreq_server(xc, 0, HVM_IOREQSRV_BUFIOREQ_ATOMIC, NULL);
    xc_reserved_device_memory_map(xc, 0, 0, 0, 0, NULL, 0);
    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40500
  #include <xenctrl.h>
  #include <xenstore.h>
  #include <stdint.h>
  #include <xen/hvm/hvm_info_table.h>
  #if !defined(HVM_MAX_VCPUS)
  # error HVM_MAX_VCPUS not defined
  #endif
  int main(void) {
    xc_interface *xc;
    xs_daemon_open();
    xc = xc_interface_open(0, 0, 0);
    xc_hvm_set_mem_type(0, 0, HVMMEM_ram_ro, 0, 0);
    xc_gnttab_open(NULL, 0);
    xc_domain_add_to_physmap(0, 0, XENMAPSPACE_gmfn, 0, 0);
    xc_hvm_inject_msi(xc, 0, 0xf0000000, 0x00000000);
    xc_hvm_create_ioreq_server(xc, 0, 0, NULL);
    return 0;
  }

#elif CONFIG_XEN_CTRL_INTERFACE_VERSION == 40200
  #include <xenctrl.h>
  #include <xenstore.h>
  #include <stdint.h>
  #include <xen/hvm/hvm_info_table.h>
  #if !defined(HVM_MAX_VCPUS)
  # error HVM_MAX_VCPUS not defined
  #endif
  int main(void) {
    xc_interface *xc;
    xs_daemon_open();
    xc = xc_interface_open(0, 0, 0);
    xc_hvm_set_mem_type(0, 0, HVMMEM_ram_ro, 0, 0);
    xc_gnttab_open(NULL, 0);
    xc_domain_add_to_physmap(0, 0, XENMAPSPACE_gmfn, 0, 0);
    xc_hvm_inject_msi(xc, 0, 0xf0000000, 0x00000000);
    return 0;
  }

#else
#error invalid CONFIG_XEN_CTRL_INTERFACE_VERSION
#endif
