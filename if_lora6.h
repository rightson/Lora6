/*
 *	Definitions of the socket-level I/O control calls.
 *	
 *	The commands defined here should not duplicate with those in <uapi\linux\sockios.h>.
 *
 */ 
 
#ifndef __LINUX_LORA6_H
#define __LINUX_LORA6_H

 
#define LORA_MAGIC	0xA5

#define LORASMODEL		_IOW(LORA_MAGIC, 0x01, int)
#define LORAGMODEL		_IOR(LORA_MAGIC, 0x02, int)
#define LORASRADIO		_IOW(LORA_MAGIC, 0x03, int)
#define LORAGRADIO		_IOR(LORA_MAGIC, 0x04, int)

#endif	/* __LINUX_LORA6_H */

