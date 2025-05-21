/**
 * @file vector_math.h
 * @brief Vector mathematics library optimized for RP2350 (ARM Cortex M33)
 * 
 * This header defines a comprehensive vector mathematics library that:
 * - Supports n-dimensional vectors
 * - Tracks physical units associated with vector components
 * - Leverages ARM DSP SIMD instructions for optimized performance
 * - Provides common vector operations (add, subtract, dot product, cross product, determinant)
 */

#ifndef VECTOR_MATH_H
#define VECTOR_MATH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Include ARM DSP headers for SIMD operations */
#include "arm_math.h"

/**
 * @brief Enumeration of supported physical units
 */
typedef enum {
    UNIT_NONE = 0,
    UNIT_METER,
    UNIT_SECOND,
    UNIT_KILOGRAM,
    UNIT_AMPERE,
    UNIT_KELVIN,
    UNIT_MOLE,
    UNIT_CANDELA,
    UNIT_RADIAN,
    UNIT_STERADIAN,
    UNIT_HERTZ,
    UNIT_NEWTON,
    UNIT_PASCAL,
    UNIT_JOULE,
    UNIT_WATT,
    UNIT_COULOMB,
    UNIT_VOLT,
    UNIT_FARAD,
    UNIT_OHM,
    UNIT_SIEMENS,
    UNIT_WEBER,
    UNIT_TESLA,
    UNIT_HENRY,
    UNIT_DEGREE_CELSIUS,
    UNIT_LUMEN,
    UNIT_LUX,
    UNIT_BECQUEREL,
    UNIT_GRAY,
    UNIT_SIEVERT,
    UNIT_KATAL,
    /* Add more units as needed */
    UNIT_CUSTOM  /* For custom-defined units */
} UnitType;

/**
 * @brief Structure to represent physical unit with exponents
 * 
 * This allows representation of complex units like m/s², kg·m/s², etc.
 */
typedef struct {
    UnitType type;
    int8_t exponent;  /* Positive for numerator, negative for denominator */
} UnitComponent;

/**
 * @brief Structure to represent a complete unit with multiple components
 */
typedef struct {
    UnitComponent* components;
    uint8_t count;
} Unit;

/**
 * @brief Structure defining a vector with unit information
 */
typedef struct {
    float* data;          /* Vector data */
    uint16_t dim;         /* Dimension of the vector */
    Unit* units;          /* Units for each component */
    bool uniform_units;   /* Flag indicating if all components have the same unit */
} Vector;

/**
 * @brief Structure defining a matrix with unit information
 */
typedef struct {
    float* data;          /* Matrix data in row-major order */
    uint16_t rows;        /* Number of rows */
    uint16_t cols;        /* Number of columns */
    Unit* units;          /* Units for each element or uniform unit */
    bool uniform_units;   /* Flag indicating if all elements have the same unit */
} Matrix;

/**
 * @brief Errors that can occur during vector operations
 */
typedef enum {
    VECTOR_SUCCESS = 0,
    VECTOR_NULL_POINTER,
    VECTOR_DIMENSION_MISMATCH,
    VECTOR_UNIT_MISMATCH,
    VECTOR_INVALID_DIMENSION,
    VECTOR_MEMORY_ERROR,
    VECTOR_INCOMPATIBLE_OPERATION,
    VECTOR_NOT_IMPLEMENTED
} VectorError;

/**
 * @brief Get a description for a vector error
 * 
 * @param error The error code
 * @return const char* Description of the error
 */
const char* vector_error_string(VectorError error);

/**
 * @brief Initialize a unit structure
 * 
 * @param unit Pointer to unit structure to initialize
 * @param capacity Initial capacity for unit components
 * @return VectorError Error code
 */
VectorError unit_init(Unit* unit, uint8_t capacity);

/**
 * @brief Add a component to a unit
 * 
 * @param unit Pointer to unit structure
 * @param type Type of the unit component
 * @param exponent Exponent of the unit component
 * @return VectorError Error code
 */
VectorError unit_add_component(Unit* unit, UnitType type, int8_t exponent);

/**
 * @brief Free a unit structure
 * 
 * @param unit Pointer to unit structure to free
 * @return VectorError Error code
 */
VectorError unit_free(Unit* unit);

/**
 * @brief Check if two units are compatible
 * 
 * @param unit1 First unit
 * @param unit2 Second unit
 * @return bool True if units are compatible, false otherwise
 */
bool unit_are_compatible(const Unit* unit1, const Unit* unit2);

/**
 * @brief Multiply two units
 * 
 * @param result Pointer to result unit
 * @param unit1 First unit
 * @param unit2 Second unit
 * @return VectorError Error code
 */
VectorError unit_multiply(Unit* result, const Unit* unit1, const Unit* unit2);

/**
 * @brief Divide two units
 * 
 * @param result Pointer to result unit
 * @param unit1 First unit (numerator)
 * @param unit2 Second unit (denominator)
 * @return VectorError Error code
 */
