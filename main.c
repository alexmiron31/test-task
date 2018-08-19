#include "stm32f7xx.h"   
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>

void initRCC(void);
void USARTSend(char);
void USARTSendString(char*);
void vRxProcessor(void*);
void vSender(void*);

QueueHandle_t xQueue;
SemaphoreHandle_t xSendSemaphore;
SemaphoreHandle_t xProcessSemaphore;
char buff[20];

int main(void)
{
	SystemInit();
	initRCC();
	SystemCoreClockUpdate();
	SysTick_Config(SystemCoreClock/1000);																			
	
	xQueue = xQueueCreate(1, sizeof(double));
	xSendSemaphore = xSemaphoreCreateBinary();
	xProcessSemaphore = xSemaphoreCreateBinary();
	
	xTaskCreate(vSender, NULL, configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
	xTaskCreate(vRxProcessor, NULL, configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
	
	vTaskStartScheduler();
	
	while(1)
	{
		
	}
}

void USART1_IRQHandler(void)
{
	static uint8_t i = 0;
	
	if(USART1->ISR & USART_ISR_RXNE)
	{
			buff[i] = USART1->RDR;
			if(buff[i++] == '\n')
			{
				i = 0;
				xSemaphoreGiveFromISR(xProcessSemaphore, 0);
			}
	}
}

// Set RCC and FLASH for 200MHz
void initRCC(void)																													
{
 	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
									RCC_AHB1ENR_GPIOIEN;																			
	
	//FLASH settings
	FLASH->ACR |= FLASH_ACR_PRFTEN;																						
	FLASH->ACR |= FLASH_ACR_LATENCY_7WS;																			
	
	// RCC settings
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE2_DIV2 | 									// AHB Prescaler = 1, APB2 Prescaler = 2
							 RCC_CFGR_PPRE1_DIV4;																					// APB1 Prescaler = 4																						
	RCC->CR |= RCC_CR_HSEON;
	while(!(RCC->CR & RCC_CR_HSERDY)){}
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE | RCC_PLLCFGR_PLLM_0 | 
									RCC_PLLCFGR_PLLM_3 | RCC_PLLCFGR_PLLM_4 |									// PLLM = 25
									RCC_PLLCFGR_PLLN_4 | RCC_PLLCFGR_PLLN_7 | 								
									RCC_PLLCFGR_PLLN_8;																				// PLLN = 400
	RCC->CR |= RCC_CR_PLLON;																									
	while(!(RCC->CR & RCC_CR_PLLRDY)){}																				
	RCC->CFGR &= ~(RCC_CFGR_SW);																							// Clear switch bits
 	RCC->CFGR |= RCC_CFGR_SW_PLL;																							
	while (!(RCC->CFGR & RCC_CFGR_SWS)){}																			// Wait for PLL is chosen as a system clock source
}

void USARTSend(char ch)
{
	while(!(USART1->ISR & USART_ISR_TC)){}
	USART1->TDR = ch;
}

void USARTSendString(char* string)
{
	while(*string)
		USARTSend(*string++);
	USARTSend('\0');
	USARTSend('\n');
}

void vRxProcessor(void* pvParameters)																						
{	
	double num1 = 0; 
	double	num2 = 0; 
	int action = 0; 
	double result = 0;
	static uint8_t i = 0;																											// input counter

	while(1)
	{
		if(xSemaphoreTake(xProcessSemaphore, portMAX_DELAY))
		{
			if(i == 0)																														
			{
				i++;
				num1 = atof(buff);
				xSemaphoreGive(xSendSemaphore);
			}
			else if(i == 1)																												
			{
				i++;
				num2 = atof(buff);
				xSemaphoreGive(xSendSemaphore);
			}
			else if(i == 2)
			{
				i = 0;
				action = atoi(buff);
				switch(action)
				{
					case 0:								
						result = num1 + num2;
						break;
					case 1:
						result = num1 - num2;
						break;
					case 2:
						result = num1 * num2;
						break;
					case 3:
						if(num2 != 0)
							result = num1 / num2;
						else
							result = 0;
						break;
					default:
						result = 0;
						break;
				}
				xQueueSend(xQueue, &result, portMAX_DELAY);
				xSemaphoreGive(xSendSemaphore);
			}
		}		
	}
}

void vSender(void* pvParameters)
{
	// USART1 Settings
	// SET TX
	GPIOA->MODER |= GPIO_MODER_MODER9_1;																			// AF mode
	GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR9;																	// Very High
	GPIOA->PUPDR |= GPIO_PUPDR_PUPDR9_0;																			// Pull-Up
	GPIOA->AFR[1] |= GPIO_AFRH_AFRH1_0 | GPIO_AFRH_AFRH1_1 |
									 GPIO_AFRH_AFRH1_2;																						
	
	// SET RX
	GPIOB->MODER |= GPIO_MODER_MODER7_1;	
	GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR7;
	GPIOB->PUPDR |= GPIO_PUPDR_PUPDR7_0;																			
	GPIOB->AFR[0] |= GPIO_AFRL_AFRL7_0 | GPIO_AFRL_AFRL7_1 |
									 GPIO_AFRL_AFRL7_2;																					
		
	// SET USART1 REG	
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;																			
	USART1->BRR |= 0x364;																											// 115200 on 100MHz
	USART1->CR1 |= USART_CR1_UE; 																							
	USART1->CR1 |= USART_CR1_TE;																							
	USART1->CR1 |= USART_CR1_RE;																							
	USART1->CR1 |= USART_CR1_RXNEIE;	
	
	NVIC_EnableIRQ(USART1_IRQn);
	
	const char* num1 = "Enter first number";
	const char* num2 = "Enter second number";
	const char* action = "Choose operation: \n0)addition; \n1)subtraction; \n2)multiplication; \n3)division";
	char resultToSend[21];
	double result;
	
	while(1)
	{
		USARTSendString((char*)num1);
		xSemaphoreTake(xSendSemaphore, portMAX_DELAY);
		USARTSendString((char*)num2);
		xSemaphoreTake(xSendSemaphore, portMAX_DELAY);
		USARTSendString((char*)action);
		xSemaphoreTake(xSendSemaphore, portMAX_DELAY);
		xQueueReceive(xQueue, &result, portMAX_DELAY);
		sprintf(resultToSend, "Result is %f", result);
		USARTSendString(resultToSend);
	}
}
