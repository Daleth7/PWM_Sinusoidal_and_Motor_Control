#include <asf.h>

#include "system_clock.h"
#include "global_ports.h"
#include "keypad.h"
#include "ssd.h"
#include "adc_dac.h"
#include "utilities.h"

void enable_motor_port(void);
void enable_sin_ports(void);
void enable_sin_tc_clocks(void);
void enable_motor_clocks(void);
void enable_sin_tc8(void);
void enable_sin_tc16(void);
void enable_motor_pwm(void);
void disable_sin_tc8(void);
void disable_sin_tc16(void);
void disable_motor_pwm(void);
    // Set up points to the appropriate structures
void configure_ports(void);

void update_sin_counter(void);
void switch_sin_counter(void);

void run_pwm_sin8(void);
void run_pwm_sin16(void);
void run_pwm_mot(void);

    // Have 8-bit register take a single step to target value with some delay in ms
BOOLEAN__ step_toward8(UINT8 target, volatile UINT8* reg, UINT32 delay);
void slow_motor_step(UINT32 target, UINT8 flag_check, UINT8* flag_src);

    // Need to be careful since all TcCount instances are part of a union
TcCount8* sin_timer8;
TcCount8* mot_timer;
TcCount16* sin_timer16;

#define MODE uint8_t
#define SIN8_MODE   0
#define SIN16_MODE  1
#define MOT_MODE    2

#define SIN_MARGIN  10
#define TIMER_PIN   13
#define MOTOR_U_PIN 23
#define MOTOR_V_PIN 22
#define ADC_PIN     11
#define AIN_PIN     0x13

MODE mode = SIN8_MODE;
    // Keypad variables
const UINT8 key_trig = SIN_MARGIN+1;
UINT8 row, col;

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

int main (void)
{
    Simple_Clk_Init();
    delay_init();

    /* Enable the sin_timer8*/
    configure_global_ports();
    configure_ssd_ports();
    configure_keypad_ports();
    configure_adc_default(AIN_PIN);
    map_to_adc_odd(ADC_PIN);
    configure_ports();

    sin_timer8->CC[1].reg = SIN_MARGIN + 1;

    while(1)
    {   // In a future implementation, write ISRs
        switch(mode){
            case SIN8_MODE:
                run_pwm_sin8();
                break;
            case SIN16_MODE:
                run_pwm_sin16();
                break;
            case MOT_MODE:
                run_pwm_mot();
                break;
            default:
                break;
        }
    }
}

void run_pwm_sin8(void){
    enable_sin_tc8();
    while(1){
        if(sin_timer8->INTFLAG.reg & 0x1){
            update_sin_counter();
            sin_timer8->INTFLAG.reg |= 0x1;
        } else if (sin_timer8->COUNT.reg == key_trig){
            check_key(&row, &col);
                // Switch modes
            if(row == 0 && col == 0xD){
                mode = SIN16_MODE;
                return;
            } else if(row == 3 && col == 0xD) {
                mode = MOT_MODE;
                return;
            }
        }
        row = 4;
        col = 16;
    }
}

void run_pwm_sin16(void){
    enable_sin_tc16();
    while(1)
    {   // In a future implementation, write ISRs
        if(sin_timer16->INTFLAG.reg & 0x1){
            update_sin_counter();
            sin_timer16->INTFLAG.reg |= 0x1;
        } else if (sin_timer16->COUNT.reg == key_trig){
            check_key(&row, &col);
                // Switch modes
            if(row == 0 && col == 0xB){
                mode = SIN8_MODE;
                return;
            } else if(row == 3 && col == 0xD) {
                mode = MOT_MODE;
                return;
            }
        }
        row = 4;
        col = 16;
    }
}

