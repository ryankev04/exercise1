/*
 * adc.c
 *
 * Created: 13.10.2026 14:49:33
 *  Author: ngabo
 */ 

void adc_init(void)
{
    // Clock on. ADC has ID 37 ? PCER1, bit 37 ? 32 = 5  (page 563) 
    PMC->PMC_PCER1 = (1u << (ID_ADC - 32));

    // Reset the ADC  (page 1332) 
    ADC->ADC_CR = ADC_CR_SWRST;

    /*  Mode (pages 1333-1335):
     *    PRESCAL = 4  -> ADC clock = 84 MHz / ((4+1)*2) = 8.4 MHz  (max 22 MHz, page 1403)
     *    FREERUN ON   -> converts over and over by itself            */
    ADC->ADC_MR = ADC_MR_PRESCAL(4)| ADC_MR_STARTUP_SUT512 | ADC_MR_SETTLING_AST3 | ADC_MR_TRACKTIM(0)| ADC_MR_TRANSFER(1) | ADC_MR_FREERUN_ON;

    // Enable channel 7 = A0 = PA16  (Table 43-3 page 1320, ADC_CHER page 1338) 
    ADC->ADC_CHER = (1u << ADC_IR_CHANNEL);

    //  Start conversion (page 1332) this si done in adc_read() so we can read the first value immediately
    ADC->ADC_CR = ADC_CR_START;
}

uint16_t adc_read(uint8_t channel)
{
    // the ADC is in free-running mode, so it is always converting.
    // We just need to read the latest value from the channel we want. 
    //The result is 12 bits (0..4095) in the lower 12 bits of the CDR register for that channel. 
    //The upper bits are reserved and should be ignored.
    //CDR = Channel Data Register (page 1351)
    return (uint16_t)(ADC->ADC_CDR[channel] & 0x0FFF);   /* 0..4095 = 0..3.3 V (page 1351) */
}