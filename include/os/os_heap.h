/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */
/****************************************************************
 * Interface definition for OS layer heap memory managment      *
 ****************************************************************/

/** \file os_heap.h
 *  \brief Heap memory management
 *
 * os_heap.h provides abstraction to heap memory management functions.
 * It allows to redefine the default malloc and free function used
 * by the implementation.
 */

#ifndef OS_HEAP_H
#define OS_HEAP_H

#if defined (__cplusplus)
extern "C" {
#endif

    /* !!!!!!!!NOTE From here no more includes are allowed!!!!!!! */

    /** \brief Allocate memory from heap
     *
     * Allocate memory from heap with the identified size.
     *
     * Possible Results:
     * - assertion failure: size == 0
     * - abort() if memory exhaustion is detected
     * - returns pointer to allocated memory
     */
    _Check_return_
    _Ret_bytecap_(size)
    OSAPI_EXPORT void *
    os_malloc(_In_range_(>, 0) size_t size)
        __attribute_malloc__
        __attribute_alloc_size__((1));

    /** \brief Reallocate memory from heap
     *
     * Reallocate memory from heap. If memblk is NULL
     * the function returns malloc(size). In contrast to
     * normal realloc, it is NOT supported to free memory
     * by passing 0. This way os_realloc() can be guaranteed
     * to never return NULL.
     * Possible Results:
     * - assertion failure: size == 0
     * - abort() if memory exhaustion is detected
     * - return pointer to reallocated memory otherwise.
     */
    _Check_return_
    _Ret_bytecap_(size)
    OSAPI_EXPORT void *
    os_realloc(
            _Pre_maybenull_ _Post_ptr_invalid_ void *memblk,
            _In_range_(>, 0) size_t size)
        __attribute_malloc__
        __attribute_alloc_size__((1));

    /** \brief Free allocated memory and return it to heap
     *
     * Free the allocated memory pointed to by \b ptr
     * and release it to the heap. When \b ptr is NULL,
     * os_free will return without doing any action.
     */
    OSAPI_EXPORT void
    os_free(_Pre_maybenull_ _Post_ptr_invalid_ void *ptr);

#if defined (__cplusplus)
}
#endif

#endif /* OS_HEAP_H */
