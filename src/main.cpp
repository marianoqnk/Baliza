/*
* -------------------------------------------------------------------------
* Baliza activada por luz para cachés de noche con Atmel ATtiny 13V
* Versión 0.1 para RL_V0.3 circuito
* -------------------------------------------------------------------------
*
*
* Se originó en la sub-electrónica foro sobre http://www.geoclub.de
* (Http://www.geoclub.de/ftopic5753.html)
* http://reaktivlicht.pbworks.com/w/page/3708758/FrontPage
Diseñado * Implementación de la luz reactivo con LDR como de windi
* En C, el consumo de energía se minimiza tanto como sea posible.
*
* Thomas Stief <geocaching@mail.thomas-stief.de>
*/
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

 
/* =================================================================
   Declaraciones
   ================================================================= */
 
#define UMBRAL_NOCHE       (unsigned int)500    //umbrales LDR noche
#define UMBRAL_DIA         (unsigned int)530	//umbral LDR dia
#define UMBRAL_DIA_COUNTER (unsigned int)64   //numero de veces que tiene que haber mucha luz para pasar a modo día
#define INC_BRILLO_ENVIAR   (int)1   //umbral parpadeo
#define TEMPORIZADOR_NOCHE      WDTO_120MS //temporizador nocturno
#define TEMPORIZADOR_DIA        WDTO_8S  //temporizador diurno

#define LED1 PORTB0 //PIN 5
#define LED2 PORTB1 //PIN 6
#define LDR_POWER PORTB3 // ENCENDER Y APAGAR LA ALIMENTACION DEL LED PIN 2
//PB4 PIN3 ENTRADA ADC2
//PB2 PIN7 LIBRE
#define TOGGLE 2
enum enum_estado {DIA_LDR,DIA,NOCHE,ENVIANDO,CAMBIANDO} estado; 
boolean doBlink(void);

 
/* =================================================================
   Las variables globales del Estado
   ================================================================= */

unsigned int brilloAnterior=0;   // Cache el último valor de brillo
unsigned int contadorUmbralDiaSuperado=0;      // Cuente cuántas veces "días" mide
unsigned int wBlinkCounter=0;      // Contador para el generador de flash


/* ================================================ =================
   Rutina de servicio de interrupción para la finalización de la conversión AD
      que realmente no hace nada
   ================================================== =============== */

ISR(ADC_vect){};
 
/* =================================================================
    Inicialización del chip
   
   In: nix
   Out: nix
   ================================================================= */
void initChip(void)
{
   
   ADMUX = 0x02;                     // Habilitar ADC2 (pin 3)
   pinMode(LED1,OUTPUT);
   pinMode(LED2,OUTPUT);
   pinMode(LDR_POWER,OUTPUT);
   //DDRB = 0x0b;                                 // 00001011
     // Resistencias pull-up para todo, excepto para el
   // salidas digitales y la ADC2 entrada analógica 
   // Habilitar (PB4) // xx100100
   // Save => actual objetivo
   //Pines a PullUP para reducir consumo?
   digitalWrite(2,HIGH);//pone los pines no utilizdos a 1 para reducir el consumo
   digitalWrite(5,HIGH);
   //PORTB = 0x24;    //valor de salida                              // 00100100
   // Las entradas digitales no utilizadas deaktivien
   // => Ahorra energía
   // No se necesitan entradas digitales // xx111111
   DIDR0 = 0x3F;                                 // 00111111
}
 
 
/* ================================================ =================
   La lectura de la entrada analógica
     la función se inicia la adquisición de la señal analógica, y
    devuelve el valor actual
   
   En: nix
   Salida: (PALABRA) señal analógica actual
   ================================================== =============== */
