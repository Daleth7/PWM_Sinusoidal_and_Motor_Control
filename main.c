#include <asf.h>

    /**********   Start type aliasing   **********/
#include <stdint.h>

#define INT32   int32_t
#define UINT32  uint32_t
#define UINT16  uint16_t
#define UINT8   uint8_t

#define BOOLEAN__     UINT8
#define TRUE__        1
#define FALSE__       0

    // Short macro functions for inlining common expressions
#define IS_NULL(P) (P == NULL)
    /**********   End type aliasing   **********/

void Simple_Clk_Init(void);
void enable_sin_ports(void);
void enable_sin_tc_clocks(void);
void enable_sin_tc8(void);
void enable_sin_tc16(void);
void disable_sin_tc8(void);
void disable_sin_tc16(void);
    // Set up points to the appropriate structures
void configure_ports(void);

void update_sin_counter(void);
void switch_sin_counter(void);

    // Find least significant ON bit
UINT32 find_lsob(UINT32);

    // Check for any input (key press) and provide debouncing functionality.
    //  0 - 15 denotes key on keypad from right to left then bottom to top
    //  TERMINATION_KEY2 denotes combo key to terminate program
    //  TERMINATION_KEY denotes combo key to terminate test program
    //    8 denotes the delete key
    //    12 denotes the enter key
void check_key(UINT8* row_dest, UINT8* col_dest);
    // Debounce key presses for validation.
UINT8 debounce_keypress(void);


Tc* timer_set;
    // Need to be careful since all TcCount instances are part of a union
TcCount8* sin_timer8;
TcCount16* sin_timer16;
Port* port;
PortGroup* bankA, *bankB;
BOOLEAN__ on8 = TRUE__;

    // Samples for the 16-bit counter
#define SIN_POP 100
UINT16 sin_samps[SIN_POP] = {
	2047,  2044,  2038,  2028,  2014,  1996,  1975,  1949,  1920,  1887, 
	1851,  1812,  1769,  1723,  1675,  1624,  1571,  1516,  1458,  1399, 
	1339,  1277,  1214,  1151,  1087,  1022,  958,  894,  831,  768, 
	706,  646,  587,  529,  474,  421,  370,  322,  276,  233, 
	194,  158,  125,  96,  70,  49,  31,  17,  7,  1, 
	0,  1,  7,  17,  31,  49,  70,  96,  125,  158, 
	194,  233,  276,  322,  370,  421,  474,  529,  587,  646, 
	706,  768,  831,  894,  958,  1022,  1087,  1151,  1214,  1277, 
	1339,  1399,  1458,  1516,  1571,  1624,  1675,  1723,  1769,  1812, 
	1851,  1887,  1920,  1949,  1975,  1996,  2014,  2028,  2038,  2044
	};

#define TIMER_PIN 13
#define SIN_MARGIN 10

int main (void)
{
    Simple_Clk_Init();

    /* Enable the sin_timer8*/
    configure_ports();
    enable_sin_tc8();

    sin_timer8->CC[1].reg = SIN_MARGIN + 1;

        // Keypad variables
    const UINT8 key_trig = SIN_MARGIN+1;
    UINT8 row, col;

    while(1)
    {   // In a future implementation, write ISRs
        if(on8 && sin_timer8->INTFLAG.reg & 0x1){
            update_sin_counter();
            sin_timer8->INTFLAG.reg |= 0x1;
        } else if(!on8 && sin_timer16->INTFLAG.reg & 0x1){
            update_sin_counter();
            sin_timer16->INTFLAG.reg |= 0x1;
        } else if (
            sin_timer8->COUNT.reg == key_trig ||
            sin_timer16->COUNT.reg == key_trig
        ){
            check_key(&row, &col);
            if(
                (row == 0 && col == 0xD && on8) ||
                (row == 0 && col == 0xB && !on8)
            ){
                switch_sin_counter();
            }
        }
    }
}

void update_sin_counter(void){
    static int8_t count = 1, counter16 = 0, count2 = 1;

    if(on8){
        sin_timer8->CC[1].reg += count;
            // Change count to either 1 or -1 or keep the same
            //  depending on whether or not a limit was reached.
/*
        if(sin_timer8->CC[1].reg >= sin_timer8->PER.reg - SIN_MARGIN) count = -1;
        else if(sin_timer8->CC[1].reg <= SIN_MARGIN)             count = 1;
/*/
        count =
            count * (                                                       // old
            (sin_timer8->CC[1].reg < (sin_timer8->PER.reg - SIN_MARGIN)) &&
            (sin_timer8->CC[1].reg > SIN_MARGIN)
            )
            + (sin_timer8->CC[1].reg <= SIN_MARGIN)                         // Increment
            - (sin_timer8->CC[1].reg >= sin_timer8->PER.reg - SIN_MARGIN)   // Decrement
            ;
//*/
    } else {
        sin_timer16->CC[1].reg = sin_samps[counter16 += count2];
        if(counter16 >= SIN_POP - 1){
            count2 = -1;
            sin_timer16->CC[1].reg = sin_samps[counter16 += count2];
        }else if(counter16 <= 0){
            count2 = 1;
            sin_timer16->CC[1].reg = sin_samps[counter16 += count2];
        }
    }
}

