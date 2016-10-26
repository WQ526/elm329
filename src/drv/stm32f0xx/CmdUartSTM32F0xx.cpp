/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#include <cstring>
#include "cortexm.h"
#include "GPIODrv.h"
#include "CmdUart.h"

using namespace std;

#ifdef BLUETOOTH_MINI_CFG
  const int TxPin = 2;
  const int RxPin = 3;
  USART_TypeDef* USARTx = USART2;
  IRQn_Type USARTx_IRQn = USART2_IRQn;
#else
  const int TxPin = 9;
  const int RxPin = 10;
  USART_TypeDef* USARTx = USART1;
  IRQn_Type USARTx_IRQn = USART1_IRQn;
#endif

const int USER_AF = GPIO_AF_1;
#define RxPort  GPIOA
#define TxPort  GPIOA


/**
 * Constructor
 */
CmdUart::CmdUart()
  : txLen_(0),
    txPos_(0),
    ready_(false),
    handler_(0)
{
}

/**
 * CmdUart singleton
 */
CmdUart* CmdUart::instance()
{
    static CmdUart instance;
    return &instance;;
}

/**
 * Configure UART0
 */
void CmdUart::configure()
{
    // Enable the peripheral clock of GPIOA
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    
    // Enable the peripheral clock USART1/2
#ifdef BLUETOOTH_MINI_CFG
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
#else
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
#endif

    // GPIO configuration for USART1 signals
    // Select AF mode on Tx/Rx pins
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = (1 << TxPin) | (1 << RxPin);
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // Connect TxPin to USART_Tx, RxPin to USART_Rx, use alternate function AF1, p165
    GPIO_PinAFConfig(TxPort, TxPin, USER_AF);
    GPIO_PinAFConfig(RxPort, RxPin, USER_AF);
}

/**
 * Use UART ROM API to configuring speed and interrupt for UART0,
 * discard the allocated UART memory block afterwards
 * @parameter[in] speed Speed to configure
 */
void CmdUart::init(uint32_t speed)
{
    USART_InitTypeDef USART_InitStruct;
    USART_InitStruct.USART_BaudRate = speed;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USARTx, &USART_InitStruct);

    // Enable USART
    USARTx->CR1 |= USART_CR1_UE; 

    // Enable the USART Receive interrupt
    USART_ITConfig(USARTx, USART_IT_RXNE, ENABLE);
    
    // Clear TC flag 
    USARTx->ICR |= USART_ICR_TCCF;

    NVIC_SetPriority(USARTx_IRQn, 2);
    NVIC_EnableIRQ(USARTx_IRQn);
}

/**
 * CmdUart TX handler
 */
void CmdUart::txIrqHandler()
{
    // Fill TX until full or until TX buffer is empty
    if (txPos_ < txLen_) {
        if (USARTx->ISR & USART_FLAG_TXE) {
            USARTx->TDR = txData_[txPos_++];
        }
    }
    else {
        USARTx->ICR |= USART_ICR_TCCF;
        USART_ITConfig(USARTx, USART_IT_TC, DISABLE);
    }
}

/**
 * CmdUart RX handler
 */
void CmdUart::rxIrqHandler()
{
    // Receive data, clear flag
    uint8_t ch = USARTx->RDR;
    if (handler_)
        ready_ = (*handler_)(ch);
}

/**
 * CmdUart IRQ handler
 */
void CmdUart::irqHandler()
{
    // If overrun condition occurs, clear the ORE flag and recover communication
    if (USARTx->ISR & USART_ISR_ORE) {
        USARTx->ICR |= USART_ICR_ORECF;
    }
    
    if(USARTx->ISR & USART_ISR_TC) {
        txIrqHandler();
    }
    if (USARTx->ISR & USART_ISR_RXNE) {
        rxIrqHandler();
    }
}

/**
 * Send one character, blocking call for echo purposes
 * Note: Do not have USART FIFO in STM32F37x, always have to 
 * check the TXE status before sending byte
 * @parameter[in] ch Character to send
 */
void CmdUart::send(uint8_t ch) 
{
    while ((USARTx->ISR & USART_FLAG_TXE) == 0)
        ;
    USARTx->TDR = ch;
}

/**
 * Send the string asynch
 * @parameter[in] str String to send
 */
void CmdUart::send(const util::string& str)
{
    // wait for TX interrupt disabled when the previous transmission completed
    while (USARTx->CR1 & USART_CR1_TCIE) {
        ;
    }

    // start the new transmission 
    txPos_ = 0;
    txLen_ = str.length();
    memcpy(txData_, str.c_str(), txLen_);
    if (txLen_ > 0) {
        // Initialize the transfer && Enable the USART Transmit complete interrupt
        USART_ITConfig(USARTx, USART_IT_TC, ENABLE);
        send(txData_[txPos_++]);
    }
}

/**
 * UARTx IRQ Handler, redirect to irqHandler
 */
#ifdef BLUETOOTH_MINI_CFG
extern "C" void USART2_IRQHandler(void)
#else
extern "C" void USART1_IRQHandler(void)
#endif
{
    if (CmdUart::instance())
        CmdUart::instance()->irqHandler();
}
