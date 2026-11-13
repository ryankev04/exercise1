
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
/************************************************************************/
/*
 * can_printmsg(): Prints the identifier, length, and payload bytes as
 * decimal values. The caller supplies a length within the eight-byte
 * payload array.
 */
/************************************************************************/


#define txMailbox 0 // CAN0 mailbox used to request transmission.
#define rxMailbox 1 // CAN0 mailbox checked for received frames.


void can_init(CanInit init, uint8_t rxInterrupt){
    // Disable CAN
    CAN0->CAN_MR &= ~CAN_MR_CANEN; 
    
    // Read CAN_SR to acknowledge its read-to-clear status bits.
    __attribute__((unused)) uint32_t ul_status = CAN0->CAN_SR;     
    
    // Disable PIO interrupts on the CAN0 logic pins.
    PIOA->PIO_IDR = PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0;
    
    // Select CAN0 RX and TX in PIOA
    PIOA->PIO_ABSR &= ~(PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0);
    
    // Disable the Parallel IO (PIO) of the Rx and Tx pins so that the peripheral controller can use them
    PIOA->PIO_PDR = PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0;
    
    // Enable pull-ups on PA0 and PA1.
    PIOA->PIO_PUER = (PIO_PA1A_CANRX0 | PIO_PA0A_CANTX0);    
    
    // Enable Clock for CAN0 in PMC
    // DIV = 0 selects no division of the master clock.
    // DIV = 0 (can clk = MCK), CMD = 1 (write), PID = 2B (CAN0)
    PMC->PMC_PCR = PMC_PCR_EN | (0 << PMC_PCR_DIV_Pos) | PMC_PCR_CMD | (ID_CAN0 << PMC_PCR_PID_Pos); 
    PMC->PMC_PCER1 = 1 << (ID_CAN0 - 32);
	
    // Set baudrate, Phase1, phase2 and propagation delay for can bus. Must match on all nodes!
    CAN0->CAN_BR = init.reg; 
    


    // Configure mailboxes
    // transmit
    CAN0->CAN_MB[txMailbox].CAN_MID = 0; // Initialize the identifier register with MIDE clear.
    CAN0->CAN_MB[txMailbox].CAN_MMR = CAN_MMR_MOT_MB_TX;
    
    // receive
    CAN0->CAN_MB[rxMailbox].CAN_MAM = 0; // Leave all identifier mask bits clear.
    CAN0->CAN_MB[rxMailbox].CAN_MID = 0; // Initialize the identifier register with MIDE clear.
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
/************************************************************************/
/*
 * can_init(): Configures PA0 and PA1 for CAN0, enables its peripheral
 * clock, and writes the supplied bit timing. Sets mailbox 0 to transmit
 * and mailbox 1 to receive. A nonzero rxInterrupt enables the mailbox
 * interrupt and the CAN0 IRQ before the controller is enabled.
 *
 * The receive mailbox starts in receive mode. can_rx() issues the
 * transfer command after copying a received frame to prepare for the
 * next one.
 */
/************************************************************************/


void can_tx(CanMsg m){
    while(!(CAN0->CAN_MB[txMailbox].CAN_MSR & CAN_MSR_MRDY)){}
    
    // Load the standard 11-bit identifier field.
    CAN0->CAN_MB[txMailbox].CAN_MID = CAN_MID_MIDvA(m.id) ;
        
    // Coerce maximum 8 byte length
    m.length = m.length > 8 ? 8 : m.length;
    
    //  Put message in can data registers
    CAN0->CAN_MB[txMailbox].CAN_MDL = m.dword[0];
    CAN0->CAN_MB[txMailbox].CAN_MDH = m.dword[1];
        
    // Set message length and mailbox ready to send
    CAN0->CAN_MB[txMailbox].CAN_MCR = (m.length << CAN_MCR_MDLC_Pos) | CAN_MCR_MTCR;
}
/************************************************************************/
/*
 * can_tx(): Waits until the transmit mailbox reports MRDY, then loads a
 * standard identifier and up to eight payload bytes and requests
 * transmission. The wait has no timeout. The function returns after
 * issuing the request.
 */
/************************************************************************/

uint8_t can_tx_try(CanMsg m){
    if(!(CAN0->CAN_MB[txMailbox].CAN_MSR & CAN_MSR_MRDY)){
        return 0; // mailbox still busy -> skip
    }
    CAN0->CAN_MB[txMailbox].CAN_MID = CAN_MID_MIDvA(m.id);
    m.length = m.length > 8 ? 8 : m.length;
    CAN0->CAN_MB[txMailbox].CAN_MDL = m.dword[0];
    CAN0->CAN_MB[txMailbox].CAN_MDH = m.dword[1];
    CAN0->CAN_MB[txMailbox].CAN_MCR = (m.length << CAN_MCR_MDLC_Pos) | CAN_MCR_MTCR;
    return 1;
}
/************************************************************************/
/*
 * can_tx_try(): Checks MRDY once and returns 0 if the transmit mailbox
 * is busy. Otherwise loads the standard identifier, limits the length to
 * eight bytes, requests transmission, and returns 1. The return value
 * reports whether the request was issued.
 */
/************************************************************************/

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
                
    // Rearm the receive mailbox after copying the frame.
    CAN0->CAN_MB[rxMailbox].CAN_MCR = CAN_MCR_MTCR;

    return 1;
}
/************************************************************************/
/*
 * can_rx(): Returns 0 if the receive mailbox is not ready. Otherwise
 * copies the standard identifier field, reported DLC, and both payload
 * words into the caller buffer. Rearms the mailbox and returns 1. The
 * receive path does not clamp the DLC or decode an extended identifier.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * // Example CAN interrupt handler
 * void CAN0_Handler(void){
 * char can_sr = CAN0->CAN_SR;
 *
 * // RX interrupt
 * if(can_sr & (1 << rxMailbox)){
 * // Add your message-handling code here
 * can_printmsg(can_rx());
 * } else {
 * printf("CAN0 message arrived in non-used mailbox\n\r");
 * }
 *
 * if(can_sr & CAN_SR_MB0){
 * // Disable interrupt
 * CAN0->CAN_IDR = CAN_IER_MB0;
 * }
 *
 * NVIC_ClearPendingIRQ(ID_CAN0);
 * }
 */
/************************************************************************/
