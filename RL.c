/*
* -------------------------------------------------------------------------
* Luz reactiva para cachés de noche con Atmel ATtiny 13V
* Versión 0.4 para RL_V0.3 circuito
* -------------------------------------------------------------------------
*
*
* Se originó en la sub-electrónica foro sobre http://www.geoclub.de
* (Http://www.geoclub.de/ftopic5753.html)
*
Diseñado * Implementación de la luz reactivo con LDR como de windi
* En C, el consumo de energía se minimiza tanto como sea posible.
*
* Thomas Stief <geocaching@mail.thomas-stief.de>
*/
 
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
 
typedef uint8_t BYTE;
typedef int8_t SBYTE;
typedef uint16_t WORD;
typedef int16_t SWORD;
 
typedef int8_t BOOL;
 
#define TRUE (0==0)
#define FALSE (0!=0)
#define LED1 PORTB0
#define LED2 PORTB1
#define ADC_VERSORGUNG PORTB3 // ENCENDER Y APAGAR LA ALIMENTACION DEL LED
 
#define ON 1
#define OFF 0
#define TOGGLE 2
 
void doBlink(void);
void controlDO(SBYTE , SBYTE );
 
/* =================================================================
   Las variables globales del Estado
   ================================================================= */
 
WORD wLetzteHelligkeit;   // Cache el último valor de brillo
BOOL fNachtMode;        //== TRUE si el modo nocturno está activo
BOOL fBlinkMode;      // == TRUE si el LED parpadea
BOOL fPrepareTag;     // == TRUE si en el modo de día, la oferta de la LDR 
                  // ¿Está activado y se puede hacer la consulta
WORD wTagCounter;      // Cuente cuántas veces "días" mide
 
WORD wBlinkCounter;      // Contador para el generador de flash
BOOL fBDimmerMode;
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
   // Excepto por LED1, LED2, el ADC_VERSORGUNG // xx001011
   DDRB = 0x0b;                                 // 00001011
     // Resistencias pull-up para todo, excepto para el
   // salidas digitales y la ADC2 entrada analógica 
   // Habilitar (PB4) // xx100100
   // Save => actual objetivo
   PORTB = 0x24;    //valor de salida                              // 00100100
   // Las entradas digitales no utilizadas deaktivien
   // => Ahorra energía
   // No se necesitan entradas digitales // xx111111
   DIDR0 = 0x3F;                                 // 00111111
   TCCR0A |=  _BV(WGM01) | _BV(WGM00);//modo fast pwm
   TCCR0B |=  _BV(CS01) |_BV(CS00);//modo normal clock /8 por systen clock preescaler y luego preescaler de timer
						//9,6Mhz/8 = 1,2Mhz preescales+r 1024 = 1,1Khz
}
void analogWrite(BYTE pin, BYTE value)
{
  //pinMode(3, OUTPUT);ya están
  //pinMode(11, OUTPUT);
  if(pin==LED1)
  {
	  if(value==0) controlDO(PORTB0, OFF);
	  else  {
	  TCCR0A |= _BV(COM0A1); //activa que el pin vaya a OCR0A ponerlo a 0 para operacion normal
	  OCR0A = value;
	  }
  }
  else if(pin==LED2)
  {
	if(value==0) controlDO(PORTB1, OFF);
	  else{
	  TCCR0A |=  _BV(COM0B1);//activa que el pin vaya a OCR0B ponerlo a 0 para operacion normal
	  OCR0B = value;
	  }
  }
  
}
 
/* ================================================ =================
   La lectura de la entrada analógica
     la función se inicia la adquisición de la señal analógica, y
    devuelve el valor actual
   
   En: nix
   Salida: (PALABRA) señal analógica actual
   ================================================== =============== */
