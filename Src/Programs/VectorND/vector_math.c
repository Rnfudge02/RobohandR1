#include "vector_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize a Unit structure with specified capacity
 * @param unit Pointer to Unit structure to initialize (must not be NULL)
 * @param capacity Maximum number of components this unit can hold (0 for dimensionless)
 * @return VECTOR_SUCCESS on success, error code on failure
 * 
 * @note A capacity of 0 creates a dimensionless unit (no components)
 * @note For units with components, consider using at least capacity 2-4
 */
VectorError unit_init(Unit* unit, uint8_t capacity) {
    if (unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    // Handle dimensionless units (capacity == 0) explicitly
    if (capacity == 0) {
        unit->components = NULL;
        unit->capacity = 0;
        unit->count = 0;
        return VECTOR_SUCCESS;
    }
    
    // Allocate memory for components
    unit->components = (UnitComponent*)calloc(capacity, sizeof(UnitComponent));
    if (unit->components == NULL) {
        return VECTOR_MEMORY_ERROR;
    }
    
    // Initialize all fields
    unit->capacity = capacity;
    unit->count = 0;
    
    return VECTOR_SUCCESS;
}

/**
 * @brief Create a dimensionless unit (convenience function)
 * @param unit Pointer to Unit structure to initialize
 * @return VECTOR_SUCCESS on success, error code on failure
 */
VectorError unit_init_dimensionless(Unit* unit) {
    return unit_init(unit, 0);
}

VectorError unit_add_component(Unit* unit, UnitType type, int8_t exponent) {
    if (unit == NULL) {
        return VECTOR_NULL_POINTER;
    }

    // Search for existing component
    for (uint8_t i = 0; i < unit->count; i++) {
        if (unit->components[i].type != type) {
            continue;  // Skip non-matching types
        }
        
        // Found matching component - update exponent
        unit->components[i].exponent += exponent;
        
        // Early return if component still has non-zero exponent
        if (unit->components[i].exponent != 0) {
            return VECTOR_SUCCESS;
        }
        
        // Component exponent is now zero - remove it by shifting
        for (uint8_t j = i; j < unit->count - 1; j++) {
            unit->components[j] = unit->components[j + 1];
        }
        unit->count--;
        return VECTOR_SUCCESS;
    }

    // Component not found - add new one only if exponent is non-zero
    if (exponent == 0) {
        return VECTOR_SUCCESS;
    }
    
    // Reallocate memory for new component
    UnitComponent* new_components = (UnitComponent*)realloc(unit->components,
                                                          (unit->count + 1) * sizeof(UnitComponent));
    if (new_components == NULL) {
        return VECTOR_MEMORY_ERROR;
    }
    
    // Add the new component
    unit->components = new_components;
    unit->components[unit->count].type = type;
    unit->components[unit->count].exponent = exponent;
    unit->count++;
    
    return VECTOR_SUCCESS;
}

VectorError unit_free(Unit* unit) {
    if (unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (unit->components != NULL) {
        free(unit->components);
        unit->components = NULL;
    }
    unit->count = 0;
    
    return VECTOR_SUCCESS;
}

bool unit_are_compatible(const Unit* unit1, const Unit* unit2) {
    if (unit1 == NULL || unit2 == NULL) {
        return false;
    }
    
    /* Units are compatible if they have the same components with same exponents */
    if (unit1->count != unit2->count) {
        return false;
    }
    
    /* Check each component */
    for (uint8_t i = 0; i < unit1->count; i++) {
        bool found = false;
        for (uint8_t j = 0; j < unit2->count; j++) {
            if (unit1->components[i].type == unit2->components[j].type &&
                unit1->components[i].exponent == unit2->components[j].exponent) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    
    return true;
}

VectorError unit_multiply(Unit* result, const Unit* unit1, const Unit* unit2) {
    if (result == NULL || unit1 == NULL || unit2 == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Initialize result with capacity for all possible components */
    VectorError err = unit_init(result, unit1->count + unit2->count);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Add components from first unit */
    for (uint8_t i = 0; i < unit1->count; i++) {
        err = unit_add_component(result, unit1->components[i].type, 
                               unit1->components[i].exponent);
        if (err != VECTOR_SUCCESS) {
            unit_free(result);
            return err;
        }
    }
    
    /* Add components from second unit */
    for (uint8_t i = 0; i < unit2->count; i++) {
        err = unit_add_component(result, unit2->components[i].type, 
                               unit2->components[i].exponent);
        if (err != VECTOR_SUCCESS) {
            unit_free(result);
            return err;
        }
    }
    
    return VECTOR_SUCCESS;
}

VectorError unit_divide(Unit* result, const Unit* unit1, const Unit* unit2) {
    if (result == NULL || unit1 == NULL || unit2 == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Initialize result with capacity for all possible components */
    VectorError err = unit_init(result, unit1->count + unit2->count);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Add components from first unit */
    for (uint8_t i = 0; i < unit1->count; i++) {
        err = unit_add_component(result, unit1->components[i].type, 
                               unit1->components[i].exponent);
        if (err != VECTOR_SUCCESS) {
            unit_free(result);
            return err;
        }
    }
    
    /* Subtract components from second unit (negative exponents) */
    for (uint8_t i = 0; i < unit2->count; i++) {
        err = unit_add_component(result, unit2->components[i].type, 
                               -unit2->components[i].exponent);
        if (err != VECTOR_SUCCESS) {
            unit_free(result);
            return err;
        }
    }
    
    return VECTOR_SUCCESS;
}

/* Vector accessor functions */
VectorError vector_set_value(Vector* vector, uint16_t index, float value) {
    if (vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (index >= vector->dim) {
        return VECTOR_INVALID_DIMENSION;
    }
    
    vector->data[index] = value;
    return VECTOR_SUCCESS;
}

VectorError vector_get_value(const Vector* vector, uint16_t index, float* value) {
    if (vector == NULL || value == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (index >= vector->dim) {
        return VECTOR_INVALID_DIMENSION;
    }
    
    *value = vector->data[index];
    return VECTOR_SUCCESS;
}

VectorError vector_set_unit(Vector* vector, uint16_t index, const Unit* unit) {
    if (vector == NULL || unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (vector->uniform_units) {
        if (index != 0) {
            return VECTOR_INCOMPATIBLE_OPERATION;
        }
        index = 0;  /* Set the single uniform unit */
    } else if (index >= vector->dim) {
        return VECTOR_INVALID_DIMENSION;
    }
    
    /* Free existing unit */
    unit_free(&vector->units[index]);
    
    /* Copy new unit */
    VectorError err = unit_init(&vector->units[index], unit->count);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    for (uint8_t i = 0; i < unit->count; i++) {
        err = unit_add_component(&vector->units[index], 
                               unit->components[i].type,
                               unit->components[i].exponent);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    }
    
    return VECTOR_SUCCESS;
}

VectorError vector_set_uniform_unit(Vector* vector, const Unit* unit) {
    if (vector == NULL || unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (!vector->uniform_units) {
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    return vector_set_unit(vector, 0, unit);
}

VectorError vector_free(Vector* vector) {
    if (vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (vector->data != NULL) {
        free(vector->data);
        vector->data = NULL;
    }
    
    if (vector->units != NULL) {
        uint16_t unit_count = vector->uniform_units ? 1 : vector->dim;
        for (uint16_t i = 0; i < unit_count; i++) {
            unit_free(&vector->units[i]);
        }
        free(vector->units);
        vector->units = NULL;
    }
    
    vector->dim = 0;
    vector->uniform_units = false;
    
    return VECTOR_SUCCESS;
}

VectorError matrix_free(Matrix* matrix) {
    if (matrix == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (matrix->data != NULL) {
        free(matrix->data);
        matrix->data = NULL;
    }
    
    if (matrix->units != NULL) {
        uint32_t unit_count = matrix->uniform_units ? 1 : (matrix->rows * matrix->cols);
        for (uint32_t i = 0; i < unit_count; i++) {
            unit_free(&matrix->units[i]);
        }
        free(matrix->units);
        matrix->units = NULL;
    }
    
    matrix->rows = 0;
    matrix->cols = 0;
    matrix->uniform_units = false;
    
    return VECTOR_SUCCESS;
}

/* Error string function */
const char* vector_error_string(VectorError error) {
    switch (error) {
        case VECTOR_SUCCESS: return "Success";
        case VECTOR_NULL_POINTER: return "Null pointer";
        case VECTOR_DIMENSION_MISMATCH: return "Dimension mismatch";
        case VECTOR_UNIT_MISMATCH: return "Unit mismatch";
        case VECTOR_INVALID_DIMENSION: return "Invalid dimension";
        case VECTOR_MEMORY_ERROR: return "Memory error";
        case VECTOR_INCOMPATIBLE_OPERATION: return "Incompatible operation";
        case VECTOR_NOT_IMPLEMENTED: return "Not implemented";
        default: return "Unknown error";
    }
}

/* Optimized vector addition using ARM DSP */
VectorError vector_add(Vector* result, const Vector* a, const Vector* b) {
    if (result == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (a->dim != b->dim) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Check unit compatibility */
    if (a->uniform_units && b->uniform_units) {
        if (!unit_are_compatible(a->units, b->units)) {
            return VECTOR_UNIT_MISMATCH;
        }
    } else if (a->uniform_units != b->uniform_units) {
        return VECTOR_UNIT_MISMATCH;
    }
    
    /* Initialize result if needed */
    if (result->dim != a->dim) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) return err;
        
        err = vector_init(result, a->dim, a->uniform_units);
        if (err != VECTOR_SUCCESS) return err;
    }
    
    /* Copy units to result */
    if (a->uniform_units) {
        VectorError err = vector_set_uniform_unit(result, a->units);
        if (err != VECTOR_SUCCESS) return err;
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
            VectorError err = vector_set_unit(result, i, &a->units[i]);
            if (err != VECTOR_SUCCESS) return err;
        }
    }
    
    /* Use ARM DSP for vector addition */
    arm_add_f32(a->data, b->data, result->data, a->dim);
    
    return VECTOR_SUCCESS;
}

/* Optimized vector subtraction using ARM DSP */
VectorError vector_subtract(Vector* result, const Vector* a, const Vector* b) {
    if (result == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (a->dim != b->dim) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Check unit compatibility */
    if (a->uniform_units && b->uniform_units) {
        if (!unit_are_compatible(a->units, b->units)) {
            return VECTOR_UNIT_MISMATCH;
        }
    } else if (a->uniform_units != b->uniform_units) {
        return VECTOR_UNIT_MISMATCH;
    }
    
    /* Initialize result if needed */
    if (result->dim != a->dim) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) return err;
        
        err = vector_init(result, a->dim, a->uniform_units);
        if (err != VECTOR_SUCCESS) return err;
    }
    
    /* Copy units to result */
    if (a->uniform_units) {
        VectorError err = vector_set_uniform_unit(result, a->units);
        if (err != VECTOR_SUCCESS) return err;
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
            VectorError err = vector_set_unit(result, i, &a->units[i]);
            if (err != VECTOR_SUCCESS) return err;
        }
    }
    
    /* Use ARM DSP for vector subtraction */
    arm_sub_f32(a->data, b->data, result->data, a->dim);
    
    return VECTOR_SUCCESS;
}

/* Optimized dot product using ARM DSP */
VectorError vector_dot_product(float* result, Unit* result_unit, const Vector* a, const Vector* b) {
    if (result == NULL || result_unit == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (a->dim != b->dim) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Calculate result unit */
    if (a->uniform_units && b->uniform_units) {
        VectorError err = unit_multiply(result_unit, a->units, b->units);
        if (err != VECTOR_SUCCESS) return err;
    } else if (!a->uniform_units && !b->uniform_units) {
        /* Check that all component products have the same unit */
        Unit temp_unit;
        Unit first_unit;
        VectorError err = unit_multiply(&first_unit, &a->units[0], &b->units[0]);
        if (err != VECTOR_SUCCESS) return err;
        
        for (uint16_t i = 1; i < a->dim; i++) {
            err = unit_multiply(&temp_unit, &a->units[i], &b->units[i]);
            if (err != VECTOR_SUCCESS) {
                unit_free(&first_unit);
                return err;
            }
            
            if (!unit_are_compatible(&first_unit, &temp_unit)) {
                unit_free(&first_unit);
                unit_free(&temp_unit);
                return VECTOR_UNIT_MISMATCH;
            }
            unit_free(&temp_unit);
        }
        
        *result_unit = first_unit;
    } else {
        return VECTOR_UNIT_MISMATCH;
    }
    
    /* Use ARM DSP for dot product */
    arm_dot_prod_f32(a->data, b->data, a->dim, result);
    
    return VECTOR_SUCCESS;
}

/* Optimized magnitude calculation using ARM DSP */
VectorError vector_magnitude(const Vector* vector, float* result, Unit* result_unit) {
    if (vector == NULL || result == NULL || result_unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Calculate result unit (same as vector components) */
    if (vector->uniform_units) {
        VectorError err = unit_init(result_unit, vector->units->count);
        if (err != VECTOR_SUCCESS) return err;
        
        for (uint8_t i = 0; i < vector->units->count; i++) {
            err = unit_add_component(result_unit, 
                                   vector->units->components[i].type,
                                   vector->units->components[i].exponent);
            if (err != VECTOR_SUCCESS) {
                unit_free(result_unit);
                return err;
            }
        }
    } else {
        /* All components must have the same unit for magnitude */
        for (uint16_t i = 1; i < vector->dim; i++) {
            if (!unit_are_compatible(&vector->units[0], &vector->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
        
        VectorError err = unit_init(result_unit, vector->units[0].count);
        if (err != VECTOR_SUCCESS) return err;
        
        for (uint8_t i = 0; i < vector->units[0].count; i++) {
            err = unit_add_component(result_unit, 
                                   vector->units[0].components[i].type,
                                   vector->units[0].components[i].exponent);
            if (err != VECTOR_SUCCESS) {
                unit_free(result_unit);
                return err;
            }
        }
    }
    
    /* Calculate magnitude using ARM DSP - more efficient than separate dot product and sqrt */
    float sum_of_squares;
    arm_dot_prod_f32(vector->data, vector->data, vector->dim, &sum_of_squares);
    
    /* Use ARM DSP square root if available, otherwise use standard sqrt */
    #ifdef ARM_MATH_DSP
        arm_sqrt_f32(sum_of_squares, result);
    #else
        *result = sqrtf(sum_of_squares);
    #endif
    
    return VECTOR_SUCCESS;
}

/* Optimized cross product for 3D vectors */
// Helper function to ensure result vector is properly initialized
static VectorError ensure_result_vector_3d(Vector* result, bool uniform_units) {
    if (result->dim == 3) {
        return VECTOR_SUCCESS;  // Already correct size
    }
    
    VectorError err = vector_free(result);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    return vector_init(result, 3, uniform_units);
}

// Helper function to handle uniform unit multiplication
static VectorError handle_uniform_units(Vector* result, const Vector* a, const Vector* b) {
    Unit result_unit;
    VectorError err = unit_multiply(&result_unit, a->units, b->units);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    err = vector_set_uniform_unit(result, &result_unit);
    unit_free(&result_unit);
    return err;
}

// Helper function to validate unit compatibility for cross product
static VectorError validate_non_uniform_units(const Vector* a, const Vector* b) {
    // For cross product: result[i] units must be compatible across components
    // This is a simplified check - could be expanded for more rigorous validation
    Unit temp1;
    Unit temp2;
    VectorError err;
    
    err = unit_multiply(&temp1, &a->units[1], &b->units[2]);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    err = unit_multiply(&temp2, &a->units[2], &b->units[1]);
    if (err != VECTOR_SUCCESS) {
        unit_free(&temp1);
        return err;
    }
    
    bool compatible = unit_are_compatible(&temp1, &temp2);
    unit_free(&temp1);
    unit_free(&temp2);
    
    return compatible ? VECTOR_SUCCESS : VECTOR_UNIT_MISMATCH;
}

// Helper function to set non-uniform units for cross product result
static VectorError handle_non_uniform_units(Vector* result, const Vector* a, const Vector* b) {
    VectorError err = validate_non_uniform_units(a, b);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    // Calculate units for first component (others will be similar)
    Unit component_unit;
    err = unit_multiply(&component_unit, &a->units[1], &b->units[2]);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    // Set units for all components (simplified - assumes all compatible)
    for (int i = 0; i < 3; i++) {
        err = vector_set_unit(result, (uint16_t) i, &component_unit);
        if (err != VECTOR_SUCCESS) {
            break;
        }
    }
    
    unit_free(&component_unit);
    return err;
}

// Helper function to handle all unit calculations
static VectorError calculate_result_units(Vector* result, const Vector* a, const Vector* b) {
    bool a_uniform = a->uniform_units;
    bool b_uniform = b->uniform_units;
    
    if (a_uniform && b_uniform) {
        return handle_uniform_units(result, a, b);
    }
    
    if (!a_uniform && !b_uniform) {
        return handle_non_uniform_units(result, a, b);
    }
    
    // Mixed uniform/non-uniform case
    return VECTOR_UNIT_MISMATCH;
}

// Helper function to perform the actual cross product calculation
static void calculate_cross_product_values(Vector* result, const Vector* a, const Vector* b) {
    // SIMD-friendly aligned arrays
    float a_data[4] __attribute__((aligned(16)));
    float b_data[4] __attribute__((aligned(16)));
    float result_data[4] __attribute__((aligned(16)));
    
    // Copy data with padding
    memcpy(a_data, a->data, 3 * sizeof(float));
    memcpy(b_data, b->data, 3 * sizeof(float));
    a_data[3] = 0.0f;
    b_data[3] = 0.0f;
    
    // Cross product: result = a × b
    result_data[0] = a_data[1] * b_data[2] - a_data[2] * b_data[1];
    result_data[1] = a_data[2] * b_data[0] - a_data[0] * b_data[2];
    result_data[2] = a_data[0] * b_data[1] - a_data[1] * b_data[0];
    
    // Copy result back
    memcpy(result->data, result_data, 3 * sizeof(float));
}

// Main function - now much simpler and focused
VectorError vector_cross_product(Vector* result, const Vector* a, const Vector* b) {
    // Input validation
    if (result == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    // Cross product is only defined for 3D vectors
    if (a->dim != 3 || b->dim != 3) {
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    // Ensure result vector is properly sized
    VectorError err = ensure_result_vector_3d(result, a->uniform_units && b->uniform_units);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    // Handle unit calculations
    err = calculate_result_units(result, a, b);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    // Perform the actual calculation
    calculate_cross_product_values(result, a, b);
    
    return VECTOR_SUCCESS;
}

/* Vector initialization */
VectorError vector_init(Vector* vector, uint16_t dim, bool uniform_units) {
    if (vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (dim == 0) {
        return VECTOR_INVALID_DIMENSION;
    }
    
    /* Allocate aligned memory for SIMD operations */
    vector->data = (float*)aligned_alloc(16, dim * sizeof(float));
    if (vector->data == NULL) {
        return VECTOR_MEMORY_ERROR;
    }
    
    /* Initialize data to zero */
    memset(vector->data, 0, dim * sizeof(float));
    
    vector->dim = dim;
    vector->uniform_units = uniform_units;
    
    /* Allocate units */
    if (uniform_units) {
        vector->units = (Unit*)calloc(1, sizeof(Unit));
    } else {
        vector->units = (Unit*)calloc(dim, sizeof(Unit));
    }
    
    if (vector->units == NULL) {
        free(vector->data);
        return VECTOR_MEMORY_ERROR;
    }
    
    /* Initialize units */
    uint16_t unit_count = uniform_units ? 1 : dim;
    for (uint16_t i = 0; i < unit_count; i++) {
        VectorError err = unit_init(&vector->units[i], 1);
        if (err != VECTOR_SUCCESS) {
            /* Clean up on error */
            for (uint16_t j = 0; j < i; j++) {
                unit_free(&vector->units[j]);
            }
            free(vector->units);
            free(vector->data);
            return err;
        }
    }
    
    return VECTOR_SUCCESS;
}

/* Matrix initialization */
VectorError matrix_init(Matrix* matrix, uint16_t rows, uint16_t cols, bool uniform_units) {
    if (matrix == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (rows == 0 || cols == 0) {
        return VECTOR_INVALID_DIMENSION;
    }
    
    /* Allocate aligned memory for SIMD operations */
    uint32_t total_elements = rows * cols;
    matrix->data = (float*)aligned_alloc(16, total_elements * sizeof(float));
    if (matrix->data == NULL) {
        return VECTOR_MEMORY_ERROR;
    }
    
    /* Initialize data to zero */
    memset(matrix->data, 0, total_elements * sizeof(float));
    
    matrix->rows = rows;
    matrix->cols = cols;
    matrix->uniform_units = uniform_units;
    
    /* Allocate units */
    if (uniform_units) {
        matrix->units = (Unit*)calloc(1, sizeof(Unit));
    } else {
        matrix->units = (Unit*)calloc(total_elements, sizeof(Unit));
    }
    
    if (matrix->units == NULL) {
        free(matrix->data);
        return VECTOR_MEMORY_ERROR;
    }
    
    /* Initialize units */
    uint32_t unit_count = uniform_units ? 1 : total_elements;
    for (uint32_t i = 0; i < unit_count; i++) {
        VectorError err = unit_init(&matrix->units[i], 1);
        if (err != VECTOR_SUCCESS) {
            /* Clean up on error */
            for (uint32_t j = 0; j < i; j++) {
                unit_free(&matrix->units[j]);
            }
            free(matrix->units);
            free(matrix->data);
            return err;
        }
    }
    
    return VECTOR_SUCCESS;
}

/* Helper function for efficient memory copies using DSP */
static inline void vector_memcpy_aligned(float* dst, const float* src, uint32_t count) {
    /* Use ARM DSP copy if available for better performance */
    arm_copy_f32(src, dst, count);
}