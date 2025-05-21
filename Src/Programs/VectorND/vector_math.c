

VectorError vector_normalize(Vector* result, const Vector* vector) {
    if (result == NULL || vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Compute magnitude */
    float magnitude;
    Unit mag_unit;
    
    VectorError err = vector_magnitude(vector, &magnitude, &mag_unit);
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
    
    /* Scale each component by 1/magnitude */
    float32_t scale = 1.0f / magnitude;
    arm_scale_f32(vector->data, scale, result->data, vector->dim);
    
    unit_free(&mag_unit);
    return VECTOR_SUCCESS;
}

VectorError vector_scale(Vector* result, const Vector* vector, float scalar, const Unit* scalar_unit) {
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
    
    /* Perform scaling using ARM DSP */
    arm_scale_f32(vector->data, scalar, result->data, vector->dim);
    
    return VECTOR_SUCCESS;
}

VectorError vector_to_matrix(Matrix* result, const Vector* vector) {
    if (result == NULL || vector == NULL) {
        return VECTOR_NULL_POINTER;
    }
    
    /* Initialize result matrix */
    VectorError err = matrix_free(result);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    err = matrix_init(result, vector->dim, 1, vector->uniform_units);
    if (err != VECTOR_SUCCESS) {
        return err;
    }
    
    /* Copy data */
    memcpy(result->data, vector->data, vector->dim * sizeof(float));
    
    /* Copy units */
    if (vector->uniform_units) {
        /* Copy uniform unit */
        Unit unit_copy;
        err = unit_init(&unit_copy, vector->units->count);
        if (err != VECTOR_SUCCESS) {
            return err;
        }
        
        for (uint8_t i = 0; i < vector->units->count; i++) {
            err = unit_add_component(&unit_copy, vector->units->components[i].type, 
                                   vector->units->components[i].exponent);
            if (err != VECTOR_SUCCESS) {
                unit_free(&unit_copy);
                return err;
            }
        }
        
        /* Free existing unit */
        unit_free(result->units);
        
        /* Copy unit */
        *result->units = unit_copy;
    } else {
        /* Copy individual units */
        for (uint16_t i = 0; i < vector->dim; i++) {
            Unit unit_copy;
            err = unit_init(&unit_copy, vector->units[i].count);
            if (err != VECTOR_SUCCESS) {
                return err;
            }
            
            for (uint8_t j = 0; j < vector->units[i].count; j++) {
                err = unit_add_component(&unit_copy, vector->units[i].components[j].type, 
                                       vector->units[i].components[j].exponent);
                if (err != VECTOR_SUCCESS) {
                    unit_free(&unit_copy);
                    return err;
                }
            }
            
            /* Free existing unit */
            unit_free(&result->units[i]);
            
            /* Copy unit */
            result->units[i] = unit_copy;
        }
    }
    
    return VECTOR_SUCCESS;
}

VectorError matrix_vector_multiply(Vector* result, const Matrix* matrix, const Vector* vector) {
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
    
    /* Perform matrix-vector multiplication */
    for (uint16_t i = 0; i < matrix->rows; i++) {
        float sum = 0.0f;
        
        /* Compute dot product of row i with vector */
        for (uint16_t j = 0; j < matrix->cols; j++) {
            sum += matrix->data[i * matrix->cols + j] * vector->data[j];
        }
        
        /* Set result */
        result->data[i] = sum;
        
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
            
            /* Only need to do this once for uniform units */
            break;
        } else if (matrix->uniform_units) {
            /* For each component of the result vector */
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
        } else if (vector->uniform_units) {
            /* For each component of the result vector */
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
        } else {
            /* Both non-uniform, calculate unit for each component */
            Unit result_unit;
            
            /* Calculate unit for this component */
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

VectorError matrix_multiply(Matrix* result, const Matrix* a, const Matrix* b) {
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
    
    /* Matrix multiplication using ARM DSP */
    arm_matrix_instance_f32 a_inst, b_inst, result_inst;
    
    /* Initialize matrix instances */
    arm_mat_init_f32(&a_inst, a->rows, a->cols, a->data);
    arm_mat_init_f32(&b_inst, b->rows, b->cols, b->data);
    arm_mat_init_f32(&result_inst, a->rows, b->cols, result->data);
    
    /* Perform multiplication */
    arm_status status = arm_mat_mult_f32(&a_inst, &b_inst, &result_inst);
    if (status != ARM_MATH_SUCCESS) {
        return VECTOR_INCOMPATIBLE_OPERATION;
    }
    
    /* Set units for non-uniform case */
    if (!(a->uniform_units && b->uniform_units)) {
        /* For simplicity, we'll only handle the case where both matrices have uniform units */
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

VectorError matrix_determinant(const Matrix* matrix, float* result, Unit* result_unit) {
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
    
    /* Calculate determinant based on matrix size */
    switch (matrix->rows) {
        case 1:
            *result = matrix->data[0];
            break;
            
        case 2:
            *result = matrix->data[0] * matrix->data[3] - matrix->data[1] * matrix->data[2];
            break;
            
        case 3: {
            /* For 3x3 matrix */
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
            break;
        }
            
        case 4: {
            /* For 4x4 matrix - use cofactor expansion */
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
            break;
        }
            
        default:
            /* For larger matrices, we would need to implement a more general algorithm */
            /* like LU decomposition, which is beyond the scope of this implementation */
            unit_free(result_unit);
            return VECTOR_NOT_IMPLEMENTED;
    }
    
    return VECTOR_SUCCESS;
}