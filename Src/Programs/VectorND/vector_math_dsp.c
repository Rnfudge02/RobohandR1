/**
 * @file vector_math_dsp.c
 * @brief DSP-optimized implementations for vector mathematics on RP2350
 */

#include "vector_math_dsp.h"
#include <math.h>
#include <string.h>

/* Include ARM CMSIS-DSP specific headers */
#include "arm_math.h"


/* Define macros for vectorized operations if SIMD is available */
#define USE_SIMD_OPERATIONS 1

/* Check if processor supports DSP instructions */
static bool dsp_instructions_available = false;

/**
 * @brief Initialize DSP functions for vector math operations
 */
VectorError vector_dsp_init(void) {
    /* Check if DSP instructions are available on this processor */
    #if (__ARM_FEATURE_DSP == 1)
        dsp_instructions_available = true;
    #endif
    
    /* Set up any necessary hardware configurations */
    /* For example, enable FPU if available */
    #if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
        /* Enable FPU */
        SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));  /* Set CP10 and CP11 to full access */
    #endif
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized vector addition
 */
VectorError vector_dsp_add(Vector* result, const Vector* a, const Vector* b) {
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
    } else if (a->uniform_units) {
        for (uint16_t i = 0; i < b->dim; i++) {
            if (!unit_are_compatible(a->units, &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else if (b->uniform_units) {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], b->units)) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    }
    
    /* Initialize result vector if not already initialized */
    if (result->dim != a->dim) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        err = vector_init(result, a->dim, a->uniform_units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    }
    
    /* Copy units from a to result */
    if (a->uniform_units) {
        VectorError err = vector_set_uniform_unit(result, a->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            VectorError err = vector_set_unit(result, i, &a->units[i]);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    }
    
    /* Use SIMD for vector addition if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        /* Use optimized ARM CMSIS-DSP function */
        arm_add_f32(a->data, b->data, result->data, a->dim);
    } else {
        /* Fallback to scalar implementation */
        for (uint16_t i = 0; i < a->dim; i++) {
            result->data[i] = a->data[i] + b->data[i];
        }
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized vector subtraction
 */
VectorError vector_dsp_subtract(Vector* result, const Vector* a, const Vector* b) {
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
    } else if (a->uniform_units) {
        for (uint16_t i = 0; i < b->dim; i++) {
            if (!unit_are_compatible(a->units, &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else if (b->uniform_units) {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], b->units)) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    }
    
    /* Initialize result vector if not already initialized */
    if (result->dim != a->dim) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        err = vector_init(result, a->dim, a->uniform_units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    }
    
    /* Copy units from a to result */
    if (a->uniform_units) {
        VectorError err = vector_set_uniform_unit(result, a->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    } else {
        for (uint16_t i = 0; i < a->dim; i++) {
            VectorError err = vector_set_unit(result, i, &a->units[i]);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    }
    
    /* Use SIMD for vector subtraction if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        /* Use optimized ARM CMSIS-DSP function */
        arm_sub_f32(a->data, b->data, result->data, a->dim);
    } else {
        /* Fallback to scalar implementation */
        for (uint16_t i = 0; i < a->dim; i++) {
            result->data[i] = a->data[i] - b->data[i];
        }
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized dot product calculation
 */
VectorError vector_dsp_dot_product(float* result, Unit* result_unit, const Vector* a, const Vector* b) {
    if (result == NULL || result_unit == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    if (a->dim != b->dim) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Check unit compatibility and calculate result unit */
    Unit temp_unit;
    VectorError err;
    
    if (a->uniform_units && b->uniform_units) {
        /* Multiply uniform units */
        err = unit_multiply(&temp_unit, a->units, b->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    } else {
        /* Initialize result unit */
        err = unit_init(&temp_unit, 1);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        /* Check compatibility of each component and calculate result unit */
        if (a->uniform_units) {
            for (uint16_t i = 0; i < b->dim; i++) {
                if (!unit_are_compatible(a->units, &b->units[i])) {
                    unit_free(&temp_unit);
                    return VECTOR_UNIT_MISMATCH;
                }
            }
            
            /* For uniform a, use a's unit and first b unit */
            Unit temp_unit2;
            err = unit_multiply(&temp_unit2, a->units, &b->units[0]);
            if (err != VECTOR_SUCCESS) {
                unit_free(&temp_unit);
                return err;
            }
            
            /* Copy to result unit */
            unit_free(&temp_unit);
            temp_unit = temp_unit2;
        } else if (b->uniform_units) {
            for (uint16_t i = 0; i < a->dim; i++) {
                if (!unit_are_compatible(&a->units[i], b->units)) {
                    unit_free(&temp_unit);
                    return VECTOR_UNIT_MISMATCH;
                }
            }
            
            /* For uniform b, use first a unit and b's unit */
            Unit temp_unit2;
            err = unit_multiply(&temp_unit2, &a->units[0], b->units);
            if (err != VECTOR_SUCCESS) {
                unit_free(&temp_unit);
                return err;
            }
            
            /* Copy to result unit */
            unit_free(&temp_unit);
            temp_unit = temp_unit2;
        } else {
            /* Both non-uniform, check each component pair */
            for (uint16_t i = 0; i < a->dim; i++) {
                if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                    unit_free(&temp_unit);
                    return VECTOR_UNIT_MISMATCH;
                }
            }
            
            /* Use first components as representative for result unit */
            Unit temp_unit2;
            err = unit_multiply(&temp_unit2, &a->units[0], &b->units[0]);
            if (err != VECTOR_SUCCESS) {
                unit_free(&temp_unit);
                return err;
            }
            
            /* Copy to result unit */
            unit_free(&temp_unit);
            temp_unit = temp_unit2;
        }
    }
    
    /* Compute dot product using ARM DSP function if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        float32_t dot_result;
        arm_dot_prod_f32(a->data, b->data, a->dim, &dot_result);
        *result = dot_result;
    } else {
        /* Fallback to scalar implementation */
        float dot_result = 0.0f;
        for (uint16_t i = 0; i < a->dim; i++) {
            dot_result += a->data[i] * b->data[i];
        }
        *result = dot_result;
    }
    
    /* Set result unit */
    unit_free(result_unit);
    *result_unit = temp_unit;
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized cross product calculation (3D vectors only)
 */
VectorError vector_dsp_cross_product(Vector* result, const Vector* a, const Vector* b) {
    if (result == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Cross product is defined only for 3D vectors */
    if (a->dim != 3 || b->dim != 3) {
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    /* Check unit compatibility */
    if (a->uniform_units && b->uniform_units) {
        if (!unit_are_compatible(a->units, b->units)) {
            return VECTOR_UNIT_MISMATCH;
        }
    } else if (a->uniform_units) {
        for (uint16_t i = 0; i < 3; i++) {
            if (!unit_are_compatible(a->units, &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else if (b->uniform_units) {
        for (uint16_t i = 0; i < 3; i++) {
            if (!unit_are_compatible(&a->units[i], b->units)) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    } else {
        for (uint16_t i = 0; i < 3; i++) {
            if (!unit_are_compatible(&a->units[i], &b->units[i])) {
                return VECTOR_UNIT_MISMATCH;
            }
        }
    }
    
    /* Initialize result vector if not already initialized */
    if (result->dim != 3) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        err = vector_init(result, 3, a->uniform_units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    }
    
    /* Copy units from a to result */
    if (a->uniform_units) {
        VectorError err = vector_set_uniform_unit(result, a->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    } else {
        for (uint16_t i = 0; i < 3; i++) {
            VectorError err = vector_set_unit(result, i, &a->units[i]);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    }
    
    /* Compute cross product using SIMD optimizations if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        /* Use SIMD intrinsics if available */
        /* For Cortex-M33, we can use CMSIS-DSP intrinsics */
        
        /* Load vectors into SIMD registers */
        float32x2_t a_xy = vld1_f32(a->data);
        float32x2_t b_xy = vld1_f32(b->data);
        float32_t a_z = a->data[2];
        float32_t b_z = b->data[2];
        
        /* Compute a.y * b.z and a.z * b.y */
        float32_t temp1 = a->data[1] * b_z;
        float32_t temp2 = a_z * b->data[1];
        
        /* Compute a.z * b.x and a.x * b.z */
        float32_t temp3 = a_z * b->data[0];
        float32_t temp4 = a->data[0] * b_z;
        
        /* Compute x = a.y * b.z - a.z * b.y */
        result->data[0] = temp1 - temp2;
        
        /* Compute y = a.z * b.x - a.x * b.z */
        result->data[1] = temp3 - temp4;
        
        /* Compute z = a.x * b.y - a.y * b.x */
        result->data[2] = a->data[0] * b->data[1] - a->data[1] * b->data[0];
    } else {
        /* Fallback to scalar implementation */
        float temp[3];
        
        /* x = a.y * b.z - a.z * b.y */
        temp[0] = a->data[1] * b->data[2] - a->data[2] * b->data[1];
        
        /* y = a.z * b.x - a.x * b.z */
        temp[1] = a->data[2] * b->data[0] - a->data[0] * b->data[2];
        
        /* z = a.x * b.y - a.y * b.x */
        temp[2] = a->data[0] * b->data[1] - a->data[1] * b->data[0];
        
        /* Copy results */
        memcpy(result->data, temp, 3 * sizeof(float));
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized vector magnitude calculation
 */
VectorError vector_dsp_magnitude(const Vector* vector, float* result, Unit* result_unit) {
    if (vector == NULL || result == NULL || result_unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Initialize result unit */
    VectorError err;
    
    if (vector->uniform_units) {
        /* For uniform units, copy and double the exponents */
        err = unit_init(result_unit, vector->units->count);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        for (uint8_t i = 0; i < vector->units->count; i++) {
            unit_add_component(result_unit, vector->units->components[i].type, 
                               vector->units->components[i].exponent);
        }
    } else {
        /* For non-uniform units, use the first component as reference */
        err = unit_init(result_unit, vector->units[0].count);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        for (uint8_t i = 0; i < vector->units[0].count; i++) {
            unit_add_component(result_unit, vector->units[0].components[i].type, 
                               vector->units[0].components[i].exponent);
        }
        
        /* Verify that all components have the same unit */
        for (uint16_t i = 1; i < vector->dim; i++) {
            if (!unit_are_compatible(&vector->units[0], &vector->units[i])) {
                unit_free(result_unit);
                return VECTOR_UNIT_MISMATCH;
            }
        }
    }
    
    /* Compute magnitude using ARM DSP if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        float32_t mag_squared;
        arm_power_f32(vector->data, vector->dim, &mag_squared);
        *result = sqrtf(mag_squared);
    } else {
        /* Fallback to scalar implementation */
        float sum_squared = 0.0f;
        for (uint16_t i = 0; i < vector->dim; i++) {
            sum_squared += vector->data[i] * vector->data[i];
        }
        *result = sqrtf(sum_squared);
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized vector normalization
 */
VectorError vector_dsp_normalize(Vector* result, const Vector* vector) {
    if (result == NULL || vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Compute magnitude */
    float magnitude;
    Unit mag_unit;
    
    VectorError err = vector_dsp_magnitude(vector, &magnitude, &mag_unit);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Check for zero magnitude */
    if (magnitude == 0.0f) {
        unit_free(&mag_unit);
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    /* Initialize result vector if not already initialized */
    if (result->dim != vector->dim) {
        err = vector_free(result);
        if (err != VECTOR_SUCCESS) {
            unit_free(&mag_unit);
            return err;
        }
        
        err = vector_init(result, vector->dim, true);  /* Normalized vector has unitless components */
        if (err != VECTOR_SUCCESS) {
            unit_free(&mag_unit);
            return err;
        }
    }
    
    /* Set uniform unitless unit for result */
    Unit unitless;
    err = unit_init(&unitless, 1);
    if (err != VECTOR_SUCCESS) {
        unit_free(&mag_unit);
        return err;
    }
    
    err = vector_set_uniform_unit(result, &unitless);
    unit_free(&unitless);
    if (err != VECTOR_SUCCESS) {
        unit_free(&mag_unit);
        return err;
    }
    
    /* Scale each component by 1/magnitude using DSP if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        float32_t scale = 1.0f / magnitude;
        arm_scale_f32(vector->data, scale, result->data, vector->dim);
    } else {
        /* Fallback to scalar implementation */
        float scale = 1.0f / magnitude;
        for (uint16_t i = 0; i < vector->dim; i++) {
            result->data[i] = vector->data[i] * scale;
        }
    }
    
    unit_free(&mag_unit);
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized vector scaling by a scalar
 */
VectorError vector_dsp_scale(Vector* result, const Vector* vector, float scalar, const Unit* scalar_unit) {
    if (result == NULL || vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Initialize result vector if not already initialized */
    if (result->dim != vector->dim) {
        VectorError err = vector_free(result);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        err = vector_init(result, vector->dim, vector->uniform_units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    }
    
    /* Calculate result units */
    if (scalar_unit != NULL) {
        if (vector->uniform_units) {
            /* Calculate new unit */
            Unit new_unit;
            VectorError err = unit_multiply(&new_unit, vector->units, scalar_unit);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
            
            /* Set unit for result */
            err = vector_set_uniform_unit(result, &new_unit);
            unit_free(&new_unit);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        } else {
            /* Calculate new unit for each component */
            for (uint16_t i = 0; i < vector->dim; i++) {
                Unit new_unit;
                VectorError err = unit_multiply(&new_unit, &vector->units[i], scalar_unit);
                if (err != VECTOR_SUCCESS) {
                    return err;
                }
                
                /* Set unit for result component */
                err = vector_set_unit(result, i, &new_unit);
                unit_free(&new_unit);
                if (err != VECTOR_SUCCESS) {
                    return err;
                }
            }
        }
    } else {
        /* No scalar unit, copy units from vector */
        if (vector->uniform_units) {
            VectorError err = vector_set_uniform_unit(result, vector->units);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        } else {
            for (uint16_t i = 0; i < vector->dim; i++) {
                VectorError err = vector_set_unit(result, i, &vector->units[i]);
                if (err != VECTOR_SUCCESS) {
                    return err;
                }
            }
        }
    }
    
    /* Perform scaling using ARM DSP if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        arm_scale_f32(vector->data, scalar, result->data, vector->dim);
    } else {
        /* Fallback to scalar implementation */
        for (uint16_t i = 0; i < vector->dim; i++) {
            result->data[i] = vector->data[i] * scalar;
        }
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized matrix-vector multiplication
 */
VectorError matrix_dsp_vector_multiply(Vector* result, const Matrix* matrix, const Vector* vector) {
    if (result == NULL || matrix == NULL || vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Check dimensions */
    if (matrix->cols != vector->dim) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Initialize result vector */
    VectorError err = vector_free(result);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    err = vector_init(result, matrix->rows, matrix->uniform_units && vector->uniform_units);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Use ARM CMSIS-DSP for matrix-vector multiplication if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        /* Create matrix instance for matrix */
        arm_matrix_instance_f32 mat_inst;
        arm_mat_init_f32(&mat_inst, matrix->rows, matrix->cols, (float32_t *)matrix->data);
        
        /* Temporary matrix for result */
        arm_matrix_instance_f32 result_inst;
        float32_t temp_result[matrix->rows];
        arm_mat_init_f32(&result_inst, matrix->rows, 1, temp_result);
        
        /* Create matrix instance for vector */
        arm_matrix_instance_f32 vec_inst;
        arm_mat_init_f32(&vec_inst, vector->dim, 1, (float32_t *)vector->data);
        
        /* Perform multiplication */
        arm_status status = arm_mat_mult_f32(&mat_inst, &vec_inst, &result_inst);
        
        if (status == ARM_MATH_SUCCESS) {
            /* Copy result to output vector */
            memcpy(result->data, temp_result, matrix->rows * sizeof(float32_t));
        } else {
            /* Fallback to scalar implementation on error */
            for (uint16_t i = 0; i < matrix->rows; i++) {
                float sum = 0.0f;
                for (uint16_t j = 0; j < matrix->cols; j++) {
                    sum += matrix->data[i * matrix->cols + j] * vector->data[j];
                }
                result->data[i] = sum;
            }
        }
    } else {
        /* Fallback to scalar implementation */
        for (uint16_t i = 0; i < matrix->rows; i++) {
            float sum = 0.0f;
            for (uint16_t j = 0; j < matrix->cols; j++) {
                sum += matrix->data[i * matrix->cols + j] * vector->data[j];
            }
            result->data[i] = sum;
        }
    }
    
    /* Set units */
    if (matrix->uniform_units && vector->uniform_units) {
        /* Calculate result unit */
        Unit result_unit;
        err = unit_multiply(&result_unit, matrix->units, vector->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        /* Set unit for all components */
        err = vector_set_uniform_unit(result, &result_unit);
        unit_free(&result_unit);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
    } else if (matrix->uniform_units) {
        /* For each component of the result vector */
        for (uint16_t i = 0; i < matrix->rows; i++) {
            Unit result_unit;
            
            /* Calculate unit for this component */
            err = unit_multiply(&result_unit, matrix->units, &vector->units[0]);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
            
            /* Set unit for this component */
            err = vector_set_unit(result, i, &result_unit);
            unit_free(&result_unit);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    } else if (vector->uniform_units) {
        /* For each component of the result vector */
        for (uint16_t i = 0; i < matrix->rows; i++) {
            Unit result_unit;
            
            /* Calculate unit for this component */
            err = unit_multiply(&result_unit, &matrix->units[i * matrix->cols], vector->units);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
            
            /* Set unit for this component */
            err = vector_set_unit(result, i, &result_unit);
            unit_free(&result_unit);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    } else {
        /* Both non-uniform, simplified handling for first elements */
        for (uint16_t i = 0; i < matrix->rows; i++) {
            Unit result_unit;
            
            /* Use first column for unit calculation */
            err = unit_multiply(&result_unit, &matrix->units[i * matrix->cols], &vector->units[0]);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
            
            /* Set unit for this component */
            err = vector_set_unit(result, i, &result_unit);
            unit_free(&result_unit);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
        }
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized matrix multiplication
 */
VectorError matrix_dsp_multiply(Matrix* result, const Matrix* a, const Matrix* b) {
    if (result == NULL || a == NULL || b == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Check dimensions */
    if (a->cols != b->rows) {
        return VECTOR_DIMENSION_MISMATCH;
    }
    
    /* Initialize result matrix */
    VectorError err = matrix_free(result);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    err = matrix_init(result, a->rows, b->cols, a->uniform_units && b->uniform_units);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Calculate unit for uniform case */
    if (a->uniform_units && b->uniform_units) {
        Unit result_unit;
        err = unit_multiply(&result_unit, a->units, b->units);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        /* Copy to result matrix */
        unit_free(result->units);
        *result->units = result_unit;
    }
    
    /* Use ARM CMSIS-DSP for matrix multiplication if available */
    if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
        /* Create matrix instances */
        arm_matrix_instance_f32 a_inst, b_inst, result_inst;
        
        /* Initialize matrix instances */
        arm_mat_init_f32(&a_inst, a->rows, a->cols, (float32_t*)a->data);
        arm_mat_init_f32(&b_inst, b->rows, b->cols, (float32_t*)b->data);
        arm_mat_init_f32(&result_inst, result->rows, result->cols, result->data);
        
        /* Perform multiplication */
        arm_status status = arm_mat_mult_f32(&a_inst, &b_inst, &result_inst);
        
        /* If DSP matrix multiplication fails, fall back to scalar implementation */
        if (status != ARM_MATH_SUCCESS) {
            /* Fallback to scalar implementation */
            for (uint16_t i = 0; i < a->rows; i++) {
                for (uint16_t j = 0; j < b->cols; j++) {
                    float sum = 0.0f;
                    for (uint16_t k = 0; k < a->cols; k++) {
                        sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
                    }
                    result->data[i * result->cols + j] = sum;
                }
            }
        }
    } else {
        /* Scalar implementation */
        for (uint16_t i = 0; i < a->rows; i++) {
            for (uint16_t j = 0; j < b->cols; j++) {
                float sum = 0.0f;
                for (uint16_t k = 0; k < a->cols; k++) {
                    sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
                }
                result->data[i * result->cols + j] = sum;
            }
        }
    }
    
    /* Set units for non-uniform case */
    if (!(a->uniform_units && b->uniform_units)) {
        /* For simplicity, handle only the case where both matrices have uniform units */
        /* A more complete implementation would handle all combinations */
        if (a->uniform_units && !b->uniform_units) {
            return VECTOR_NOT_IMPLEMENTED;
        } else if (!a->uniform_units && b->uniform_units) {
            return VECTOR_NOT_IMPLEMENTED;
        } else {
            return VECTOR_NOT_IMPLEMENTED;
        }
    }
    
    return VECTOR_SUCCESS;
}

/**
 * @brief DSP-optimized determinant calculation
 */
VectorError matrix_dsp_determinant(const Matrix* matrix, float* result, Unit* result_unit) {
    if (matrix == NULL || result == NULL || result_unit == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Determinant is defined only for square matrices */
    if (matrix->rows != matrix->cols) {
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    /* Initialize result unit */
    VectorError err;
    
    if (matrix->uniform_units) {
        /* For uniform units, copy and multiply by dimension */
        err = unit_init(result_unit, matrix->units->count);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        for (uint8_t i = 0; i < matrix->units->count; i++) {
            unit_add_component(result_unit, matrix->units->components[i].type, 
                              matrix->units->components[i].exponent * matrix->rows);
        }
    } else {
        /* For non-uniform units, we would need to check compatibility and compute the result unit */
        /* For simplicity, let's assume all elements have the same unit */
        err = unit_init(result_unit, matrix->units[0].count);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        for (uint8_t i = 0; i < matrix->units[0].count; i++) {
            unit_add_component(result_unit, matrix->units[0].components[i].type, 
                              matrix->units[0].components[i].exponent * matrix->rows);
        }
        
        /* Check that all elements have the same unit */
        for (uint16_t i = 1; i < matrix->rows * matrix->cols; i++) {
            if (!unit_are_compatible(&matrix->units[0], &matrix->units[i])) {
                unit_free(result_unit);
                return VECTOR_UNIT_MISMATCH;
            }
        }
    }
    
    /* Use optimized implementations based on matrix size */
    switch (matrix->rows) {
        case 1:
            /* 1x1 matrix */
            *result = matrix->data[0];
            break;
            
        case 2:
            /* 2x2 matrix */
            *result = matrix->data[0] * matrix->data[3] - matrix->data[1] * matrix->data[2];
            break;
            
        case 3: {
            /* 3x3 matrix - using SIMD operations if available */
            if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
                /* Load matrix data */
                float32x2_t m0 = vld1_f32(&matrix->data[0]); /* a, b */
                float32_t c = matrix->data[2];
                float32x2_t m1 = vld1_f32(&matrix->data[3]); /* d, e */
                float32_t f = matrix->data[5];
                float32x2_t m2 = vld1_f32(&matrix->data[6]); /* g, h */
                float32_t i = matrix->data[8];
                
                /* Calculate cofactors */
                float32_t cofactor1 = m1[1] * i - f * m2[1]; /* e*i - f*h */
                float32_t cofactor2 = m1[0] * i - f * m2[0]; /* d*i - f*g */
                float32_t cofactor3 = m1[0] * m2[1] - m1[1] * m2[0]; /* d*h - e*g */
                
                /* Calculate determinant */
                *result = m0[0] * cofactor1 - m0[1] * cofactor2 + c * cofactor3;
            } else {
                /* Scalar implementation */
                float a = matrix->data[0];
                float b = matrix->data[1];
                float c = matrix->data[2];
                float d = matrix->data[3];
                float e = matrix->data[4];
                float f = matrix->data[5];
                float g = matrix->data[6];
                float h = matrix->data[7];
                float i = matrix->data[8];
                
                *result = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            }
            break;
        }
            
        case 4: {
            /* 4x4 matrix - can use LU decomposition from ARM CMSIS-DSP */
            if (dsp_instructions_available && USE_SIMD_OPERATIONS) {
                /* Create a copy of the matrix */
                float32_t temp_mat[16];
                memcpy(temp_mat, matrix->data, 16 * sizeof(float32_t));
                
                /* Pivot vector */
                uint32_t piv[4];
                arm_matrix_instance_f32 mat_inst;
                arm_mat_init_f32(&mat_inst, 4, 4, temp_mat);
                
                /* LU decomposition */
                arm_status status = arm_mat_inverse_f32(&mat_inst, &mat_inst);
                
                if (status == ARM_MATH_SUCCESS) {
                    /* The determinant is the product of the diagonal elements of U */
                    /* But since we used inverse function, we need to compute it differently */
                    /* For 4x4, we use cofactor expansion along the first row */
                    float det = 0.0f;
                    
                    /* Temporary 3x3 matrix for cofactors */
                    float temp[9];
                    
                    /* Calculate for each element in first row */
                    for (int i = 0; i < 4; i++) {
                        /* Create cofactor matrix */
                        int idx = 0;
                        for (int r = 1; r < 4; r++) {
                            for (int c = 0; c < 4; c++) {
                                if (c != i) {
                                    temp[idx++] = matrix->data[r * 4 + c];
                                }
                            }
                        }
                        
                        /* Calculate 3x3 determinant */
                        float cofactor = temp[0] * (temp[4] * temp[8] - temp[5] * temp[7]) -
                                         temp[1] * (temp[3] * temp[8] - temp[5] * temp[6]) +
                                         temp[2] * (temp[3] * temp[7] - temp[4] * temp[6]);
                        
                        /* Add to determinant with sign */
                        det += ((i & 1) ? -1.0f : 1.0f) * matrix->data[i] * cofactor;
                    }
                    
                    *result = det;
                } else {
                    /* Fallback to scalar implementation */
                    float det = 0.0f;
                    
                    /* Temporary 3x3 matrix for cofactors */
                    float temp[9];
                    
                    /* Calculate for each element in first row */
                    for (int i = 0; i < 4; i++) {
                        /* Create cofactor matrix */
                        int idx = 0;
                        for (int r = 1; r < 4; r++) {
                            for (int c = 0; c < 4; c++) {
                                if (c != i) {
                                    temp[idx++] = matrix->data[r * 4 + c];
                                }
                            }
                        }
                        
                        /* Calculate 3x3 determinant */
                        float cofactor = temp[0] * (temp[4] * temp[8] - temp[5] * temp[7]) -
                                         temp[1] * (temp[3] * temp[8] - temp[5] * temp[6]) +
                                         temp[2] * (temp[3] * temp[7] - temp[4] * temp[6]);
                        
                        /* Add to determinant with sign */
                        det += ((i & 1) ? -1.0f : 1.0f) * matrix->data[i] * cofactor;
                    }
                    
                    *result = det;
                }
            } else {
                /* Scalar implementation */
                float det = 0.0f;
                
                /* Temporary 3x3 matrix for cofactors */
                float temp[9];
                
                /* Calculate for each element in first row */
                for (int i = 0; i < 4; i++) {
                    /* Create cofactor matrix */
                    int idx = 0;
                    for (int r = 1; r < 4; r++) {
                        for (int c = 0; c < 4; c++) {
                            if (c != i) {
                                temp[idx++] = matrix->data[r * 4 + c];
                            }
                        }
                    }
                    
                    /* Calculate 3x3 determinant */
                    float cofactor = temp[0] * (temp[4] * temp[8] - temp[5] * temp[7]) -
                                     temp[1] * (temp[3] * temp[8] - temp[5] * temp[6]) +
                                     temp[2] * (temp[3] * temp[7] - temp[4] * temp[6]);
                    
                    /* Add to determinant with sign */
                    det += ((i & 1) ? -1.0f : 1.0f) * matrix->data[i] * cofactor;
                }
                
                *result = det;
            }
            break;
        }
            
        default:
            /* For larger matrices, implement a general algorithm like LU decomposition */
            if (dsp_instructions_available && USE_SIMD_OPERATIONS && matrix->rows <= 8) {
                /* For matrices <= 8x8, we can implement an optimized algorithm */
                /* This would be complex and beyond the scope of this example */
                /* For now, return not implemented */
                unit_free(result_unit);
                return VECTOR_NOT_IMPLEMENTED;
            } else {
                /* For very large matrices, this is even more complex */
                unit_free(result_unit);
                return VECTOR_NOT_IMPLEMENTED;
            }
    }
    
    return VECTOR_SUCCESS;
}