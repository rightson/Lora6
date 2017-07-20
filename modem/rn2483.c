/*
 *	commands for GL6509 LoRa modem
 *	Put this file in <driver/lora>
 */


#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h> /*put before rn2483.h for dependency*/
#include <linux/netdevice.h>
#include <linux/tty.h>
#include "lora6.h"
#include "rn2483.h"

static int rn2483_dev_writeout(struct lora6 *lr6);

static int 
rn2483_radio_init(struct lora6 *lr6)
{	
	int actual;
	printk(KERN_INFO "LORA6: rn2483_radio_init start\n");

	//set_bit(SLF_INIT, &lr6->flags);

	printk(KERN_INFO "LORA6: rn2483_radio_init start\n");
	spin_lock_bh(&lr6->lock);

	load_xbuff(lr6, "sys get ver\r\n", 13, 13);
	printk(KERN_DEBUG "LORA6: sys get ver \n");

	load_xbuff(lr6, "sys reset\r\n", 11, 11);
	printk(KERN_DEBUG "LORA6: sys reset \n");

	load_xbuff(lr6, "radio set mod lora\r\n", 20, 20);
	printk(KERN_DEBUG "LORA6: set mod \n");
	
	load_xbuff(lr6, "radio set freq 868000000\r\n", 26, 26);
	printk(KERN_DEBUG "LORA6: set freq \n");;
	
	load_xbuff(lr6, "radio set pwr 14\r\n", 18, 18);
	printk(KERN_DEBUG "LORA6: set pwr \n");;
	
	load_xbuff(lr6, "radio set sf sf12\r\n", 19, 19);
	printk(KERN_DEBUG "LORA6: set sf \n");;
	
	load_xbuff(lr6, "radio set afcbw 125\r\n", 21, 21);
	printk(KERN_DEBUG "LORA6: set afcbw \n");;
	
	load_xbuff(lr6, "radio set rxbw 250\r\n", 20, 20);
	printk(KERN_DEBUG "LORA6: set rxbw \n");;
	
	load_xbuff(lr6, "radio set fdev 5000\r\n", 21, 21);
	printk(KERN_DEBUG "LORA6: set fdev \n");;
	
	load_xbuff(lr6, "radio set prlen 8\r\n", 19, 19);
	printk(KERN_DEBUG "LORA6: set prlen \n");;
	
	load_xbuff(lr6, "radio set crc on\r\n", 18, 18);
	printk(KERN_DEBUG "LORA6: set crc \n");;
	
	load_xbuff(lr6, "radio set cr 4/8\r\n", 18, 18);
	printk(KERN_DEBUG "LORA6: set cr \n");
	
	load_xbuff(lr6, "radio set wdt 0\r\n", 17, 17);
	printk(KERN_DEBUG "LORA6: set wdt \n");
	
	load_xbuff(lr6, "radio set sync 12\r\n", 19, 19);
	printk(KERN_DEBUG "LORA6: set sync \n");
	
	load_xbuff(lr6, "radio set bw 250\r\n", 18, 18);
	printk(KERN_DEBUG "LORA6: set bw \n");
	
	load_xbuff(lr6, "mac pause\r\n", 11, 11);
	printk(KERN_DEBUG "LORA6: mac pause \n");
	
	load_xbuff(lr6, "radio rx 0\r\n", 12, 12);

	printk(KERN_DEBUG "LORA6: radio rx 0 \n");
	set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
	actual = rn2483_dev_writeout(lr6);
	lr6->xleft -= actual;
	lr6->xhead += actual;
	spin_unlock_bh(&lr6->lock);

	
	printk(KERN_INFO "LORA6: rn2483_radio_init done\n");
	return 0;

}

static int 
rn2483_dev_writeout(struct lora6 *lr6)
{
	unsigned char *p = lr6->xhead;
	int len = lr6->xleft;
	int count = 0;

	while(len--){
		switch(*p){
			case '\r':
				set_bit(SLF_TXCR, &lr6->flags);
				break;
			case '\n':
				if(test_and_clear_bit(SLF_TXCR, &lr6->flags)){
					printk("LORA6: %.*s\n", count, p-count);
					set_bit(SLF_TXEND, &lr6->flags);
				}
				break;
			default:
				if(test_bit(SLF_TXEND, &lr6->flags)){
					if(!test_and_clear_bit(SLF_RXEND, &lr6->flags)){
						//printk("LORA6: do wait\n");
						return count;
					}
					else
						clear_bit(SLF_TXEND, &lr6->flags);		
				}

				break;
		}

		if(lr6->tty->ops->write(lr6->tty, p++, 1) == 0){
			printk("LORA6: write fail\n");
			return count;
		}
		count++;
	}

	return count;
}

