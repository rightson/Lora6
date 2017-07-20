/*
 * lora6.c	This module implements the lora module use as modem for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's INET protocol layers.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include "lora6.h"
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/slhc_vj.h>
#endif

#define LORA6_VERSION	"VERSION 0.0.1"

static struct net_device **lora6_devs;

static int lora6_maxdev = SL_NRUNIT;
module_param(lora6_maxdev, int, 0);
MODULE_PARM_DESC(lora6_maxdev, "Maximum number of lora6 devices");



int 
load_xbuff(struct lora6 *lr6, unsigned char *cmd, int len, int reserve)
{
  int space, to_end;
  
  
  space = lr6->buffsize - lr6->xleft;
  if(space < reserve){
    printk(KERN_WARNING "LORA6: unable to load commands, space not enough\n");
    return -1;
  }
  to_end = space - (lr6->xhead - lr6->xbuff);
  if(to_end < reserve){
    memmove(lr6->xbuff, lr6->xhead, lr6->xleft);
    lr6->xhead = lr6->xbuff;
  }
  memcpy(lr6->xhead + lr6->xleft, cmd, len);
  lr6->xleft += len;
  

  return 0;
}
EXPORT_SYMBOL_GPL(load_xbuff);

/********************************
*  Buffer administration routines:
*	lr6_alloc_bufs()
*	lr6_free_bufs()
*	lr6_realloc_bufs()
*
* NOTE: lr6_realloc_bufs != lr6_free_bufs + lr6_alloc_bufs, because
*	lr6_realloc_bufs provides strong atomicity and reallocation
*	on actively running device.
*********************************/

/*
   Allocate channel buffers.
 */


static int lr6_alloc_bufs(struct lora6 *lr6, int mtu)
{
	int err = -ENOBUFS;
	unsigned long len;
	char *rbuff = NULL;
	char *xbuff = NULL;
	struct lora_cmd *lrcmd = NULL;

	/*
	 * Allocate the LORA6 frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * xbuff	Transmit buffer.
	 * cbuff        Temporary compression buffer.
	 */
	len = mtu * 2;

	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)
		len = 576 * 2;
	rbuff = kmalloc(len + 4, GFP_KERNEL);
	if (rbuff == NULL)
		goto err_exit;
	xbuff = kmalloc(len + 4, GFP_KERNEL);
	if (xbuff == NULL)
		goto err_exit;
	lrcmd = kmalloc(sizeof(struct lora_cmd), GFP_KERNEL);
	if (lrcmd == NULL)
		goto err_exit;

	spin_lock_bh(&lr6->lock);
	if (lr6->tty == NULL) {
		spin_unlock_bh(&lr6->lock);
		err = -ENODEV;
		goto err_exit;
	}
	lr6->mtu	     = mtu;
	lr6->buffsize = len;
	lr6->rcount   = 0;
	lr6->xleft    = 0;
	rbuff = xchg(&lr6->rbuff, rbuff);
	xbuff = xchg(&lr6->xbuff, xbuff);
	lrcmd = xchg(&lr6->cmd, lrcmd);
	loracmd_set(lr6, MODEL_RN2483);
	spin_unlock_bh(&lr6->lock);
	err = 0;

	/* Cleanup */
err_exit:
	kfree(xbuff);
	kfree(rbuff);
	kfree(lrcmd);
	return err;
}

/* Free a LORA6 channel buffers. */
static void lr6_free_bufs(struct lora6 *lr6)
{
	/* Free all LORA6 frame buffers. */
	kfree(xchg(&lr6->rbuff, NULL));
	kfree(xchg(&lr6->xbuff, NULL));
	kfree(xchg(&lr6->cmd, NULL));
}

/*
   Reallocate lora6 channel buffers.
 */

