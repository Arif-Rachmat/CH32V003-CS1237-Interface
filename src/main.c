#include <ch32v00x.h>
#include <stdlib.h>
#include <stdio.h>

/* --- Global Variables --- */
volatile uint8_t spi_rx_buffer[6];
volatile uint8_t spi_rx_idx = 0;
volatile uint8_t adc_data_ready = 0;
volatile int32_t final_adc_value = 0;

/* --- UART Buffers (From your previous code) --- */
#define TX_BUF_SIZE 64
volatile uint8_t tx_buffer[TX_BUF_SIZE];
volatile uint16_t tx_head = 0;
volatile uint16_t tx_tail = 0;

/* --- Initialization Functions --- */

void USART1_Init_Config(void) {
    // Enable Clocks
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;

    // PD5 (TX) -> Alt Push-Pull
    GPIOD->CFGLR &= ~(0xF << (4 * 5));
    GPIOD->CFGLR |= (0xB << (4 * 5));

    // Baud Rate (9600 @ 48MHz)
    uint32_t baud_rate = 9600;
    uint32_t usart_div = SystemCoreClock / baud_rate;
    USART1->BRR = ((usart_div / 16) << 4) | (usart_div % 16);

    // Enable UART
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE;
    // Note: RX not needed for this demo, so kept simple
}

void SPI1_Init_Config(void) {
    // Enable Clocks
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_SPI1 | RCC_APB2Periph_AFIO;

    // PC5 (SCK) -> Alt Push-Pull (10MHz)
    GPIOC->CFGLR &= ~(0xF << (4 * 5));
    GPIOC->CFGLR |= (0xB << (4 * 5));

    // PC6 (MOSI) -> Multiplexed Open-Drain or Push-Pull?
    // CS1237 drives the line, but we also drive it in config mode.
    // For 1-wire reading, Alt Push-Pull is fine as we toggle BIDIOE.
    // Ideally, enable internal Pull-Up to prevent floating when both release.
    GPIOC->CFGLR &= ~(0xF << (4 * 6));
    GPIOC->CFGLR |= (0x8 << (4 * 6)); 
    GPIOC->BSHR = GPIO_BSHR_BS6; // Internal Pull-up

    // Reset SPI
    SPI1->CTLR1 = 0;

    // Config: Master, BIDI Mode, Div 64 (48MHz/64 = 750kHz < 1.1MHz max)
    // CPOL=0, CPHA=0 (Mode 0: Sample on 1st edge)
    // SSM/SSI Enabled (Prevents Mode Fault)
    SPI1->CTLR1 = SPI_CTLR1_BIDIMODE | SPI_CTLR1_MSTR | 
                  SPI_CTLR1_CPHA | SPI_CTLR1_SSM | 
                  SPI_CTLR1_SSI | (5 << 3); // BR[2:0] = 101 -> Div64

    // Ensure we start in Receive mode (Output disabled)
    SPI1->CTLR1 &= ~SPI_CTLR1_BIDIOE; 

    // Enable SPI Interrupt in Core
    NVIC_EnableIRQ(SPI1_IRQn);
}

void EXTI_Init_Config(void) {
    // Connect PC6 to EXTI6
    AFIO->EXTICR |= AFIO_EXTICR1_EXTI6_PC;

    // Configure Falling Edge Trigger (CS1237 pulls Low when ready)
    EXTI->FTENR |= EXTI_FTENR_TR6;
    EXTI->RTENR &= ~EXTI_RTENR_TR6;

    // Unmask Interrupt
    EXTI->INTENR |= EXTI_INTENR_MR6;

    // Enable EXTI Interrupt in Core
    NVIC_EnableIRQ(EXTI7_0_IRQn);
}

/* --- UART Helper Functions --- */
int USART1_Transmit(char data) {
    uint16_t next_head = (tx_head + 1) % TX_BUF_SIZE;
    if (next_head == tx_tail) return -1;
    tx_buffer[tx_head] = data;
    tx_head = next_head;
    USART1->CTLR1 |= USART_CTLR1_TXEIE;
    return 0;
}

