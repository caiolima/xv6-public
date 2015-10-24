/*
 *  Description: This file is the types pre-defined
 *               using the <linux/list.h> from Linux Kernel codebase.
 *  Author: Caio Lima
 *  Date: 31 - 05 - 2015
 */

#ifndef XV6_TYPES_H_
#define XV6_TYPES_H_

#define offsetof(st, m) __builtin_offsetof(st, m)

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

/*
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({                      \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
            (type *)( (char *)__mptr - offsetof(type,member) );})

#endif /* XV6_TYPES_H_ */

