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
void enable_port(void);
void enable_tc_clocks(void);
void enable_tc(void);
    // Set up points to the appropriate structures
void configure_ports(void);

Tc* timer_set;
TcCount8* timer;
Port* port;
PortGroup* bankA;

#define TIMER_PIN 13

int main (void)
{
    Simple_Clk_Init();

    /* Enable the timer*/
    configure_ports();
    enable_tc();

    int8_t count = 1;
    const int8_t margin = 10;
    while(1)
    {
        timer->CC[1].reg += count;
        if(timer->CC[1].reg == timer->PER.reg - margin) count = -1;
        else if(timer->CC[1].reg == margin)             count = 1;
        /*
        old = count *
            (
                (timer->CC[1].reg <= timer->PER.reg - margin) &&
                (timer->CC[1].reg >= margin)
            )
        up = (timer->CC[1].reg == margin);
        down = (timer->CC[1].reg == timer->PER.reg - margin);
        */
    }
}

void configure_ports(void){
    timer_set = (Tc*)(TC2);
    timer = (TcCount8*)(&timer_set->COUNT8);
    port = (Port*)(PORT);
    bankA = (PortGroup*)(&port->Group[0]);
}

/* Set correct PA pins as TC pins for PWM operation */
void enable_port(void)
{

        // Set up timer pin to use the timer
    bankA->PINCFG[TIMER_PIN].reg |= 0x1;            // Enable multiplexing
            // timer pin is odd
    bankA->PMUX[(TIMER_PIN-1)/2].reg |= 0x4 << 4u;    // Select function E (timer)
}

/* Perform Clock configuration to source the TC 
1) ENABLE THE APBC CLOCK FOR THE CORREECT MODULE
2) WRITE THE PROPER GENERIC CLOCK SELETION ID*/
void enable_tc_clocks(void)
{
    PM->APBCMASK.reg |= (1 << 10u);  // PM_APBCMASK is in the 10 position
    
    uint32_t temp=0x14;   // ID for TC2 is 0x14  (see table 14-2)
    temp |= 0<<8;         //  Selection Generic clock generator 0
    GCLK->CLKCTRL.reg=temp;   //  Setup in the CLKCTRL register
    GCLK->CLKCTRL.reg |= 0x1u << 14;    // enable it.
}

/* Configure the basic timer/counter to have a period of________ or a frequency of _________  */
void enable_tc(void)
{
    enable_port();
    enable_tc_clocks();

        // To mess with registers, disable timer first
    timer->CTRLA.reg &= ~(1 << 1u);

    while(timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    timer->CTRLA.reg |=
        (0x1 << 12u)        // Set presynchronizer to prescaled clock
        | (0x0 << 8u)       // Prescale clock by 1
        | (0x1 << 2u)       // Start in 8-bit mode
        | (0x2 << 5u)       // Select the Normal PWM waveform
        ;
    timer->PER.reg = 112;

    while(timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    timer->CTRLA.reg |= 1 << 1u;    // Re-enable the timer
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