unsigned int readADC(void)
{
   unsigned int wValue;
   
   set_sleep_mode(SLEEP_MODE_ADC); //Seleciona el modo dormir y que lo despierte el ADC
   sleep_enable();
   sei(); //Enable interrupt
 
   // ADC en, // 1XXXXXXX
   // // ADC inicio x1xxxxxx
   // No AUTO gatillo // xx0xxxxx
   // Habilitar ADC interrupción // xxxx1xxx
   // Divisor del reloj ADC (1: 2) // XXXXX000
   
   ADCSRA = 0xc8;               // 11001000
                         // La corrección de errores (gracias NC666):
                     // El 0xd8 valor original es incorrecto, ya que también establece ADIF
   // ADCSRA = 0x88; // 10001000
                        // Propuesta NC666: Probablemente hace falta establecer ADSC poco
                     // A continuación, el ADC se inicia en el modo de suspensión automática - Todavía no he probado
 
   
   sleep_cpu(); //Enviar a dormir  procesador durante la conversion
   sleep_disable();
   cli(); //Disable interrupts
   wValue = ((unsigned int)ADCL + (((unsigned int)(ADCH))<<8)) & 0x03ff;   
   ADCSRA = 0x00;              //Off  ADC
   return wValue;
}
 

 
/* ================================================ =================
   La inicialización del temporizador de vigilancia y establecer el 
     intervalo 
     se establece en el modo de interrupción
   
   En: (unsidned char) bTimeConst
   Nix falló
   ================================================== =============== */
void setWD(unsigned char bTimeConst)
{
   
   cli(); // Desactivar interrupción
   wdt_enable(bTimeConst);
   // Reinicio de la vigilancia Disable (sólo queremos interrumpir)
   // Primero borrar el bit correspondiente en MCUSR, de lo contrario, la enmienda
   // En WDTCR ningún efecto
   MCUSR &= ~_BV(WDRF);
    // Watchdog Timer de interrupción del interruptor wAlthough -> 1
   // Cambiar a fuera, debe "bit de bloqueo"
   // WDCE WDCE debe configurarse -> 1
   // Cambiar WDE off -> 0                           WDE  -> 0
   WDTCR = (WDTCR & ~_BV(WDE)) |_BV(WDCE) |_BV(WDTIE);
   sei();// Habilita interrupciones
}
 
/* ================================================ =================
   Rutina de servicio de interrupción para el procesamiento de las alarmas cíclicas
        => En realidad el programa principal
   ================================================== =============== */

ISR(WDT_vect) //INterrupcion del WatchDog
{
unsigned int brilloActual;
int incrementoBrillo;

if(estado!=DIA && estado!=ENVIANDO){  
   brilloActual = readADC();
   incrementoBrillo = brilloActual - brilloAnterior;
   brilloAnterior = brilloActual;      
   }
switch(estado)
   {
      case DIA:
            digitalWrite(LDR_POWER,HIGH); // Espera a algo; sólo entonces consultar
            estado=DIA_LDR;
            setWD(TEMPORIZADOR_NOCHE); //ok
            break;

      case ENVIANDO:
            if(doBlink())estado=NOCHE; //Envia
            setWD(TEMPORIZADOR_NOCHE);
            break;

      case DIA_LDR:
            if(brilloActual < UMBRAL_NOCHE)  // Umbral noche Si está oscuro
            {// => De a modo nocturno
               estado=NOCHE;
               setWD(TEMPORIZADOR_NOCHE);
               
            }else                     // Si todavía es luz apago la LDR
            {                        // => Inténtalo de nuevo en 8s
               digitalWrite(LDR_POWER,LOW);
               estado=DIA;
               setWD(TEMPORIZADOR_DIA);
               
            }
            break;

      case NOCHE:
            if(incrementoBrillo > INC_BRILLO_ENVIAR )   // Si el brillo en el último ciclo
               {               // Se ha elevado => rumblinken algo
                  wBlinkCounter = 0;      // Blinkgenerator initialisieren
                  estado=ENVIANDO;
               }
            else if(brilloActual > UMBRAL_DIA) estado=CAMBIANDO;    
            setWD(TEMPORIZADOR_NOCHE);     
            break;

      case CAMBIANDO:
            if(brilloActual > UMBRAL_DIA)   //tiene que superar el umbral de día por UMBRAL_DIA_COUNTER veces
            { 
               contadorUmbralDiaSuperado++;                  
               if(contadorUmbralDiaSuperado > UMBRAL_DIA_COUNTER)   //Si supera el número de veces, es de día
               {                       
                  estado=DIA;
                  contadorUmbralDiaSuperado = 0;
                  digitalWrite(LDR_POWER,LOW);
                  setWD(TEMPORIZADOR_DIA);
               }
               else setWD(TEMPORIZADOR_NOCHE);
               
            }
            else            // Si todavía está oscuro, fue un error de medida.  vuelta a la noche
            {
               contadorUmbralDiaSuperado=0;
               estado=NOCHE;
               setWD(TEMPORIZADOR_NOCHE);
            }
            break;
     
   }  
} 

