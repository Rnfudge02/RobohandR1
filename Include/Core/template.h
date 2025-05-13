/**
* @file module_name.h
* @brief Brief description of the module
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [Current Date]
* @version 1.0
* 
* Detailed description of what this module does,
* its purpose, and how it fits into the system.
* 
* @section features Features
* - Feature 1
* - Feature 2
* 
* @section usage Basic Usage
* @code
* //Example usage code
* module_init();
* module_do_something();
* @endcode
* 
* @section notes Implementation Notes
* Any important implementation details
*/

#ifndef MODULE_NAME_H
#define MODULE_NAME_H

/**
 * @defgroup module_constants Module Configuration
 * @{
 */

/** Configuration constant with description */
#define CONSTANT_NAME value

/** @} */

/**
 * @struct struct_name_t
 * @brief Brief description of the structure
 * 
 * Detailed description of the structure's purpose
 * and how it's used.
 */
typedef struct {
    type member;    /**< Description of member */
} struct_name_t;

/**
 * @defgroup module_api Module API Functions
 * @{
 */

/**
 * @brief Brief description of function
 * 
 * Detailed description of what the function does,
 * any algorithms it uses, etc.
 * 
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter
 * @return Description of return value
 * 
 * @pre Preconditions (what must be true before calling)
 * @post Postconditions (what is true after calling)
 * 
 * @note Any additional notes
 * @warning Any warnings about usage
 * @see Related functions
 * 
 * @code
 * //Example usage
 * result = function_name(arg1, arg2);
 * @endcode
 */
return_type function_name(param_type param1, param_type param2);

/** @} */ //end of module_api

#endif //MODULE_NAME_H