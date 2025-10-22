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

#include <linux/slab.h>     
#include <linux/uaccess.h> 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesd-circular-buffer.h"
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Vaishnavi Jadhav"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
     struct aesd_dev *dev;
     
     
     //pointer to deviec structure
     dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
     
     //saving it 
     filp->private_data = dev;
      PDEBUG("device opened successfully");
     
    return 0;
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

     PDEBUG("inside read");
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
     
     
    struct aesd_dev *dev = filp->private_data;
    
    const struct aesd_buffer_entry *entry;
    
    size_t entry_offset = 0;
    size_t bytes_to_copy;
    size_t bytes_available;
    
    //lock the device
     if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;  //if interrupted by signal return error
        
        
        
        
     // find which buffer entry and offset to read from based on file position
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    
    
    
    //if no entry is found it returns 0 bytes  raed and goes to unlock 
     if (!entry) 
     {
        retval = 0;
        goto out;
    }
    
    
    
    // calculate how many bytes are left to read from this entry
    bytes_available = entry->size - entry_offset;
    
    
    
    //copies only number of bytes that are requested or no. of bytes available 
    bytes_to_copy = min(count, bytes_available);
    
    
    
     // copy data from kernel space to user space and if copy fails return error
    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) 
    {
        retval = -EFAULT;
        goto out;// go to unclock
    }



       // update file offset for next read 
     *f_pos += bytes_to_copy;
     
     
     
     //bytes successfully copied
    retval = bytes_to_copy;
    
    out:
    mutex_unlock(&dev->lock);//release lock 
   
      PDEBUG("read completed ");
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{

      PDEBUG("inside write");
      
    ssize_t retval = -ENOMEM;
    
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
     
     
     
    struct aesd_dev *dev = filp->private_data;
   
    char *kbuf = NULL;
    
   

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

   //temp kernel buff allocated to copy user data
    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        goto out_unlock;

    //Copy from user space to kernel space
    size_t bytes_not_copied = copy_from_user(kbuf, buf, count);
    
    
    retval = count - bytes_not_copied;

    if (retval == 0)
        goto out_free_kbuf;

    // If the data contains a newline it completes command
    if (memchr(kbuf, '\n', retval)) 
    {
        struct aesd_buffer_entry new_entry;
        
        char *complete_buf;
        size_t total_size = dev->partial_write_size + retval;

        // allocate memory in kernel space for the complete command buffer
        // total_size = size of previous partial data + current write 
        complete_buf = kmalloc(total_size, GFP_KERNEL);
        
        
        if (!complete_buf) 
        {
            retval = -ENOMEM;
            goto out_free_kbuf;
        }
        
       

         //old partial data is merged with the new write
        if (dev->partial_write_buf) 
        {
            memcpy(complete_buf, dev->partial_write_buf, dev->partial_write_size);
            
            //free old buff that was written partiually
            kfree(dev->partial_write_buf);
            
            dev->partial_write_buf = NULL;
            
            dev->partial_write_size = 0;
        }
        
        //append current write entry's data to the previous data if any
        memcpy(complete_buf + (total_size - retval), kbuf, retval);


           //this is a complete entry that can be added to buff
          new_entry.buffptr = complete_buf;
        new_entry.size = total_size;



       
        const char *to_free = NULL;
        
        
        //if buff ois full next write will overwrite its oldeest entry 
        if (dev->buffer.full)
         {
            uint8_t idx = dev->buffer.in_offs;
            
            to_free = dev->buffer.entry[idx].buffptr;//save oldest entry's pointer
        }


        //add new entry 
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);


        //if old entry was overwritten then free its memomory
        if (to_free)
        kfree(to_free);
        
        
    }
    else 
    {
       // total size = previously stored partial data + new data received
        size_t total_size = dev->partial_write_size + retval;
        
        
        //allocate mem for new buff to hold prev data and new data
        char *new_buf = kmalloc(total_size, GFP_KERNEL);
        
        if (!new_buf) 
        {
        
        
            retval = -ENOMEM;
            
            goto out_free_kbuf;
        }



         //if any partial data is tehre copy it to buff first 
        if (dev->partial_write_buf)
         {
            memcpy(new_buf, dev->partial_write_buf, dev->partial_write_size);
            
            kfree(dev->partial_write_buf);//free old mem
        }


        //copy new data right afterr partail write was added
        memcpy(new_buf + dev->partial_write_size, kbuf, retval);
        
        dev->partial_write_buf = new_buf;// point to buff holding old + new data
        
        dev->partial_write_size = total_size; //size of partail data so far 
    }

out_free_kbuf:
    kfree(kbuf); //free mem

out_unlock:
    mutex_unlock(&dev->lock); //unloack
    
    
     PDEBUG("write completed");
    return retval;
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

      PDEBUG("inside init module");
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
     * TODO: initialize the AESD specific portion of the device
     */
    
    
    // initialize device lock
    mutex_init(&aesd_device.lock);     
    
    
    // initialize circular buffer
    aesd_circular_buffer_init(&aesd_device.buffer);
    
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    
     PDEBUG("init module done");
    return result;
    
}

void aesd_cleanup_module(void)
{

     PDEBUG("inside clenup");
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
     
     
    struct aesd_buffer_entry *entry;
    
    uint8_t index;
    
    
    //each entry related data and pointers are cleaned here 
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) 
    {
        if (entry->buffptr) 
        {
            kfree(entry->buffptr);
            
            entry->buffptr = NULL;
            
            entry->size = 0;
        }
    }

     
      // Free any partial write buffer
    if (aesd_device.partial_write_buf) 
    {
        kfree(aesd_device.partial_write_buf);
        
        aesd_device.partial_write_buf = NULL;
        
        aesd_device.partial_write_size = 0;
    }
    
      mutex_destroy(&aesd_device.lock);
   
    PDEBUG("cleanup done");
   
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