__attribute__((used)) int __wrap__write(int file, char *ptr, int len) {
    if (file == 1 || file == 2) {
        for (int i = 0; i < len; i++) {
            if (ptr[i] == '\n') while (USART1_Transmit('\r') == -1);
            while (USART1_Transmit(ptr[i]) == -1);
        }
        return len;
    }
    return -1;
}

/* --- MAIN --- */
int main(void)
{
    SystemCoreClockUpdate();
    
    USART1_Init_Config();
    SPI1_Init_Config();
    
    // Enable UART Interrupt for printing
    NVIC_EnableIRQ(USART1_IRQn);

    // Print Header
    printf("CH32V003 CS1237 ADC Reader\n");
    
    // Initialize EXTI last to start detecting
    EXTI_Init_Config();

    while (1)
    {
        if (adc_data_ready)
        {
            // Print the value captured in the ISR
            printf("ADC: %ld\n", final_adc_value);
            
            // Reset flag to wait for next sample
            adc_data_ready = 0;
        }
    }
}

/* --- Interrupt Handlers --- */

// 1. UART Handler (For printf)
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART1_IRQHandler(void)
{
    if (USART1->STATR & USART_STATR_TXE) {
        if (tx_tail != tx_head) {
            USART1->DATAR = tx_buffer[tx_tail];
            tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
        } else {
            USART1->CTLR1 &= ~USART_CTLR1_TXEIE;
        }
    }
}

// 2. EXTI Handler (Detects Data Ready)
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void)
{
    if (EXTI->INTFR & EXTI_INTENR_MR6)
    {
        // A. Disable EXTI to prevent self-triggering while clocking data
        EXTI->INTENR &= ~EXTI_INTENR_MR6; 
        
        // B. Clear EXTI Flag
        EXTI->INTFR = EXTI_INTENR_MR6;

        SPI1->CTLR1 &= ~SPI_CTLR1_SPE; // Disable SPI Peripheral to reset state

        volatile uint32_t dummy;
        while (SPI1->STATR & SPI_STATR_RXNE) {
            dummy = SPI1->DATAR; // Read and discard old data
        }
        // Reading STATR right after reading DATAR clears the Overrun (OVR) flag
        dummy = SPI1->STATR;

        // C. Start SPI Transaction
        spi_rx_idx = 0;
        
        // Enable RX Buffer Not Empty Interrupt
        SPI1->CTLR2 |= SPI_CTLR2_RXNEIE; 
        
        // Enable SPI Peripheral (This starts the Clock in Master Mode)
        SPI1->CTLR1 |= SPI_CTLR1_SPE;
    }
}

// 3. SPI Handler (Reads 3 Bytes Non-Blocking)
void SPI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SPI1_IRQHandler(void)
{
    // Check if RX Buffer Not Empty
    if (SPI1->STATR & SPI_STATR_RXNE)
    {
        // Read Byte
        spi_rx_buffer[spi_rx_idx++] = SPI1->DATAR;

        // Check if we have 3 bytes
        if (spi_rx_idx >= 6)
        {
            // 1. Stop SPI Clock immediately to avoid extra clocks
            // (CS1237 enters config mode if clocks > 29)
            SPI1->CTLR1 &= ~SPI_CTLR1_SPE;

            // 2. Disable SPI Interrupt
            SPI1->CTLR2 &= ~SPI_CTLR2_RXNEIE;

            // 3. Reconstruct 24-bit integer
            // CS1237 sends MSB first. 
            uint32_t temp = ((uint32_t)spi_rx_buffer[0] << 16) | 
                            ((uint32_t)spi_rx_buffer[1] << 8)  | 
                             (uint32_t)spi_rx_buffer[2];

            // 4. Handle 24-bit Signed extension to 32-bit
            if (temp & 0x800000) {
                temp |= 0xFF000000;
            }
            final_adc_value = (int32_t)temp;

            // 5. Signal Main Loop
            adc_data_ready = 1;

            // 6. Clear EXTI flag again
            // (Toggling data lines during SPI read might have set it)
            EXTI->INTFR = EXTI_INTENR_MR6;

            // 7. Re-enable EXTI for next sample
            EXTI->INTENR |= EXTI_INTENR_MR6;
        }
    }
}