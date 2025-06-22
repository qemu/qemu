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
#include <asm-generic/ioctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/eventfd.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fdtable.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jotham Wong"); // Replace with your
MODULE_DESCRIPTION("CXL Switch driver");

#define DRIVER_NAME "cxl_switch_client"
#define DRIVER_VERSION "0.1"
#define DEVICE_NAME "cxl_switch_client"

#define CXL_VENDOR_ID 0x1AF4
#define CXL_DEVICE_ID 0x1337

// BAR1 Control Register offsets (from QEMU device cxl-switch-client.c)
#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS   0x04
#define REG_NOTIF_STATUS     0x08
#define REG_INTERRUPT_MASK   0x0C
#define REG_INTERRUPT_STATUS 0x10

// Support just one device instance at a time
#define MAX_DEVICES 1

static int device_count = 0;

// Page offsets for mmap
#define MMAP_OFFSET_PGOFF_BAR0 0
#define MMAP_OFFSET_PGOFF_BAR1 1
#define MMAP_OFFSET_PGOFF_BAR2 2

// ioctl command definitions
#include "../includes/ioctl_defs.h"

/**
    Instead of mapping the entire bar2 to allow a server to service multiple
    clients concurrently at once, we now provide an anon inode for each shared
    memory channel for each client connected to the server that the server maps.
    The server mmaps it and performs ops directly on it.
*/
struct cxl_channel_ctx {
    uint64_t physical_offset;
    uint64_t size;
};

/* cxl_channel ops */

static int cxl_channel_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct cxl_channel_ctx *ctx = filp->private_data;
    unsigned long req_size = vma->vm_end - vma->vm_start;
    int ret;

    if (!ctx) {
        pr_err("%s: No channel ctx was found when mmap\n", DRIVER_NAME);
        return -EINVAL;
    }

    pr_info("%s: mmap called on channel fd. Mapping phys 0x%llx, size 0x%llx\n", DRIVER_NAME, ctx->physical_offset, ctx->size);

    if (req_size > ctx->size) {
        pr_err("%s: Requested mmap size (0x%lx) > channel size (0x%llx)\n", DRIVER_NAME, req_size, ctx->size);
        return -EINVAL;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP | VM_PFNMAP);
    ret = io_remap_pfn_range(vma, vma->vm_start, ctx->physical_offset >> PAGE_SHIFT, req_size, vma->vm_page_prot);

    if (ret) {
        pr_err("%s: mmap failed, error=%d\n", DRIVER_NAME, ret);
        return ret;
    }
    return 0;
}

static int cxl_channel_release(struct inode *inode, struct file *filp)
{
    pr_info("%s: Releasing channel file\n", DRIVER_NAME);
    kfree(filp->private_data);
    filp->private_data = NULL;
    return 0;
}

static const struct file_operations cxl_channel_fops = {
    .owner = THIS_MODULE,
    .mmap  = cxl_channel_mmap,
    .release = cxl_channel_release,
};

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
    int irq;                 // IRQ number assigned for MSI

    struct eventfd_ctx *eventfd_notify_ctx; // New client notifications
    struct eventfd_ctx *eventfd_cmd_ctx;    // Command ready notifications
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

    pr_info("%s: Releasing device %s\n", DRIVER_NAME, pci_name(dev->pdev));

    // Release the eventfd contexts, this is a simple device driver
    // and so will always have one ref to it

    if (dev->eventfd_cmd_ctx) {
        eventfd_ctx_put(dev->eventfd_cmd_ctx);
        dev->eventfd_cmd_ctx = NULL;
        pr_info("%s: Released eventfd_cmd_ctx for %s\n", DRIVER_NAME, pci_name(dev->pdev));
    }

    if (dev->eventfd_notify_ctx) {
        eventfd_ctx_put(dev->eventfd_notify_ctx);
        dev->eventfd_notify_ctx = NULL;
        pr_info("%s: Released eventfd_notify_ctx for %s\n", DRIVER_NAME, pci_name(dev->pdev));
    }

	pr_info("%s: Closed device %s\n", DRIVER_NAME, pci_name(dev->pdev));
	return 0;
}