WORD readADC(void)
{
   WORD wValue;
   
   // Preparar el tendido sueño del procesador
   // -> Procesador en el modo de envío de reducción de ruido ADC: 
   // Modo de sueño más profundo, lo que permite un despertar por ADC
   set_sleep_mode(SLEEP_MODE_ADC);
   sleep_enable();
   sei();
 
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
   cli();
 
   // Leer el valor (los dos bits más bajos de ADCH 
   // (8 bit desplazado a la izquierda) + ADCL
   wValue = ((WORD)ADCL + (((WORD)(ADCH))<<8)) & 0x03ff;
   
   //Off  ADC
   ADCSRA = 0x00;               // 00000000
   
   return wValue;
}
 
/* ================================================ =================
   Rutina de servicio de interrupción para la finalización de la conversión AD
      que realmente no hace nada
   ================================================== =============== */
ISR(SIG_ADC)
{
}
 
/* ================================================ =================
   Los valores de salida a las salidas digitales
    estados posibles:
      ON (= 1) LED está encendido (conectado a Vcc)
      OFF (= 0) LED está apagado (a tierra)
      CAMBIO (= 2) se cambia el estado del LED
   
   En: (SBYTE) sbLedID
      (SBYTE) sbLedChange
   Nix falló
   ================================================== ===============*/
 

 
void controlDO(SBYTE sbLedID, SBYTE sbLedChange)
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
}
 
/* ================================================ =================
   La inicialización del temporizador de vigilancia y establecer el 
     Auswachintervalls
     se establece en el modo de interrupción
   
   En: (BYTE) bTimeConst
   Nix falló
   ================================================== =============== */
void setWD(BYTE bTimeConst)
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
#define SCHWELLE_NACHT       (WORD)500    //umbrales noche
#define SCHWELLE_TAG         (WORD)530	//dia
#define SCHWELLE_TAG_COUNTER (WORD)64   //contador dia
#define SCHWELLE_BLINKMODE   (SWORD)1   //umbral parpadeo
#define WARTEZEIT_NACHT      WDTO_120MS //temporizador nocturno
#define WARTEZEIT_TAG        WDTO_8S  //temporizador diurno
 
ISR(SIG_WATCHDOG_TIMEOUT)
{
   WORD wHelligkeit;
   SWORD swDeltaHelligkeit;
   
   if(!fNachtMode && !fPrepareTag)     // Si es de día y el suministro de
   {                          // LDR está desactivada y habilitar
      controlDO(ADC_VERSORGUNG,ON); // Espera a algo; sólo entonces consultar
      fPrepareTag = TRUE;
      setWD(WARTEZEIT_NACHT);
   }
   else
   {
      wHelligkeit = readADC();
      swDeltaHelligkeit = wHelligkeit - wLetzteHelligkeit;
      wLetzteHelligkeit = wHelligkeit;
 
      if(!fNachtMode)           // Todo lo que hay que hacer en el día
      {   // -> Probar si ahora es oscuro y el modo nocturno 
         // Se puede activar
         if(wHelligkeit < SCHWELLE_NACHT)  // Umbral noche Si está oscuro
         {// => De a modo nocturno
            fNachtMode = TRUE;
            setWD(WARTEZEIT_NACHT);
         }
         else                     // Si todavía es luz 
         {                        // => Inténtalo de nuevo en 8s
            controlDO(ADC_VERSORGUNG,OFF);
            fPrepareTag = FALSE;
            setWD(WARTEZEIT_TAG);
         }
      }
      else             // Todo es lo que debe hacer en la noche
      {
         if(swDeltaHelligkeit > SCHWELLE_BLINKMODE //umbral de parpadeo
            && !fBlinkMode)   // Si el brillo en el último ciclo
         {               // Se ha elevado => rumblinken algo
            wBlinkCounter = 0;      // Blinkgenerator initialisieren
            fBlinkMode = TRUE;      // und Blink-Flag setzen
         }
         
         if(fBlinkMode)         // Inicializa el generador de flash
         {                  // Set y Bandera parpadeo
            doBlink();
            setWD(WARTEZEIT_NACHT);
         }
         else         // Si el modo intermitente activo
         {            // => Probar si ha quedado claro
            if(wHelligkeit > SCHWELLE_TAG)   
            { // Brillo supera el valor del tag => desde el modo de día
               wTagCounter++;                  // Pero no inmediatamente
               if(wTagCounter > SCHWELLE_TAG_COUNTER)   
               {                       // Pero sólo si hay más de 
                  fNachtMode = FALSE; // SCHWELLE_TAG_COUNTER era tan x
                  wTagCounter = 0;
                  controlDO(ADC_VERSORGUNG,OFF);
                  fPrepareTag = FALSE;
                  setWD(WARTEZEIT_TAG);
               }
               else
               {
                  setWD(WARTEZEIT_NACHT);
               }
            }
            else            // Si todavía está oscuro   
            {// => Poner el contador a cero
               wTagCounter=0;
               setWD(WARTEZEIT_NACHT);
            }
         }
      }
   }
}
 