static int lr6_realloc_bufs(struct lora6 *lr6, int mtu)
{
	int err = 0;
	struct net_device *dev = lr6->dev;
	unsigned char *xbuff, *rbuff;
	int len = mtu * 2;

/*
 * allow for arrival of larger UDP packets, even if we say not to
 * also fixes a bug in which SunOS sends 512-byte packets even with
 * an MSS of 128
 */
	if (len < 576 * 2)
		len = 576 * 2;

	xbuff = kmalloc(len + 4, GFP_ATOMIC);
	rbuff = kmalloc(len + 4, GFP_ATOMIC);

	if (xbuff == NULL || rbuff == NULL)  {
		if (mtu > lr6->mtu) {
			printk(KERN_WARNING "%s: unable to grow lora6 buffers, MTU change cancelled.\n",
			       dev->name);
			err = -ENOBUFS;
		}
		goto done;
	}
	spin_lock_bh(&lr6->lock);

	err = -ENODEV;
	if (lr6->tty == NULL)
		goto done_on_bh;

	xbuff    = xchg(&lr6->xbuff, xbuff);
	rbuff    = xchg(&lr6->rbuff, rbuff);

	if (lr6->xleft)  {
		if (lr6->xleft <= len)  {
			memcpy(lr6->xbuff, lr6->xhead, lr6->xleft);

		} else  {
			lr6->xleft = 0;
			dev->stats.tx_dropped++;
		}
	}
	lr6->xhead = lr6->xbuff;

	if (lr6->rcount)  {
		if (lr6->rcount <= len) {
			memcpy(lr6->rbuff, rbuff, lr6->rcount);
		} else  {
			lr6->rcount = 0;
			dev->stats.rx_over_errors++;
			set_bit(SLF_ERROR, &lr6->flags);
		}
	}
	lr6->mtu      = mtu;
	dev->mtu      = mtu;
	lr6->buffsize = len;
	err = 0;

done_on_bh:
	spin_unlock_bh(&lr6->lock);

done:
	kfree(xbuff);
	kfree(rbuff);

	return err;
}


/* Set the "sending" flag.  This must be atomic hence the set_bit. */
static inline void lr6_lock(struct lora6 *lr6)
{
	netif_stop_queue(lr6->dev);
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void lr6_unlock(struct lora6 *lr6)
{
	netif_wake_queue(lr6->dev);
}

/* Send one completely decapsulated IP datagram to the IP layer. */
void lr6_bump(struct lora6 *lr6, unsigned char *srcbuf, int len)
{
	struct net_device *dev = lr6->dev;
	struct sk_buff *skb;


	dev->stats.rx_bytes += len;

	skb = dev_alloc_skb(len);
	if (skb == NULL) {
		printk(KERN_WARNING "%s: memory squeeze, dropping packet.\n", dev->name);
		dev->stats.rx_dropped++;
		return;
	}
	skb->dev = dev;
	memcpy(skb_put(skb, len), srcbuf, len);
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IP);
	netif_rx_ni(skb);
	dev->stats.rx_packets++;
}
EXPORT_SYMBOL_GPL(lr6_bump);

/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void lr6_encaps(struct lora6 *lr6, unsigned char *icp, int len)
{
	if(lr6->cmd->radio_encaps == NULL)
		printk(KERN_WARNING "No valid tx encapsulation\n");

	lr6->cmd->radio_encaps(lr6, icp, len);
}

/* Write out any remaining transmit buffer. Scheduled when tty is writable */
static void lora6_transmit(struct work_struct *work)
{
	struct lora6 *lr6 = container_of(work, struct lora6, tx_work);
	int actual;
	//printk(KERN_INFO "LORA6: lora6_transmit start\n");
	spin_lock_bh(&lr6->lock);
	/* First make sure we're connected. */
	if (!lr6->tty || lr6->magic != LORA6_MAGIC ) {
		printk(KERN_INFO "LORA6: lora6_transmit can'transmit\n");
		spin_unlock_bh(&lr6->lock);
		return;
	}

	if (lr6->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		lr6->dev->stats.tx_packets++;
		clear_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
		spin_unlock_bh(&lr6->lock);
		lr6_unlock(lr6);
		printk(KERN_INFO "LORA6: lora6_transmit no left\n");
		return;
	}

	actual = lr6->cmd->dev_writeout(lr6);
	lr6->xleft -= actual;
	lr6->xhead += actual;
	spin_unlock_bh(&lr6->lock);
	if(lr6->xleft > 0)
		schedule_work(&lr6->tx_work);
	//printk(KERN_INFO "LORA6: lora6_transmit done\n");
}

/*
 * Called by the driver when there's room for more data.
 * Schedule the transmit.
 */
static void lora6_write_wakeup(struct tty_struct *tty)
{
	struct lora6 *lr6 = tty->disc_data;
	//printk(KERN_INFO "LORA6: write_wakeup start\n");
	schedule_work(&lr6->tx_work);
}

static void lr6_tx_timeout(struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	spin_lock(&lr6->lock);

	if (netif_queue_stopped(dev)) {
		if (!netif_running(dev))
			goto out;

		/* May be we must check transmitter timeout here ?
		 *      14 Oct 1994 Dmitry Gorodchanin.
		 */
	}
out:
	spin_unlock(&lr6->lock);
}


