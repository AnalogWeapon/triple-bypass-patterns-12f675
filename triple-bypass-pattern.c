#pragma config FOSC = INTRCIO   // Oscillator Selection bits (INTOSC oscillator: I/O function on GP4/OSC2/CLKOUT pin, I/O function on GP5/OSC1/CLKIN)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = OFF      // Power-Up Timer Enable bit (PWRT disabled)
#pragma config MCLRE = OFF      // GP3/MCLR pin function select (GP3/MCLR pin function is digital I/O, MCLR internally tied to VDD)
#pragma config BOREN = OFF      // Brown-out Detect Enable bit (BOD disabled)
#pragma config CP = OFF         // Code Protection bit (Program Memory code protection is disabled)
#pragma config CPD = OFF        // Data Code Protection bit (Data memory code protection is disabled)

#include <xc.h>

#define _XTAL_FREQ 4000000
#define MAX_HOLD_MS 1000
#define BTN_DEBOUNCE_MS 5
#define TAP_MS 250
#define MAX_LOOP_MS 60000
#define SILENCE_PULSE_BEFORE 10
#define SILENCE_PULSE_AFTER 10

#define BTN         GP0
#define POT         GP1
#define PULSE       GP2
#define MAIN_OUT    GP5

struct bf
{
    unsigned btnOn : 1;
    unsigned effectOn : 1;
    unsigned modeChangeFlag : 1;
    unsigned lastEffectState : 1;
    unsigned potOffsetActive : 1;
    unsigned SW6 : 1;
    unsigned SW7 : 1;
    unsigned SW8 : 1;
};

unsigned int btnPressed = 0;
unsigned int sinceLastPress = 0;
signed int loopTime = 1000;
signed int loopMoment = 0;
signed int loopOffset = 0;
signed int potValue;
signed int lastPotValue =0;
char gateOn = 0;
enum modes { regular, hold, tap };
enum modes mode;
struct bf b;

void init() {

    // interrupts
    INTCONbits.PEIE = 1;// enable peripheral interrupts
    INTCONbits.GIE = 1; // enable global interrupts
    
    //Timer0 Registers Prescaler= 1 - TMR0 Preset = 6 - Freq = 1000.00 Hz - Period = 0.001000 seconds
    INTCONbits.TMR0IE = 1;      // enable timer0 interrupt
    INTCONbits.TMR0IF = 0;      // clear timer0 interrupt
    OPTION_REGbits.T0CS = 0;    // bit 5  TMR0 Clock Source Select bit...0 = Internal Clock (CLKO) 1 = Transition on T0CKI pin
    OPTION_REGbits.T0SE = 0;    // bit 4 TMR0 Source Edge Select bit 0 = low/high 1 = high/low
    OPTION_REGbits.PSA = 0;     // bit 3  Prescaler Assignment bit...0 = Prescaler is assigned to the WDT
    OPTION_REGbits.PS2 = 0;     // bits 2-0  PS2:PS0: Prescaler Rate Select bits
    OPTION_REGbits.PS1 = 0;
    OPTION_REGbits.PS0 = 1;
    TMR0 = 6;                   // preset for timer register
    
    // init
    GPIO = 0;
    ANSEL = 0x00;       // no analog GPIOs
    CMCON = 0x07;       // comparator off
    //VRCON = 0;          // comparator reference voltage off
    
    // I/O
    /*
    GP0, pin 7 input for safety (btn)
    GP1, pin 6 input for safety (pot)
    GP2, pin 5 output
    GP3, pin 4 output
    GP4, pin 3 output
    GP5, pin 2 output (led)
    */
    TRISIO = 0b00000011;
    
    /*
    GP0, pin 7 starts off (btn)
    GP1, pin 6 starts off (pot)
    GP2, pin 5 starts off (pulse)
    GP3, pin 4 starts off
    GP4, pin 3 starts off
    GP5, pin 2 starts off (led)
    */
    GPIO = 0b00000000;
    
    // ADC
    ANSELbits.ANS1 = 1;     // AN1 analog mode
    ADCON0bits.ADFM = 1;    // ADC result is right justified
    ADCON0bits.VCFG = 0;    // 0 = Vdd is the +ve reference
    ADCON0bits.CHS = 1;     // select analog input, AN1
    ADCON0bits.ADON = 1;    // Turn on the ADC
    
    // read the pot 
    ADCON0bits.GO = 1;
    while(ADCON0bits.GO==1){};
    potValue = (ADRESH<<8)+ADRESL;
    
    // states
    b.btnOn = 0;
    b.effectOn = 0;
    b.modeChangeFlag = 0;
    b.potOffsetActive = 0;
    mode = tap;
}

