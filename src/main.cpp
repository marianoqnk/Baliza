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
#include <util/delay.h>

 
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
//void digitalWrite(char , char );
 
/* =================================================================
   Las variables globales del Estado
   ================================================================= */

unsigned int brilloAnterior;   // Cache el último valor de brillo
unsigned int contadorUmbralDiaSuperado;      // Cuente cuántas veces "días" mide
unsigned int wBlinkCounter;      // Contador para el generador de flash
boolean fBDimmerMode;
/* ================================================ =================
   Rutina de servicio de interrupción para la finalización de la conversión AD
      que realmente no hace nada
   ================================================== =============== */
void AdcInterrupt(void)
{
}
void WdtInterrupt(void);

 
/* =================================================================
    Inicialización del chip
   
   In: nix
   Out: nix
   ================================================================= */
void initChip(void)
{
   // ADC
   // ------------------------------------------------ ------
                                 
   // Habilitar ADC2 (pin 3) // XXXXXX10
   // // Vcc como tensión de referencia x0xxxxxx
   //Alineado a la derecha  Resultados xx0xxxxx
   
   ADMUX = 0x02;                     // 00000010
 
    // IO Digital
   // ------------------------------------------------ ------
   // En primer lugar en la entrada 
   // Excepto por LED1, LED2, el LDR_POWER // xx001011
   pinMode(LED1,OUTPUT);
   pinMode(LED2,OUTPUT);
   pinMode(LDR_POWER,OUTPUT);
   //DDRB = 0x0b;                                 // 00001011
     // Resistencias pull-up para todo, excepto para el
   // salidas digitales y la ADC2 entrada analógica 
   // Habilitar (PB4) // xx100100
   // Save => actual objetivo
   digitalWrite(2,HIGH);//pone los pines no utilizdos a 1 para reducir el consumo
   digitalWrite(5,HIGH);
   //PORTB = 0x24;    //valor de salida                              // 00100100
   // Las entradas digitales no utilizadas deaktivien
   // => Ahorra energía
   // No se necesitan entradas digitales // xx111111
   DIDR0 = 0x3F;                                 // 00111111
   TCCR0A |=  _BV(WGM01) | _BV(WGM00);//modo fast pwm
   TCCR0B |=  _BV(CS01) |_BV(CS00);//modo normal clock /8 por systen clock preescaler y luego preescaler de timer
						//9,6Mhz/8 = 1,2Mhz preescales+r 1024 = 1,1Khz
   attachInterrupt(9,AdcInterrupt,RISING);
   attachInterrupt(8,WdtInterrupt,RISING);
}
/*void analogWrite(unsidned char pin, unsidned char value)
{
 
  if(pin==LED1)
  {
	  if(value==0) digitalWrite(PORTB0, LOW);
	  else  {
	  TCCR0A |= _BV(COM0A1); //activa que el pin vaya a OCR0A ponerlo a 0 para operacion normal
	  OCR0A = value;
	  }
  }
  else if(pin==LED2)
  {
	if(value==0) digitalWrite(PORTB1, LOW);
	  else{
	  TCCR0A |=  _BV(COM0B1);//activa que el pin vaya a OCR0B ponerlo a 0 para operacion normal
	  OCR0B = value;
	  }
  }
  
}*/
 
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
   
   // Preparar el tendido sueño del procesador
   // -> Procesador en el modo de envío de reducción de ruido ADC: 
   // Modo de sueño más profundo, lo que permite un despertar por ADC
   set_sleep_mode(SLEEP_MODE_ADC); 
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
 
   //Enviar a dormir  procesador
   sleep_cpu();
   // Buenos días, procesador! Dormimos bien ...
   // ... Y para que no se duerma otra vez:
   sleep_disable();
   cli(); //Disable interrupts
 
   // Leer el valor (los dos bits más bajos de ADCH 
   // (8 bit desplazado a la izquierda) + ADCL
   wValue = ((unsigned int)ADCL + (((unsigned int)(ADCH))<<8)) & 0x03ff;
   
   //Off  ADC
   ADCSRA = 0x00;               // 00000000
   
   return wValue;
}
 

 
/* ================================================ =================
   Los valores de salida a las salidas digitales
    estados posibles:
      ON (= 1) LED está encendido (conectado a Vcc)
      OFF (= 0) LED está apagado (a tierra)
      CAMBIO (= 2) se cambia el estado del LED
   
   En: (char) sbLedID
      (char) sbLedChange
   Nix falló
   ================================================== ===============*/
 

 
/*void digitalWrite(char sbLedID, char sbLedChange)
{
if(sbLedID==LED1) TCCR0A &= ~_BV(COM0A1); else TCCR0A &= ~_BV(COM0B1);
   switch (sbLedChange)
   {
      case ON:   // Einschalten
         PORTB |= _BV(sbLedID);
         break;
      case OFF:   // Ausschalten
         PORTB &= ~_BV(sbLedID);
         break;
      case TOGGLE:// Zustand tauschen
         PORTB ^= _BV(sbLedID);
         break;
      default:
         break;
   }
}*/
 
/* ================================================ =================
   La inicialización del temporizador de vigilancia y establecer el 
     intervalo 
     se establece en el modo de interrupción
   
   En: (unsidned char) bTimeConst
   Nix falló
   ================================================== =============== */
void setWD(unsigned char bTimeConst)
{
   // Desactivar interrupción
   cli();
   // Establecer hora
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
   // Interruptor de Interrupción
   sei();
}
 
/* ================================================ =================
   Rutina de servicio de interrupción para el procesamiento de las alarmas cíclicas
        => En realidad el programa principal
   ================================================== =============== */






