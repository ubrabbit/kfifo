/*
 * A generic kernel FIFO implementation
 *
 * Copyright (C) 2013 Stefani Seibold <stefani@seibold.net>
 */

/*
https://github.com/liigo/kfifo
*/

#ifndef _LINUX_KFIFO_H
#define _LINUX_KFIFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How to porting drivers to the new generic FIFO API:
 *
 * - Modify the declaration of the "struct kfifo *" object into a
 *   in-place "struct kfifo" object
 * - Init the in-place object with kfifo_alloc() or kfifo_init()
 *   Note: The address of the in-place "struct kfifo" object must be
 *   passed as the first argument to this functions
 * - Replace the use of __kfifo_put into kfifo_in and __kfifo_get
 *   into kfifo_out
 * - Replace the use of kfifo_put into kfifo_in_spinlocked and kfifo_get
 *   into kfifo_out_spinlocked
 *   Note: the spinlock pointer formerly passed to kfifo_init/kfifo_alloc
 *   must be passed now to the kfifo_in_spinlocked and kfifo_out_spinlocked
 *   as the last parameter
 * - The formerly __kfifo_* functions are renamed into kfifo_*
 */

/*
 * Note about locking: There is no locking required until only one reader
 * and one writer is using the fifo and no kfifo_reset() will be called.
 * kfifo_reset_out() can be safely used, until it will be only called
 * in the reader thread.
 * For multiple writer and one reader there is only a need to lock the writer.
 * And vice versa for only one writer and multiple reader there is only a need
 * to lock the reader.
 */

#include <stdlib.h>
#include <errno.h>

#define __must_check
#define ARRAY_SIZE(ary) (sizeof((ary))/sizeof(*(ary)))
#ifdef __GNUC__
	#define typeof __typeof__
	#define smp_wmb __sync_synchronize
#endif

struct __kfifo {
	unsigned int	in;
	unsigned int	out;
	unsigned int	mask;
	unsigned int	esize;
	void		*data;
};

#define __STRUCT_KFIFO_COMMON(datatype, recsize, ptrtype) \
	union { \
		struct __kfifo	stkfifo; \
		datatype	*type; \
		const datatype	*const_type; \
		char		(*rectype)[recsize]; \
		ptrtype		*ptr; \
		ptrtype const	*ptr_const; \
	}

#define __STRUCT_KFIFO(type, size, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type buf[((size < 2) || (size & (size - 1))) ? -1 : size]; /* liigo: ensure `size` is power of 2 */\
}

#define STRUCT_KFIFO(type, size) \
	struct __STRUCT_KFIFO(type, size, 0, type)

#define __STRUCT_KFIFO_PTR(type, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type		buf[0]; \
}

#define STRUCT_KFIFO_PTR(type) \
	struct __STRUCT_KFIFO_PTR(type, 0, type)

/*
 * define compatibility "struct kfifo" for dynamic allocated fifos
 */
struct kfifo __STRUCT_KFIFO_PTR(unsigned char, 0, void);

#define STRUCT_KFIFO_REC_1(size) \
	struct __STRUCT_KFIFO(unsigned char, size, 1, void)

#define STRUCT_KFIFO_REC_2(size) \
	struct __STRUCT_KFIFO(unsigned char, size, 2, void)

/*
 * define kfifo_rec types
 */
struct kfifo_rec_ptr_1 __STRUCT_KFIFO_PTR(unsigned char, 1, void);
struct kfifo_rec_ptr_2 __STRUCT_KFIFO_PTR(unsigned char, 2, void);

/*
 * helper macro to distinguish between real in place fifo where the fifo
 * array is a part of the structure and the fifo type where the array is
 * outside of the fifo structure.
 */
#define	__is_kfifo_ptr(fifo) \
	(sizeof(*fifo) == sizeof(STRUCT_KFIFO_PTR(typeof(*(fifo)->type))))