// Map device's BAR into user-space process's address space
// This function is called when user-space process calls mmap on the device file
// for a selected bar
// NOTE: We should not be mmapping the bar2
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

static long cxl_switch_client_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
  struct cxl_switch_client_dev *dev = filp->private_data;
  int efd_user_fd;  // User-space fd for the event fd
  struct eventfd_ctx **target_ctx_ptr = NULL;

  // Check command type and permissions before switch case
  if (_IOC_TYPE(cmd) != CXL_SWITCH_IOCTL_MAGIC) return -ENOTTY;
  if (_IOC_DIR(cmd) & _IOC_READ) {
    if (!access_ok((void __user *) arg, _IOC_SIZE(cmd))) return -EFAULT;
  }
  if (_IOC_DIR(cmd) & _IOC_WRITE) {
    if (!access_ok((void __user *) arg, _IOC_SIZE(cmd))) return -EFAULT;
  }

  if (copy_from_user(&efd_user_fd, (int __user *)arg, sizeof(efd_user_fd))) {
    pr_err("%s: ioctl failed to copy eventfd user fd from user space\n", DRIVER_NAME);
    return -EFAULT;
  }

  switch (cmd) {
  case CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY:
    target_ctx_ptr = &dev->eventfd_notify_ctx;
    pr_info("%s: Setting eventfd for new client notifications.\n", DRIVER_NAME);
    break;
  case CXL_SWITCH_IOCTL_SET_EVENTFD_CMD_READY:
    target_ctx_ptr = &dev->eventfd_cmd_ctx;
    pr_info("%s: Setting eventfd for command ready notifications.\n", DRIVER_NAME);
    break;
  case CXL_SWITCH_IOCTL_MAP_CHANNEL:
    cxl_channel_map_info_t map_info;
    struct cxl_channel_ctx *ctx;
    int new_fd;

    if (copy_from_user(&map_info, (void __user *) arg, sizeof(map_info))) {
      return -EFAULT;
    }
    
    pr_info("%s: Mapping channel with physical offset 0x%llx, size 0x%llx\n",
            DRIVER_NAME, map_info.physical_offset, map_info.size);
    // Allocate a private context for the new file
    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        return -ENOMEM;
    }

    ctx->physical_offset = map_info.physical_offset;
    ctx->size = map_info.size;
    
    // Spawn a new fd using an anonymous inode which the server
    // uses to interact with region
    new_fd = anon_inode_getfd("[cxl_channel]", &cxl_channel_fops, ctx, O_RDWR | O_CLOEXEC);

    if (new_fd < 0) {
        pr_err("%s: Failed to create anonymous inode for channel, error=%d\n", DRIVER_NAME, new_fd);
        kfree(ctx);
        return new_fd;
    }

    // Return this new fd to the userspace app (server)
    if (copy_to_user((void __user*) arg, &new_fd, sizeof(new_fd))) {
        put_unused_fd(new_fd);
        close_fd(new_fd);
        return -EFAULT;
    }
    pr_info("%s: Successfully created channel fd %d with physical offset 0x%llx, size 0x%llx\n",
            DRIVER_NAME, new_fd, ctx->physical_offset, ctx->size);

    // target_ctx_ptr remains unassigned, so we essentially
    // terminate here
    return 0;
  default:
    pr_warn("%s: Unknown ioctl command 0x%x\n", DRIVER_NAME, cmd);
    return -ENOTTY;
  }

  if (!target_ctx_ptr) {
    return 0;
  }

  struct eventfd_ctx *new_ctx = NULL;

  if (efd_user_fd >= 0) {
    new_ctx = eventfd_ctx_fdget(efd_user_fd);
    if (IS_ERR(new_ctx)) {
      pr_err("%s: Failed to get eventfd context from fd %d, error=%ld\n", DRIVER_NAME, efd_user_fd, PTR_ERR(new_ctx));
      return PTR_ERR(new_ctx);
    }
  }

  if (*target_ctx_ptr) {
    eventfd_ctx_put(*target_ctx_ptr);
    pr_info("%s: Replaced existing eventfd context for %s.\n", DRIVER_NAME, (cmd == CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY) ? "notify" : "command ready");
  }
  *target_ctx_ptr = new_ctx;
  if (new_ctx) {
    pr_info("%s: Set new eventfd context for %s (fd=%d).\n", DRIVER_NAME, (cmd == CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY) ? "notify" : "command ready", efd_user_fd);
  } else {
    pr_info("%s: Cleared eventfd context for %s.\n", DRIVER_NAME, (cmd == CXL_SWITCH_IOCTL_SET_EVENTFD_NOTIFY) ? "notify" : "command ready");
  }
  return 0;
}