/* =================================================================
   Parpadeo rutina - es llamado por el ISR del perro guardián
   
   In:  nix
   Out: nix
   ================================================================= */
// Morsecode für "N 50 12 345"
const unsigned char bSequenz1[] PROGMEM = {    0xe8, 0x0a, 0xa8, 0xee, 
                           0xee, 0xe0, 0x2e, 0xee, 
                           0xe2, 0xbb, 0xb8, 0x0a, 
                           0xbb, 0x8a, 0xae, 0x2a, 
                           0xa0, 0x00, 0x00, 0x00 };
// Morsecode für "E 008 54 321"
const unsigned char bSequenz2[] PROGMEM = {    0x80, 0xee, 0xee, 0xe3, 
                           0xbb, 0xbb, 0x8e, 0xee, 
                           0xa0, 0x2a, 0xa2, 0xab, 
                           0x80, 0xab, 0xb8, 0xae, 
                           0xee, 0x2e, 0xee, 0xe0 };
boolean doBlink(void)
{
   unsigned char bByte,bBit;   
   if(wBlinkCounter < 128)
   {
      // Byte leído de la memoria flash (todo, desde el bit 3 del tapajuntas contador
      // Determina la posición del byte
      bByte = pgm_read_byte(&bSequenz1[(unsigned char)(wBlinkCounter>>3)]); //divide entre 8 haciendo 3 rotaciones y lee ese caracter del array
       // Bit 0: 2 determinar el bit en el byte de datos, pero a la inversa
      // Orden
      bBit = 7 - (unsigned char)(wBlinkCounter&7); //cacula que bit debe de sacar
      
      if((bByte&(1<<bBit)) != 0) // Comprobar si el bit es entonces ......
         digitalWrite(LED1,HIGH); 
      else               
         digitalWrite(LED1,LOW);
 
      // Todo el asunto para el otro LED
      bByte = pgm_read_byte(&bSequenz2[(unsigned char)(wBlinkCounter>>3)]);
      if((bByte&(1<<bBit)) != 0)
         digitalWrite(LED2,HIGH);
      else
         digitalWrite(LED2,LOW);
   }
   else   // Si la secuencia es más: Switch LEDs
   {
      digitalWrite(LED1,LOW);
      digitalWrite(LED2,LOW);
   }
   
   if(wBlinkCounter++ > 130) // Dos ciclos más, entonces el 
      return true;     // Secuencia a su fin y la bandera se borra
   return false;
}
 
 
/* =================================================================
    Programa principal
        => En realidad hace casi nada, excepto que el procesador
           inicializar, empezar el organismo de control, y en un
           Introduzca bucle infinito
   ================================================================= */
int main (void)
{

   initChip();   // Inicializar el micro
   estado=DIA;
   digitalWrite(LDR_POWER,LOW); //Quita alimentacion a LDR
   setWD(TEMPORIZADOR_DIA);// Habilitar Watchdog y de hecho para el modo día
   while(1)  // Bucle infinito que envía repetidamente que el procesador a dormir
   {
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      sleep_cpu();
     
   }       
   return (0);
}