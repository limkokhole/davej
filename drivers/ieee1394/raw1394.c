/*
 * IEEE 1394 for Linux
 *
 * Raw interface to the bus
 *
 * Copyright (C) 1999 Andreas E. Bombe
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "ieee1394_core.h"
#include "hosts.h"
#include "highlevel.h"
#include "ieee1394_transactions.h"
#include "raw1394.h"


LIST_HEAD(host_info_list);
static int host_count = 0;
spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

static struct hpsb_highlevel *hl_handle = NULL;

static void queue_complete_cb(struct pending_request *req);

static struct pending_request *__alloc_pending_request(int flags)
{
        struct pending_request *req;

        req = (struct pending_request *)kmalloc(sizeof(struct pending_request),
                                                flags);
        if (req != NULL) {
                memset(req, 0, sizeof(struct pending_request));
                INIT_LIST_HEAD(&req->list);
                req->tq.routine = (void(*)(void*))queue_complete_cb;
        }

        return req;
}

inline static struct pending_request *alloc_pending_request(void)
{
        return __alloc_pending_request(SLAB_KERNEL);
}

static void free_pending_request(struct pending_request *req)
{
        if (req->ibs) {
                if (atomic_dec_and_test(&req->ibs->refcount)) {
                        kfree(req->ibs);
                }
        } else if (req->free_data) {
                kfree(req->data);
        }
        free_hpsb_packet(req->packet);
        kfree(req);
}

static void queue_complete_req(struct pending_request *req)
{
        unsigned long flags;
        struct file_info *fi = req->file_info;

        spin_lock_irqsave(&fi->reqlists_lock, flags);
        list_del(&req->list);
        list_add_tail(&req->list, &fi->req_complete);
        spin_unlock_irqrestore(&fi->reqlists_lock, flags);

        up(&fi->complete_sem);
        wake_up_interruptible(&fi->poll_wait_complete);
}

static void queue_complete_cb(struct pending_request *req)
{
        struct hpsb_packet *packet = req->packet;
        int rcode = (packet->header[1] >> 12) & 0xf;

        switch (packet->ack_code) {
        case ACKX_NONE:
        case ACKX_SEND_ERROR:
                req->req.error = RAW1394_ERROR_SEND_ERROR;
                break;
        case ACKX_ABORTED:
                req->req.error = RAW1394_ERROR_ABORTED;
                break;
        case ACKX_TIMEOUT:
                req->req.error = RAW1394_ERROR_TIMEOUT;
                break;
        default:
                req->req.error = (packet->ack_code << 16) | rcode;
                break;
        }

        if (!((packet->ack_code == ACK_PENDING) && (rcode == RCODE_COMPLETE))) {
                req->req.length = 0;
        }

        free_tlabel(packet->host, packet->node_id, packet->tlabel);
        queue_complete_req(req);
}


static void add_host(struct hpsb_host *host)
{
        struct host_info *hi;

        hi = (struct host_info *)kmalloc(sizeof(struct host_info), SLAB_KERNEL);
        if (hi != NULL) {
                INIT_LIST_HEAD(&hi->list);
                hi->host = host;
                INIT_LIST_HEAD(&hi->file_info_list);

                spin_lock_irq(&host_info_lock);
                list_add_tail(&hi->list, &host_info_list);
                host_count++;
                spin_unlock_irq(&host_info_lock);
        }
}


static struct host_info *find_host_info(struct hpsb_host *host)
{
        struct list_head *lh;
        struct host_info *hi;

        lh = host_info_list.next;
        while (lh != &host_info_list) {
                hi = list_entry(lh, struct host_info, list);
                if (hi->host == host) {
                        return hi;
                }
                lh = lh->next;
        }

        return NULL;
}

static void remove_host(struct hpsb_host *host)
{
        struct host_info *hi;

        spin_lock_irq(&host_info_lock);
        hi = find_host_info(host);

        if (hi != NULL) {
                list_del(&hi->list);
                host_count--;
        }
        spin_unlock_irq(&host_info_lock);

        if (hi == NULL) {
                printk(KERN_ERR "raw1394: attempt to remove unknown host "
                       "0x%p\n", host);
                return;
        }

        kfree(hi);
}

static void host_reset(struct hpsb_host *host)
{
        unsigned long flags;
        struct list_head *lh;
        struct host_info *hi;
        struct file_info *fi;
        struct pending_request *req;

        spin_lock_irqsave(&host_info_lock, flags);
        hi = find_host_info(host);

        if (hi != NULL) {
                lh = hi->file_info_list.next;

                while (lh != &hi->file_info_list) {
                        fi = list_entry(lh, struct file_info, list);
                        req = __alloc_pending_request(SLAB_ATOMIC);

                        if (req != NULL) {
                                req->file_info = fi;
                                req->req.type = RAW1394_REQ_BUS_RESET;
                                req->req.generation = get_hpsb_generation();
                                req->req.misc = (host->node_id << 16) 
                                        | host->node_count;
                                queue_complete_req(req);
                        }
                                
                        lh = lh->next;
                }
        }
        spin_unlock_irqrestore(&host_info_lock, flags);
}

static void iso_receive(struct hpsb_host *host, int channel, quadlet_t *data,
                        unsigned int length)
{
        unsigned long flags;
        struct list_head *lh;
        struct host_info *hi;
        struct file_info *fi;
        struct pending_request *req;
        struct iso_block_store *ibs = NULL;
        LIST_HEAD(reqs);

        spin_lock_irqsave(&host_info_lock, flags);
        hi = find_host_info(host);

        if (hi != NULL) {
                for (lh = hi->file_info_list.next; lh != &hi->file_info_list;
                     lh = lh->next) {
                        fi = list_entry(lh, struct file_info, list);

                        if (!(fi->listen_channels & (1ULL << channel))) {
                                continue;
                        }

                        if (!ibs) {
                                ibs = kmalloc(sizeof(struct iso_block_store)
                                              + length, SLAB_ATOMIC);
                                if (!ibs) break;

                                atomic_set(&ibs->refcount, 0);
                                memcpy(ibs->data, data, length);
                        }

                        req = __alloc_pending_request(SLAB_ATOMIC);
                        if (!req) {
                                kfree(ibs);
                                break;
                        }

                        atomic_inc(&ibs->refcount);

                        req->file_info = fi;
                        req->ibs = ibs;
                        req->data = ibs->data;
                        req->req.type = RAW1394_REQ_ISO_RECEIVE;
                        req->req.generation = get_hpsb_generation();
                        req->req.misc = 0;
                        req->req.recvb = fi->iso_buffer;
                        req->req.length = MIN(length, fi->iso_buffer_length);
                        
                        list_add_tail(&req->list, &reqs);
                }
        }
        spin_unlock_irqrestore(&host_info_lock, flags);

        lh = reqs.next;
        while (lh != &reqs) {
                req = list_entry(lh, struct pending_request, list);
                lh = lh->next;
                queue_complete_req(req);
        }
}


static int dev_read(struct file *file, char *buffer, size_t count,
                    loff_t *offset_is_ignored)
{
        struct file_info *fi = (struct file_info *)file->private_data;
        struct list_head *lh;
        struct pending_request *req;

        if (count != sizeof(struct raw1394_request)) {
                return -EINVAL;
        }

        if (!access_ok(VERIFY_WRITE, buffer, count)) {
                return -EFAULT;
        }

        if (file->f_flags & O_NONBLOCK) {
                if (down_trylock(&fi->complete_sem)) {
                        return -EAGAIN;
                }
        } else {
                if (down_interruptible(&fi->complete_sem)) {
                        return -ERESTARTSYS;
                }
        }

        spin_lock_irq(&fi->reqlists_lock);
        lh = fi->req_complete.next;
        list_del(lh);
        spin_unlock_irq(&fi->reqlists_lock);

        req = list_entry(lh, struct pending_request, list);

        if (req->req.length) {
                if (copy_to_user(req->req.recvb, req->data, req->req.length)) {
                        req->req.error = RAW1394_ERROR_MEMFAULT;
                }
        }
        __copy_to_user(buffer, &req->req, sizeof(req->req));

        free_pending_request(req);
        return sizeof(struct raw1394_request);
}


static int state_opened(struct file_info *fi, struct pending_request *req)
{
        if (req->req.type == RAW1394_REQ_INITIALIZE) {
                if (req->req.misc == RAW1394_KERNELAPI_VERSION) {
                        fi->state = initialized;
                        req->req.error = RAW1394_ERROR_NONE;
                        req->req.generation = get_hpsb_generation();
                } else {
                        req->req.error = RAW1394_ERROR_COMPAT;
                        req->req.misc = RAW1394_KERNELAPI_VERSION;
                }
        } else {
                req->req.error = RAW1394_ERROR_STATE_ORDER;
        }

        req->req.length = 0;
        queue_complete_req(req);
        return sizeof(struct raw1394_request);
}

static int state_initialized(struct file_info *fi, struct pending_request *req)
{
        struct list_head *lh;
        struct host_info *hi;
        struct raw1394_khost_list *khl;

        if (req->req.generation != get_hpsb_generation()) {
                req->req.error = RAW1394_ERROR_GENERATION;
                req->req.generation = get_hpsb_generation();
                req->req.length = 0;
                queue_complete_req(req);
                return sizeof(struct raw1394_request);
        }

        switch (req->req.type) {
        case RAW1394_REQ_LIST_CARDS:
                spin_lock_irq(&host_info_lock);
                khl = kmalloc(sizeof(struct raw1394_khost_list) * host_count,
                              SLAB_ATOMIC);

                if (khl != NULL) {
                        req->req.misc = host_count;
                        req->data = (quadlet_t *)khl;
                        
                        lh = host_info_list.next;
                        while (lh != &host_info_list) {
                                hi = list_entry(lh, struct host_info, list);

                                khl->nodes = hi->host->node_count;
                                strcpy(khl->name, hi->host->template->name);

                                khl++;
                                lh = lh->next;
                        }
                }
                spin_unlock_irq(&host_info_lock);

                if (khl != NULL) {
                        req->req.error = RAW1394_ERROR_NONE;
                        req->req.length = MIN(req->req.length,
                                              sizeof(struct raw1394_khost_list)
                                              * req->req.misc);
                        req->free_data = 1;
                } else {
                        return -ENOMEM;
                }
                break;

        case RAW1394_REQ_SET_CARD:
                lh = NULL;

                spin_lock_irq(&host_info_lock);
                if (req->req.misc < host_count) {
                        lh = host_info_list.next;
                        while (req->req.misc--) {
                                lh = lh->next;
                        }
                        hi = list_entry(lh, struct host_info, list);
                        hpsb_inc_host_usage(hi->host);
                        list_add_tail(&fi->list, &hi->file_info_list);
                        fi->host = hi->host;
                        fi->state = connected;
                }
                spin_unlock_irq(&host_info_lock);

                if (lh != NULL) {
                        req->req.error = RAW1394_ERROR_NONE;
                        req->req.misc = (fi->host->node_id << 16) 
                                | fi->host->node_count;
                } else {
                        req->req.error = RAW1394_ERROR_INVALID_ARG;
                }

                req->req.length = 0;
                break;

        default:
                req->req.error = RAW1394_ERROR_STATE_ORDER;
                req->req.length = 0;
                break;
        }

        queue_complete_req(req);
        return sizeof(struct raw1394_request);
}

static void handle_iso_listen(struct file_info *fi, struct pending_request *req)
{
        int channel = req->req.misc;

        spin_lock(&host_info_lock);
        if ((channel > 63) || (channel < -64)) {
                req->req.error = RAW1394_ERROR_INVALID_ARG;
        } else if (channel >= 0) {
                /* allocate channel req.misc */
                if (fi->listen_channels & (1ULL << channel)) {
                        req->req.error = RAW1394_ERROR_ALREADY;
                } else {
                        fi->listen_channels |= 1ULL << channel;
                        hpsb_listen_channel(hl_handle, fi->host, channel);
                        fi->iso_buffer = req->req.recvb;
                        fi->iso_buffer_length = req->req.length;
                }
        } else {
                /* deallocate channel (one's complement neg) req.misc */
                channel = ~channel;

                if (fi->listen_channels & (1ULL << channel)) {
                        hpsb_unlisten_channel(hl_handle, fi->host, channel);
                        fi->listen_channels &= ~(1ULL << channel);
                } else {
                        req->req.error = RAW1394_ERROR_INVALID_ARG;
                }
        }

        req->req.length = 0;
        queue_complete_req(req);
        spin_unlock(&host_info_lock);
}

