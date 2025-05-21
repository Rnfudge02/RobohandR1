/**
 * @file vector_math_dsp.h
 * @brief DSP-optimized implementations for vector mathematics on RP2350
 * 
 * This header contains the DSP-specific optimizations for the vector math library,
 * leveraging the ARM Cortex M33 DSP and SIMD instructions for improved performance.
 */

#ifndef VECTOR_MATH_DSP_H
#define VECTOR_MATH_DSP_H

#include "vector_math.h"

/**
 * @brief Initialize DSP functions for vector math operations
 * 
 * This function should be called once at program startup to configure
 * DSP settings and initialize any necessary hardware.
 * 
 * @return VectorError Error code
 */
VectorError vector_dsp_init(void);

/**
 * @brief DSP-optimized vector addition
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_dsp_add(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief DSP-optimized vector subtraction
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_dsp_subtract(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief DSP-optimized dot product calculation
 * 
 * @param result Pointer to store the result
 * @param result_unit Pointer to store the unit of the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_dsp_dot_product(float* result, Unit* result_unit, const Vector* a, const Vector* b);

/**
 * @brief DSP-optimized cross product calculation (3D vectors only)
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_dsp_cross_product(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief DSP-optimized vector magnitude calculation
 * 
 * @param vector Input vector
 * @param result Pointer to store the magnitude
 * @param result_unit Pointer to store the unit of the magnitude
 * @return VectorError Error code
 */
VectorError vector_dsp_magnitude(const Vector* vector, float* result, Unit* result_unit);

/**
 * @brief DSP-optimized vector normalization
 * 
 * @param result Pointer to store the normalized vector
 * @param vector Input vector
 * @return VectorError Error code
 */
VectorError vector_dsp_normalize(Vector* result, const Vector* vector);

/**
 * @brief DSP-optimized vector scaling by a scalar
 * 
 * @param result Pointer to store the result
 * @param vector Input vector
 * @param scalar Scalar value
 * @param scalar_unit Unit of the scalar (can be NULL for unitless scalar)
 * @return VectorError Error code
 */
VectorError vector_dsp_scale(Vector* result, const Vector* vector, float scalar, const Unit* scalar_unit);

/**
 * @brief DSP-optimized matrix-vector multiplication
 * 
 * @param result Pointer to store the result vector
 * @param matrix Input matrix
 * @param vector Input vector
 * @return VectorError Error code
 */
VectorError matrix_dsp_vector_multiply(Vector* result, const Matrix* matrix, const Vector* vector);

/**
 * @brief DSP-optimized matrix multiplication
 * 
 * @param result Pointer to store the result matrix
 * @param a First matrix
 * @param b Second matrix
 * @return VectorError Error code
 */
VectorError matrix_dsp_multiply(Matrix* result, const Matrix* a, const Matrix* b);

/**
 * @brief DSP-optimized determinant calculation
 * 
 * @param matrix Input matrix
 * @param result Pointer to store the determinant
 * @param result_unit Pointer to store the unit of the determinant
 * @return VectorError Error code
 */
VectorError matrix_dsp_determinant(const Matrix* matrix, float* result, Unit* result_unit);

#endif /* VECTOR_MATH_DSP_H */