void WdtInterrupt(void)
{
   unsigned int brilloActual;
   int incrementoBrillo;

switch(estado)
   {
         case DIA:
            digitalWrite(LDR_POWER,HIGH); // Espera a algo; sólo entonces consultar
            setWD(TEMPORIZADOR_NOCHE); //ok
            estado=DIA_LDR;
            break;

         case ENVIANDO:
            if(doBlink())estado=NOCHE; //Envia
            setWD(TEMPORIZADOR_NOCHE);
            break;

         brilloActual = readADC();
         incrementoBrillo = brilloActual - brilloAnterior;
         brilloAnterior = brilloActual;

         case DIA_LDR:
            if(brilloActual < UMBRAL_NOCHE)  // Umbral noche Si está oscuro
            {// => De a modo nocturno
               
               setWD(TEMPORIZADOR_NOCHE);
               estado=NOCHE;
            }else                     // Si todavía es luz 
            {                        // => Inténtalo de nuevo en 8s
               digitalWrite(LDR_POWER,LOW);
               setWD(TEMPORIZADOR_DIA);
               estado=DIA;
            }
            break;

         case NOCHE:
            if(incrementoBrillo > INC_BRILLO_ENVIAR )   // Si el brillo en el último ciclo
               {               // Se ha elevado => rumblinken algo
                  wBlinkCounter = 0;      // Blinkgenerator initialisieren
                  estado=ENVIANDO;
               }
            else if(brilloActual > UMBRAL_DIA) estado=CAMBIANDO;         
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




  /* unsigned int adcSample;
   int incrementoLuz;
   
   if(!modoNoche && !ldrAlimentada)     // Si es de día y la
   {                          // LDR está desactivada y habilitar LDR
      digitalWrite(LDR_POWER,HIGH); // espera el temporizador de noche y entonces mide
      ldrAlimentada = TRUE;
      setWD(TEMPORIZADOR_NOCHE);
   }
   else
   {
      adcSample = readADC();
      incrementoLuz = adcSample - brilloAnterior;
      brilloAnterior = adcSample;
 
      if(!modoNoche)           // Si es de dia
      {   
         if(adcSample < UMBRAL_NOCHE)  // si hay poca luz hay que pasar a noche
         {// => De a modo nocturno
            modoNoche = TRUE;
            setWD(TEMPORIZADOR_NOCHE);
         }
         else                     // Si hay mucha luz
         {                        // desconecta LDR y espera a la siguiente medida
            digitalWrite(LDR_POWER,LOW);
            ldrAlimentada = FALSE;
            setWD(TEMPORIZADOR_DIA);
         }
      }
      else             // Todo es lo que debe hacer en la noche
      {
         if(incrementoLuz > INC_BRILLO_ENVIAR 
            && !modoParpadeo)  //Si se ha incrementado la luz y no estoy parpadeando
         {               
            wBlinkCounter = 0;      // Me pongo a parpadear
            modoParpadeo = TRUE;      
         }
         
         if(modoParpadeo)         // Si estoy parpadeando
         {                  // llamo al siguiente caracter morse a enviar
            doBlink();
            setWD(TEMPORIZADOR_NOCHE);
         }
         else         // Si el modo intermitente no  activo
         {            
            if(adcSample > UMBRAL_DIA)   //Si ya hay luz es de día pero espero a hacer variar medidas para asegurarme
            { // Brillo supera el valor del tag => desde el modo de día
               contadorUmbralDiaSuperado++;                  // Pero no inmediatamente
               if(contadorUmbralDiaSuperado > UMBRAL_DIA_COUNTER)   
               {                       
                  modoNoche = FALSE; 
                  contadorUmbralDiaSuperado = 0;
                  digitalWrite(LDR_POWER,LOW);
                  ldrAlimentada = FALSE;
                  setWD(TEMPORIZADOR_DIA);
               }
               else
               {
                  setWD(TEMPORIZADOR_NOCHE);
               }
            }
            else            // Si todavía está oscuro  sigue siendo de noche y si hubo medida de luz fue un error, contador a 0 
            {
               contadorUmbralDiaSuperado=0;
               setWD(TEMPORIZADOR_NOCHE);
            }
         }
      }
   }*/
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
   /*if(fBDimmerMode==1)
	{doDimmer();
	return;}*/
   
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
 
 void doDimmer(void)
 {
 unsigned char n;
 cli();
 for(n=1;n<255;n++)
   {
   analogWrite(LED1,n);
   analogWrite(LED2,n);
   _delay_ms(200);
   }
digitalWrite(LED1,LOW);
digitalWrite(LED2,LOW);
//modoParpadeo = FALSE;
 sei();
 }
 
/* =================================================================
    Programa principal
        => En realidad hace casi nada, excepto que el procesador
           inicializar, empezar el organismo de control, y en un
           Introduzca bucle infinito
   ================================================================= */
int main (void)
{
   // Inicializar el chip
   fBDimmerMode=0;//1 modo dimmer 0 modo Blink
   initChip();   // Inicializar el estado
   estado=DIA;
   brilloAnterior = 0;
   //modoParpadeo = FALSE;
   contadorUmbralDiaSuperado = 0;
   digitalWrite(LDR_POWER,LOW);
 //doDimmer();
   wBlinkCounter = 0;
   // Habilitar Watchdog y de hecho para el modo día
    setWD(TEMPORIZADOR_DIA);
 // Bucle infinito que envía repetidamente que el procesador del sueño
    while(1)
   {
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      //sleep_bod_disable(); //no hace falta si esta deshabilitado por soft
      sleep_cpu();
     
   }
        
    return (0);
}