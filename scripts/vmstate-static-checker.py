#!/usr/bin/python
#
# Compares vmstate information stored in JSON format, obtained from
# the -dump-vmstate QEMU command.
#
# Copyright 2014 Amit Shah <amit.shah@redhat.com>
# Copyright 2014 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.

import argparse
import json
import sys

# Count the number of errors found
taint = 0

def bump_taint():
    global taint

    # Ensure we don't wrap around or reset to 0 -- the shell only has
    # an 8-bit return value.
    if taint < 255:
        taint = taint + 1


def check_fields_match(name, s_field, d_field):
    if s_field == d_field:
        return True

    # Some fields changed names between qemu versions.  This list
    # is used to whitelist such changes in each section / description.
    changed_names = {
        'apic': ['timer', 'timer_expiry'],
        'e1000': ['dev', 'parent_obj'],
        'ehci': ['dev', 'pcidev'],
        'I440FX': ['dev', 'parent_obj'],
        'ich9_ahci': ['card', 'parent_obj'],
        'ich9-ahci': ['ahci', 'ich9_ahci'],
        'ioh3420': ['PCIDevice', 'PCIEDevice'],
        'ioh-3240-express-root-port': ['port.br.dev',
                                       'parent_obj.parent_obj.parent_obj',
                                       'port.br.dev.exp.aer_log',
                                'parent_obj.parent_obj.parent_obj.exp.aer_log'],
        'cirrus_vga': ['hw_cursor_x', 'vga.hw_cursor_x',
                       'hw_cursor_y', 'vga.hw_cursor_y'],
        'lsiscsi': ['dev', 'parent_obj'],
        'mch': ['d', 'parent_obj'],
        'pci_bridge': ['bridge.dev', 'parent_obj', 'bridge.dev.shpc', 'shpc'],
        'pcnet': ['pci_dev', 'parent_obj'],
        'PIIX3': ['pci_irq_levels', 'pci_irq_levels_vmstate'],
        'piix4_pm': ['dev', 'parent_obj', 'pci0_status',
                     'acpi_pci_hotplug.acpi_pcihp_pci_status[0x0]',
                     'pm1a.sts', 'ar.pm1.evt.sts', 'pm1a.en', 'ar.pm1.evt.en',
                     'pm1_cnt.cnt', 'ar.pm1.cnt.cnt',
                     'tmr.timer', 'ar.tmr.timer',
                     'tmr.overflow_time', 'ar.tmr.overflow_time',
                     'gpe', 'ar.gpe'],
        'rtl8139': ['dev', 'parent_obj'],
        'qxl': ['num_surfaces', 'ssd.num_surfaces'],
        'usb-ccid': ['abProtocolDataStructure', 'abProtocolDataStructure.data'],
        'usb-host': ['dev', 'parent_obj'],
        'usb-mouse': ['usb-ptr-queue', 'HIDPointerEventQueue'],
        'usb-tablet': ['usb-ptr-queue', 'HIDPointerEventQueue'],
        'vmware_vga': ['card', 'parent_obj'],
        'vmware_vga_internal': ['depth', 'new_depth'],
        'xhci': ['pci_dev', 'parent_obj'],
        'x3130-upstream': ['PCIDevice', 'PCIEDevice'],
        'xio3130-express-downstream-port': ['port.br.dev',
                                            'parent_obj.parent_obj.parent_obj',
                                            'port.br.dev.exp.aer_log',
                                'parent_obj.parent_obj.parent_obj.exp.aer_log'],
        'xio3130-downstream': ['PCIDevice', 'PCIEDevice'],
        'xio3130-express-upstream-port': ['br.dev', 'parent_obj.parent_obj',
                                          'br.dev.exp.aer_log',
                                          'parent_obj.parent_obj.exp.aer_log'],
    }

    if not name in changed_names:
        return False

    if s_field in changed_names[name] and d_field in changed_names[name]:
        return True

    return False

def get_changed_sec_name(sec):
    # Section names can change -- see commit 292b1634 for an example.
    changes = {
        "ICH9 LPC": "ICH9-LPC",
        "e1000-82540em": "e1000",
    }

    for item in changes:
        if item == sec:
            return changes[item]
        if changes[item] == sec:
            return item
    return ""

def exists_in_substruct(fields, item):
    # Some QEMU versions moved a few fields inside a substruct.  This
    # kept the on-wire format the same.  This function checks if
    # something got shifted inside a substruct.  For example, the
    # change in commit 1f42d22233b4f3d1a2933ff30e8d6a6d9ee2d08f

    if not "Description" in fields:
        return False

    if not "Fields" in fields["Description"]:
        return False

    substruct_fields = fields["Description"]["Fields"]

    if substruct_fields == []:
        return False

    return check_fields_match(fields["Description"]["name"],
                              substruct_fields[0]["field"], item)


