/*
 * AES_128.c
 *
 * Created: 12/3/2017 6:33:24 PM
 * Author: Juanpi
 */

#include <io.h> 
#include <stdio.h>
#include <string.h>
#include <delay.h>
#include <ff.h>
#include "display.h"
#include "AES.h"

#asm
    .equ __lcd_port=0x11
    .equ __lcd_EN=4
    .equ __lcd_RS=5
    .equ __lcd_D4=0
    .equ __lcd_D5=1
    .equ __lcd_D6=2
    .equ __lcd_D7=3
#endasm

unsigned int time_count_all;
unsigned int time_count_ciph;
unsigned int br, n;    
unsigned char i;
unsigned char option, key_set = 0;
unsigned char file_name[50];   
unsigned char key[16], expanded_key[176]; 
unsigned char aux[20];
    
// TO READ AND WRITE 16 BLOCKS AT ONCE   
unsigned char buffer_in[512];
unsigned char buffer_out[512];     

// Timer 0 output compare A interrupt service routine
interrupt [TIM0_COMPA] void timer0_compa_isr(void) {
    // ONE COUNT = 1 ms
    time_count_ciph++;
}

// Timer1 output compare A interrupt service routine
interrupt [TIM1_COMPA] void timer1_compa_isr(void) {
    disk_timerproc();
    /* MMC/SD/SD HC card access low level timing function */
}

// Timer2 output compare interrupt service routine
interrupt [TIM2_COMPA] void timer2_compa_isr(void){
    time_count_all++;
}

void start_timer_all() {
    TCCR2B=(0<<WGM22) | (0<<CS22) | (1<<CS21) | (1<<CS20);
}

void reset_timer_all() {
    time_count_all = 0;
    TCNT2 = 0x00;
}        

void stop_timer_all() {
    TCCR2B = 0x00;
}

void start_timer_ciph() {
    TCCR0B = (0<<WGM02) | (0<<CS02) | (1<<CS01) | (1<<CS00);
}

void reset_timer_ciph() {
    time_count_ciph = 0; 
    TCNT0 = 0;
} 

void stop_timer_ciph() {
    TCCR0B = 0x00;
}

