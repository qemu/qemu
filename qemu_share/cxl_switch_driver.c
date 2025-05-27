/**
	This file implements the CXL switch driver for Linux kernel.

	References: 
		https://www.kernel.org/doc/html/next/PCI/pci.html
		https://github.com/ysan/qemu-edu-driver/blob/main/driver/qemuedu.c
		@author Jotham Wong
*/

#include "linux/cdev.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/mm.h"
#include "linux/mm_types.h"
#include "linux/mod_devicetable.h"
#include "linux/node.h"
#include "linux/pci.h"
#include "linux/slab.h"
#include "linux/types.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cs5250"); // Replace with your
MODULE_DESCRIPTION("A sample kernel module");

#define DRIVER_NAME "cxl_switch_client"
#define DRIVER_VERSION "0.1"
#define DEVICE_NAME "cxl_switch_client"

#define CXL_VENDOR_ID 0x1AF4
#define CXL_DEVICE_ID 0x1337

// Support just one device instance at a time
#define MAX_DEVICES 1

static int device_count = 0;

// Page offsets for mmap
#define MMAP_OFFSET_PGOFF_BAR0 0
#define MMAP_OFFSET_PGOFF_BAR1 1
#define MMAP_OFFSET_PGOFF_BAR2 2

/* Per-device data structure */
struct cxl_switch_client_dev {
	struct pci_dev *pdev;

    // Bar 0 (Management mailbox)
	void __iomem *bar0_kva; // kernel virtual address
	resource_size_t bar0_start;
	resource_size_t bar0_len;

    // Bar 1 (Control registers)
    void __iomem *bar1_kva;
    resource_size_t bar1_start;
    resource_size_t bar1_len;

    // Bar 2 (Data window)
    void __iomem *bar2_kva;
    resource_size_t bar2_start;
    resource_size_t bar2_len;

	dev_t devt;              // major/minor number
	struct cdev c_dev;       // character device structure
	struct class *dev_class; // For automatic /dev node creation
    struct device *device;   // For device_create
};

static struct cxl_switch_client_dev *cxl_switch_devs[MAX_DEVICES];

/* File operations */

static int cxl_switch_client_open(struct inode *inode, struct file *filp)
{
	struct cxl_switch_client_dev *dev;

	dev = container_of(inode->i_cdev, struct cxl_switch_client_dev, c_dev);
	filp->private_data = dev;

	pr_info("%s: Opened device %s\n", DRIVER_NAME, pci_name(dev->pdev));
	return 0;
}

static int cxl_switch_client_release(struct inode *inode, struct file *filp)
{
	struct cxl_switch_client_dev *dev = filp->private_data;

	pr_info("%s: Closed device %s\n", DRIVER_NAME, pci_name(dev->pdev));
	return 0;
}

