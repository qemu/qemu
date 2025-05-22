#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cs5250"); // Replace with your
MODULE_DESCRIPTION("A sample kernel module");

static int __init hello_init(void)
{
    printk(KERN_NOTICE "Hello World!\n");
    pr_info("Hello, world!\n");
    pr_debug("Greetings from %s.\n", THIS_MODULE->name);

    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("Goodbye world.\n");
}

module_init(hello_init);
module_exit(hello_exit);