void configure_ports(void){
    timer_set = (Tc*)(TC2);
    sin_timer8 = (TcCount8*)(&timer_set->COUNT8);
    sin_timer16 = (TcCount16*)(&timer_set->COUNT16);
    port = (Port*)(PORT);
    bankA = (PortGroup*)(&port->Group[0]);
    bankB = (PortGroup*)(&port->Group[1]);
    bankB->DIR.reg |= 0x00000200; // Controls sign LED
        // Controls power to the keypad and SSDs. 0000 1111 0000
    bankA->DIR.reg |= 0x000000F0;

        // For reading input from keypad. 1111 0000 0000 0000 0000 
        //  Active high logic for input.
    bankA->DIR.reg &= ~0x000F0000;
    UINT8 i;
    for(i = 16; i < 20; ++i){
            // Enable input (bit 1).
            //  Note that the pins are externally pulled low,
            //  so disable pull up.
        bankA->PINCFG[i].reg = PORT_PINCFG_INEN;
    }
}

/* Set correct PA pins as TC pins for PWM operation */
void enable_sin_ports(void)
{

        // Set up sin_timer8 pin to use the sin_timer8
    bankA->PINCFG[TIMER_PIN].reg |= 0x1;            // Enable multiplexing
            // sin_timer8 pin is odd
    bankA->PMUX[(TIMER_PIN-1)/2].reg |= 0x4 << 4u;    // Select function E (sin_timer8)
}

/* Perform Clock configuration to source the TC 
1) ENABLE THE APBC CLOCK FOR THE CORREECT MODULE
2) WRITE THE PROPER GENERIC CLOCK SELETION ID*/
void enable_sin_tc_clocks(void)
{
    PM->APBCMASK.reg |= (1 << 10u);  // PM_APBCMASK is in the 10 position
    
    uint32_t temp=0x14;   // ID for TC2 is 0x14  (see table 14-2)
    temp |= 0<<8;         //  Selection Generic clock generator 0
    GCLK->CLKCTRL.reg=temp;   //  Setup in the CLKCTRL register
    GCLK->CLKCTRL.reg |= 0x1u << 14;    // enable it.
}

// Set presynchronizer to prescaled clock
// Prescale clock by 1
// Start in 8-bit mode
// Select the Normal PWM waveform
#define TC8_SETTINGS (  \
        (0x1 << 12u)    \
        | (0x0 << 8u)   \
        | (0x1 << 2u)   \
        | (0x2 << 5u)   \
    )
// Set presynchronizer to prescaled clock
// Prescale clock by 1
// Start in 16-bit mode
// Select the Normal PWM waveform
#define TC16_SETTINGS (     \
        (0x1 << 12u)        \
        | (0x0 << 8u)       \
        | (0x0 << 2u)       \
        | (0x3 << 5u)       \
    )

