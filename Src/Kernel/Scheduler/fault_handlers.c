/**
* @file fault_handlers.c
* @brief Implementation of fault handlers for scheduler exceptions
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
* 
* This file contains exception handlers for various fault conditions
* that may occur during task execution, including memory access violations,
* TrustZone security exceptions, and other system faults.
*/

#include "scheduler.h"
#include "scheduler_mpu.h"
#include "scheduler_tz.h"
#include "spinlock_manager.h"
#include "stats.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>

/* Fault status register addresses */
#define SCB_CFSR        (*(volatile uint32_t *)(0xE000ED28))  /* Configurable Fault Status Register */
#define SCB_HFSR        (*(volatile uint32_t *)(0xE000ED2C))  /* HardFault Status Register */
#define SCB_DFSR        (*(volatile uint32_t *)(0xE000ED30))  /* Debug Fault Status Register */
#define SCB_MMFAR       (*(volatile uint32_t *)(0xE000ED34))  /* MemManage Fault Address Register */
#define SCB_BFAR        (*(volatile uint32_t *)(0xE000ED38))  /* BusFault Address Register */
#define SCB_AFSR        (*(volatile uint32_t *)(0xE000ED3C))  /* Auxiliary Fault Status Register */
#define NVIC_IABR0      (*(volatile uint32_t *)(0xE000E300))  /* Interrupt Active Bit Register */

/* CFSR bit definitions */
#define CFSR_IACCVIOL   (1UL << 0)  /* Instruction access violation */
#define CFSR_DACCVIOL   (1UL << 1)  /* Data access violation */
#define CFSR_MUNSTKERR  (1UL << 3)  /* Unstacking error */
#define CFSR_MSTKERR    (1UL << 4)  /* Stacking error */
#define CFSR_MLSPERR    (1UL << 5)  /* Floating-point lazy state preservation error */
#define CFSR_MMARVALID  (1UL << 7)  /* MMFAR holds a valid address */
#define CFSR_IBUSERR    (1UL << 8)  /* Instruction bus error */
#define CFSR_PRECISERR  (1UL << 9)  /* Precise data bus error */
#define CFSR_IMPRECISERR (1UL << 10) /* Imprecise data bus error */
#define CFSR_UNSTKERR   (1UL << 11) /* Unstacking error */
#define CFSR_STKERR     (1UL << 12) /* Stacking error */
#define CFSR_LSPERR     (1UL << 13) /* Floating-point lazy state preservation error */
#define CFSR_BFARVALID  (1UL << 15) /* BFAR holds a valid address */
#define CFSR_UNDEFINSTR (1UL << 16) /* Undefined instruction */
#define CFSR_INVSTATE   (1UL << 17) /* Invalid state */
#define CFSR_INVPC      (1UL << 18) /* Invalid PC load */
#define CFSR_NOCP       (1UL << 19) /* No coprocessor */
#define CFSR_UNALIGNED  (1UL << 24) /* Unaligned access */
#define CFSR_DIVBYZERO  (1UL << 25) /* Divide by zero */

/* HFSR bit definitions */
#define HFSR_VECTTBL    (1UL << 1)  /* Vector table read fault */
#define HFSR_FORCED     (1UL << 30) /* Forced hard fault */
#define HFSR_DEBUGEVT   (1UL << 31) /* Debug event hard fault */

/* Maximum number of tracked faults */
#define MAX_FAULT_RECORDS 16

/* Structure to store fault information for debugging */
typedef struct {
    uint32_t task_id;          /* ID of task that caused the fault */
    uint32_t fault_type;       /* Type of fault (from SCB_CFSR) */
    uint32_t fault_address;    /* Address that caused the fault */
    uint32_t lr;               /* Link register value */
    uint32_t pc;               /* Program counter */
    uint32_t psr;              /* Program Status Register */
    uint32_t fault_count;      /* Number of times this fault has occurred */
    absolute_time_t time;      /* Timestamp of the fault */
} fault_record_t;

/* Default task stack frame layout (ARM Cortex-M33) */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
} stack_frame_t;

/* Global fault record storage */
static fault_record_t fault_records[MAX_FAULT_RECORDS];
static uint8_t num_fault_records = 0;
static uint32_t total_fault_count = 0;

/* Spinlock for fault handler synchronization */
static uint32_t fault_spinlock_num;

/* Forward declaration of fault recovery function */
static bool attempt_fault_recovery(uint32_t fault_type, uint32_t fault_address, uint32_t task_id);
static void record_fault(uint32_t task_id, uint32_t fault_type, uint32_t fault_address,
    uint32_t lr, uint32_t pc, uint32_t psr);