// Map device's BAR into user-space process's address space
// This function is called when user-space process calls mmap on the device file
// for a selected bar
static int cxl_switch_client_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct cxl_switch_client_dev *dev = filp->private_data;
    unsigned long offset_in_bar = 0;
    unsigned long user_mmap_pgoff = vma->vm_pgoff;  // Page offset from user mmap
    resource_size_t bar_phys_start, bar_len;
    int selected_bar_idx, ret;

    // The offset from the user-space mmap determines which BAR is selected
    // Any offset within the BAR itself should be handled by the userspace
    // This can be done by mmap ptr + ptr_offset
    // For io_remap_pfn_range, phys addr is start of BAR
    if (user_mmap_pgoff == MMAP_OFFSET_PGOFF_BAR0) {
        selected_bar_idx = 0;
        bar_phys_start = dev->bar0_start;
        bar_len = dev->bar0_len;
        pr_info("%s: Mapping BAR0 for %s\n", DRIVER_NAME, pci_name(dev->pdev));
    } else if (user_mmap_pgoff == MMAP_OFFSET_PGOFF_BAR1) {
        selected_bar_idx = 1;
        bar_phys_start = dev->bar1_start;
        bar_len = dev->bar1_len;
        pr_info("%s: Mapping BAR1 for %s\n", DRIVER_NAME, pci_name(dev->pdev));
    } else if (user_mmap_pgoff == MMAP_OFFSET_PGOFF_BAR2) {
        selected_bar_idx = 2;
        bar_phys_start = dev->bar2_start;
        bar_len = dev->bar2_len;
        pr_info("%s: Mapping BAR2 for %s\n", DRIVER_NAME, pci_name(dev->pdev));
    } else {
        pr_err("%s: Invalid mmap offset %lu\n", DRIVER_NAME, user_mmap_pgoff);
        return -EINVAL;
    }

    if (bar_len == 0) {
        pr_err("%s: BAR%d is not enabled or has zero length\n", DRIVER_NAME, selected_bar_idx);
        return -ENODEV;
    }

    unsigned long vsize = vma->vm_end - vma->vm_start;
    pr_info("%s: vma start=0x%lx, end=0x%lx, size=0x%lx\n", 
            DRIVER_NAME, vma->vm_start, vma->vm_end, vsize);
    
    if ((offset_in_bar + vsize) > bar_len) {
        pr_err("%s: mmap failed, requested size exceeds BAR%d size\n", DRIVER_NAME, selected_bar_idx);
        return -EINVAL;
    }

    // do not cache page
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	// prevent touching page for swap in and swap out
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP | VM_PFNMAP);

    // Physical address for io_remap_pfn_range is start of BAR + internal offset
    unsigned long phys_addr_for_remap = bar_phys_start + offset_in_bar;
	// make MMIO accessible to user-space
	ret = io_remap_pfn_range(vma, vma->vm_start, phys_addr_for_remap >> PAGE_SHIFT, vsize, vma->vm_page_prot);
	
	if (ret) {
		pr_err("%s: mmap failed, error=%d\n", DRIVER_NAME, ret);
		return ret;
	}

    pr_info("%s: Successfully mapped BAR%d (phys addr 0x%lx, size 0x%lx) to user space.\n",
            DRIVER_NAME, selected_bar_idx, phys_addr_for_remap, vsize);
	return 0;
}

static const struct file_operations cxl_switch_client_fops = {
	.owner = THIS_MODULE,
	.open = cxl_switch_client_open,
	.release = cxl_switch_client_release,
	.mmap = cxl_switch_client_mmap,
};

/* PCI Driver */

// Helper function to probe and map a single BAR
static int probe_bar(struct cxl_switch_client_dev *dev, int bar_idx,
                     resource_size_t *bar_start, resource_size_t *bar_len,
                     void __iomem **bar_kva, const char* bar_name)
{
    struct pci_dev *pdev = dev->pdev;
    int ret;

    *bar_start = pci_resource_start(pdev, bar_idx);
    *bar_len = pci_resource_len(pdev, bar_idx);

    if (!*bar_start || !*bar_len) {
        pr_err("%s: Failed to get %s resource\n", DRIVER_NAME, bar_name);
        return -ENODEV;
    }
    pr_info("%s: %s mapped at guest_phys 0x%llx, len 0x%llx for %s.\n",
            DRIVER_NAME, bar_name, (unsigned long long)*bar_start, (unsigned long long)*bar_len, pci_name(pdev));
    ret = pci_request_region(pdev, bar_idx, DRIVER_NAME);
    if (ret) {
        pr_err("%s: Failed to request %s region, error=%d\n", DRIVER_NAME, bar_name, ret);
        return ret;
    }

    *bar_kva = pcim_iomap(pdev, bar_idx, *bar_len);
    if (!(*bar_kva)) {
        pr_err("%s: Failed to map %s, error=%d\n", DRIVER_NAME, bar_name, ret);
        pci_release_region(pdev, bar_idx);
        return -EIO;
    }
    pr_info("%s: %s for %s mapped to kernel virtual address %p\n", 
            DRIVER_NAME, bar_name, pci_name(pdev), *bar_kva);
    return 0;
}

