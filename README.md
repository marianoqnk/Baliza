# Baliza
Baliza para Geocaching. Cuando la iluminas de noche te devuelve un código con las coordenadas finales.
Hay que grabar el ATTiny13 con una mapa de fusibles determinado, para que arranque con el oscilador de 128Khz. Para grabar el mapa de fusible se utiliza Avrdude , como resulta dificil de configurar he buscado un interface gráfico que se llama AVRDUDESS pero me encontrado con un mensaje que dice que no encuentra el PID y VID del grabador. Después de mucho probar he visto que el problema es con el avrdude que se descarga, he copiado al directorio uno que tenía antiguo y ya ha funcionado.
Pendiente de ver porque en el arranque tarda tanto en empezar a andar y hacer medidas del consumo para estimar la duración de la pila y ver que se puede optimizar.
Consumo estimado:
    *Modo día con WDT activado 5uA
    *Cada conversión 250uA * TC=ciclos*CLK son 13 si ADC estaba habilitado o 24 si no lo estaba CLK= SYSTEM/PREESCALER
    *Los led ...
    *EL WD funciona con el reloj de 128Khz más un preescaler hasta 1024