static int handle_local_request(struct file_info *fi,
                                struct pending_request *req)
{
        u64 addr = req->req.address & 0xffffffffffffULL;

        req->data = kmalloc(req->req.length, SLAB_KERNEL);
        if (!req->data) return -ENOMEM;
        req->free_data = 1;

        switch (req->req.type) {
        case RAW1394_REQ_ASYNC_READ:
                req->req.error = highlevel_read(fi->host, req->data, addr,
                                                req->req.length);
                break;

        case RAW1394_REQ_ASYNC_WRITE:
                if (copy_from_user(req->data, req->req.sendb, 
                                   req->req.length)) {
                        req->req.error = RAW1394_ERROR_MEMFAULT;
                        break;
                }

                req->req.error = highlevel_write(fi->host, req->data, addr,
                                                 req->req.length);
                req->req.length = 0;
                break;

        case RAW1394_REQ_LOCK:
                if ((req->req.misc != EXTCODE_FETCH_ADD)
                    && (req->req.misc != EXTCODE_LITTLE_ADD)) {
                        if (req->req.length != 4) {
                                req->req.error = RAW1394_ERROR_INVALID_ARG;
                                break;
                        }
                } else {
                        if (req->req.length != 8) {
                                req->req.error = RAW1394_ERROR_INVALID_ARG;
                                break;
                        }
                }

                if (copy_from_user(req->data, req->req.sendb,
                                   req->req.length)) {
                        req->req.error = RAW1394_ERROR_MEMFAULT;
                        break;
                }

                if (req->req.length == 8) {
                        req->req.error = highlevel_lock(fi->host, req->data,
                                                        addr, req->data[1],
                                                        req->data[0],
                                                        req->req.misc);
                        req->req.length = 4;
                } else {
                        req->req.error = highlevel_lock(fi->host, req->data,
                                                        addr, req->data[0], 0,
                                                        req->req.misc);
                }
                break;

        case RAW1394_REQ_LOCK64:
        default:
                req->req.error = RAW1394_ERROR_STATE_ORDER;
        }