/*refer to sl_encaps in slip.c*/
static int 
rn2483_radio_encaps(struct lora6 *lr6, unsigned char *icp, int len)
{
	int actual;

	if (len > lr6->mtu) {		/* Sigh, shouldn't occur BUT ... */
		printk(KERN_WARNING "LORA6: %s: truncating oversized transmit packet!\n", lr6->dev->name);
		lr6->dev->stats.tx_dropped++;
		netif_wake_queue(lr6->dev);
		return -EFBIG;
	}


	//load_xbuff(lr6, "radio tx ", 9, 9 + len + 2);
	//load_xbuff(lr6, icp, len, len + 2);
	//load_xbuff(lr6, "\r\n", 2, 2);
	load_xbuff(lr6, "radio tx 66726f6d4950\r\n", 23, 23);

	set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
	actual = rn2483_dev_writeout(lr6);
	lr6->xleft -= actual;
	lr6->xhead += actual;

	return 0;
}

/* to buffer and check each character */
static void rn2483_radio_dencaps(struct lora6 *lr6, unsigned char s)
{
	/*need to modify : if incming pkt larger than rbuff*/
	int actual;
	switch(s){
		case '\r':
			set_bit(SLF_RXCR, &lr6->flags);
			break;
		case '\n':
			if(test_and_clear_bit(SLF_RXCR, &lr6->flags)){
				lr6->rcount--;
				printk(KERN_INFO "LORA6: %.*s\n", lr6->rcount, lr6->rbuff);
				set_bit(SLF_RXEND, &lr6->flags);
				
				if(strncmp(lr6->rbuff, "radio_rx", 8) == 0){
					if (netif_running(lr6->dev) && !test_and_clear_bit(SLF_ERROR, &lr6->flags))
						lr6_bump(lr6, lr6->xbuff+9, lr6->rcount-9);

					spin_lock_bh(&lr6->lock);
					load_xbuff(lr6, "radio tx 41434b\r\n", 17, 17);
					set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
					actual = rn2483_dev_writeout(lr6);
					lr6->xleft -= actual;
					lr6->xhead += actual;
					spin_unlock_bh(&lr6->lock);
				}
				else if(strncmp(lr6->rbuff, "radio_tx_ok", 11) == 0){
					spin_lock_bh(&lr6->lock);
					load_xbuff(lr6, "radio rx 5000\r\n", 15, 15);
					set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
					actual = rn2483_dev_writeout(lr6);
					lr6->xleft -= actual;
					lr6->xhead += actual;
					spin_unlock_bh(&lr6->lock);
				}


				lr6->rcount = 0;
				return;
			}
			break;
	}

	if (!test_bit(SLF_ERROR, &lr6->flags))  {
		if (lr6->rcount < lr6->buffsize)  {
			lr6->rbuff[lr6->rcount++] = s;
			return;
		}
		lr6->dev->stats.rx_over_errors++;
		set_bit(SLF_ERROR, &lr6->flags);
	}

	return;
}


static int 
rn2483_wan_encaps(unsigned char *s, unsigned char *d, int len)
{
	return 0;
}

static int
rn2483_dev_ioctl(struct tty_struct *tty, struct file *file,
                unsigned int cmd, unsigned long arg)
{
	int actual;
	struct lora6 *lr6 = tty->disc_data;
	unsigned int tmp;
	int __user *p = (int __user *)arg;

	switch (cmd) {
	case LORASMODEL:
		if (get_user(tmp, p))
			return -EFAULT;
		if(tmp == lr6->cmd->id)
			return 0;
		if(loracmd_set(lr6, tmp) != 0)
			return -EFAULT;
		lr6->cmd->radio_init(lr6);
		return 0;

	case LORAGMODEL:
		if (put_user(lr6->cmd->id, p))
			return -EFAULT;
		return 0;

	case LORASRADIO:
		if (get_user(tmp, p))
			return -EFAULT;

		if(tmp == LORA_MODE_RADIO){
			spin_lock_bh(&lr6->lock);
			load_xbuff(lr6, "mac pause\r\n", 11, 11);
			set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
			actual = lr6->tty->ops->write(lr6->tty, lr6->xhead, lr6->xleft);
			lr6->xleft -= actual;
			lr6->xhead += actual;
			spin_unlock_bh(&lr6->lock);
		}
		else{
			spin_lock_bh(&lr6->lock);
			load_xbuff(lr6, "mac resume\r\n", 12, 12);
			set_bit(TTY_DO_WRITE_WAKEUP, &lr6->tty->flags);
			actual = lr6->tty->ops->write(lr6->tty, lr6->xhead, lr6->xleft);
			lr6->xleft -= actual;
			lr6->xhead += actual;
			spin_unlock_bh(&lr6->lock);
		}

		return 0;

	case LORAGRADIO:
		if (put_user(lr6->mode, p))
			return -EFAULT;
		return 0;

	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
	}

}

void rn2483_setcmd(struct lora_cmd *cmd)
{
	if(cmd == NULL){
		printk(KERN_WARNING "LORA6 NULL cmd!!\n");
		return;
	}
	cmd->radio_init = rn2483_radio_init;
	cmd->dev_writeout = rn2483_dev_writeout;
	cmd->radio_encaps = rn2483_radio_encaps;
	cmd->radio_dencaps = rn2483_radio_dencaps;
	//cmd->radio_rxhandler = rn2483_radio_rxhandler;
	cmd->wan_encaps = rn2483_wan_encaps;
	cmd->dev_ioctl = rn2483_dev_ioctl;
	return;
}
EXPORT_SYMBOL(rn2483_setcmd);