/**
 * DECLARE_KFIFO_PTR - macro to declare a fifo pointer object
 * @fifo: name of the declared fifo
 * @type: type of the fifo elements
 */
#define DECLARE_KFIFO_PTR(fifo, type)	STRUCT_KFIFO_PTR(type) fifo

/**
 * DECLARE_KFIFO - macro to declare a fifo object
 * @fifo: name of the declared fifo
 * @type: type of the fifo elements
 * @size: the number of elements in the fifo, this must be a power of 2
 */
#define DECLARE_KFIFO(fifo, type, size)	STRUCT_KFIFO(type, size) fifo

/**
 * INIT_KFIFO - Initialize a fifo declared by DECLARE_KFIFO
 * @fifo: name of the declared fifo datatype
 */
#define INIT_KFIFO(fifo) \
(void)({ \
	typeof(&(fifo)) __tmp = &(fifo); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	__kfifo->in = 0; \
	__kfifo->out = 0; \
	__kfifo->mask = ARRAY_SIZE(__tmp->buf) - 1;\
	__kfifo->esize = sizeof(*__tmp->buf); \
	__kfifo->data = __tmp->buf; \
})

/**
 * DEFINE_KFIFO - macro to define and initialize a fifo
 * @fifo: name of the declared fifo datatype
 * @type: type of the fifo elements
 * @size: the number of elements in the fifo, this must be a power of 2
 *
 * Note: the macro can be used for global and local fifo data type variables.
 */
#define DEFINE_KFIFO(fifo, type, size) \
	DECLARE_KFIFO(fifo, type, size) = \
	(typeof(fifo)) { \
		{ \
			{ \
			.in	= 0, \
			.out	= 0, \
			.mask	= __is_kfifo_ptr(&(fifo)) ? \
				  0 : \
				  ARRAY_SIZE((fifo).buf) - 1, \
			.esize	= sizeof(*(fifo).buf), \
			.data	= __is_kfifo_ptr(&(fifo)) ? \
				NULL : \
				(fifo).buf, \
			} \
		} \
	}


static inline unsigned int __must_check
__kfifo_uint_must_check_helper(unsigned int val)
{
	return val;
}

static inline int __must_check
__kfifo_int_must_check_helper(int val)
{
	return val;
}

/**
 * kfifo_initialized - Check if the fifo is initialized
 * @fifo: address of the fifo to check
 *
 * Return %true if fifo is initialized, otherwise %false.
 * Assumes the fifo was 0 before.
 */
#define kfifo_initialized(fifo) ((fifo)->stkfifo.mask)

/**
 * kfifo_esize - returns the size of the element managed by the fifo
 * @fifo: address of the fifo to be used
 */
#define kfifo_esize(fifo)	((fifo)->stkfifo.esize)

/**
 * kfifo_recsize - returns the size of the record length field
 * @fifo: address of the fifo to be used
 */
#define kfifo_recsize(fifo)	(sizeof(*(fifo)->rectype))

/**
 * kfifo_size - returns the size of the fifo in elements
 * @fifo: address of the fifo to be used
 */
#define kfifo_size(fifo)	((fifo)->stkfifo.mask + 1)

/**
 * kfifo_reset - removes the entire fifo content
 * @fifo: address of the fifo to be used
 *
 * Note: usage of kfifo_reset() is dangerous. It should be only called when the
 * fifo is exclusived locked or when it is secured that no other thread is
 * accessing the fifo.
 */
#define kfifo_reset(fifo) \
(void)({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	__tmp->stkfifo.in = __tmp->stkfifo.out = 0; \
})

/**
 * kfifo_reset_out - skip fifo content
 * @fifo: address of the fifo to be used
 *
 * Note: The usage of kfifo_reset_out() is safe until it will be only called
 * from the reader thread and there is only one concurrent reader. Otherwise
 * it is dangerous and must be handled in the same way as kfifo_reset().
 */
#define kfifo_reset_out(fifo)	\
(void)({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	__tmp->stkfifo.out = __tmp->stkfifo.in; \
})

