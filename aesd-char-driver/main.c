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
#include <linux/types.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>

#include "aesdchar.h"
#include "aesd_ioctl.h"
#include "access_ok_version.h" // taken from ldd3

// gpio defines
int gpio_pins[] = {5, 6, 12, 13, 16, 19, 20, 21, 25, 26};

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ben Tait");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/*
** Handler for GPIO pushbutton
*/
// static irqreturn_t pushbutton_irq_handler(int irq,  void *dev_id) 
// {
//     // todo
// }

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
    struct aesd_dev *dev = filp->private_data;
    size_t entry_ind;
    struct aesd_buffer_entry *cur_entry;
    int cnt = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    /**
     *  handle read
     */

    if(mutex_lock_interruptible(&dev->mut) != 0) {
        PDEBUG("mut lock err!\n");
        return -ERESTARTSYS;
    }

    cur_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &entry_ind);
    if(cur_entry == NULL) {
        PDEBUG ("bad read\n");
        *f_pos = 0;
        mutex_unlock(&dev->mut);
        return 0;
    }

    // note: idea from https://stackoverflow.com/questions/21006513/argument-invalid-when-using-cat-to-read-a-character-device-driver
    cnt = cur_entry->size - entry_ind;
    if(cnt>count)
        cnt = count;

    *f_pos += cnt;
    PDEBUG("%d bytes of %d\n", cnt, count);
    PDEBUG("%.*s\n", cnt, cur_entry->buffptr+entry_ind);
    if(copy_to_user(buf, cur_entry->buffptr+entry_ind, cnt) != 0) {
        PDEBUG("copy_to_user fail\n");
        mutex_unlock(&dev->mut);
        return -EFAULT;
    }

    gpio_set_value(3, 0);

    mutex_unlock(&dev->mut);

    PDEBUG("aesd_read completed\n");

    return cnt;
}

int isNewlinePresent(char *str, int n) {
    int i;
    for (i=0; i<n; i++) {
        if(str[i] == '\n') {
            return 1;
        }
    }

    return 0;
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
        kfree(buf_mem);
        retval = -EFAULT;
        return retval;
    }

    if(mutex_lock_interruptible(&dev->mut) != 0) {
        PDEBUG("mut lock err!\n");
        kfree(buf_mem);
        return -ERESTARTSYS;
    }

    dev->unterm.buffptr = krealloc(dev->unterm.buffptr, dev->unterm.size+count, GFP_KERNEL);
    if(dev->unterm.buffptr == NULL) {
        PDEBUG("Error on krealloc\n");
        mutex_unlock(&dev->mut);
        return retval;
    }
    memcpy(dev->unterm.buffptr+dev->unterm.size, buf_mem, count);
    kfree(buf_mem);
    dev->unterm.size = dev->unterm.size+count;

    if(isNewlinePresent(dev->unterm.buffptr, dev->unterm.size) != 0) { // handle terminated case
        // free circ_buf mem if needed
        if(dev->circ_buf.full) {
            if(dev->circ_buf.entry[dev->circ_buf.in_offs].buffptr != NULL) {
                kfree(dev->circ_buf.entry[dev->circ_buf.in_offs].buffptr);
            }
        }

        // add new entry to circ_buf
        struct aesd_buffer_entry *add_entry = kmalloc(sizeof(struct aesd_buffer_entry *), GFP_KERNEL);
        if(add_entry == NULL) {
            PDEBUG("Error on kmalloc\n");
            return retval;
        }
        add_entry->size = dev->unterm.size;
        PDEBUG("add_entry size: %d\n", add_entry->size);
        add_entry->buffptr = dev->unterm.buffptr;
        aesd_circular_buffer_add_entry(&dev->circ_buf, add_entry);
        kfree(add_entry);

        // Update f_pos
        *f_pos += dev->unterm.size;

        // clear unterm
        dev->unterm.size = 0;
        dev->unterm.buffptr = NULL;
    }

    gpio_set_value(3, 1);

    mutex_unlock(&dev->mut);

    return count;
}

