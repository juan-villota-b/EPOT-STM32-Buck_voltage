/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         main.c
  * @author       STMicroelectronics
  * @brief        Main program body
  * @details      This file provides main function and peripheral init 
  *               functions
  ******************************************************************************
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file in
  * the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  *
 * @history Date       Version Person  Change
 * @history ---------- ------- ------- ----------------------------------------
  * @history 2020-02-14 1.0     PBo     Created with the support of Biricha Digital Power LTd
  * @history 2022-01-04 1.0     RGo     Header modifications
  * @history 2022-01-10 2.0     NSa     Moved Systick_Handler to ISR file and Load_Handler to 
  *                                     app code file
 */

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "fmac.h"
#include "hrtim.h"
#include "rtc.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32g4xx_it.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// #define RUN_OPEN_LOOP
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* FMAC configuration structure */
FMAC_FilterConfigTypeDef sFmacConfig;
DMA_HandleTypeDef hdma_fmac_read;

/* Array of filter coefficients A (feedback coefficients) in Q1.15 format */
static int16_t aFilterCoeffA[COEFF_VECTOR_A_SIZE] = {A1,A2,A3};

/* Array of filter coefficients B (feed-forward taps) in Q1.15 format */
static int16_t aFilterCoeffB[COEFF_VECTOR_B_SIZE] = {(int16_t)-B0,(int16_t)-B1,(int16_t)-B2,(int16_t)-B3};

/* Array of output data to preload in Q1.15 format */
static int16_t aOutputDataToPreload[COEFF_VECTOR_A_SIZE] = {0x0000, 0x0000, 0x0000};

/* Expected number of calculated samples */
uint16_t ExpectedCalculatedOutputSize = (uint16_t) 1;
uint32_t *Fmac_Wdata;
int16_t Fmac_output;
int16_t bTransient = 0;

/* Voltage step: toggle between 1.8V and 2.8V every 2s */
static const uint16_t refTable[] = {444, 691};
#define REF_STEP_MS 2000
uint8_t refIndex = 0;
uint32_t lastRefTick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_HRTIM1_Init();
  MX_FMAC_Init();
  /* USER CODE BEGIN 2 */

     /*## Configure the FMAC peripheral ###########################################*/
  sFmacConfig.InputBaseAddress  = INPUT_BUFFER_BASE;
  sFmacConfig.InputBufferSize   = INPUT_BUFFER_SIZE;
  sFmacConfig.InputThreshold    = INPUT_THRESHOLD;
  sFmacConfig.CoeffBaseAddress  = COEFFICIENT_BUFFER_BASE;
  sFmacConfig.CoeffBufferSize   = COEFFICIENT_BUFFER_SIZE;
  sFmacConfig.OutputBaseAddress = OUTPUT_BUFFER_BASE;
  sFmacConfig.OutputBufferSize  = OUTPUT_BUFFER_SIZE;
  sFmacConfig.OutputThreshold   = OUTPUT_THRESHOLD;
  sFmacConfig.pCoeffA           = aFilterCoeffA;
  sFmacConfig.CoeffASize        = COEFF_VECTOR_A_SIZE;
  sFmacConfig.pCoeffB           = aFilterCoeffB;
  sFmacConfig.CoeffBSize        = COEFF_VECTOR_B_SIZE;
  sFmacConfig.Filter            = FMAC_FUNC_IIR_DIRECT_FORM_1;
  sFmacConfig.InputAccess       = FMAC_BUFFER_ACCESS_NONE;
  sFmacConfig.OutputAccess      = FMAC_BUFFER_ACCESS_IT;
  sFmacConfig.Clip              = FMAC_CLIP_ENABLED;
  sFmacConfig.P                 = COEFF_VECTOR_B_SIZE;
  sFmacConfig.Q                 = COEFF_VECTOR_A_SIZE;
  sFmacConfig.R                 = post_shift;

  
  if (HAL_FMAC_FilterConfig(&hfmac, &sFmacConfig) != HAL_OK)
  {
    /* Configuration Error */
    Error_Handler();
  }

  /*## Preload the input and output buffers ####################################*/
  if (HAL_FMAC_FilterPreload(&hfmac, NULL, INPUT_BUFFER_SIZE,
                           aOutputDataToPreload, COEFF_VECTOR_A_SIZE) != HAL_OK)
  {
    /* Configuration Error */
    Error_Handler();
  }
  /* Start calculation of IIR filter */
   if (HAL_FMAC_FilterStart(&hfmac,&Fmac_output,&ExpectedCalculatedOutputSize) != HAL_OK)
  {
    /* Processing Error */
    Error_Handler();
  }

  /* Perform an ADC automatic self-calibration and enable ADC */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

  
#ifdef RUN_OPEN_LOOP
  /* Do not start ADC and DMA, instead set fixed duty cycle */
  __HAL_HRTIM_SETCOMPARE( &hhrtim1, HRTIM_TIMERINDEX_TIMER_C, HRTIM_COMPAREUNIT_1, 10000);  
#else
  /* Start ADC and DMA */
  Fmac_Wdata = (uint32_t *) FMAC_WDATA;  
  HAL_ADC_Start_DMA(&hadc1,Fmac_Wdata,1);
#endif

  /* Start the PWMs */
  HAL_HRTIM_WaveformOutputStart(&hhrtim1, HRTIM_OUTPUT_TC1 | HRTIM_OUTPUT_TC2);
  HAL_HRTIM_WaveformCounterStart(&hhrtim1, HRTIM_TIMERID_TIMER_C);

  
  /* Enable T6 for Buck only mode usage */
  HAL_GPIO_WritePin(GPIOB, BUCKBOOST_P2_DRIVE_Pin, GPIO_PIN_SET);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (bTransient)
    {
      HAL_GPIO_WritePin(GPIOA, LED_DOWN_BLUE_Pin, GPIO_PIN_SET);

      // Toggle both Loads
      HAL_GPIO_TogglePin(GPIOC, BUCKBOOST_LOAD_1_Pin);
      HAL_GPIO_TogglePin(GPIOC, BUCKBOOST_LOAD_2_Pin);
      HAL_GPIO_TogglePin(GPIOB, LED_RIGHT_GREEN_Pin);
      HAL_GPIO_TogglePin(GPIOB, LED_LEFT_ORANGE_Pin);
          
      // Delay
      HAL_Delay(500);    
    } else {
      HAL_GPIO_WritePin(GPIOA, LED_DOWN_BLUE_Pin, GPIO_PIN_RESET);
    }
    
    // Voltage sweep: 1.8V to 2.8V, step every REF_STEP_MS
    if (HAL_GetTick() - lastRefTick >= REF_STEP_MS)
    {
      lastRefTick = HAL_GetTick();
      refIndex++;
      if (refIndex >= (sizeof(refTable) / sizeof(refTable[0])))
        refIndex = 0;
      hadc1.Instance->OFR1 = (hadc1.Instance->OFR1 & 0xFC000000) | refTable[refIndex];
    }
    
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV10;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
	Demo_Error_Handler();

  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