VectorError unit_divide(Unit* result, const Unit* unit1, const Unit* unit2);

/**
 * @brief Initialize a vector with specified dimension
 * 
 * @param vector Pointer to vector structure to initialize
 * @param dim Dimension of the vector
 * @param uniform_units Flag indicating if all components have the same unit
 * @return VectorError Error code
 */
VectorError vector_init(Vector* vector, uint16_t dim, bool uniform_units);

/**
 * @brief Initialize a matrix with specified dimensions
 * 
 * @param matrix Pointer to matrix structure to initialize
 * @param rows Number of rows
 * @param cols Number of columns
 * @param uniform_units Flag indicating if all elements have the same unit
 * @return VectorError Error code
 */
VectorError matrix_init(Matrix* matrix, uint16_t rows, uint16_t cols, bool uniform_units);

/**
 * @brief Set the unit for a vector component
 * 
 * @param vector Pointer to vector structure
 * @param index Index of the component
 * @param unit Unit to set
 * @return VectorError Error code
 */
VectorError vector_set_unit(Vector* vector, uint16_t index, const Unit* unit);

/**
 * @brief Set the same unit for all vector components
 * 
 * @param vector Pointer to vector structure
 * @param unit Unit to set
 * @return VectorError Error code
 */
VectorError vector_set_uniform_unit(Vector* vector, const Unit* unit);

/**
 * @brief Set the value of a vector component
 * 
 * @param vector Pointer to vector structure
 * @param index Index of the component
 * @param value Value to set
 * @return VectorError Error code
 */
VectorError vector_set_value(Vector* vector, uint16_t index, float value);

/**
 * @brief Get the value of a vector component
 * 
 * @param vector Pointer to vector structure
 * @param index Index of the component
 * @param value Pointer to store the value
 * @return VectorError Error code
 */
VectorError vector_get_value(const Vector* vector, uint16_t index, float* value);

/**
 * @brief Free a vector structure
 * 
 * @param vector Pointer to vector structure to free
 * @return VectorError Error code
 */
VectorError vector_free(Vector* vector);

/**
 * @brief Free a matrix structure
 * 
 * @param matrix Pointer to matrix structure to free
 * @return VectorError Error code
 */
VectorError matrix_free(Matrix* matrix);

/**
 * @brief Add two vectors
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_add(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief Subtract vector b from vector a
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_subtract(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief Calculate the dot product of two vectors
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_dot_product(float* result, Unit* result_unit, const Vector* a, const Vector* b);

/**
 * @brief Calculate the cross product of two 3D vectors
 * 
 * @param result Pointer to store the result
 * @param a First vector
 * @param b Second vector
 * @return VectorError Error code
 */
VectorError vector_cross_product(Vector* result, const Vector* a, const Vector* b);

/**
 * @brief Calculate the magnitude of a vector
 * 
 * @param vector Input vector
 * @param result Pointer to store the magnitude
 * @param result_unit Pointer to store the unit of the magnitude
 * @return VectorError Error code
 */
VectorError vector_magnitude(const Vector* vector, float* result, Unit* result_unit);

/**
 * @brief Normalize a vector
 * 
 * @param result Pointer to store the normalized vector
 * @param vector Input vector
 * @return VectorError Error code
 */
VectorError vector_normalize(Vector* result, const Vector* vector);

/**
 * @brief Scale a vector by a scalar
 * 
 * @param result Pointer to store the result
 * @param vector Input vector
 * @param scalar Scalar value
 * @param scalar_unit Unit of the scalar (can be NULL for unitless scalar)
 * @return VectorError Error code
 */
VectorError vector_scale(Vector* result, const Vector* vector, float scalar, const Unit* scalar_unit);

/**
 * @brief Calculate the determinant of a matrix
 * 
 * @param matrix Input matrix
 * @param result Pointer to store the determinant
 * @param result_unit Pointer to store the unit of the determinant
 * @return VectorError Error code
 */
VectorError matrix_determinant(const Matrix* matrix, float* result, Unit* result_unit);

/**
 * @brief Convert a vector to a matrix with one column
 * 
 * @param result Pointer to store the matrix
 * @param vector Input vector
 * @return VectorError Error code
 */
VectorError vector_to_matrix(Matrix* result, const Vector* vector);

/**
 * @brief Multiply a matrix by a vector
 * 
 * @param result Pointer to store the result vector
 * @param matrix Input matrix
 * @param vector Input vector
 * @return VectorError Error code
 */
VectorError matrix_vector_multiply(Vector* result, const Matrix* matrix, const Vector* vector);

/**
 * @brief Multiply two matrices
 * 
 * @param result Pointer to store the result matrix
 * @param a First matrix
 * @param b Second matrix
 * @return VectorError Error code
 */
VectorError matrix_multiply(Matrix* result, const Matrix* a, const Matrix* b);

#endif /* VECTOR_MATH_H */