/**
 * kfifo_len - returns the number of used elements in the fifo
 * @fifo: address of the fifo to be used
 */
#define kfifo_len(fifo) \
({ \
	typeof((fifo) + 1) __tmpl = (fifo); \
	__tmpl->stkfifo.in - __tmpl->stkfifo.out; \
})

/**
 * kfifo_is_empty - returns true if the fifo is empty
 * @fifo: address of the fifo to be used
 */
#define	kfifo_is_empty(fifo) \
({ \
	typeof((fifo) + 1) __tmpq = (fifo); \
	__tmpq->stkfifo.in == __tmpq->stkfifo.out; \
})

/**
 * kfifo_is_full - returns true if the fifo is full
 * @fifo: address of the fifo to be used
 */
#define	kfifo_is_full(fifo) \
({ \
	typeof((fifo) + 1) __tmpq = (fifo); \
	kfifo_len(__tmpq) > __tmpq->stkfifo.mask; \
})

/**
 * kfifo_avail - returns the number of unused elements in the fifo
 * @fifo: address of the fifo to be used
 */
#define	kfifo_avail(fifo) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmpq = (fifo); \
	const size_t __recsize = sizeof(*__tmpq->rectype); \
	unsigned int __avail = kfifo_size(__tmpq) - kfifo_len(__tmpq); \
	(__recsize) ? ((__avail <= __recsize) ? 0 : \
	__kfifo_max_r(__avail - __recsize, __recsize)) : \
	__avail; \
}) \
)

/**
 * kfifo_skip - skip output data
 * @fifo: address of the fifo to be used
 */
#define	kfifo_skip(fifo) \
(void)({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	if (__recsize) \
		__kfifo_skip_r(__kfifo, __recsize); \
	else \
		__kfifo->out++; \
})

/**
 * kfifo_skip - skip output data
 * @fifo: address of the fifo to be used
 * @n: number of bytes to skip
 */
#define	kfifo_skip_n(fifo, n) \
(void)({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	__kfifo->out += n; \
})


/**
 * kfifo_peek_len - gets the size of the next fifo record
 * @fifo: address of the fifo to be used
 *
 * This function returns the size of the next fifo record in number of bytes.
 */
#define kfifo_peek_len(fifo) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	(!__recsize) ? kfifo_len(__tmp) * sizeof(*__tmp->type) : \
	__kfifo_len_r(__kfifo, __recsize); \
}) \
)

/**
 * kfifo_alloc - dynamically allocates a new fifo buffer
 * @fifo: pointer to the fifo
 * @size: the number of elements in the fifo, this must be a power of 2
 * @gfp_mask: get_free_pages mask, passed to kmalloc()
 *
 * This macro dynamically allocates a new fifo buffer.
 *
 * The number of elements will be rounded-up to a power of 2.
 * The fifo will be release with kfifo_free().
 * Return 0 if no error, otherwise an error code.
 */
#define kfifo_alloc(fifo, size) \
__kfifo_int_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	__is_kfifo_ptr(__tmp) ? \
	__kfifo_alloc(__kfifo, size, sizeof(*__tmp->type)) : \
	-EINVAL; \
}) \
)

/**
 * kfifo_free - frees the fifo
 * @fifo: the fifo to be freed
 */
#define kfifo_free(fifo) \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	if (__is_kfifo_ptr(__tmp)) \
		__kfifo_free(__kfifo); \
})

/**
 * kfifo_init - initialize a fifo using a preallocated buffer
 * @fifo: the fifo to assign the buffer
 * @buffer: the preallocated buffer to be used
 * @size: the size of the internal buffer, in bytes, this have to be a power of 2
 *
 * This macro initializes a fifo using a preallocated buffer.
 *
 * The number of elements will be rounded-up to a power of 2.
 * Return 0 if no error, otherwise an error code.
 */