void run_pwm_mot(void){
    enable_motor_pwm();
    const UINT32 period = mot_timer->PER.reg;

    UINT32 stop_duty = period/2;
        // Stores bit flags detailing safety and control settings
        //  Bits:
        //      0 - safely stop the motor before switch modes
        //      1 - safely start the motor before giving user control
        //      2 - Switch between control with the keypad and potentiometer
        //      3 - safely switch to pot control
    UINT8 safe_set = 0x2;

    UINT16 ten_place = 100;

        // Gradually start the motor before allowing control
    slow_motor_step(map32(read_adc(), 0, 0xFFFF, SIN_MARGIN, period-SIN_MARGIN), 1, &safe_set);

    while(1)
    {   // In a future implementation, write ISRs
        check_key(&row, &col);
            // Switch modes
        if(row == 3 && col == 0xB){ // Exit motor mmode
            safe_set |= 0x1;
        } else if(row == 2 && col == 0xD){  // Change to pot control
            safe_set &= ~(1<<2u);
            safe_set |= (1<<3u);
        } else if(row == 2 && col == 0xB){  // Change to key control
            safe_set |= (1<<2u);
        }
            // Gradually stop the motor before switching modes
        while(safe_set & 0x1){
            slow_motor_step(stop_duty, 0, &safe_set);
            mode = SIN8_MODE;
            return;
        }
            // Safely switch to pot control
        if(safe_set & (1<<3u)){
            slow_motor_step(map32(read_adc(), 0, 0xFFFF, SIN_MARGIN, period-SIN_MARGIN), 3, &safe_set);
        }
                // Change speed
        if(safe_set & (1<<2u)){   // Use keypad
            if(row == 0){
                if(col == 0x2 && mot_timer->CC[0].reg != period - SIN_MARGIN){
                    ++mot_timer->CC[0].reg;
                    ++mot_timer->CC[1].reg;
                }else if(col == 0x8 && mot_timer->CC[0].reg != SIN_MARGIN){
                    --mot_timer->CC[0].reg;
                    --mot_timer->CC[1].reg;
                }
            }
/*
              // Allow user to enter an arbitrary duty cycle
            else if(col != 0 && (col_conv = find_lsob(col)) > 1){
                col_conv = (3-row)*3 + (3-col_conv);
                if(mot_timer->CC[0].reg < 20){
                    mot_timer->CC[0].reg = mot_timer->CC[0].reg * 10 + col_conv;
                    mot_timer->CC[1].reg = mot_timer->CC[0].reg;
                }
            }
*/
        } else {    // Use potentiometer
            mot_timer->CC[0].reg = map32(read_adc(), 0, 0xFFFF, SIN_MARGIN, period-SIN_MARGIN);
            mot_timer->CC[1].reg = mot_timer->CC[0].reg;
        }
        if(mot_timer->INTFLAG.reg &= 0x1){
            display_dig(
                0,
                //((safe_set & (1<<2u)) ? 10000 : 0),
                (((mot_timer->CC[0].reg*100)/period)%ten_place)*10/ten_place,
                row, FALSE__, FALSE__
                );
        }
            // Display the current duty cycle
        ten_place *= 10;
        if(ten_place > 10000)  ten_place = 10;
    }
}