        if (req->req.error) req->req.length = 0;
        req->req.error |= 0x00100000;
        queue_complete_req(req);
        return sizeof(struct raw1394_request);
}

static int handle_remote_request(struct file_info *fi,
                                 struct pending_request *req, int node)
{
        struct hpsb_packet *packet = NULL;
        u64 addr = req->req.address & 0xffffffffffffULL;

        switch (req->req.type) {
        case RAW1394_REQ_ASYNC_READ:
                if (req->req.length == 4) {
                        packet = hpsb_make_readqpacket(fi->host, node, addr);
                        if (!packet) return -ENOMEM;

                        req->data = &packet->header[3];
                } else if ((req->req.length % 4) == 0) {
                        packet = hpsb_make_readbpacket(fi->host, node, addr,
                                                       req->req.length);
                        if (!packet) return -ENOMEM;

                        req->data = packet->data;
                } else {
                        req->req.error = RAW1394_ERROR_UNTIDY_LEN;
                }
                break;

        case RAW1394_REQ_ASYNC_WRITE:
                if (req->req.length == 4) {
                        packet = hpsb_make_writeqpacket(fi->host, node, addr,
                                                        0);
                        if (!packet) return -ENOMEM;

                        if (copy_from_user(&packet->header[3], req->req.sendb,
                                           4)) {
                                req->req.error = RAW1394_ERROR_MEMFAULT;
                        }
                } else if ((req->req.length % 4) == 0) {
                        packet = hpsb_make_writebpacket(fi->host, node, addr,
                                                        req->req.length);
                        if (!packet) return -ENOMEM;

                        if (copy_from_user(packet->data, req->req.sendb,
                                           req->req.length)) {
                                req->req.error = RAW1394_ERROR_MEMFAULT;
                        }
                } else {
                        req->req.error = RAW1394_ERROR_UNTIDY_LEN;
                }
                req->req.length = 0;
                break;

        case RAW1394_REQ_LOCK:
                if ((req->req.misc != EXTCODE_FETCH_ADD)
                    && (req->req.misc != EXTCODE_LITTLE_ADD)) {
                        if (req->req.length != 4) {
                                req->req.error = RAW1394_ERROR_INVALID_ARG;
                                break;
                        }
                } else {
                        if (req->req.length != 8) {
                                req->req.error = RAW1394_ERROR_INVALID_ARG;
                                break;
                        }
                }

                packet = hpsb_make_lockpacket(fi->host, node, addr,
                                              req->req.misc);
                if (!packet) return -ENOMEM;

                if (copy_from_user(packet->data, req->req.sendb,
                                   req->req.length)) {
                        req->req.error = RAW1394_ERROR_MEMFAULT;
                        break;
                }

                req->req.length = 4;
                break;

        case RAW1394_REQ_LOCK64:
        default:
                req->req.error = RAW1394_ERROR_STATE_ORDER;
        }