static const struct file_operations cxl_switch_client_fops = {
	.owner = THIS_MODULE,
	.open = cxl_switch_client_open,
	.release = cxl_switch_client_release,
	.mmap = cxl_switch_client_mmap,
    .unlocked_ioctl = cxl_switch_client_ioctl,
};

/* ISR */
static irqreturn_t cxl_switch_client_isr(int irq, void *dev_id) {
  struct cxl_switch_client_dev *dev = dev_id;
  u32 irq_status;
  u32 irq_mask;
  u32 active_interrupts;

  // Check that BAR1 was mapped
  if (!dev->bar1_kva) {
    pr_warn("%s: BAR1 not mapped, cannot handle IRQ %d\n", DRIVER_NAME, irq);
    return IRQ_NONE;
  }

  irq_status = ioread32(dev->bar1_kva + REG_INTERRUPT_STATUS);
  irq_mask = ioread32(dev->bar1_kva + REG_INTERRUPT_MASK);
  active_interrupts = irq_status & irq_mask;
  if (active_interrupts == 0) {
    return IRQ_NONE;
  }

  pr_info("%s: Handling IRQ %d for device %s, active interrupts=0x%x\n",
          DRIVER_NAME, irq, pci_name(dev->pdev), active_interrupts);
  
  u32 handled_irqs = 0;
  if (active_interrupts & IRQ_SOURCE_NEW_CLIENT_NOTIFY) {
    pr_info("%s: New client notification received.\n", DRIVER_NAME);
    if (dev->eventfd_notify_ctx) {
      eventfd_signal(dev->eventfd_notify_ctx);
      pr_info("%s: Signaled eventfd for new client notification.\n", DRIVER_NAME);
    } else {
      pr_info("%s: No eventfd context for new client notifications, skipping signal.\n", DRIVER_NAME);
    }
    handled_irqs |= IRQ_SOURCE_NEW_CLIENT_NOTIFY;
  }

  if (active_interrupts & IRQ_SOURCE_CLOSE_CHANNEL_NOTIFY) {
    pr_info("%s: Close channel notification received.\n", DRIVER_NAME);
    if (dev->eventfd_notify_ctx) {
      eventfd_signal(dev->eventfd_notify_ctx);
      pr_info("%s: Signaled eventfd for new client notification.\n", DRIVER_NAME);
    } else {
      pr_info("%s: No eventfd context for new client notifications, skipping signal.\n", DRIVER_NAME);
    }
    handled_irqs |= IRQ_SOURCE_CLOSE_CHANNEL_NOTIFY;
  }

  if (active_interrupts & IRQ_SOURCE_CMD_RESPONSE_READY) {
    pr_info("%s: Command response ready notification received.\n", DRIVER_NAME);
    if (dev->eventfd_cmd_ctx) {
      eventfd_signal(dev->eventfd_cmd_ctx);
      pr_info("%s: Signaled eventfd for command response ready.\n", DRIVER_NAME);
    } else {
      pr_info("%s: No eventfd context for command responses, skipping signal.\n", DRIVER_NAME);
    }
    handled_irqs |= IRQ_SOURCE_CMD_RESPONSE_READY;
  }


  if (handled_irqs > 0) {
    iowrite32(handled_irqs, dev->bar1_kva + REG_INTERRUPT_STATUS);
    pr_info("%s: Acknowledged handled IRQs: 0x%x\n", DRIVER_NAME, handled_irqs);
    return IRQ_HANDLED;
  }

  pr_warn("%s: No known IRQs to handle for device %s, status=0x%x, mask=0x%x\n",
            DRIVER_NAME, pci_name(dev->pdev), irq_status, irq_mask);
  return IRQ_NONE;
}

