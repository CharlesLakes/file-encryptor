/**
 * @file    vector.h
 * @brief   Generic dynamic array.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section vector_overview Overview
 *
 * Uses the standard dynamic array growth strategy.
 * When the capacity is full, it doubles the capacity and reallocates the elements.
 *
 * Works with generic data types: each element is stored as a copy of
 * byte_size bytes in a contiguous block of memory.
 *
 * @section vector_complexity Complexity Summary
 *
 * n = number of elements, s = byte_size.
 *
 * | Operation          | Time                                 | Extra space          |
 * |--------------------|--------------------------------------|----------------------|
 * | create_vector()    | O(1)                                 | O(s)                 |
 * | push_back_vector() | O(s) amortized, O(n * s) worst case  | O(n * s) worst case  |
 * | get_value_vector() | O(1)                                 | O(1)                 |
 * | free_vector()      | O(1)                                 | O(1)                 |
 *
 * Total memory: O(m * s), with n <= m < 2n (for n >= 1).
 *
 * @section vector_usage Usage
 *
 * @code
 * vector vec = create_vector((unsigned int) sizeof(long long));
 *
 * // Append elements (a copy of the value is stored).
 * for(long long i = 1; i <= 5; i++)
 *     push_back_vector(&vec, (void *) &i);
 *
 * // Read and modify by index (0 <= index < vec.n).
 * long long *value = (long long *) get_value_vector(&vec, 0);
 * *value = 7;
 *
 * // Iterate over all the elements.
 * for(unsigned int i = 0; i < vec.n; i++){
 *     long long *value = (long long *) get_value_vector(&vec, i);
 *     printf("%lld ", *value);
 * }
 *
 * free_vector(&vec);
 * @endcode
 *
 * Structs work the same way:
 *
 * @code
 * typedef struct{ int x, y; } point;
 *
 * vector points = create_vector((unsigned int) sizeof(point));
 * point p = {1, 2};
 * push_back_vector(&points, (void *) &p);
 * point *q = (point *) get_value_vector(&points, 0);
 * free_vector(&points);
 * @endcode
 *
 * @section vector_notes Notes
 *
 * - vec.n is the number of elements, vec.m the reserved capacity.
 * - get_value_vector() does not check bounds.
 * - A pointer returned by get_value_vector() becomes invalid after the next
 *   push_back_vector(), because the array may be reallocated.
 * - Memory allocation failures are not checked.
 * - The vector must not be used after free_vector() unless create_vector()
 *   is called again.
 */

#ifndef VECTOR_H
#define VECTOR_H

/**
 * @brief Dynamic array of elements of byte_size bytes.
 */
typedef struct{
	unsigned int m; 			    /**< Reserved size. */
	unsigned int n;				    /**< Abstract size. */
	void *arr;				  	    /**< Array pointer. */
	unsigned int byte_size;		/**< Size of each element in bytes. */
}vector;

/**
 * @brief  Creates an empty vector with capacity 1.
 * @param  byte_size Size in bytes of each element.
 * @return The new vector.
 * @par Complexity
 * Time O(1), space O(s).
 */
vector 			create_vector(unsigned int byte_size);

/**
 * @brief Appends a copy of element at the end, doubling the capacity if full.
 * @param vec     Vector.
 * @param element Pointer to the element to copy (byte_size bytes are read).
 * @par Complexity
 * Time O(s) amortized; O(n * s) when the array is reallocated.
 * Space O(n * s) when the array is reallocated, O(1) otherwise.
 */
void			push_back_vector(vector* vec, void* element);

/**
 * @brief  Returns a pointer to the element at index. No bounds checking.
 * @param  vec   Vector.
 * @param  index Position of the element, 0 <= index < vec->n.
 * @return Pointer to the stored element, valid until the next push_back_vector().
 * @par Complexity
 * Time O(1), space O(1).
 */
void*			get_value_vector(vector* vec,unsigned int index);

/**
 * @brief Frees the internal array.
 * @param vec Vector.
 * @par Complexity
 * Time O(1), space O(1).
 */
void			free_vector(vector* vec);

#endif
