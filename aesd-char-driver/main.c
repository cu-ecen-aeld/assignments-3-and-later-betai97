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
#include <linux/fs.h> // file_operations
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ben Tait");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    

    /*  Find the device */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    /* and use filp->private_data to point to the device data */
    filp->private_data = dev;

    return 0; /* success */
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *cur_entry;
    size_t entry_ind;
    int i = 0, copied=0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    char *read_buf;

    /**
     *  handle read
     */

    mutex_lock(&dev->mut);

    for(i=0; i<count; i++) {
        read_buf = krealloc(read_buf, 1+i, GFP_KERNEL);
        PDEBUG("Read iteration [%d]\n", i);
        cur_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, i, &entry_ind);
        if(cur_entry == NULL) {
            PDEBUG("found null entry\n");
            if(copied==0) {
                return -EFAULT;
            } else {
                break;
            }
        }
        
        read_buf[i] = cur_entry->buffptr[entry_ind];
        copied++;
    }

    PDEBUG("Finished iterating. Now do copy_to_user of %d bytes\n", copied);
    PDEBUG("copying address %x to address %x\n", read_buf, buf);

    for(i=0; i<copied; i++) {
        if(copy_to_user(buf+i, read_buf+i, 1) != 0) {
                PDEBUG("copy_to_user() fail\n");
                mutex_unlock(&dev->mut);
                *f_pos = retval;
                kfree(read_buf);
                return -EFAULT;
        }
    }

    kfree(read_buf);

    *f_pos += copied;

    mutex_unlock(&dev->mut);

    PDEBUG("aesd_read completed\n");

    return copied;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    char *buf_mem;
    char *term_pos;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;
    
    if((buf_mem = kmalloc(count, GFP_KERNEL)) == NULL) {
        PDEBUG("Error on kmalloc\n");
        return retval;
    }

    if(copy_from_user(buf_mem, buf, count) != 0) {
        PDEBUG("Error on copy from user\n");
        retval = -EFAULT;
        return retval;
    }


    mutex_lock(&dev->mut);

    dev->unterm.buffptr = krealloc(dev->unterm.buffptr, dev->unterm.size+count, GFP_KERNEL);
    if(dev->unterm.buffptr == NULL) {
        PDEBUG("Error on krealloc\n");
        mutex_unlock(&dev->mut);
        return retval;
    }
    memcpy(dev->unterm.buffptr+dev->unterm.size, buf_mem, count);
    kfree(buf_mem);
    dev->unterm.size = dev->unterm.size+count;

    if((term_pos = strchr(dev->unterm.buffptr, '\n')) != NULL) { // handle terminated case
        // free circ_buf mem if needed
        if(dev->circ_buf.full) {
            kfree(dev->circ_buf.entry[dev->circ_buf.in_offs].buffptr);
        }

        // add new entry to circ_buf
        struct aesd_buffer_entry *add_entry = kmalloc(sizeof(struct aesd_buffer_entry *), GFP_KERNEL);
        add_entry->size = dev->unterm.size;
        add_entry->buffptr = dev->unterm.buffptr;
        aesd_circular_buffer_add_entry(&dev->circ_buf, add_entry);
        kfree(add_entry);

        // clear unterm
        dev->unterm.size = 0;
        dev->unterm.buffptr = NULL;
    }

    mutex_unlock(&dev->mut);

    return count;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     *  initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.mut);
    aesd_circular_buffer_init(&aesd_device.circ_buf);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     *  cleanup AESD specific poritions here as necessary
     */
    mutex_destroy(&aesd_device.mut);

    struct aesd_buffer_entry *entry;
    int i;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circ_buf, i) {
        kfree(entry->buffptr);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