#define kfifo_init(fifo, buffer, size) \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	__is_kfifo_ptr(__tmp) ? \
	__kfifo_init(__kfifo, buffer, size, sizeof(*__tmp->type)) : \
	-EINVAL; \
})

/**
 * kfifo_peek - get data from the fifo without removing
 * @fifo: address of the fifo to be used
 * @val: address where to store the data
 *
 * This reads the data from the fifo without removing it from the fifo.
 * It returns 0 if the fifo was empty. Otherwise it returns the number
 * processed elements.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_peek(fifo, val) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr) __val = (val); \
	unsigned int __ret; \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	if (__recsize) \
		__ret = __kfifo_out_peek_r(__kfifo, __val, sizeof(*__val), \
			__recsize); \
	else { \
		__ret = !kfifo_is_empty(__tmp); \
		if (__ret) { \
			*(typeof(__tmp->type))__val = \
				(__is_kfifo_ptr(__tmp) ? \
				((typeof(__tmp->type))__kfifo->data) : \
				(__tmp->buf) \
				)[__kfifo->out & __tmp->stkfifo.mask]; \
			smp_wmb(); \
		} \
	} \
	__ret; \
}) \
)

/**
 * kfifo_in - put data into the fifo
 * @fifo: address of the fifo to be used
 * @buf: the data to be added
 * @n: number of elements to be added
 *
 * This macro copies the given buffer into the fifo and returns the
 * number of copied elements.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_in(fifo, buf, n) \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr_const) __buf = (buf); \
	unsigned int __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	(__recsize) ?\
	__kfifo_in_r(__kfifo, __buf, __n, __recsize) : \
	__kfifo_in(__kfifo, __buf, __n); \
})

/**
 * kfifo_out - get data from the fifo
 * @fifo: address of the fifo to be used
 * @buf: pointer to the storage buffer
 * @n: max. number of elements to get
 *
 * This macro get some data from the fifo and return the numbers of elements
 * copied.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_out(fifo, buf, n) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr) __buf = (buf); \
	unsigned int __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	(__recsize) ?\
	__kfifo_out_r(__kfifo, __buf, __n, __recsize) : \
	__kfifo_out(__kfifo, __buf, __n); \
}) \
)

/**
 * kfifo_out_peek - gets some data from the fifo
 * @fifo: address of the fifo to be used
 * @buf: pointer to the storage buffer
 * @n: max. number of elements to get
 *
 * This macro get the data from the fifo and return the numbers of elements
 * copied. The data is not removed from the fifo.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_out_peek(fifo, buf, n) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr) __buf = (buf); \
	unsigned int __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->stkfifo; \
	(__recsize) ? \
	__kfifo_out_peek_r(__kfifo, __buf, __n, __recsize) : \
	__kfifo_out_peek(__kfifo, __buf, __n); \
}) \
)

extern int __kfifo_init(struct __kfifo *fifo, void *buffer,
	unsigned int size, size_t esize);

extern unsigned int __kfifo_in(struct __kfifo *fifo,
	const void *buf, unsigned int len);

extern unsigned int __kfifo_out(struct __kfifo *fifo,
	void *buf, unsigned int len);

extern unsigned int __kfifo_out_peek(struct __kfifo *fifo,
	void *buf, unsigned int len);

extern unsigned int __kfifo_in_r(struct __kfifo *fifo,
	const void *buf, unsigned int len, size_t recsize);

extern unsigned int __kfifo_out_r(struct __kfifo *fifo,
	void *buf, unsigned int len, size_t recsize);

extern unsigned int __kfifo_len_r(struct __kfifo *fifo, size_t recsize);

extern void __kfifo_skip_r(struct __kfifo *fifo, size_t recsize);

extern unsigned int __kfifo_out_peek_r(struct __kfifo *fifo,
	void *buf, unsigned int len, size_t recsize);

extern unsigned int __kfifo_max_r(unsigned int len, size_t recsize);

#ifdef __cplusplus
} // extern C
#endif

#endif