static int cxl_switch_client_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct cxl_switch_client_dev *dev;
	int ret;
	int current_dev_idx = 0;

	pr_info("%s: Probing PCI device %04x:%04x\n", DRIVER_NAME, pdev->vendor, pdev->device);

	// We probably do not need to support more than one for our set up
	// since each VM only connects to one device
	// So we can simplify the development here
	if (device_count >= MAX_DEVICES) {
		pr_err("%s: Maximum device count reached\n", DRIVER_NAME);
		return -ENODEV;
	}

	current_dev_idx = device_count;

	dev = kzalloc(sizeof(struct cxl_switch_client_dev), GFP_KERNEL);
	if (!dev) {
		pr_err("%s: Failed to allocate memory for device\n", DRIVER_NAME);
		return -ENOMEM;
	}
	dev->pdev = pdev;

	// 1. Enable the PCI device
	ret = pci_enable_device(pdev);
	if (ret) {
		pr_err("%s: Failed to enable PCI device, error=%d\n", DRIVER_NAME, ret);
		kfree(dev);
		return ret;
	}

	// 2. Request MMIO/IOP resources
	//    Probe each bar with helper function
    
    ret = probe_bar(dev, 0, &dev->bar0_start, &dev->bar0_len, &dev->bar0_kva, "BAR0 Mailbox");
    if (ret) goto err_disable_device;

    ret = probe_bar(dev, 1, &dev->bar1_start, &dev->bar1_len, &dev->bar1_kva, "BAR1 Control");
    if (ret) goto err_release_bar0;

    ret = probe_bar(dev, 2, &dev->bar2_start, &dev->bar2_len, &dev->bar2_kva, "BAR2 Data");
    if (ret) goto err_release_bar1;

	// 3. Set DMA ... (not applicable for now?)
	// 4. Allocate and init shared control data space (N/A?)
	// 5. Access device configuration space (N/A?)
	// 6. Register IRQ handler (N/A?)

	// 7. Initialize character device to talk to cxl switch PCI device

	ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("%s: Failed to allocate char device number, error=%d\n", DRIVER_NAME, ret);
		goto err_release_bar2;
	}

	cdev_init(&dev->c_dev, &cxl_switch_client_fops);
	dev->c_dev.owner = THIS_MODULE;
	ret = cdev_add(&dev->c_dev, dev->devt, 1);
	if (ret) {
		pr_err("%s: Failed to add cdev, error=%d\n", DRIVER_NAME, ret);
		goto err_unregister_char_dev;
	}

	dev->dev_class = class_create(DEVICE_NAME);
	if (IS_ERR(dev->dev_class)) {
		pr_err("%s: Failed to create class for %s, error=%ld\n", DRIVER_NAME, pci_name(dev->pdev), PTR_ERR(dev->dev_class));
		ret = PTR_ERR(dev->dev_class);
		goto err_cdev_del;
	}

    // Creates device node /dev/cxl_switch_client0
    dev->device = device_create(dev->dev_class, &pdev->dev, dev->devt, NULL, "%s%d", DEVICE_NAME, current_dev_idx);
    if (IS_ERR(dev->device)) {
        pr_err("%s: Failed to create device node for %s\n", DRIVER_NAME, pci_name(dev->pdev));
        ret = -ENODEV;
        goto err_class_destroy;

    }

	pci_set_drvdata(pdev, dev);
	cxl_switch_devs[current_dev_idx] = dev;
	device_count++;
	pr_info("%s: Device %s registered with major %d, minor %d\n", DRIVER_NAME, pci_name(dev->pdev), MAJOR(dev->devt), MINOR(dev->devt));
	return 0;

err_class_destroy:
	class_destroy(dev->dev_class);
err_cdev_del:
	cdev_del(&dev->c_dev);
