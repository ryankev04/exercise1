
#include "sam.h"
#include "can.h"
#include <stdio.h>

void can_printmsg(CanMsg m){
    printf("CanMsg(id:%d, length:%d, data:{", m.id, m.length);
    if(m.length){
        printf("%d", m.byte[0]);
    }
    for(uint8_t i = 1; i < m.length; i++){
        printf(", %d", m.byte[i]);
    }
    printf("})\n");
}


#define txMailbox 0
#define rxMailbox 1


void can_init(CanInit init, uint8_t rxInterrupt){
    // Disable CAN
    CAN0->CAN_MR &= ~CAN_MR_CANEN; 
    
    // Clear status register by reading it
    __attribute__((unused)) uint32_t ul_status = CAN0->CAN_SR;     
    
    // Disable interrupts on CANH and CANL pins
    PIOA->PIO_IDR = PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0;   // what was before was PIO_PA8A_URXD | PIO_PA9A_UTXD
    
    // Select CAN0 RX and TX in PIOA
    PIOA->PIO_ABSR &= ~(PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0);
    
    // Disable the Parallel IO (PIO) of the Rx and Tx pins so that the peripheral controller can use them
    PIOA->PIO_PDR = PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0;
    
    // Enable pull up on CANH and CANL pin
    PIOA->PIO_PUER = (PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0);    
    
    // Enable Clock for CAN0 in PMC
	// here the DIV for us is 0 which is 84Mhz
    // DIV = 0 (can clk = MCK), CMD = 1 (write), PID = 2B (CAN0)
    PMC->PMC_PCR = PMC_PCR_EN | (0 << PMC_PCR_DIV_Pos) | PMC_PCR_CMD | (ID_CAN0 << PMC_PCR_PID_Pos); 
    PMC->PMC_PCER1 = 1 << (ID_CAN0 - 32);
	
/************************************************************************/
/* 
|= is questionable use, because these registers are write-only / command registers. So these are changed:
 PMC->PMC_PCER1 |= 1 << (ID_CAN0 - 32);
 CAN0->CAN_MB[rxMailbox].CAN_MCR |= CAN_MCR_MTCR; //Change applied in can_rx(). But in can_init(), this was removed:
	According to the SAM3X datasheet, once MOT is configured for Receive Mode, MRDY is automatically cleared and the mailbox waits for its first accepted message.
	So MTCR transfer command does not need to be used just to start receiving, because it is already ready. 
	BUT this situation is can_init() unique. The opposite is true for can_rx(), because there you want to be ready for the NEXT message that can arrive,
		and CAN0->CAN_MB[rxMailbox].CAN_MMR = CAN_MMR_MOT_MB_RX; is not needed over there, because once RX is configured as RX, it will stay as an RX mailbox, no matter the message arrived.
 CAN0->CAN_IER |= (1 << rxMailbox); 
*/
/************************************************************************/
    
    //Set baudrate, Phase1, phase2 and propagation delay for can bus. Must match on all nodes!
    CAN0->CAN_BR = init.reg; 
    


    // Configure mailboxes
    // transmit
    CAN0->CAN_MB[txMailbox].CAN_MID = 0;            // old : CAN_MID_MIDE; this selected can version 2b extended  128bit
    CAN0->CAN_MB[txMailbox].CAN_MMR = CAN_MMR_MOT_MB_TX;
    
    // receive
    CAN0->CAN_MB[rxMailbox].CAN_MAM = 0; // Accept all messages
    CAN0->CAN_MB[rxMailbox].CAN_MID = 0;					//CAN_MID_MIDE; same as above
    CAN0->CAN_MB[rxMailbox].CAN_MMR = CAN_MMR_MOT_MB_RX;
    if(rxInterrupt){
        // Enable interrupt on receive
        CAN0->CAN_IER = (1 << rxMailbox); 
        // Enable interrupt in NVIC 
        NVIC_EnableIRQ(ID_CAN0);
    }

    // Enable CAN
    CAN0->CAN_MR |= CAN_MR_CANEN;
}


void can_tx(CanMsg m){
    while(!(CAN0->CAN_MB[txMailbox].CAN_MSR & CAN_MSR_MRDY)){}
    
    // Set message ID and use CAN 2.0B protocol
    CAN0->CAN_MB[txMailbox].CAN_MID = CAN_MID_MIDvA(m.id) ;      //| CAN_MID_MIDE 
        
    // Coerce maximum 8 byte length
    m.length = m.length > 8 ? 8 : m.length;
    
    //  Put message in can data registers
    CAN0->CAN_MB[txMailbox].CAN_MDL = m.dword[0];
    CAN0->CAN_MB[txMailbox].CAN_MDH = m.dword[1];
        
    // Set message length and mailbox ready to send
    CAN0->CAN_MB[txMailbox].CAN_MCR = (m.length << CAN_MCR_MDLC_Pos) | CAN_MCR_MTCR;
}

uint8_t can_rx(CanMsg* m){
    if(!(CAN0->CAN_MB[rxMailbox].CAN_MSR & CAN_MSR_MRDY)){
        return 0;
    }
    // Get message ID
    m->id = (uint16_t)((CAN0->CAN_MB[rxMailbox].CAN_MID & CAN_MID_MIDvA_Msk) >> CAN_MID_MIDvA_Pos);
        
    // Get data length
    m->length = (uint8_t)((CAN0->CAN_MB[rxMailbox].CAN_MSR & CAN_MSR_MDLC_Msk) >> CAN_MSR_MDLC_Pos);
    
    // Get data from CAN mailbox
    m->dword[0] = CAN0->CAN_MB[rxMailbox].CAN_MDL;
    m->dword[1] = CAN0->CAN_MB[rxMailbox].CAN_MDH;
                
    // Reset for new receive
    CAN0->CAN_MB[rxMailbox].CAN_MCR = CAN_MCR_MTCR;

/************************************************************************/
/* 
Check out the previous comment box on why 
	CAN0->CAN_MB[rxMailbox].CAN_MMR = CAN_MMR_MOT_MB_RX;
was removed.
*/
/************************************************************************/
    return 1;
}
    
    

    
/*
// Example CAN interrupt handler
void CAN0_Handler(void){
    char can_sr = CAN0->CAN_SR; 
    
    // RX interrupt
    if(can_sr & (1 << rxMailbox)){
        // Add your message-handling code here
        can_printmsg(can_rx());
    } else {
        printf("CAN0 message arrived in non-used mailbox\n\r");
    }
    
    if(can_sr & CAN_SR_MB0){
        // Disable interrupt
        CAN0->CAN_IDR = CAN_IER_MB0;
    }
    
    NVIC_ClearPendingIRQ(ID_CAN0);
} 
*/

