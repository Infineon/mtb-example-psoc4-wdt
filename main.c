/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PSoC 4 Watch dog interrupt and
* reset Application for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2020-2021, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*****************************************************************************
* Header file includes
*****************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"


/*****************************************************************************
* Macros
*****************************************************************************/
/* WDT demo options */
/* Select WDT_DEMO as either WDT_RESET_DEMO or WDT_INTERRUPT_DEMO */
#define WDT_RESET_DEMO             (1U)
#define WDT_INTERRUPT_DEMO         (2U)
#define WDT_DEMO                   (WDT_INTERRUPT_DEMO)

/* ILO Frequency in Hz */
#define ILO_FREQUENCY_HZ           (40000U)

/* Interval between two LED blinks in milliseconds */
#define DELAY_IN_MS                (500U)

/* WDT interrupt period in milliseconds.
Max limit is 1698 ms. */
#define WDT_INTERRUPT_INTERVAL_MS  (1000U)

/* WDT interrupt priority */
#define WDT_INTERRUPT_PRIORITY     (0U)

/* Define desired delay in microseconds */
#define DESIRED_WDT_INTERVAL       (WDT_INTERRUPT_INTERVAL_MS  * 1000U)

/* Set the desired number of ignore bits */
#define IGNORE_BITS                (0U)

/* Waiting time, in milliseconds, for proper start-up of ILO */
#define ILO_START_UP_TIME          (2U)

/* LED states */
#ifdef TARGET_CY8CKIT_149
    #define LED_STATE_ON               (1U)
    #define LED_STATE_OFF              (0U)
#else
    #define LED_STATE_ON               (0U)
    #define LED_STATE_OFF              (1U)
#endif

/*****************************************************************************
* Function Prototypes
*****************************************************************************/
/* WDT initialization function */
void wdt_init(void);
/* WDT interrupt service routine */
void wdt_isr(void);


/*****************************************************************************
* Global Variables
*****************************************************************************/
/* WDT interrupt service routine configuration */
const cy_stc_sysint_t wdt_isr_cfg =
{
    .intrSrc = srss_interrupt_wdt_IRQn, /* Interrupt source is WDT interrupt */
    .intrPriority = WDT_INTERRUPT_PRIORITY /* Interrupt priority is 0 */
};

/* Variable to check whether WDT interrupt is triggered */
volatile bool interrupt_flag = false;

/* Variable to store the counts required after ILO compensation */
static uint32_t ilo_compensated_counts = 0U;
static uint32_t temp_ilo_counts = 0U;


/*****************************************************************************
 * Function Name: main
 *****************************************************************************
 *
 *  The main function performs the following actions:
 *   1. Initializes BSP.
 *   2. Checks whether the reset is caused due to WDT or other reset causes
 *      and blinks blue LED accordingly.
 *   3. Initializes the WDT.
 *   4. Checks continuously if interrupt is triggered due to WDT and toggles
 *      blue LED and also clears the interrupt.
 *   5. Compensates the ilo_compensated_counts in case of interrupt mode.
 ****************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Check the reason for device reset */
    if(CY_SYSLIB_RESET_HWWDT == Cy_SysLib_GetResetReason())
    {
        /* WDT reset event occurred- blink LED thrice */
        for(uint8_t i = 0; i < 3; i++)
        {
            Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, LED_STATE_ON);
            Cy_SysLib_Delay(DELAY_IN_MS);
            Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, LED_STATE_OFF);
            Cy_SysLib_Delay(DELAY_IN_MS);
        }
    }

    /* In case of POR/ XRES event/ Software reset- blink LED once */
    else
    {
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, LED_STATE_ON);
        Cy_SysLib_Delay(DELAY_IN_MS);
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, LED_STATE_OFF);
        Cy_SysLib_Delay(DELAY_IN_MS);
    }

    /* Initialize WDT */
    wdt_init();

    cy_stc_sysclk_context_t sysClkContext;

    cy_stc_syspm_callback_params_t sysClkCallbackParams =
    {
        .base       = NULL,
        .context    = (void*)&sysClkContext
    };

    /* Callback declaration for Deep Sleep mode */
    cy_stc_syspm_callback_t sysClkCallback =
    {
        .callback       = &Cy_SysClk_DeepSleepCallback,
        .type           = CY_SYSPM_DEEPSLEEP,
        .skipMode       = 0UL,
        .callbackParams = &sysClkCallbackParams,
        .prevItm        = NULL,
        .nextItm        = NULL,
        .order          = 0
    };

    /* Register Deep Sleep callback */
    Cy_SysPm_RegisterCallback(&sysClkCallback);

    for (;;)
    {
        #if(WDT_DEMO == WDT_INTERRUPT_DEMO)
            /* Check if the WDT interrupt has been triggered */
            if (interrupt_flag == true)
            {
                /* Clear WDT Interrupt */
                Cy_WDT_ClearInterrupt();
                /* Unmask the WDT interrupt */
                Cy_WDT_UnmaskInterrupt();
                /* Clear the interrupt flag */
                interrupt_flag = false;
                /* Update the match count */
                Cy_WDT_SetMatch((uint16_t)(Cy_WDT_GetMatch() +\
                                                    ilo_compensated_counts));
                /* User Task- Invert the state of LED */
                Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
            }
            /* Get the ILO compensated counts i.e. the actual counts for the
             desired ILO frequency. ILO default accuracy is +/- 60%.
             Note that DESIRED_WDT_INTERVAL should be less than the total
             count time */
             if(CY_SYSCLK_SUCCESS ==\
              Cy_SysClk_IloCompensate(DESIRED_WDT_INTERVAL, &temp_ilo_counts))
             {
                 ilo_compensated_counts = (uint32_t)temp_ilo_counts;
             }
             /* Stop ILO measurement before entering deep sleep mode */
             Cy_SysClk_IloStopMeasurement();
             /* Enter deep sleep mode */
             Cy_SysPm_CpuEnterDeepSleep();
             /* Start ILO measurement after wake up */
             Cy_SysClk_IloStartMeasurement();
        #endif

        #if(WDT_DEMO == WDT_RESET_DEMO)
            /* Execute your time bounded task */
            while(1);
            /* Clear WDT Interrupt */
            Cy_WDT_ClearInterrupt();
            /* Stop ILO measurement before entering deep sleep mode */
            Cy_SysClk_IloStopMeasurement();
            /* Enter deep sleep mode */
            Cy_SysPm_CpuEnterDeepSleep();
            /* Start ILO measurement after wake up */
            Cy_SysClk_IloStartMeasurement();
        #endif
    }
}