void main(void) {
    /* FAT function result */
    FRESULT res;

    /* will hold the information for logical drive 0: */
    FATFS drive;
    FIL file_in; // file objects 
    FIL file_out;      
    
    CLKPR=0x80;
    CLKPR=0;    //Micro Trabajar· a 16MHz
    
    /*Configurar el PORTB I/O*/
    DDRB=0b11101101;
    
    // Timer/Counter 0 initialization
    // Clock source: System Clock
    // Clock value: 125.000 kHz
    // Mode: CTC top=OCR0A
    // OC0A output: Disconnected
    // OC0B output: Disconnected
    // Timer Period: 1 ms
    TCCR0A=(0<<COM0A1) | (0<<COM0A0) | (0<<COM0B1) | (0<<COM0B0) | (1<<WGM01) | (0<<WGM00);
    TCCR0B=(0<<WGM02) | (0<<CS02) | (1<<CS01) | (1<<CS00);
    TCNT0=0x00;
    OCR0A=0x7C;
    OCR0B=0x00;      
    
    // Timer/Counter 0 Interrupt(s) initialization
    TIMSK0=(0<<OCIE0B) | (1<<OCIE0A) | (0<<TOIE0);

    
    // CÛdigo para hacer una interrupciÛn periÛdica cada 10ms
    // Timer/Counter 1 initialization
    // Clock source: System Clock
    // Clock value: 1000.000 kHz
    // Mode: CTC top=OCR1A
    // Compare A Match Interrupt: On
    TCCR1B=0x09;
    OCR1AH=0x27;
    OCR1AL=0x10;
    TIMSK1=0x02;        
    
    // Timer/Counter 2 initialization
    // Clock source: System Clock
    // Clock value: 250.000 kHz
    // Mode: CTC top=OCR2A
    // OC2A output: Disconnected
    // OC2B output: Disconnected
    // Timer Period: 1 ms
    ASSR=(0<<EXCLK) | (0<<AS2);
    TCCR2A=(0<<COM2A1) | (0<<COM2A0) | (0<<COM2B1) | (0<<COM2B0) | (1<<WGM21) | (0<<WGM20);
    TCCR2B=(0<<WGM22) | (0<<CS22) | (1<<CS21) | (1<<CS20);
    TCNT2=0x00;
    OCR2A=0xF9;
    OCR2B=0x00;       
    
    // Timer/Counter 2 Interrupt(s) initialization
    TIMSK2=(0<<OCIE2B) | (1<<OCIE2A) | (0<<TOIE2);

    
    stop_timer_all(); 
    stop_timer_ciph();
    reset_timer_all();
    reset_timer_ciph();   
    
    SetupLCD();
    
    // USART1 initialization
    // Communication Parameters: 8 Data, 1 Stop, No Parity
    // USART1 Receiver: On
    // USART1 Transmitter: On
    // USART1 Mode: Asynchronous
    // USART1 Baud Rate: 9600 (Double Speed Mode)
    UCSR1A=(0<<RXC1) | (0<<TXC1) | (0<<UDRE1) | (0<<FE1) | (0<<DOR1) | (0<<UPE1) | (1<<U2X1) | (0<<MPCM1);
    UCSR1B=(0<<RXCIE1) | (0<<TXCIE1) | (0<<UDRIE1) | (1<<RXEN1) | (1<<TXEN1) | (0<<UCSZ12) | (0<<RXB81) | (0<<TXB81);
    UCSR1C=(0<<UMSEL11) | (0<<UMSEL10) | (0<<UPM11) | (0<<UPM10) | (0<<USBS1) | (1<<UCSZ11) | (1<<UCSZ10) | (0<<UCPOL1);
    UBRR1H=0x00;
    UBRR1L=0x67;   
    
    // Global enable interrupts
    #asm("sei")   
    
    MoveCursor(0,0);
    StringLCD("MOUNTING");     
    
    /* Inicia el puerto SPI para la SD */
    disk_initialize(0);
    delay_ms(200);       
    
    /* mount logical drive 0: */
    if ((res=f_mount(0,&drive))==FR_OK){
        MoveCursor(0,0);
        StringLCD("DRIVE FOUND!");
        delay_ms(1500);  
        
        
        while (1) {           
            if (key_set == 1) {
                puts("WELCOME!\nCHOOSE AN OPTION FROM OUR MENU\n\n");         
                puts("1. CIPHER\n");
                puts("2. DECIPHER\n");
                puts("3. CHANGE KEY\n");
                
                option = getchar();
                switch (option) {
                    case '1':    
                        stop_timer_all(); 
                        stop_timer_ciph();
                        reset_timer_all();
                        reset_timer_ciph();  
                        
                        puts("\n\nTYPE NAME OF FILE (WITH EXTENSION):");   
                        gets(file_name, 100);
                        
                        /*Lectura de Archivo*/
                        res = f_open(&file_in, file_name, FA_OPEN_ALWAYS | FA_WRITE | FA_READ);
                        
                        if (res==FR_OK) {   
                            MoveCursor(0,0);
                            StringLCDVar(file_name);
                            delay_ms(1500);
                                   
                            puts("\n\nTYPE NAME OF OUTPUT FILE (WITHOUT EXTENSION):");   
                            gets(file_name, 100);
                            strcat(file_name, ".aes");
                            res = f_open(&file_out, file_name, FA_OPEN_ALWAYS | FA_WRITE | FA_READ);  
                                
                            if (res==FR_OK) {            
                                MoveCursor(0,0);
                                StringLCDVar(file_name);  
                                puts(file_name);
                                delay_ms(1500);

                                EraseLCD();
                                MoveCursor(0,0);
                                StringLCD("READING...");
                                delay_ms(1500);
                                                        
                                EraseLCD();
                                MoveCursor(0,0);
                                StringLCD("PROCESSING...");
                                delay_ms(1500);  
                                                    
                                
                                expand_key(key, expanded_key);  
                                start_timer_all();
                                f_read(&file_in, buffer_in, 256,&br); //leer archivo  
                                n = 0;
                                while (br == 256) {  
                                    n++;
                                    start_timer_ciph();
                                    cipher(buffer_in, br, expanded_key, buffer_out); 
                                    stop_timer_ciph();
                                    i = br; 
                                    f_write(&file_out, buffer_out, i, &br);
                                    f_read(&file_in, buffer_in, 256,&br); //leer archivo   
                                    sprintf(aux, "BLOCK %i", n*16);
                                    MoveCursor(0,1);
                                    StringLCDVar(aux);
                                }               
                                stop_timer_all(); 
                                
                                // ANTES DE LLAMAR LA FUNCION IMPRIMIR EL NOMBRE DEL ARCHIVO
                                printf("BYTES/BITS CYPHERED: %i/%i\n", n*16, n*16*8);
                                printf("NUMBER OF BLOCKS: %i\n", n*16);
                                printf("TOTAL TIME: %f\n", time_count_all/0.001);     
                                printf("CYPHER TIME: %f\n", time_count_ciph/0.001);
                
                                EraseLCD();
                                MoveCursor(0,1);
                                StringLCD("FILE SAVED");
                                delay_ms(1500);

                                f_close(&file_out);
                                delay_ms(500);   
                            } else
                                StringLCD("FILE NOT FOUND");  
                            f_close(&file_in);
                        } else
                            StringLCD("FILE NOT FOUND");
                        break;
                    case '2':
                        puts("\n\nTYPE NAME OF FILE (WITHOUT EXTENSION)");    
                        break; 
                    case '3':
                        key_set = 0;
                        break;
                    default:
                        puts("WRONG INPUT");
                }   
            } else {
                puts("WELCOME!\n PLEASE ENTER THE NAME OF THE FILE WITH THE KEY (.txt NO EXTENSION)\n\n"); 
                gets(file_name,100);     
                strcat(file_name, ".txt");
                /*Lectura de Archivo*/
                res = f_open(&file_in, file_name, FA_OPEN_ALWAYS | FA_WRITE | FA_READ);
                if (res==FR_OK){
                    MoveCursor(0,1);
                    StringLCD("FILE FOUND");
                    delay_ms(1500);

                    EraseLCD();
                    MoveCursor(0,1);
                    StringLCD("READING  ...");
                    delay_ms(1500);

                    f_read(&file_in, buffer_in, 16,&br); //leer archivo
                                
                    if (br == 16) {  
                        for (i = 0; i < 16; i++) {
                            key[i] = buffer_in[i];
                        }        
                        key_set = 1;
                        EraseLCD();
                        MoveCursor(0,1);
                        StringLCD("KEY READ");
                        delay_ms(1500);
                    }

                    f_close(&file_in);
                    delay_ms(500);
                } else
                    StringLCD("FILE NOT FOUND");
            }               
        }      
    } else {
        StringLCD("DRIVE NOT FOUND");
    } 
    f_mount(0, 0); //Cerrar drive de SD
    while(1);
}