/* Encapsulate an IP datagram and kick it into a TTY queue. */
static netdev_tx_t
lr6_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	spin_lock(&lr6->lock);
	if (!netif_running(dev)) {
		spin_unlock(&lr6->lock);
		printk(KERN_WARNING "%s: xmit call when iface is down\n", dev->name);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	if (lr6->tty == NULL) {
		spin_unlock(&lr6->lock);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	lr6_lock(lr6);
	dev->stats.tx_bytes += skb->len;
	lr6_encaps(lr6, skb->data, skb->len);
	spin_unlock(&lr6->lock);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}


/******************************************
 *   Routines looking at netdevice side.
 ******************************************/

/* Netdevice UP -> DOWN routine */

static int
lr6_close(struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	spin_lock_bh(&lr6->lock);
	if (lr6->tty)
		/* TTY discipline is running. */
		clear_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
	netif_stop_queue(dev);
	lr6->rcount   = 0;
	lr6->xleft    = 0;
	spin_unlock_bh(&lr6->lock);

	return 0;
}

/* Netdevice DOWN -> UP routine */

static int lr6_open(struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	if (lr6->tty == NULL)
		return -ENODEV;

	lr6->flags &= (1 << SLF_INUSE);
	netif_start_queue(dev);
	return 0;
}

/* Netdevice change MTU request */

static int lr6_change_mtu(struct net_device *dev, int new_mtu)
{
	struct lora6 *lr6 = netdev_priv(dev);

	if (new_mtu < 68 || new_mtu > 65534)
		return -EINVAL;

	if (new_mtu != dev->mtu)
		return lr6_realloc_bufs(lr6, new_mtu);
	return 0;
}

/* Netdevice get statistics request */

static struct rtnl_link_stats64 *
lr6_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct net_device_stats *devstats = &dev->stats;

	stats->rx_packets     = devstats->rx_packets;
	stats->tx_packets     = devstats->tx_packets;
	stats->rx_bytes       = devstats->rx_bytes;
	stats->tx_bytes       = devstats->tx_bytes;
	stats->rx_dropped     = devstats->rx_dropped;
	stats->tx_dropped     = devstats->tx_dropped;
	stats->tx_errors      = devstats->tx_errors;
	stats->rx_errors      = devstats->rx_errors;
	stats->rx_over_errors = devstats->rx_over_errors;

	return stats;
}

/* Netdevice register callback */

static int lr6_init(struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	/*
	 *	Finish setting up the DEVICE info.
	 */

	dev->mtu		= lr6->mtu;
	dev->type		= ARPHRD_LORA6;
#ifdef SL_CHECK_TRANSMIT
	dev->watchdog_timeo	= 20*HZ;
#endif
	return 0;
}


static void lr6_uninit(struct net_device *dev)
{
	struct lora6 *lr6 = netdev_priv(dev);

	lr6_free_bufs(lr6);
}

/* Hook the destructor so we can free lora6 devices at the right point in time */
static void lr6_free_netdev(struct net_device *dev)
{
	int i = dev->base_addr;
	free_netdev(dev);
	lora6_devs[i] = NULL;
}

static const struct net_device_ops lr6_netdev_ops = {
	.ndo_init		= lr6_init,
	.ndo_uninit	  	= lr6_uninit,
	.ndo_open		= lr6_open,
	.ndo_stop		= lr6_close,
	.ndo_start_xmit		= lr6_xmit,
	.ndo_get_stats64        = lr6_get_stats64,
	.ndo_change_mtu		= lr6_change_mtu,
	.ndo_tx_timeout		= lr6_tx_timeout,
	//.ndo_do_ioctl		= lr6_ioctl,
};


static void lr6_setup(struct net_device *dev)
{
	dev->netdev_ops		= &lr6_netdev_ops;
	dev->destructor		= lr6_free_netdev;

	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->tx_queue_len	= 10;

	/* New-style flags. */
	dev->flags		= IFF_NOARP|IFF_POINTOPOINT|IFF_MULTICAST;
}

/******************************************
  Routines looking at TTY side.
 ******************************************/


/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of LORA6 data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing. This will not
 * be re-entered while running but other ldisc functions may be called
 * in parallel
 */