/*****************************************************************************
* Function Name: wdt_init
******************************************************************************
* Summary:
* This function initializes the WDT block
*
* Parameters:
*  void
*
* Return:
*  void
*
*****************************************************************************/
void wdt_init(void)
{
    cy_en_sysint_status_t status = CY_SYSINT_BAD_PARAM;

    /* Step 1- Write the ignore bits - operate with full 16 bits */
    Cy_WDT_SetIgnoreBits(IGNORE_BITS);
    if(Cy_WDT_GetIgnoreBits() != IGNORE_BITS)
    {
        CY_ASSERT(0);
    }

    /* Step 2- Clear match event interrupt, if any */
    Cy_WDT_ClearInterrupt();

    /* Step 3- Enable ILO */
    Cy_SysClk_IloEnable();
    /* Waiting for proper start-up of ILO */
    Cy_SysLib_Delay(ILO_START_UP_TIME);

    /* Starts the ILO accuracy/Trim measurement */
    Cy_SysClk_IloStartMeasurement();
    /* Calculate the count value to set as WDT match since ILO is inaccurate */
    if(CY_SYSCLK_SUCCESS ==\
                Cy_SysClk_IloCompensate(DESIRED_WDT_INTERVAL, &temp_ilo_counts))
    {
        ilo_compensated_counts = (uint32_t)temp_ilo_counts;
    }

    if(WDT_DEMO == WDT_INTERRUPT_DEMO)
    {
        /* Step 4- Write match value if periodic interrupt mode selected */
        Cy_WDT_SetMatch(ilo_compensated_counts);
        if(Cy_WDT_GetMatch() != ilo_compensated_counts)
        {
            CY_ASSERT(0);
        }

        /* Step 5 - Initialize and enable interrupt if periodic interrupt
        mode selected */
        status = Cy_SysInt_Init(&wdt_isr_cfg, wdt_isr);
        if(status != CY_SYSINT_SUCCESS)
        {
            CY_ASSERT(0);
        }
        NVIC_EnableIRQ(wdt_isr_cfg.intrSrc);
        Cy_WDT_UnmaskInterrupt();
    }

    /* Step 6- Enable WDT */
    Cy_WDT_Enable();
    if(Cy_WDT_IsEnabled() == false)
    {
        CY_ASSERT(0);
    }
}


/*****************************************************************************
* Function Name: wdt_isr
******************************************************************************
* Summary:
* This function is the handler for the WDT interrupt
*
* Parameters:
*  void
*
* Return:
*  void
*
*****************************************************************************/
void wdt_isr(void)
{
    #if (WDT_DEMO == WDT_INTERRUPT_DEMO)
        /* Mask the WDT interrupt to prevent further triggers */
        Cy_WDT_MaskInterrupt();
        /* Set the interrupt flag */
        interrupt_flag = true;
    #endif

    #if (WDT_DEMO == WDT_RESET_DEMO)
        /* Do Nothing*/
    #endif
}

/* [] END OF FILE */