        req->packet = packet;

        if (req->req.error) {
                req->req.length = 0;
                queue_complete_req(req);
                return sizeof(struct raw1394_request);
        }

        req->tq.data = req;
        queue_task(&req->tq, &packet->complete_tq);

        spin_lock_irq(&fi->reqlists_lock);
        list_add_tail(&req->list, &fi->req_pending);
        spin_unlock_irq(&fi->reqlists_lock);

        if (!hpsb_send_packet(packet)) {
                req->req.error = RAW1394_ERROR_SEND_ERROR;
                req->req.length = 0;
                queue_complete_req(req);
        }
        return sizeof(struct raw1394_request);
}

static int state_connected(struct file_info *fi, struct pending_request *req)
{
        int node = req->req.address >> 48;

        req->req.error = RAW1394_ERROR_NONE;

        if (req->req.generation != get_hpsb_generation()) {
                req->req.error = RAW1394_ERROR_GENERATION;
                req->req.generation = get_hpsb_generation();
                req->req.length = 0;
                queue_complete_req(req);
                return sizeof(struct raw1394_request);
        }

        if (req->req.type == RAW1394_REQ_ISO_LISTEN) {
                handle_iso_listen(fi, req);
                return sizeof(struct raw1394_request);
        }

        if (req->req.length == 0) {
                req->req.error = RAW1394_ERROR_INVALID_ARG;
                queue_complete_req(req);
                return sizeof(struct raw1394_request);
        }

        if (fi->host->node_id == node) {
                return handle_local_request(fi, req);
        }

        return handle_remote_request(fi, req, node);
}