/* PCI Driver */

// Helper function to probe a single BAR for its start and length
static int probe_bar_start_length(struct cxl_switch_client_dev *dev, int bar_idx,
                                  resource_size_t *bar_start, resource_size_t *bar_len,
                                  const char* bar_name)
{
    struct pci_dev *pdev = dev->pdev;
    *bar_start = pci_resource_start(pdev, bar_idx);
    *bar_len = pci_resource_len(pdev, bar_idx);
    if (!*bar_start || !*bar_len) {
        pr_err("%s: Failed to get %s resource\n", DRIVER_NAME, bar_name);
        return -ENODEV;
    }
    pr_info("%s: %s mapped at guest_phys 0x%llx, len 0x%llx for %s.\n",
            DRIVER_NAME, bar_name, (unsigned long long)*bar_start, (unsigned long long)*bar_len, pci_name(pdev));
    return 0;
}


static int map_bar(struct cxl_switch_client_dev *dev, int bar_idx,
                   resource_size_t *bar_len,
                   void __iomem **bar_kva, const char* bar_name)
{
    struct pci_dev *pdev = dev->pdev;
    int ret = pci_request_region(pdev, bar_idx, DRIVER_NAME);
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

// static int probe_bar(struct cxl_switch_client_dev *dev, int bar_idx,
//                      resource_size_t *bar_start, resource_size_t *bar_len,
//                      void __iomem **bar_kva, const char* bar_name)
// {
//     struct pci_dev *pdev = dev->pdev;
//     int ret;

//     *bar_start = pci_resource_start(pdev, bar_idx);
//     *bar_len = pci_resource_len(pdev, bar_idx);

//     if (!*bar_start || !*bar_len) {
//         pr_err("%s: Failed to get %s resource\n", DRIVER_NAME, bar_name);
//         return -ENODEV;
//     }
//     pr_info("%s: %s mapped at guest_phys 0x%llx, len 0x%llx for %s.\n",
//             DRIVER_NAME, bar_name, (unsigned long long)*bar_start, (unsigned long long)*bar_len, pci_name(pdev));
//     ret = pci_request_region(pdev, bar_idx, DRIVER_NAME);
//     if (ret) {
//         pr_err("%s: Failed to request %s region, error=%d\n", DRIVER_NAME, bar_name, ret);
//         return ret;
//     }

//     *bar_kva = pcim_iomap(pdev, bar_idx, *bar_len);
//     if (!(*bar_kva)) {
//         pr_err("%s: Failed to map %s, error=%d\n", DRIVER_NAME, bar_name, ret);
//         pci_release_region(pdev, bar_idx);
//         return -EIO;
//     }
//     pr_info("%s: %s for %s mapped to kernel virtual address %p\n", 
//             DRIVER_NAME, bar_name, pci_name(pdev), *bar_kva);
//     return 0;
// }
// Helper function to map a single BAR 

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

    // 1. Allocate memory for device-specific struct
	dev = kzalloc(sizeof(struct cxl_switch_client_dev), GFP_KERNEL);
	if (!dev) {
		pr_err("%s: Failed to allocate memory for device\n", DRIVER_NAME);
		return -ENOMEM;
	}
	dev->pdev = pdev;

	// 2. Enable the PCI device
	ret = pci_enable_device(pdev);
	if (ret) {
		pr_err("%s: Failed to enable PCI device, error=%d\n", DRIVER_NAME, ret);
		kfree(dev);
		return ret;
	}

    // 3. Enable bus mastering (for DMA and MSI)
    pci_set_master(pdev);
    pr_info("%s: Enabled bus mastering for %s.\n", DRIVER_NAME, DEVICE_NAME);

	// 4. Request MMIO/IOP resources
	//    Probe each bar with helper function
    
    // We need to probe start/length for bar0/1/2
    // and map for bar 0/1
    ret = probe_bar_start_length(dev, 0, &dev->bar0_start, &dev->bar0_len, "BAR0 Mailbox");
    if (ret) goto err_disable_device;
    ret = map_bar(dev, 0, &dev->bar0_len, &dev->bar0_kva, "BAR0 Mailbox");
    if (ret) goto err_disable_device;

    ret = probe_bar_start_length(dev, 1, &dev->bar1_start, &dev->bar1_len, "BAR1 Control");
    if (ret) goto err_release_bar0;
    ret = map_bar(dev, 1, &dev->bar1_len, &dev->bar1_kva, "BAR1 Control");
    if (ret) goto err_release_bar0;

    ret = probe_bar_start_length(dev, 2, &dev->bar2_start, &dev->bar2_len, "BAR2 Data");
    if (ret) goto err_release_bar1;

    // 5. Setup MSI
    //    Try to allocate one MSI vector
    int nvecs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (nvecs < 0) {
      ret = nvecs;
      pr_err("%s: Failed to allocate MSI vectors for %s, error = %d.\n", DRIVER_NAME, pci_name(pdev), ret);
      goto err_release_bar2;
    }
    //    Get IRQ number for 0th allocated vector
    dev->irq = pci_irq_vector(pdev, 0);
    pr_info("%s: MSI vector allocated for %s, IRQ %d.\n", DRIVER_NAME, pci_name(pdev), dev->irq);

    //    Request IRQ line for just the 0th allocated vector
    ret = request_irq(dev->irq, cxl_switch_client_isr, 0, DRIVER_NAME, dev);
    if (ret) {
      pr_err("%s: Failed to request IRQ %d for %s, error=%d\n", DRIVER_NAME, dev->irq, pci_name(pdev), ret);
      goto err_free_irq_vectors;
    }
    pr_info("%s: Successfully requested IRQ %d for %s\n", DRIVER_NAME, dev->irq, pci_name(pdev));
    //    Enable interrupts on the device by writing to its interrupt mask 
    //    register in BAR1
    if (dev->bar1_kva) {
    iowrite32(ALL_INTERRUPT_SOURCES, dev->bar1_kva + REG_INTERRUPT_MASK);
      pr_info("%s: Enabled all interrupts for %s\n", DRIVER_NAME, pci_name(pdev));
    } else {
        pr_err("%s: BAR1 not mapped, cannot enable interrupts for %s\n", DRIVER_NAME, pci_name(pdev));
        ret = -EIO;
        goto err_free_irq_handler;
        
    }

	// 6. Set DMA ... (not applicable for now?)
	// 7. Allocate and init shared control data space (N/A?)
	// 8. Access device configuration space (N/A?)

	// 9. Initialize character device to talk to cxl switch PCI device

	ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("%s: Failed to allocate char device number, error=%d\n", DRIVER_NAME, ret);
		goto err_disable_device_irqs;
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
err_disable_device_irqs:
    if (dev->bar1_kva) {
      iowrite32(0, dev->bar1_kva + REG_INTERRUPT_MASK);
      pr_info("%s: Disabled all interrupts for %s during cleanup\n", DRIVER_NAME, pci_name(pdev));
    }
err_free_irq_handler:
    if (dev->irq > 0) free_irq(dev->irq, dev);
err_free_irq_vectors:
    pci_free_irq_vectors(pdev);
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
    pci_clear_master(pdev);
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

    if (dev->bar1_kva) {
      iowrite32(0, dev->bar1_kva + REG_INTERRUPT_MASK);
    }

    if (dev->irq > 0) {
      free_irq(dev->irq, dev);
    }

    pci_free_irq_vectors(pdev);

    if (dev->bar2_kva) pcim_iounmap(pdev, dev->bar2_kva);
    if (dev->bar2_len) pci_release_region(pdev, 2);
    if (dev->bar1_kva) pcim_iounmap(pdev, dev->bar1_kva);
    if (dev->bar1_len) pci_release_region(pdev, 1);
    if (dev->bar0_kva) pcim_iounmap(pdev, dev->bar0_kva);
    if (dev->bar0_len) pci_release_region(pdev, 0);

    pci_clear_master(pdev);
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