void main(void) {
    __delay_ms(500);
    init();
    while(1);
}

void interrupt ISR(void) {
    if (INTCONbits.TMR0IF == 1) {
        if (loopMoment > MAX_LOOP_MS)
            loopMoment = MAX_LOOP_MS;
        if (sinceLastPress > MAX_LOOP_MS)
            sinceLastPress = MAX_LOOP_MS;
            
        // read the pot 
        ADCON0bits.GO = 1;
        while(ADCON0bits.GO==1){};
        if ((ADRESH<<8)+ADRESL > lastPotValue + 3 || (ADRESH<<8)+ADRESL < lastPotValue - 3) {
            // pot has moved for real
            b.potOffsetActive = 1;
            potValue = (ADRESH<<8)+ADRESL;
        }
        lastPotValue = potValue;
        
        if (BTN == 1) {
            // count btn down time
            btnPressed = btnPressed > MAX_HOLD_MS ? MAX_HOLD_MS : btnPressed++;
        }
        else {
            if (b.btnOn == 1) {
                // the button has been released
                if (mode == hold) {
                    b.effectOn = b.effectOn == 1 ? 0 : 1;
                    PULSE = 1;
                    __delay_ms(SILENCE_PULSE_BEFORE);
                    MAIN_OUT = b.effectOn;
                    __delay_ms(SILENCE_PULSE_AFTER);
                    PULSE = 0;
                    mode = regular;
                }
                b.modeChangeFlag = 0;
                b.btnOn = 0;
                btnPressed = 0;
            }
        }
        
        sinceLastPress++;
        
        if (btnPressed == BTN_DEBOUNCE_MS && b.btnOn == 0 && b.modeChangeFlag == 0) {
            // normal tap
            if (sinceLastPress >= TAP_MS) {
                if (mode != tap) {
                    b.effectOn = b.effectOn == 1 ? 0 : 1;
                }
                else {
                    b.potOffsetActive = 0;
                }
            }
            // double tap
            else if (mode != tap) {
                b.modeChangeFlag = 1;
                mode = tap;
                b.lastEffectState = b.effectOn;
            }
            // double tap
            if (mode == tap) {
                loopTime = sinceLastPress;
                loopMoment = 0;
            }
            PULSE = 1;
            __delay_ms(SILENCE_PULSE_BEFORE);
            MAIN_OUT = b.effectOn;
            __delay_ms(SILENCE_PULSE_AFTER);
            PULSE = 0;
            sinceLastPress = 0;
            b.btnOn = 1;
        }
        
        if (btnPressed == MAX_HOLD_MS && b.btnOn == 1 && b.modeChangeFlag == 0) {
            b.modeChangeFlag = 1;
            if (mode == regular) {
                mode = hold;
            }
            else if (mode == hold || mode == tap) {
                mode = regular;
                b.effectOn = b.lastEffectState;
            }
        }
        
        if (mode == tap) {
            loopOffset = b.potOffsetActive == 1 ? (potValue - 512) * 2 : 0;
            if (loopTime + loopOffset < BTN_DEBOUNCE_MS) {
                loopOffset = 0 - (loopTime - BTN_DEBOUNCE_MS);
            }
            if (loopMoment % (loopTime + loopOffset) == 0) {
                b.effectOn = b.effectOn == 1 ? 0 : 1;
                PULSE = 1;
                __delay_ms(SILENCE_PULSE_BEFORE);
                MAIN_OUT = b.effectOn;
                __delay_ms(SILENCE_PULSE_AFTER);
                PULSE = 0;
                loopMoment = 0;
            }
            loopMoment++;
        }

    }
    INTCONbits.TMR0IF = 0;
}