static int dev_write(struct file *file, const char *buffer, size_t count,
                     loff_t *offset_is_ignored)
{
        struct file_info *fi = (struct file_info *)file->private_data;
        struct pending_request *req;
        int retval = 0;

        if (count != sizeof(struct raw1394_request)) {
                return -EINVAL;
        }

        req = alloc_pending_request();
        if (req == NULL) {
                return -ENOMEM;
        }
        req->file_info = fi;

        if (copy_from_user(&req->req, buffer, sizeof(struct raw1394_request))) {
                free_pending_request(req);
                return -EFAULT;
        }

        switch (fi->state) {
        case opened:
                retval = state_opened(fi, req);
                break;

        case initialized:
                retval = state_initialized(fi, req);
                break;

        case connected:
                retval = state_connected(fi, req);
                break;
        }

        if (retval < 0) {
                free_pending_request(req);
        }

        return retval;
}

static unsigned int dev_poll(struct file *file, poll_table *pt)
{
        struct file_info *fi = file->private_data;
        unsigned int mask = POLLOUT | POLLWRNORM;

        poll_wait(file, &fi->poll_wait_complete, pt);

        spin_lock_irq(&fi->reqlists_lock);
        if (!list_empty(&fi->req_complete)) {
                mask |= POLLIN | POLLRDNORM;
        }
        spin_unlock_irq(&fi->reqlists_lock);

        return mask;
}