def check_fields(src_fields, dest_fields, desc, sec):
    # This function checks for all the fields in a section.  If some
    # fields got embedded into a substruct, this function will also
    # attempt to check inside the substruct.

    d_iter = iter(dest_fields)
    s_iter = iter(src_fields)

    # Using these lists as stacks to store previous value of s_iter
    # and d_iter, so that when time comes to exit out of a substruct,
    # we can go back one level up and continue from where we left off.

    s_iter_list = []
    d_iter_list = []

    advance_src = True
    advance_dest = True
    unused_count = 0

    while True:
        if advance_src:
            try:
                s_item = s_iter.next()
            except StopIteration:
                if s_iter_list == []:
                    break

                s_iter = s_iter_list.pop()
                continue
        else:
            if unused_count == 0:
                # We want to avoid advancing just once -- when entering a
                # dest substruct, or when exiting one.
                advance_src = True

        if advance_dest:
            try:
                d_item = d_iter.next()
            except StopIteration:
                if d_iter_list == []:
                    # We were not in a substruct
                    print "Section \"" + sec + "\",",
                    print "Description " + "\"" + desc + "\":",
                    print "expected field \"" + s_item["field"] + "\",",
                    print "while dest has no further fields"
                    bump_taint()
                    break

                d_iter = d_iter_list.pop()
                advance_src = False
                continue
        else:
            if unused_count == 0:
                advance_dest = True

        if unused_count > 0:
            if advance_dest == False:
                unused_count = unused_count - s_item["size"]
                if unused_count == 0:
                    advance_dest = True
                    continue
                if unused_count < 0:
                    print "Section \"" + sec + "\",",
                    print "Description \"" + desc + "\":",
                    print "unused size mismatch near \"",
                    print s_item["field"] + "\""
                    bump_taint()
                    break
                continue

            if advance_src == False:
                unused_count = unused_count - d_item["size"]
                if unused_count == 0:
                    advance_src = True
                    continue
                if unused_count < 0:
                    print "Section \"" + sec + "\",",
                    print "Description \"" + desc + "\":",
                    print "unused size mismatch near \"",
                    print d_item["field"] + "\""
                    bump_taint()
                    break
                continue

        if not check_fields_match(desc, s_item["field"], d_item["field"]):
            # Some fields were put in substructs, keeping the
            # on-wire format the same, but breaking static tools
            # like this one.

            # First, check if dest has a new substruct.
            if exists_in_substruct(d_item, s_item["field"]):
                # listiterators don't have a prev() function, so we
                # have to store our current location, descend into the
                # substruct, and ensure we come out as if nothing
                # happened when the substruct is over.
                #
                # Essentially we're opening the substructs that got
                # added which didn't change the wire format.
                d_iter_list.append(d_iter)
                substruct_fields = d_item["Description"]["Fields"]
                d_iter = iter(substruct_fields)
                advance_src = False
                continue

            # Next, check if src has substruct that dest removed
            # (can happen in backward migration: 2.0 -> 1.5)
            if exists_in_substruct(s_item, d_item["field"]):
                s_iter_list.append(s_iter)
                substruct_fields = s_item["Description"]["Fields"]
                s_iter = iter(substruct_fields)
                advance_dest = False
                continue

            if s_item["field"] == "unused" or d_item["field"] == "unused":
                if s_item["size"] == d_item["size"]:
                    continue

                if d_item["field"] == "unused":
                    advance_dest = False
                    unused_count = d_item["size"] - s_item["size"]
                    continue

                if s_item["field"] == "unused":
                    advance_src = False
                    unused_count = s_item["size"] - d_item["size"]
                    continue

            print "Section \"" + sec + "\",",
            print "Description \"" + desc + "\":",
            print "expected field \"" + s_item["field"] + "\",",
            print "got \"" + d_item["field"] + "\"; skipping rest"
            bump_taint()
            break

        check_version(s_item, d_item, sec, desc)

        if not "Description" in s_item:
            # Check size of this field only if it's not a VMSTRUCT entry
            check_size(s_item, d_item, sec, desc, s_item["field"])

        check_description_in_list(s_item, d_item, sec, desc)