// Implement fixed-size llseek() discussed in assignment course video
loff_t aesd_llseek(struct file *filp, loff_t off, int whence) 
{
    ssize_t retval = -EFAULT;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    int i = 0, circ_buf_size = 0;

    PDEBUG("llseek type %d with offset %lld", whence, off);

    // tally current circular buffer size
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->circ_buf, i) {
        if(entry->buffptr != NULL) {
            circ_buf_size += entry->size;
        }
    }

    // lock cbuffer access
    if(mutex_lock_interruptible(&dev->mut) != 0) {
        PDEBUG("llseek: mut lock err!\n");
        return -ERESTARTSYS;
    }

    // perform seek
    retval = fixed_size_llseek(filp, off, whence, circ_buf_size);

    // unlock cbuffer access
    mutex_unlock(&dev->mut);

    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    uint32_t absind; // absoulute command index in cbuffer
    struct aesd_dev *dev = filp->private_data;
    char *target = NULL, *cur = NULL;
    struct aesd_buffer_entry *cur_entry;
    loff_t f_pos_off = 0; 
    size_t entry_off = 0;
    struct aesd_seekto seekto;

    PDEBUG("ioctl cmd %u with arg %lld", cmd, arg);

    // Ensure valid command
    if(_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if(_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    PDEBUG("ioctl cmd passed validity checks\n");

    // Integrity checks on passed data
    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    PDEBUG("ioctl cmd passed integrity checks\n");

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
                return -EFAULT;
            } else {
                PDEBUG("ioctl ind: %u offset: %u\n", seekto.write_cmd, seekto.write_cmd_offset);

                // lock cbuffer access
                if(mutex_lock_interruptible(&dev->mut) != 0) {
                    PDEBUG("ioctl: mut lock err!\n");
                    return -ERESTARTSYS;
                }

                absind = (seekto.write_cmd + dev->circ_buf.out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                
                // validate arg
                if(seekto.write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
                    PDEBUG("Command index %u is out of range!\n", seekto.write_cmd);
                    return -EFAULT;
                } else if(dev->circ_buf.entry[absind].buffptr == NULL) {
                    PDEBUG("Command index %u  is not yet written!\n", seekto.write_cmd);
                    return -EFAULT;
                } else if(dev->circ_buf.entry[absind].size <= seekto.write_cmd_offset) {
                    PDEBUG("Command offset %u is beyond command %u size of %u\n", seekto.write_cmd_offset, seekto.write_cmd, dev->circ_buf.entry[absind].size);
                    return -EFAULT;
                }

                // Get offset to specified seek
                target = dev->circ_buf.entry[absind].buffptr + seekto.write_cmd_offset;
                while(f_pos_off < SOME_REASONABLE_CUTOFF_VALUE) {
                    cur_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, (size_t)f_pos_off, &entry_off);
                    cur = &cur_entry->buffptr[entry_off];

                    PDEBUG("{%.*s: %c} ", (int)cur_entry->size, cur_entry->buffptr, *cur);

                    if(cur == target) {
                        break;
                    } else {
                        f_pos_off++;
                    }
                }
                PDEBUG("\n");

                // update fpos
                mutex_lock(&filp->f_pos_lock);
                filp->f_pos += f_pos_off;
                mutex_unlock(&filp->f_pos_lock);

                // unlock cbuffer access
                mutex_unlock(&dev->mut);
            }
            break;

        default:
            return -ENOTTY;
    }

    if(err == 0) {
        PDEBUG("Successful completion of ioctl\n");
        PDEBUG("f_pos_off was %lld\n", f_pos_off);
    }

    return err;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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

// Init a gpio pin for kernel usage
void init_gpio_out(int gpio_pin)
{
    char label[10];

    sprintf(label, "GPIO_%d", gpio_pin);

    // Check GPIO valid
    if(!gpio_is_valid(gpio_pin)) {
        PDEBUG("GPIO pin %d not valid!\n", gpio_pin);
        return;
    }

    // Request the GPIO pin
    if(gpio_request(gpio_pin, (const char*) label) < 0) {
        PDEBUG("GPIO pin %d reqest error!\n", gpio_pin);
        gpio_free(gpio_pin);
        return;
    }

    // Configure GPIO as an output
    gpio_direction_output(gpio_pin, 0);

    // let it be accessed by sysfs in /sys/class/gpio/
    // i.e. can do - echo 1 > /sys/glass/gpio/gpio3/value
    gpio_export(gpio_pin, false);
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    int i;
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

    // /*
    // ** set up gpio
    // */
    // for(i=0; i<sizeof(gpio_pins)/sizeof(gpio_pins[0]); i++) {
    //     init_gpio_out(gpio_pins[i]);
    // }
    init_gpio_out(3);

    // // set up gpio for all output pins we're using for leds
    // gpio_direction_output(4, 0);
    // pio_direction_output(5, 0);
    // ...

    // gpio_direction_input(24); // set input for pushbutton
    // GPIO_irqNumber = gpio_to_irq(24);
    // if (request_irq(GPIO_irqNumber,             
    //               (void *)pushbutton_irq_handlerpushbutton_irq_handler,                     IRQF_TRIGGER_RISING,       
    //               "aesd_dev",               
    //               NULL)) {                    
    // printk(KERN_ERR "aesd_dev: cannot register pushbutton IRQ ");
    // return -1;
    // }


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
        if(entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