static int dev_open(struct inode *inode, struct file *file)
{
        struct file_info *fi;

        if (MINOR(inode->i_rdev)) {
                return -ENXIO;
        }

        fi = kmalloc(sizeof(struct file_info), SLAB_KERNEL);
        if (fi == NULL) {
                return -ENOMEM;
        }
        
        memset(fi, 0, sizeof(struct file_info));

        INIT_LIST_HEAD(&fi->list);
        fi->state = opened;
        INIT_LIST_HEAD(&fi->req_pending);
        INIT_LIST_HEAD(&fi->req_complete);
        sema_init(&fi->complete_sem, 0);
        spin_lock_init(&fi->reqlists_lock);
        init_waitqueue_head(&fi->poll_wait_complete);

        file->private_data = fi;

        MOD_INC_USE_COUNT;
        return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
        struct file_info *fi = file->private_data;
        struct list_head *lh;
        struct pending_request *req;
        int done = 0, i;

        for (i = 0; i < 64; i++) {
                if (fi->listen_channels & (1ULL << i)) {
                        hpsb_unlisten_channel(hl_handle, fi->host, i);
                }
        }

        spin_lock(&host_info_lock);
        fi->listen_channels = 0;
        spin_unlock(&host_info_lock);

        while (!done) {
                spin_lock_irq(&fi->reqlists_lock);

                while (!list_empty(&fi->req_complete)) {
                        lh = fi->req_complete.next;
                        list_del(lh);

                        req = list_entry(lh, struct pending_request, list);

                        free_pending_request(req);
                }

                if (list_empty(&fi->req_pending)) {
                        done = 1;
                }

                spin_unlock_irq(&fi->reqlists_lock);

                if (!done) {
                        down_interruptible(&fi->complete_sem);
                }
        }

        if (fi->state == connected) {
                spin_lock_irq(&host_info_lock);
                list_del(&fi->list);
                spin_unlock_irq(&host_info_lock);

                hpsb_dec_host_usage(fi->host);
        }

        kfree(fi);

        MOD_DEC_USE_COUNT;
        return 0;
}

static struct hpsb_highlevel_ops hl_ops = {
        add_host,
        remove_host,
        host_reset,
        iso_receive
};

static struct file_operations file_ops = {
        read:     dev_read, 
        write:    dev_write, 
        poll:     dev_poll, 
        open:     dev_open, 
        release:  dev_release, 
};

int init_raw1394(void)
{
        hl_handle = hpsb_register_highlevel(RAW1394_DEVICE_NAME, &hl_ops);
        if (hl_handle == NULL) {
                HPSB_ERR("raw1394 failed to register with ieee1394 highlevel");
                return -ENOMEM;
        }

        if (register_chrdev(RAW1394_DEVICE_MAJOR, RAW1394_DEVICE_NAME, 
                            &file_ops)) {
                HPSB_ERR("raw1394 failed to allocate device major");
                return -EBUSY;
        }

        return 0;
}

void cleanup_raw1394(void)
{
        unregister_chrdev(RAW1394_DEVICE_MAJOR, RAW1394_DEVICE_NAME);
        hpsb_unregister_highlevel(hl_handle);
}

#ifdef MODULE

int init_module(void)
{
        return init_raw1394();
}

void cleanup_module(void)
{
        return cleanup_raw1394();
}

#endif