static void lora6_receive_buf(struct tty_struct *tty, const unsigned char *cp,
							char *fp, int count)
{
	struct lora6 *lr6 = tty->disc_data;

	printk(KERN_INFO "LORA6: radio rx handler start\n");
	if (!lr6 || lr6->magic != LORA6_MAGIC){
		printk(KERN_INFO "LORA6: radio rx no lr6->dev\n");
		return;
	}

	if(lr6->cmd->radio_dencaps == NULL){
		printk(KERN_WARNING "LORA6: No valid radio rx handler\n");
	}
	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			if (!test_and_set_bit(SLF_ERROR, &lr6->flags))
				lr6->dev->stats.rx_errors++;
			cp++;
			continue;
		}
		lr6->cmd->radio_dencaps(lr6, *cp++);
	}
	

}

/************************************
 *  lora6_open helper routines.
 ************************************/

/* Collect hanged up channels */
static void lr6_sync(void)
{
	int i;
	struct net_device *dev;
	struct lora6	  *lr6;

	for (i = 0; i < lora6_maxdev; i++) {
		dev = lora6_devs[i];
		if (dev == NULL)
			break;

		lr6 = netdev_priv(dev);
		if (lr6->tty || lr6->leased)
			continue;
		if (dev->flags & IFF_UP)
			dev_close(dev);
	}
}


/* Find a free LORA6 channel, and link in this `tty' line. */
static struct lora6 *lr6_alloc(dev_t line)
{
	int i;
	char name[IFNAMSIZ];
	struct net_device *dev = NULL;
	struct lora6       *lr6;

	for (i = 0; i < lora6_maxdev; i++) {
		dev = lora6_devs[i];
		if (dev == NULL)
			break;
	}
	/* Sorry, too many, all lr6ots in use */
	if (i >= lora6_maxdev)
		return NULL;

	sprintf(name, "lr6%d", i);
	dev = alloc_netdev(sizeof(*lr6), name, NET_NAME_UNKNOWN, lr6_setup);
	if (!dev)
		return NULL;

	dev->base_addr  = i;
	lr6 = netdev_priv(dev);

	/* Initialize channel control data */
	lr6->magic = LORA6_MAGIC;
	lr6->dev = dev;
	spin_lock_init(&lr6->lock);
	init_waitqueue_head(&lr6->event);
	INIT_WORK(&lr6->tx_work, lora6_transmit);
	lr6->mode        = LORA_MODE_RADIO;
	lora6_devs[i] = dev;
	return lr6;
}

/*
 * Open the high-level part of the LORA6 channel.
 * This function is called by the TTY module when the
 * LORA6 line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free LORA6 channel...
 *
 * Called in process context serialized from other ldisc calls.
 */

static int lora6_open(struct tty_struct *tty)
{
	struct lora6 *lr6;
	int err;

	printk(KERN_INFO "LORA6: lora6_open start\n");
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	/* RTnetlink lock is misused here to serialize concurrent
	   opens of lora6 channels. There are better ways, but it is
	   the simplest one.
	 */
	rtnl_lock();

	/* Collect hanged up channels. */
	lr6_sync();

	lr6 = tty->disc_data;

	err = -EEXIST;
	/* First make sure we're not already connected. */
	if (lr6 && lr6->magic == LORA6_MAGIC)
		goto err_exit;

	/* OK.  Find a free LORA6 channel to use. */
	err = -ENFILE;
	lr6 = lr6_alloc(tty_devnum(tty));
	if (lr6 == NULL)
		goto err_exit;

	lr6->tty = tty;
	tty->disc_data = lr6;
	lr6->pid = current->pid;
	if (!test_bit(SLF_INUSE, &lr6->flags)) {
		/* Perform the low-level LORA6 initialization. */
		err = lr6_alloc_bufs(lr6, SL_MTU);
		if (err)
			goto err_free_chan;

		set_bit(SLF_INUSE, &lr6->flags);

		err = register_netdevice(lr6->dev);
		if (err)
			goto err_free_bufs;

		if(lr6->cmd->radio_init!=NULL)
			lr6->cmd->radio_init(lr6);
	}

	
	/* Done.  We have linked the TTY line to a channel. */
	rtnl_unlock();
	tty->receive_room = 65536;	/* We don't flow control */
	printk(KERN_INFO "LORA6: lora6_open done\n");
	/* TTY layer expects 0 on success */
	return 0;

err_free_bufs:
	lr6_free_bufs(lr6);

err_free_chan:
	lr6->tty = NULL;
	tty->disc_data = NULL;
	clear_bit(SLF_INUSE, &lr6->flags);

err_exit:
	rtnl_unlock();

	/* Count references from TTY module */
	return err;
}

/*
 * Close down a LORA6 channel.
 * This means flushing out any pending queues, and then returning. This
 * call is serialized against other ldisc functions.
 *
 * We also use this method fo a hangup event
 */