err_unregister_char_dev:
	unregister_chrdev_region(dev->devt, 1);
err_release_bar2:
    if (dev->bar2_kva) pci_iounmap(pdev, dev->bar2_kva);
    if (dev->bar2_len) pci_release_region(pdev, 2);
err_release_bar1:
    if (dev->bar1_kva) pcim_iounmap(pdev, dev->bar1_kva);
    if (dev->bar1_len) pci_release_region(pdev, 1);
err_release_bar0:
    if (dev->bar0_kva) pcim_iounmap(pdev, dev->bar0_kva);
    if (dev->bar0_len) pci_release_region(pdev, 0);
err_disable_device:
	pci_disable_device(pdev);
	kfree(dev);
	return ret;
}

static void cxl_switch_client_pci_remove(struct pci_dev *pdev)
{
	struct cxl_switch_client_dev *dev = pci_get_drvdata(pdev);
	int i;

	pr_info("%s: Removing PCI device %s (VID: %04x, DID: %04x)\n", DRIVER_NAME, pci_name(pdev), pdev->vendor, pdev->device);

	if (!dev) {
		pr_err("%s: Device data not found for %s\n", DRIVER_NAME, pci_name(pdev));
		return;
	}

	// Cleanup in reverse order of probe
    if (dev->device) device_destroy(dev->dev_class, dev->devt);
	if (dev->dev_class) class_destroy(dev->dev_class);
	cdev_del(&dev->c_dev);
	unregister_chrdev_region(dev->devt, 1);

    if (dev->bar2_kva) pcim_iounmap(pdev, dev->bar2_kva);
    if (dev->bar2_len) pci_release_region(pdev, 2);
    if (dev->bar1_kva) pcim_iounmap(pdev, dev->bar1_kva);
    if (dev->bar1_len) pci_release_region(pdev, 1);
    if (dev->bar0_kva) pcim_iounmap(pdev, dev->bar0_kva);
    if (dev->bar0_len) pci_release_region(pdev, 0);

    pci_disable_device(pdev);
	// Primitive tracking
	for (i = 0; i < MAX_DEVICES; ++i) {
		if (cxl_switch_devs[i] == dev) {
			cxl_switch_devs[i] = NULL;
            		device_count--;
            		break;
        	}
   	}
	kfree(dev);
	pci_set_drvdata(pdev, NULL);

    pr_info("%s: Device %s removed successfully\n", DRIVER_NAME, pci_name(pdev));
}

// PCI ID Table: What hardware is supported
static const struct pci_device_id cxl_switch_client_ids[] = {
	{ PCI_DEVICE(CXL_VENDOR_ID, CXL_DEVICE_ID) },
	{ 0, }
};
// Exposes IDs to user-space and module loading tools
MODULE_DEVICE_TABLE(pci, cxl_switch_client_ids);

// PCI driver structure
static struct pci_driver cxl_switch_client_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = cxl_switch_client_ids,
	.probe = cxl_switch_client_pci_probe,
	.remove = cxl_switch_client_pci_remove,
};

/* Module init/exit */

static int __init cxl_switch_init_module(void)
{
	pr_info("%s: Initializing CXL Switch Driver\n", DRIVER_NAME);
	return pci_register_driver(&cxl_switch_client_pci_driver);
}

static void __exit cxl_switch_exit_module(void)
{
	pr_info("%s: Exiting CXL Switch Driver\n", DRIVER_NAME);
	pci_unregister_driver(&cxl_switch_client_pci_driver);	
}

module_init(cxl_switch_init_module);
module_exit(cxl_switch_exit_module);

MODULE_LICENSE("GPL v2"); // SPDX-License-Identifier requires "GPL v2" or similar for GPL-2.0
MODULE_AUTHOR("Jotham Wong");
MODULE_DESCRIPTION("Basic Linux driver for CXL Replicated Switch (BAR2 mmap)");
