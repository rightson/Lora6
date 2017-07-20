/*
 * lora.h defines the lora module device driver interface and constants.
 * this file should eventually move to include/linux
 */

#ifndef _LINUX_LORA6_H
#define _LINUX_LORA6_H

#include <linux/workqueue.h>
#include <linux/tty.h>
#include "if_lora6.h"

#ifndef ARPHRD_LORA6 
#define ARPHRD_LORA6 128
#endif
#ifndef N_LORA6
#define N_LORA6 26
#endif

/* LORA6 configuration. */
#define SL_NRUNIT	256		/* MAX number of LORA6 channels;
					   This can be overridden with
					   insmod -oslip_maxdev=nnn	*/
#define SL_MTU		296		/* 296; I am used to 600- FvK	*/

#define MODEL_DEFAULT 0
#define MODEL_RN2483  1

struct lora6;

/* struct lora_cmd - to store different handlers for a specific lora module
 * 
 * @id:   identifier of a lora module
 * @radio_encaps:     callback to encapsulate xmit pkt into radio transmitting 
 *                    command for the lora module
 * @radio_rxhandler:  callback to decapsulate rx pkt according to the spec of 
 *                    radio rx of the lora module
 * @wan_encaps:       callback to encapsulate xmit pkt into lorawan transmitting 
 *                    command for the lora module
 */

struct lora_cmd
{
  u16 id;

  int (*radio_init)(struct lora6 *lr6);

  //refer to slip_encaps
  int (*radio_encaps)(struct lora6 *lr6, unsigned char *icp, int len);
  void (*radio_dencaps)(struct lora6 *lr6, unsigned char s);
  int (*dev_writeout)(struct lora6 *lr6);
  int (*wan_encaps)(unsigned char *s, unsigned char *d, int len);
  int (*dev_ioctl)(struct tty_struct *tty, struct file *file,
                   unsigned int cmd, unsigned long arg);
};


struct lora6 {
  int			magic;

  /* Various fields. */
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
  struct net_device	*dev;		/* easy for intr handling	*/
  struct lora_cmd *cmd;     /* new cmd variable */
  spinlock_t		lock;
  //spinlock_t   xmit_lock;
  struct work_struct	tx_work;	/* Flushes transmit buffer	*/
  wait_queue_head_t event;
  /* These are pointers to the malloc()ed frame buffers. */
  unsigned char		*rbuff;		/* receiver buffer		*/
  int             rcount;   /* received chars counter       */
  unsigned char		*xbuff;		/* transmitter buffer		*/
  unsigned char   *xhead;   /* pointer to next byte to XMIT */        
  int     xleft;  /* bytes left in XMIT queue     */
  int			mtu;		/* Our mtu (to spot changes!)   */
  int     buffsize;       /* Max buffers sizes            */


  unsigned long		flags;		/* Flag values/ mode etc	*/
#define SLF_INUSE	0		/* Channel in use               */
#define SLF_ERROR	1   /* Parity, etc. error           */
#define SLF_RXEND	2		/* End of a received message		*/
#define SLF_TXEND 3   /* End of a sent message    */
#define SLF_INPKT 4   /* receive a network packet    */
#define SLF_RXCR	5		 /* carriage return character received    */
#define SLF_TXCR  6    /* carriage return character sent    */
#define SLF_ACTIONOK 7 /*last command succeeds*/
  unsigned char		mode;
#define  LORA_MODE_RADIO 0
#define  LORA_MODE_WAN 1

  unsigned char		leased;
  pid_t			pid;

};

#define LORA6_MAGIC 0x9302

extern int load_xbuff(struct lora6 *lr6, unsigned char *cmd, int len, int reserve);
extern int loracmd_set(struct lora6 *lr6, u16 id);
extern void lr6_bump(struct lora6 *lr6, unsigned char *srcbuf, int len);
#endif	/* _LINUX_LORA6_H */