/* =================================================================
   Parpadeo rutina - es llamado por el ISR del perro guardián
   
   In:  nix
   Out: nix
   ================================================================= */
// Morsecode für "N 50 12 345"
const BYTE bSequenz1[] PROGMEM = {    0xe8, 0x0a, 0xa8, 0xee, 
                           0xee, 0xe0, 0x2e, 0xee, 
                           0xe2, 0xbb, 0xb8, 0x0a, 
                           0xbb, 0x8a, 0xae, 0x2a, 
                           0xa0, 0x00, 0x00, 0x00 };
// Morsecode für "E 008 54 321"
const BYTE bSequenz2[] PROGMEM = {    0x80, 0xee, 0xee, 0xe3, 
                           0xbb, 0xbb, 0x8e, 0xee, 
                           0xa0, 0x2a, 0xa2, 0xab, 
                           0x80, 0xab, 0xb8, 0xae, 
                           0xee, 0x2e, 0xee, 0xe0 };
void doBlink(void)
{
   BYTE bByte,bBit;
   if(fBDimmerMode==1)
	{doDimmer();
	return;}
   
   if(wBlinkCounter < 128)
   {
      // Byte leído de la memoria flash (todo, desde el bit 3 del tapajuntas contador
      // Determina la posición del byte
      bByte = pgm_read_byte(&bSequenz1[(BYTE)(wBlinkCounter>>3)]);
       // Bit 0: 2 determinar el bit en el byte de datos, pero a la inversa
      // Orden
      bBit = 7 - (BYTE)(wBlinkCounter&7);
      // Comprobar si el bit es entonces ......
      if((bByte&(1<<bBit)) != 0)
         controlDO(LED1,ON); // ... LED an
      else               // sonst ...
         controlDO(LED1,OFF);// ... LED aus
 
      // Todo el asunto para el otro LED
      bByte = pgm_read_byte(&bSequenz2[(BYTE)(wBlinkCounter>>3)]);
      if((bByte&(1<<bBit)) != 0)
         controlDO(LED2,ON);
      else
         controlDO(LED2,OFF);
   }
   else   // Si la secuencia es más: Switch LEDs
   {
      controlDO(LED1,OFF);
      controlDO(LED2,OFF);
   }
   
   if(wBlinkCounter++ > 130) // Dos ciclos más, entonces el 
      fBlinkMode = FALSE;     // Secuencia a su fin y la bandera se borra
}
 
 void doDimmer(void)
 {
 BYTE n;
 cli();
 for(n=1;n<255;n++)
 {
 analogWrite(LED1,n);
 //analogWrite(LED2,n);
 _delay_ms(200);
 }
   controlDO(LED1,OFF);
   controlDO(LED2,OFF);
   fBlinkMode = FALSE;
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
   initChip();
   // Inicializar el estado
   fNachtMode = FALSE;
   wLetzteHelligkeit = 0;
   fBlinkMode = FALSE;
   wTagCounter = 0;
   fPrepareTag = FALSE;
   controlDO(ADC_VERSORGUNG,OFF);
 doDimmer();
   wBlinkCounter = 0;
   // Habilitar Watchdog y de hecho para el modo día
    setWD(WARTEZEIT_TAG);
 // Bucle infinito que envía repetidamente que el procesador del sueño
    while(1)
   {
      if(~fBlinkMode){set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      sleep_cpu();}
   }
        
    return (0);
}