static void lora6_close(struct tty_struct *tty)
{
	struct lora6 *lr6 = tty->disc_data;

	printk(KERN_INFO "LORA6: lora6_close start\n");
	/* First make sure we're connected. */
	if (!lr6 || lr6->magic != LORA6_MAGIC || lr6->tty != tty)
	{
		printk(KERN_INFO "LORA6: lora6_close NULL struct\n");
		return;
	}

	spin_lock_bh(&lr6->lock);
	tty->disc_data = NULL;
	lr6->tty = NULL;
	spin_unlock_bh(&lr6->lock);

	flush_work(&lr6->tx_work);

	/* Flush network side */
	unregister_netdev(lr6->dev);
	/* This will complete via lr6_free_netdev */
	printk(KERN_INFO "LORA6: lora6_close done\n");
}

static int lora6_hangup(struct tty_struct *tty)
{
	lora6_close(tty);
	return 0;
}

/* Perform I/O control on an active LORA6 channel. */
static int lora6_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct lora6 *lr6 = tty->disc_data;

	/* First make sure we're connected. */
	if (!lr6 || lr6->magic != LORA6_MAGIC || lr6->cmd->dev_ioctl == NULL) 
		return -EINVAL;

	return lr6->cmd->dev_ioctl(tty, file, cmd, arg);
}


/* function do_ioctl called from net/core/dev.c
   to allow get/set outfill/keepalive parameter
   by ifconfig                                 */

/* Need some change !!!!!!! */
/*static int lr6_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct lora6 *lr6 = netdev_priv(dev);
	unsigned long *p = (unsigned long *)&rq->ifr_ifru;

	if (lr6 == NULL)		
		return -ENODEV;

	return 0;
}
*/

static struct tty_ldisc_ops lr6_ldisc = {
	.owner 		= THIS_MODULE,
	.magic 		= TTY_LDISC_MAGIC,
	.name 		= "lora6",
	.open 		= lora6_open,
	.close	 	= lora6_close,
	.hangup	 	= lora6_hangup,
	.ioctl		= lora6_ioctl,
	.receive_buf	= lora6_receive_buf,
	.write_wakeup	= lora6_write_wakeup,
};

static int __init lora6_init(void)
{
	int status;

	if (lora6_maxdev < 4)
		lora6_maxdev = 4; /* Sanity */

	printk(KERN_INFO "LORA6: version %s (dynamic channels, max=%d)" ".\n",
	       LORA6_VERSION, lora6_maxdev);

	lora6_devs = kzalloc(sizeof(struct net_device *)*lora6_maxdev,
								GFP_KERNEL);
	if (!lora6_devs)
		return -ENOMEM;

	/* Fill in our line protocol discipline, and register it */
	status = tty_register_ldisc(N_LORA6, &lr6_ldisc);
	if (status != 0) {
		printk(KERN_ERR "LORA6: can't register line discipline (err = %d)\n", status);
		kfree(lora6_devs);
	}
	return status;
}

static void __exit lora6_exit(void)
{
	int i;
	struct net_device *dev;
	struct lora6 *lr6;
	unsigned long timeout = jiffies + HZ;
	int busy = 0;

	if (lora6_devs == NULL)
		return;

	/* First of all: check for active disciplines and hangup them.
	 */
	do {
		if (busy)
			msleep_interruptible(100);

		busy = 0;
		for (i = 0; i < lora6_maxdev; i++) {
			dev = lora6_devs[i];
			if (!dev)
				continue;
			lr6 = netdev_priv(dev);
			spin_lock_bh(&lr6->lock);
			if (lr6->tty) {
				busy++;
				tty_hangup(lr6->tty);
			}
			spin_unlock_bh(&lr6->lock);
		}
	} while (busy && time_before(jiffies, timeout));

	/* FIXME: hangup is async so we should wait when doing this second
	   phase */

	for (i = 0; i < lora6_maxdev; i++) {
		dev = lora6_devs[i];
		if (!dev)
			continue;
		lora6_devs[i] = NULL;

		lr6 = netdev_priv(dev);
		if (lr6->tty) {
			printk(KERN_ERR "%s: tty discipline still running\n",
			       dev->name);
			/* Intentionally leak the control block. */
			dev->destructor = NULL;
		}

		unregister_netdev(dev);
	}

	kfree(lora6_devs);
	lora6_devs = NULL;

	i = tty_unregister_ldisc(N_LORA6);
	if (i != 0)
		printk(KERN_ERR "LORA6: can't unregister line discipline (err = %d)\n", i);
}

module_init(lora6_init);
module_exit(lora6_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_LORA6);