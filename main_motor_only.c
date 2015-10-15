#include <asf.h>

void Simple_Clk_Init(void);
void enable_motor_port(void);
void enable_motor_clocks(void);
void enable_motor_pwm(void);
void disable_motor_pwm(void);
    // Set up points to the appropriate structures
void configure_ports(void);


Tc* mot_timer_set;
    // Need to be careful since all TcCount instances are part of a union
TcCount8* mot_timer;
Port* port;
PortGroup* bankA, *bankB;

#define MODE uint8_t
#define SIN8_MODE   0
#define SIN16_MODE  1
#define MOT_MODE    2

MODE mode = SIN8_MODE;

#define MOTOR_U_PIN 23
#define MOTOR_V_PIN 22

int main (void)
{
    Simple_Clk_Init();

    /* Enable the sin_timer8*/
    configure_ports();
    enable_motor_pwm();

    while(1)
    {
    }
}

void configure_ports(void){
    mot_timer_set = (Tc*)(TC4);
    mot_timer = (TcCount8*)(&mot_timer_set->COUNT8);
    port = (Port*)(PORT);
    bankA = (PortGroup*)(&port->Group[0]);
    bankB = (PortGroup*)(&port->Group[1]);
}

/* Set correct PA pins as TC pins for PWM operation */
void enable_motor_port(void)
{

        // Set up timer pin to use the timer
    bankA->PINCFG[MOTOR_U_PIN].reg |= 0x1;            // Enable multiplexing
    bankA->PINCFG[MOTOR_V_PIN].reg |= 0x1;            // Enable multiplexing

    bankA->PMUX[(MOTOR_U_PIN-1)/2].reg |= 0x5 << 4u;    // Select function F (timer)
    bankA->PMUX[MOTOR_V_PIN/2].reg |= 0x5;    // Select function F (timer)
}

void enable_motor_clocks(void)
{
    PM->APBCMASK.reg |= (1 << 12u);  // PM_APBCMASK is in the 12 position
    
    uint32_t temp=0x15;   // ID for TC4 is 0x15  (see table 14-2)
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

void enable_motor_pwm(void)
{
    enable_motor_port();
    enable_motor_clocks();

        // To mess with registers, disable timer first
    disable_motor_pwm();

    mot_timer->CTRLA.reg |= TC8_SETTINGS;
    mot_timer->PER.reg = 200;
    mot_timer->CC[0].reg = mot_timer->CC[1].reg = 50;  // Start with 50% duty cycle

    while(mot_timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

        // Invert the waveform from one of the outputs
    mot_timer->CTRLC.reg |= 0x1;
    mot_timer->CTRLA.reg |= 1 << 1u;    // Re-enable the timer
    mode = MOT_MODE;
}

void disable_motor_pwm(void)
{
    mot_timer->CTRLA.reg &= ~(1 << 1u);
    while(mot_timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
        // Revert the waveform from one of the outputs
    mot_timer->CTRLC.reg &= ~0x1;
    mot_timer->CTRLA.reg &= ~TC8_SETTINGS;
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
