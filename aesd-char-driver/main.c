/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "aesdchar.h"
#include "aesd-circular-buffer.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Misha");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp,
                  char __user *buf,
                  size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;

    struct aesd_dev *dev = filp->private_data;

    struct aesd_buffer_entry *entry;

    size_t entry_offset;

    size_t bytes_to_copy;

    size_t total_copied = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    mutex_lock(&dev->lock);

    while (count > 0) {

        entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                    &dev->buffer,
                    *f_pos,
                    &entry_offset);

        if (!entry) {
            break;
        }

        bytes_to_copy = min(count,
                            entry->size - entry_offset);

        if (copy_to_user(buf + total_copied,
                         entry->buffptr + entry_offset,
                         bytes_to_copy)) {

            retval = -EFAULT;
            goto out;
        }

        *f_pos += bytes_to_copy;
        total_copied += bytes_to_copy;
        count -= bytes_to_copy;
    }

    retval = total_copied;

out:
    mutex_unlock(&dev->lock);

    return retval;
}

ssize_t aesd_write(struct file *filp,
                   const char __user *buf,
                   size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;

    struct aesd_dev *dev = filp->private_data;

    char *temp_buffer;

    char *new_buffer;

    char *final_buffer;

    struct aesd_buffer_entry entry;

    struct aesd_buffer_entry old_entry;

    bool buffer_was_full = false;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    temp_buffer = kmalloc(count, GFP_KERNEL);

    if (!temp_buffer) {
        return -ENOMEM;
    }

    if (copy_from_user(temp_buffer, buf, count)) {
        kfree(temp_buffer);
        return -EFAULT;
    }

    mutex_lock(&dev->lock);

    new_buffer = krealloc(dev->pending_write_buffer,
                          dev->pending_write_size + count,
                          GFP_KERNEL);

    if (!new_buffer) {
        retval = -ENOMEM;
        goto out;
    }

    dev->pending_write_buffer = new_buffer;

    memcpy(dev->pending_write_buffer + dev->pending_write_size,
           temp_buffer,
           count);

    dev->pending_write_size += count;

    /*
     * Only commit to circular buffer when newline received
     */
    if (memchr(dev->pending_write_buffer,
               '\n',
               dev->pending_write_size) != NULL) {

        final_buffer = kmalloc(dev->pending_write_size,
                               GFP_KERNEL);

        if (!final_buffer) {
            retval = -ENOMEM;
            goto out;
        }

        memcpy(final_buffer,
               dev->pending_write_buffer,
               dev->pending_write_size);

        entry.buffptr = final_buffer;
        entry.size = dev->pending_write_size;

        /*
         * Save overwritten entry BEFORE add_entry changes offsets
         */
        if (dev->buffer.full) {
            old_entry =
                dev->buffer.entry[dev->buffer.in_offs];

            buffer_was_full = true;
        }

        aesd_circular_buffer_add_entry(&dev->buffer,
                                       &entry);

        kfree(dev->pending_write_buffer);

        dev->pending_write_buffer = NULL;
        dev->pending_write_size = 0;

        /*
         * Free overwritten entry after replacement
         */
        if (buffer_was_full && old_entry.buffptr) {
            kfree((void *)old_entry.buffptr);
        }
    }

    retval = count;

out:
    mutex_unlock(&dev->lock);

    kfree(temp_buffer);

    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);

    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;

    err = cdev_add(&dev->cdev, devno, 1);

    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }

    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;

    int result;

    result = alloc_chrdev_region(&dev,
                                 aesd_minor,
                                 1,
                                 "aesdchar");

    aesd_major = MAJOR(dev);

    if (result < 0) {
        printk(KERN_WARNING
               "Can't get major %d\n",
               aesd_major);

        return result;
    }

    memset(&aesd_device,
           0,
           sizeof(struct aesd_dev));

    /*
     * Initialize AESD device structure
     */
    aesd_circular_buffer_init(&aesd_device.buffer);

    mutex_init(&aesd_device.lock);

    aesd_device.pending_write_buffer = NULL;
    aesd_device.pending_write_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if (result) {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

void aesd_cleanup_module(void)
{
    uint8_t index;

    struct aesd_buffer_entry *entry;

    dev_t devno = MKDEV(aesd_major,
                        aesd_minor);

    cdev_del(&aesd_device.cdev);

    /*
     * Free all circular buffer entries
     */
    AESD_CIRCULAR_BUFFER_FOREACH(entry,
                                 &aesd_device.buffer,
                                 index) {

        if (entry->buffptr) {
            kfree((void *)entry->buffptr);
        }
    }

    /*
     * Free partial pending write if exists
     */
    kfree(aesd_device.pending_write_buffer);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
