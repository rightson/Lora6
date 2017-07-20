/*
 *	Put this file in <driver/lora>
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include "lora6.h"
#include "modem/rn2483.h"

static int radio_init(struct lora6 *lr6)
{
	printk(KERN_INFO "LORA6: Default radio_init. Do nothing\n");
	return 0;
}

static int dev_writeout(struct lora6 *lr6)
{
	return lr6->tty->ops->write(lr6->tty, lr6->xhead, lr6->xleft);
}

static int radio_encaps(struct lora6 *lr6, unsigned char *icp, int len)
{
	printk(KERN_INFO "LORA6: Default radio_encaps. Do nothing\n");
	return 0;
}

static void radio_dencaps(struct lora6 *lr6, unsigned char s)
{
	return;
}

static int 
wan_encaps(unsigned char *s, unsigned char *d, int len)
{
	printk(KERN_INFO "LORA6: Default wan_encaps. Do nothing\n");
	return 0;
}

static int
dev_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct lora6 *lr6 = tty->disc_data;
	unsigned int tmp;
	int __user *p = (int __user *)arg;

	printk(KERN_INFO "LORA6: Default dev_ioctl.\n");

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
	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
	}
}

int loracmd_set(struct lora6 *lr6, u16 id)
{
	struct lora_cmd *cmd;

	if(!lr6 || !lr6->cmd)
		return -EINVAL;


	cmd = lr6->cmd;
	cmd->id = id;

	switch(id){
		case MODEL_DEFAULT:
			cmd->radio_init = radio_init;
			cmd->radio_encaps = radio_encaps;
			cmd->radio_dencaps = radio_dencaps;
			cmd->dev_writeout = dev_writeout;
			cmd->wan_encaps = wan_encaps;
			cmd->dev_ioctl = dev_ioctl;
			break;
		case MODEL_RN2483:
			rn2483_setcmd(cmd);
			break;
	}
	return 0;

}
EXPORT_SYMBOL(loracmd_set);