/**
 * @brief Attempt to recover from a fault
 * 
 * This function tries to recover from certain types of faults
 * to prevent system crashes. Recovery strategies include:
 * - Skipping the faulting instruction
 * - Terminating the offending task
 * - Adjusting MPU permissions
 * 
 * @param fault_type Fault type from SCB_CFSR
 * @param fault_address Address that caused the fault
 * @param task_id Task ID that caused the fault
 * @return true if recovery was successful, false otherwise
 */

static bool attempt_fault_recovery(uint32_t fault_type, uint32_t fault_address, uint32_t task_id) {
    (void) fault_address;
    bool recovery_successful = false;
    
    // Check if it's a memory access violation that we can recover from
    if (fault_type & (CFSR_DACCVIOL | CFSR_IACCVIOL)) {
        // Check if this is a memory boundary issue we can resolve
        // For example, we might relax MPU permissions for this task
        
        // First, try to terminate the offending task
        if (scheduler_delete_task(task_id)) {
            recovery_successful = true;
        }
    }
    
    // Check if it's an undefined instruction or invalid state
    else if (fault_type & (CFSR_UNDEFINSTR | CFSR_INVSTATE)) {
        // Not much we can do except terminate the task
        recovery_successful = scheduler_delete_task(task_id);
    }
    
    // Check if it's a divide-by-zero error
    else if (fault_type & CFSR_DIVBYZERO) {
        // We might be able to skip this instruction
        // In a real implementation, we would need to modify the
        // saved context to skip the faulting instruction
        
        // For now, just terminate the task
        recovery_successful = scheduler_delete_task(task_id);
    }
    
    return recovery_successful;
}

/**
 * @brief Clear fault records
 */

void clear_fault_records(void) {
    uint32_t owner_irq = hw_spinlock_acquire(fault_spinlock_num, scheduler_get_current_task());
    
    // Clear records
    memset(fault_records, 0, sizeof(fault_records));
    num_fault_records = 0;
    
    // Release spinlock
    hw_spinlock_release(fault_spinlock_num, owner_irq);
}

/* Initialize fault handlers */
void fault_handlers_init(void) {
    // Initialize spinlock for fault handler

    fault_spinlock_num =  hw_spinlock_allocate(SPINLOCK_CAT_FAULT, "fault_manager");
    
    // Clear fault records
    memset(fault_records, 0, sizeof(fault_records));
    num_fault_records = 0;
    total_fault_count = 0;
}

/**
 * @brief Get fault description
 * 
 * Converts a fault type code into a human-readable description
 * 
 * @param fault_type Fault type from SCB_CFSR
 * @return String description of the fault
 */

const char* get_fault_description(uint32_t fault_type) {
    // Check for memory management faults
    if (fault_type & CFSR_IACCVIOL)
        return "Instruction access violation";
    if (fault_type & CFSR_DACCVIOL)
        return "Data access violation";
    if (fault_type & CFSR_MUNSTKERR)
        return "Memory unstacking error";
    if (fault_type & CFSR_MSTKERR)
        return "Memory stacking error";
    
    // Check for bus faults
    if (fault_type & CFSR_IBUSERR)
        return "Instruction bus error";
    if (fault_type & CFSR_PRECISERR)
        return "Precise data bus error";
    if (fault_type & CFSR_IMPRECISERR)
        return "Imprecise data bus error";
    if (fault_type & CFSR_UNSTKERR)
        return "Bus unstacking error";
    if (fault_type & CFSR_STKERR)
        return "Bus stacking error";
    
    // Check for usage faults
    if (fault_type & CFSR_UNDEFINSTR)
        return "Undefined instruction";
    if (fault_type & CFSR_INVSTATE)
        return "Invalid state";
    if (fault_type & CFSR_INVPC)
        return "Invalid PC load";
    if (fault_type & CFSR_NOCP)
        return "No coprocessor";
    if (fault_type & CFSR_UNALIGNED)
        return "Unaligned access";
    if (fault_type & CFSR_DIVBYZERO)
        return "Divide by zero";
    
    // Default
    return "Unknown fault";
}

/**
 * @brief Get fault records for diagnostic purposes
 * 
 * @param records Array to store fault records
 * @param max_records Maximum number of records to retrieve
 * @return Number of records retrieved
 */