void update_sin_counter(void){
    static int8_t count = 1, counter16 = 0, count2 = 1;

    if(mode == SIN8_MODE){
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
    sin_timer8 = timer2_8;
    mot_timer = timer4_8;
    sin_timer16 = timer2_16;
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

/* Set correct PA pins as TC pins for PWM operation */
void enable_motor_port(void)
{
        // timer pin and adc output pins are shared, so change its settings
    bankA->PINCFG[TIMER_PIN].reg &= ~0x1;            // Disable multiplexing
    bankA->DIR.reg |= (1 << TIMER_PIN);
    bankA->OUT.reg |= (1 << TIMER_PIN);            // Set high

        // Set up timer pin to use the timer
    bankA->PINCFG[MOTOR_U_PIN].reg |= 0x1;            // Enable multiplexing
    bankA->PINCFG[MOTOR_V_PIN].reg |= 0x1;            // Enable multiplexing

    bankA->PMUX[(MOTOR_U_PIN-1)/2].reg |= 0x5 << 4u;    // Select function F (timer)
    bankA->PMUX[MOTOR_V_PIN/2].reg |= 0x5;    // Select function F (timer)
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
// Set presynchronizer to prescaled clock
// Prescale clock by 1
// Start in 16-bit mode
// Select the Match PWM waveform
#define TC16_SETTINGS (     \
        (0x1 << 12u)        \
        | (0x0 << 8u)       \
        | (0x0 << 2u)       \
        | (0x3 << 5u)       \
    )

void enable_sin_tc8(void)
{
    enable_sin_ports();
    enable_sin_tc_clocks();

        // To mess with registers, disable sin_timer8 first
    disable_sin_tc8();
    disable_sin_tc16();
    disable_motor_pwm();

    bankB->OUT.reg &= ~0x00000200;

    sin_timer8->CTRLA.reg |= TC8_SETTINGS;
    sin_timer8->PER.reg = 49;
//    sin_timer8->PER.reg = 107;    // for if statement
    sin_timer8->CC[1].reg = 40;

    while(sin_timer8->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    sin_timer8->CTRLA.reg |= 1 << 1u;    // Re-enable the sin_timer8
    mode = SIN8_MODE;
}

void enable_sin_tc16(void)
{
    enable_sin_ports();
    enable_sin_tc_clocks();

    bankB->OUT.reg |= 0x00000200;

        // To mess with registers, disable timer first
    disable_sin_tc8();
    disable_sin_tc16();
    disable_motor_pwm();

    sin_timer16->CTRLA.reg |= TC16_SETTINGS;
    sin_timer16->CC[1].reg = 40;

    while(sin_timer16->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    sin_timer16->CTRLA.reg |= 1 << 1u;    // Re-enable the timer
    mode = SIN16_MODE;
}

void enable_motor_pwm(void)
{
    enable_motor_port();
    enable_motor_clocks();

        // To mess with registers, disable timer first
    disable_sin_tc8();
    disable_sin_tc16();
    disable_motor_pwm();

    mot_timer->CTRLA.reg |= TC8_SETTINGS;
    mot_timer->PER.reg = 200;
    mot_timer->CC[0].reg = mot_timer->CC[1].reg = 100;  // Start with 50% duty cycle

    while(mot_timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding

    mot_timer->CTRLC.reg |= 0x1;    // Invert the waveform from one of the outputs
    mot_timer->CTRLA.reg |= 1 << 1u;    // Re-enable the timer
    mode = MOT_MODE;
}

void disable_sin_tc8(void)
{
    sin_timer8->CTRLA.reg &= ~(1 << 1u);
    while(sin_timer8->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
    sin_timer8->CTRLA.reg &= ~TC8_SETTINGS;
}

void disable_sin_tc16(void)
{
    sin_timer16->CTRLA.reg &= ~(1 << 1u);
    while(sin_timer16->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
    sin_timer16->CTRLA.reg &= ~TC16_SETTINGS;
}

void disable_motor_pwm(void)
{
    mot_timer->CTRLA.reg &= ~(1 << 1u);
    while(mot_timer->STATUS.reg & (1 << 7u));    // Synchronize before proceeding
        // Revert the waveform from one of the outputs
    mot_timer->CTRLC.reg &= ~0x1;
    mot_timer->CTRLA.reg &= ~TC8_SETTINGS;
}

void switch_sin_counter(void){
    if(mode == SIN8_MODE)   enable_sin_tc16();
    else                    enable_sin_tc8();
}

BOOLEAN__ step_toward8(UINT8 target, volatile UINT8* reg, UINT32 delay){
    if(target > *reg)       ++(*reg);
    else if(target < *reg)  --(*reg);
    delay_ms(delay);
    return target == *reg;
}

void slow_motor_step(UINT32 target, UINT8 flag_check, UINT8* flag_src){
    UINT8 flag_check_bit = 1 << flag_check;
    while(*flag_src & flag_check_bit){
        *flag_src &= ~flag_check_bit;
        *flag_src |= !step_toward8(target, &mot_timer->CC[0].reg, 10) << flag_check;
        mot_timer->CC[1].reg = mot_timer->CC[0].reg;
    }
}