def check_subsections(src_sub, dest_sub, desc, sec):
    for s_item in src_sub:
        found = False
        for d_item in dest_sub:
            if s_item["name"] != d_item["name"]:
                continue

            found = True
            check_descriptions(s_item, d_item, sec)

        if not found:
            print "Section \"" + sec + "\", Description \"" + desc + "\":",
            print "Subsection \"" + s_item["name"] + "\" not found"
            bump_taint()


def check_description_in_list(s_item, d_item, sec, desc):
    if not "Description" in s_item:
        return

    if not "Description" in d_item:
        print "Section \"" + sec + "\", Description \"" + desc + "\",",
        print "Field \"" + s_item["field"] + "\": missing description"
        bump_taint()
        return

    check_descriptions(s_item["Description"], d_item["Description"], sec)


def check_descriptions(src_desc, dest_desc, sec):
    check_version(src_desc, dest_desc, sec, src_desc["name"])

    if not check_fields_match(sec, src_desc["name"], dest_desc["name"]):
        print "Section \"" + sec + "\":",
        print "Description \"" + src_desc["name"] + "\"",
        print "missing, got \"" + dest_desc["name"] + "\" instead; skipping"
        bump_taint()
        return

    for f in src_desc:
        if not f in dest_desc:
            print "Section \"" + sec + "\"",
            print "Description \"" + src_desc["name"] + "\":",
            print "Entry \"" + f + "\" missing"
            bump_taint()
            continue

        if f == 'Fields':
            check_fields(src_desc[f], dest_desc[f], src_desc["name"], sec)

        if f == 'Subsections':
            check_subsections(src_desc[f], dest_desc[f], src_desc["name"], sec)


def check_version(s, d, sec, desc=None):
    if s["version_id"] > d["version_id"]:
        print "Section \"" + sec + "\"",
        if desc:
            print "Description \"" + desc + "\":",
        print "version error:", s["version_id"], ">", d["version_id"]
        bump_taint()

    if not "minimum_version_id" in d:
        return

    if s["version_id"] < d["minimum_version_id"]:
        print "Section \"" + sec + "\"",
        if desc:
            print "Description \"" + desc + "\":",
            print "minimum version error:", s["version_id"], "<",
            print d["minimum_version_id"]
            bump_taint()


def check_size(s, d, sec, desc=None, field=None):
    if s["size"] != d["size"]:
        print "Section \"" + sec + "\"",
        if desc:
            print "Description \"" + desc + "\"",
        if field:
            print "Field \"" + field + "\"",
        print "size mismatch:", s["size"], ",", d["size"]
        bump_taint()


def check_machine_type(s, d):
    if s["Name"] != d["Name"]:
        print "Warning: checking incompatible machine types:",
        print "\"" + s["Name"] + "\", \"" + d["Name"] + "\""
    return


def main():
    help_text = "Parse JSON-formatted vmstate dumps from QEMU in files SRC and DEST.  Checks whether migration from SRC to DEST QEMU versions would break based on the VMSTATE information contained within the JSON outputs.  The JSON output is created from a QEMU invocation with the -dump-vmstate parameter and a filename argument to it.  Other parameters to QEMU do not matter, except the -M (machine type) parameter."

    parser = argparse.ArgumentParser(description=help_text)
    parser.add_argument('-s', '--src', type=file, required=True,
                        help='json dump from src qemu')
    parser.add_argument('-d', '--dest', type=file, required=True,
                        help='json dump from dest qemu')
    parser.add_argument('--reverse', required=False, default=False,
                        action='store_true',
                        help='reverse the direction')
    args = parser.parse_args()

    src_data = json.load(args.src)
    dest_data = json.load(args.dest)
    args.src.close()
    args.dest.close()

    if args.reverse:
        temp = src_data
        src_data = dest_data
        dest_data = temp

    for sec in src_data:
        dest_sec = sec
        if not dest_sec in dest_data:
            # Either the section name got changed, or the section
            # doesn't exist in dest.
            dest_sec = get_changed_sec_name(sec)
            if not dest_sec in dest_data:
                print "Section \"" + sec + "\" does not exist in dest"
                bump_taint()
                continue

        s = src_data[sec]
        d = dest_data[dest_sec]

        if sec == "vmschkmachine":
            check_machine_type(s, d)
            continue

        check_version(s, d, sec)

        for entry in s:
            if not entry in d:
                print "Section \"" + sec + "\": Entry \"" + entry + "\"",
                print "missing"
                bump_taint()
                continue

            if entry == "Description":
                check_descriptions(s[entry], d[entry], sec)

    return taint


if __name__ == '__main__':
    sys.exit(main())