uint8_t get_fault_records(fault_record_t *records, uint8_t max_records) {
    if (!records || max_records == 0) {
        return 0;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(fault_spinlock_num, scheduler_get_current_task());
    
    // Copy records
    uint8_t count = num_fault_records;
    if (count > max_records) {
        count = max_records;
    }
    
    memcpy(records, fault_records, count * sizeof(fault_record_t));
    
    // Release spinlock
    hw_spinlock_release(fault_spinlock_num, owner_irq);
    
    return count;
}

/**
 * @brief Get total fault count
 * 
 * @return Total number of faults encountered
 */

uint32_t get_total_fault_count(void) {
    return total_fault_count;
}

/**
 * @brief Common fault handler implementation
 * 
 * This function is called by all the specific fault handlers.
 * It analyzes the fault, records it, and attempts recovery if possible.
 * 
 * @param stack_frame Pointer to exception stack frame
 * @param is_hard_fault Whether this is a hard fault (vs. memmanage, busfault, etc.)
 * @return true if execution can continue, false if fatal
 */

static bool handle_fault(stack_frame_t *stack_frame, bool is_hard_fault) {
    uint32_t cfsr = SCB_CFSR;
    uint32_t fault_address = 0;
    uint32_t fault_type = cfsr;
    
    // Get current task ID
    uint32_t task_id = scheduler_get_current_task();
    
    // Determine fault address
    if (cfsr & CFSR_MMARVALID) {
        fault_address = SCB_MMFAR;
    } else if (cfsr & CFSR_BFARVALID) {
        fault_address = SCB_BFAR;
    } else {
        fault_address = stack_frame->pc;  // Use PC as a fallback
    }
    
    // Record the fault for later analysis
    record_fault(task_id, fault_type, fault_address, 
        stack_frame->lr, stack_frame->pc, stack_frame->psr);
    
    // Attempt recovery if possible
    bool recovery_successful = attempt_fault_recovery(fault_type, fault_address, task_id);
    
    // If recovery failed and this is a hard fault, we might need to reset
    if (!recovery_successful && is_hard_fault) {
        // In a real system, we might reset or enter a safe mode
        // For now, just try to terminate the task as a last resort
        recovery_successful = scheduler_delete_task(task_id);
    }
    
    // Clear fault status registers to prevent recurrence
    SCB_CFSR = SCB_CFSR;  // Write value back to clear
    SCB_HFSR = SCB_HFSR;
    SCB_DFSR = SCB_DFSR;
    
    return recovery_successful;
}

/**
 * @brief C handler for BusFault
 * 
 * Called by the assembly BusFault_Handler to process the fault
 * 
 * @param stack_frame Pointer to exception stack frame
 */

void handle_bus_fault(stack_frame_t *stack_frame) {
    bool can_continue = handle_fault(stack_frame, false);
    
    if (can_continue) {
        // We can return from the exception
        return;
    } else {
        // Escalate to HardFault
        __asm volatile ("b HardFault_Handler");
    }
}

/**
 * @brief C handler for HardFault
 * 
 * Called by the assembly HardFault_Handler to process the fault
 * 
 * @param stack_frame Pointer to exception stack frame
 */

void handle_hard_fault(stack_frame_t *stack_frame) {
    bool can_continue = handle_fault(stack_frame, true);
    
    if (can_continue) {
        // We can return from the exception
        return;
    } else {
        // Fatal error, cannot continue - in a real system we would reset
        // or enter a safe state
        
        // For now, just enter an infinite loop
        while (1) {
            // Maybe blink an LED to indicate fatal error
            // In real implementation, we would log fault info
            // and perform system reset
        }
    }
}

/**
 * @brief C handler for MemManage fault
 * 
 * Called by the assembly MemManage_Handler to process the fault
 * 
 * @param stack_frame Pointer to exception stack frame
 */

void handle_memmanage_fault(stack_frame_t *stack_frame) {
    bool can_continue = handle_fault(stack_frame, false);
    
    if (can_continue) {
        // We can return from the exception
        return;
    } else {
        // Escalate to HardFault
        __asm volatile ("b HardFault_Handler");
    }
}

/**
 * @brief C handler for SecureFault
 * 
 * Called by the assembly SecureFault_Handler to process the fault
 * 
 * @param stack_frame Pointer to exception stack frame
 */

void handle_secure_fault(stack_frame_t *stack_frame) {
    // For TrustZone security violations, we handle similarly to other faults
    bool can_continue = handle_fault(stack_frame, false);
    
    if (can_continue) {
        // We can return from the exception
        return;
    } else {
        // Escalate to HardFault
        __asm volatile ("b HardFault_Handler");
    }
}

/**
 * @brief C handler for UsageFault
 * 
 * Called by the assembly UsageFault_Handler to process the fault
 * 
 * @param stack_frame Pointer to exception stack frame
 */

void handle_usage_fault(stack_frame_t *stack_frame) {
    bool can_continue = handle_fault(stack_frame, false);
    
    if (can_continue) {
        // We can return from the exception
        return;
    } else {
        // Escalate to HardFault
        __asm volatile ("b HardFault_Handler");
    }
}

/**
 * @brief Record a fault for later analysis
 * 
 * @param task_id Task ID that caused the fault
 * @param fault_type Fault type from SCB_CFSR
 * @param fault_address Address that caused the fault
 * @param lr Link register value
 * @param pc Program counter value
 * @param psr Program Status Register value
 */

static void record_fault(uint32_t task_id, uint32_t fault_type, uint32_t fault_address,
    uint32_t lr, uint32_t pc, uint32_t psr) {

    uint32_t owner_irq = hw_spinlock_acquire(fault_spinlock_num, scheduler_get_current_task());
    
    // Increment total fault count
    total_fault_count++;
    
    // Check if we already have a record for this fault
    bool found = false;
    for (int i = 0; i < num_fault_records; i++) {
        if (fault_records[i].task_id == task_id && 
            fault_records[i].fault_type == fault_type &&
            fault_records[i].fault_address == fault_address) {
            // Update existing record
            fault_records[i].fault_count++;
            fault_records[i].time = get_absolute_time();
            fault_records[i].lr = lr;
            fault_records[i].pc = pc;
            fault_records[i].psr = psr;
            found = true;
            break;
        }
    }
    
    // If not found and we have space, create a new record
    if (!found && num_fault_records < MAX_FAULT_RECORDS) {
        fault_record_t *record = &fault_records[num_fault_records++];
        record->task_id = task_id;
        record->fault_type = fault_type;
        record->fault_address = fault_address;
        record->lr = lr;
        record->pc = pc;
        record->psr = psr;
        record->fault_count = 1;
        record->time = get_absolute_time();
    }
    
    // Release spinlock
    hw_spinlock_release(fault_spinlock_num, owner_irq);
}

/**
 * @brief BusFault handler
 * 
 * Called when a bus error occurs, such as an invalid memory access
 * or alignment error.
 */
void __attribute__((naked)) BusFault_Handler(void) {
    __asm volatile (
        "tst lr, #4                        \n" // Test EXC_RETURN[2]
        "ite eq                            \n" // If zero (using MSP)...
        "mrseq r0, msp                     \n" // Use MSP as stack frame pointer
        "mrsne r0, psp                     \n" // Else use PSP
        "ldr r1, =handle_bus_fault         \n" // Load C handler address
        "bx r1                             \n" // Branch to C handler
    );
}

/**
 * @brief HardFault handler
 * 
 * This is called for serious system faults that cannot be handled
 * by other fault handlers.
 */
void __attribute__((naked)) HardFault_Handler(void) {
    __asm volatile (
        "tst lr, #4                        \n" // Test EXC_RETURN[2]
        "ite eq                            \n" // If zero (using MSP)...
        "mrseq r0, msp                     \n" // Use MSP as stack frame pointer
        "mrsne r0, psp                     \n" // Else use PSP
        "ldr r1, =handle_hard_fault        \n" // Load C handler address
        "bx r1                             \n" // Branch to C handler
    );
}

/**
 * @brief MemManage fault handler
 * 
 * Called when a memory access violation occurs, typically due to
 * MPU region violations.
 */
void __attribute__((naked)) MemManage_Handler(void) {
    __asm volatile (
        "tst lr, #4                        \n" // Test EXC_RETURN[2]
        "ite eq                            \n" // If zero (using MSP)...
        "mrseq r0, msp                     \n" // Use MSP as stack frame pointer
        "mrsne r0, psp                     \n" // Else use PSP
        "ldr r1, =handle_memmanage_fault   \n" // Load C handler address
        "bx r1                             \n" // Branch to C handler
    );
}

/**
 * @brief SecureFault handler (TrustZone-specific)
 * 
 * Called when a TrustZone security violation occurs.
 */
void __attribute__((naked)) SecureFault_Handler(void) {
    __asm volatile (
        "tst lr, #4                        \n" // Test EXC_RETURN[2]
        "ite eq                            \n" // If zero (using MSP)...
        "mrseq r0, msp                     \n" // Use MSP as stack frame pointer
        "mrsne r0, psp                     \n" // Else use PSP
        "ldr r1, =handle_secure_fault      \n" // Load C handler address
        "bx r1                             \n" // Branch to C handler
    );
}

/**
 * @brief UsageFault handler
 * 
 * Called for various program errors such as executing undefined
 * instructions, attempting unaligned access, or divide-by-zero.
 */
void __attribute__((naked)) UsageFault_Handler(void) {
    __asm volatile (
        "tst lr, #4                        \n" // Test EXC_RETURN[2]
        "ite eq                            \n" // If zero (using MSP)...
        "mrseq r0, msp                     \n" // Use MSP as stack frame pointer
        "mrsne r0, psp                     \n" // Else use PSP
        "ldr r1, =handle_usage_fault       \n" // Load C handler address
        "bx r1                             \n" // Branch to C handler
    );
}