/* Configure the basic sin_timer8/counter to have a period of________ or a frequency of _________  */
void enable_sin_tc8(void)
{
    enable_sin_ports();
    enable_sin_tc_clocks();

        // To mess with registers, disable sin_timer8 first
    disable_sin_tc8();
    disable_sin_tc16();

    bankB->OUT.reg &= ~0x00000200;

    sin_timer16->CTRLA.reg &= ~TC16_SETTINGS;
    sin_timer8->CTRLA.reg |= TC8_SETTINGS;
    sin_timer8->PER.reg = 48;
//    sin_timer8->PER.reg = 107;    // for if statement
    sin_timer8->CC[1].reg = 40;

    while(sin_timer8->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    sin_timer8->CTRLA.reg |= 1 << 1u;    // Re-enable the sin_timer8
}

/* Configure the basic sin_timer8/counter to have a period of________ or a frequency of _________  */
void enable_sin_tc16(void)
{
    enable_sin_ports();
    enable_sin_tc_clocks();

    bankB->OUT.reg |= 0x00000200;

        // To mess with registers, disable timer first
    disable_sin_tc8();
    disable_sin_tc16();

    while(sin_timer16->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    sin_timer8->CTRLA.reg &= ~TC8_SETTINGS;
    sin_timer16->CTRLA.reg |= TC16_SETTINGS;
    sin_timer16->CC[1].reg = 40;

    while(sin_timer16->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    sin_timer16->CTRLA.reg |= 1 << 1u;    // Re-enable the timer
}

void disable_sin_tc8(void)
{
    sin_timer8->CTRLA.reg &= ~(1 << 1u);
    while(sin_timer8->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
}

void disable_sin_tc16(void)
{
    sin_timer16->CTRLA.reg &= ~(1 << 1u);
    while(sin_timer16->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
}

void switch_sin_counter(void){
    if(on8) enable_sin_tc16();
    else    enable_sin_tc8();
    on8 = !on8;
}

UINT32 find_lsob(UINT32 target){
    UINT32 toreturn = 0u;
    while(!(target & 0x1)){
        ++toreturn;
        target >>= 1;
    }
    return toreturn;
}

void check_key(UINT8* row_dest, UINT8* col_dest){
    if(IS_NULL(row_dest) || IS_NULL(col_dest))  return;
    static UINT8 cur_row = 0u;
    // Provide power to one specific row
    bankA->OUT.reg |= 0x000000F0;
    bankA->OUT.reg &= ~(1u << (4u + cur_row));
        // Extract the four bits we're interested in from
        //   the keypad.
    *col_dest = debounce_keypress();
    *row_dest = cur_row;

        // Prepare for the next row. If we were on the last
        //  row, cycle back to the first row.
        // Take modulous to retrieve current value of cur_row when
        //  we were not on the last row. If that is the case,
        //  reset row_bit to 0.
    cur_row = (cur_row+1u)%4u;
}


UINT8 debounce_keypress(void){
    // Triggered the instant the first key press is detected
    //  Returns the resulting hex number

    UINT8 toreturn = (bankA->IN.reg >> 16u) & 0xF;

        // Check if more than one button in a row was pressed.
        //  If so, checking for glitches is no longer important.
    UINT32 counter = 0x0;
    BOOLEAN__ already_on = FALSE__;
    for(; counter < 4; ++counter){
        if(already_on & (toreturn >> counter))  return toreturn;
        else    already_on = (toreturn >> counter) & 0x1;
    }

    if(!toreturn)   return 0x0;
#define MAX_JITTER  5
#define MAX_JITTER2 1000
#define RELEASE_LIM 7500

    // First, read up to MAX_JITTER times to swallow spikes as button is
    //  pressed. If no key press was detected in this time, the noise is
    //  not from a button press.
    for(counter = 0x0; counter < MAX_JITTER; ++counter){
        if(!((bankA->IN.reg >> 16u) & 0xF))    return 0x0;
    }

    // Now swallow the spikes as the button is released. Do not exit
    //  until the spikes are no longer detected after MAX_JITTER reads.
    //  If the user is holding down the button, release manually based
    //  on RELEASE_LIM.
    volatile UINT32 release = 0x0;
    for(
        counter = 0x0;
        counter < MAX_JITTER2 && release < RELEASE_LIM;
        ++counter, ++release
    ){
        if((bankA->IN.reg >> 16u) & 0xF)    counter = 0x0;
    }

    return toreturn;

#undef MAX_JITTER
#undef RELEASE_LIM
}

//Simple Clock Initialization
void Simple_Clk_Init(void)
{
    /* Various bits in the INTFLAG register can be set to one at startup.
       This will ensure that these bits are cleared */
    
    SYSCTRL->INTFLAG.reg = SYSCTRL_INTFLAG_BOD33RDY | SYSCTRL_INTFLAG_BOD33DET |
            SYSCTRL_INTFLAG_DFLLRDY;
            
    system_flash_set_waitstates(0);  //Clock_flash wait state =0

    SYSCTRL_OSC8M_Type temp = SYSCTRL->OSC8M;      /* for OSC8M initialization  */

    temp.bit.PRESC    = 0;    // no divide, i.e., set clock=8Mhz  (see page 170)
    temp.bit.ONDEMAND = 1;    //  On-demand is true
    temp.bit.RUNSTDBY = 0;    //  Standby is false
    
    SYSCTRL->OSC8M = temp;

    SYSCTRL->OSC8M.reg |= 0x1u << 1;  //SYSCTRL_OSC8M_ENABLE bit = bit-1 (page 170)
    
    PM->CPUSEL.reg = (uint32_t)0;    // CPU and BUS clocks Divide by 1  (see page 110)
    PM->APBASEL.reg = (uint32_t)0;     // APBA clock 0= Divide by 1  (see page 110)
    PM->APBBSEL.reg = (uint32_t)0;     // APBB clock 0= Divide by 1  (see page 110)
    PM->APBCSEL.reg = (uint32_t)0;     // APBB clock 0= Divide by 1  (see page 110)

    PM->APBAMASK.reg |= 01u<<3;   // Enable Generic clock controller clock (page 127)

    /* Software reset Generic clock to ensure it is re-initialized correctly */

    GCLK->CTRL.reg = 0x1u << 0;   // Reset gen. clock (see page 94)
    while (GCLK->CTRL.reg & 0x1u ) {  /* Wait for reset to complete */ }
    
    // Initialization and enable generic clock #0

    *((uint8_t*)&GCLK->GENDIV.reg) = 0;  // Select GCLK0 (page 104, Table 14-10)

    GCLK->GENDIV.reg  = 0x0100;            // Divide by 1 for GCLK #0 (page 104)

    GCLK->GENCTRL.reg = 0x030600;           // GCLK#0 enable, Source=6(OSC8M), IDC=1 (page 101)
}
