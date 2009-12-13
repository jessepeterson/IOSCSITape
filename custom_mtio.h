/*
 *  custom_mtio.h
 *  IOSCSITape
 *
 *  Created by Jesse Peterson on 12/13/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef _CUSTOM_MTIO_H_
#define _CUSTOM_MTIO_H_

#define	MTCMPRESS	16	/* set/clear device compression */
#define	MTEWARN		17	/* set/clear early warning behaviour */

/*
 * When more SCSI-3 SSC (streaming device) devices are out there
 * that support the full 32 byte type 2 structure, we'll have to
 * rethink these ioctls to support all the entities they haul into
 * the picture (64 bit blocks, logical file record numbers, etc..).
 */
#define	MTIOCRDSPOS	_IOR('m', 5, uint32_t)	/* get logical blk addr */
#define	MTIOCRDHPOS	_IOR('m', 6, uint32_t)	/* get hardware blk addr */
#define	MTIOCSLOCATE	_IOW('m', 5, uint32_t)	/* seek to logical blk addr */
#define	MTIOCHLOCATE	_IOW('m', 6, uint32_t)	/* seek to hardware blk addr */

#endif /* _CUSTOM_MTIO_H_ */