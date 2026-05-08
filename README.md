# Vastago 0.04W LittleFS ADS1115 + SHT40

Sistema de monitoreo experimental para lectura de fuerza/presion mediante ADS1115 y lectura ambiental mediante sensor SHT40/SHT4x. El equipo publica una interfaz web desde LittleFS, transmite datos en vivo por WebSocket, guarda un registro continuo en tarjeta SD y permite descargar pruebas/rangos desde la web.

## Funcionamiento general

El firmware corre en ESP32-S3 y usa:

- `ADS1115` por I2C para leer la senal analogica del sensor principal.
- `Adafruit_SHT4x` para leer temperatura ambiente y humedad relativa desde un SHT40/SHT4x.
- `SD` por SPI para guardar un archivo continuo `/datalog.csv`.
- `LittleFS` para servir la interfaz web ubicada en la carpeta `data`.
- `WebSocket` en el puerto `81` para enviar mediciones en tiempo real al navegador.
- Pantalla OLED SH1106 128x32 para mostrar IP, temperatura/humedad y lectura ADC.

Al iniciar, el equipo intenta conectarse a la red WiFi configurada en el sketch. Si no logra conectarse, levanta un punto de acceso local:

- SSID: `Vastago_Monitor`
- Password: `12345678`

La pantalla muestra la IP disponible para abrir la interfaz desde un navegador.

## Datos medidos

El sistema calcula y transmite:

- ADC crudo del ADS1115.
- Voltaje del canal analogico.
- Peso en gramos.
- Peso en newtons.
- Presion en mmHg.
- Temperatura en grados Celsius.
- Humedad relativa en `%RH`.
- Estado de deteccion del SHT40.

La temperatura y humedad se leen cada 1 segundo. Los datos principales se envian a la web con mayor frecuencia para mantener las graficas fluidas.

## Registro en tarjeta SD

Si la SD esta disponible, el firmware crea o agrega datos al archivo:

```text
/datalog.csv
```

El encabezado esperado para archivos nuevos es:

```csv
ADC,Voltaje,Peso_Newtons,Peso_Gramos,Presion_mmHg,Temperatura_C,Humedad_RH
```

Nota: si ya existe un `datalog.csv` antiguo sin las columnas de temperatura y humedad, el firmware seguira agregando filas con las columnas nuevas, pero no reescribira el encabezado anterior. Para un archivo limpio, borrar o renombrar el CSV viejo antes de iniciar una nueva sesion.

## Interfaz web

La carpeta `data` contiene la web servida desde LittleFS:

- `index.html`: tablero principal, controles de captura y logica de exportacion.
- `chart.js`: libreria local para graficos.
- `Sortable.min.js`: libreria incluida en el sistema de archivos.

La web muestra:

- Grafico de ADC y voltaje.
- Grafico de peso y presion.
- Grafico de temperatura y humedad.
- Tarjetas con valores instantaneos.
- Estado de conexion WebSocket.
- Estado del SHT40.
- Controles para comenzar, detener y guardar un rango de prueba.

## Pruebas generadas desde la web

Desde la interfaz se puede capturar solo el tramo de interes:

1. Presionar `Comenzar`.
2. Ejecutar la prueba.
3. Presionar `Detener`.
4. Presionar `Guardar rango`.

La web descarga archivos locales en el navegador:

- `experimento_rango_vX.csv`
- `sensor_rango_vX.png`
- `peso_presion_rango_vX.png`
- `temperatura_humedad_rango_vX.png`

El CSV de la prueba incluye:

```csv
sampleIndex,timestampIso,elapsedMs,rawI2C,voltage,weightGrams,weightNewtons,pressureMmHg,temperatureC,humidityRH
```

Estos archivos se generan en el navegador; no se escriben automaticamente en la SD.

## TARA

La tara se puede activar de dos formas:

- Desde la web, usando el boton `TARA`.
- Desde el puerto serial, enviando `t` o `T`.

La tara usa la lectura instantanea actual del ADC como referencia para ajustar el calculo de peso.

## Pantalla OLED

La pantalla muestra tres lineas:

- IP del equipo o IP del modo AP.
- Temperatura y humedad del SHT40.
- ADC crudo y voltaje.

Si el SHT40 no se detecta, se muestra un mensaje indicando que el sensor no esta disponible.

## Librerias requeridas

Instalar las siguientes librerias en Arduino IDE:

- `Adafruit ADS1X15`
- `Adafruit SHT4x Library`
- `Adafruit BusIO`
- `Adafruit Unified Sensor`
- `U8g2`
- `WebSockets`

Tambien se usan librerias del core ESP32:

- `WiFi`
- `WebServer`
- `LittleFS`
- `SPI`
- `SD`
- `Wire`

## Actualizacion de la web en LittleFS

Cuando se modifica la carpeta `data`, se debe volver a generar y subir la imagen LittleFS al ESP32. En este proyecto existe `mklittlefs.bin`, generado desde la carpeta `data`.

El tamano usado para la particion LittleFS/SPIFS del proyecto es `0x160000` bytes, equivalente a `1441792` bytes.

## Disclaimer de responsabilidad

Este proyecto se entrega como herramienta experimental y educativa. No esta certificado como instrumento medico, industrial, metrologico ni de seguridad. Las lecturas de fuerza, presion, temperatura y humedad dependen de la calibracion, montaje mecanico, alimentacion electrica, sensores usados, condiciones ambientales y procesamiento del firmware.

El usuario es responsable de:

- Verificar la calibracion antes de cada uso critico.
- Confirmar que el hardware este correctamente conectado y protegido.
- Validar los datos con instrumentos de referencia cuando se requiera precision.
- Evaluar riesgos electricos, mecanicos, termicos o de seguridad asociados al montaje.
- No usar el sistema como unico criterio para decisiones medicas, clinicas, legales, industriales o de seguridad.

Los autores o modificadores del proyecto no se hacen responsables por danos directos, indirectos, incidentales, perdidas de datos, errores de medicion, lesiones, fallas de equipos o cualquier consecuencia derivada del uso, modificacion, montaje o interpretacion de los resultados de este sistema.

## Licencia

Este proyecto se distribuye bajo licencia Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.

CC BY-NC-SA 4.0

Usted puede:

- Compartir: copiar y redistribuir el material en cualquier medio o formato.
- Adaptar: remezclar, transformar y construir a partir del material.

Bajo las siguientes condiciones:

- Atribucion: debe dar credito adecuado al autor original, indicar si se hicieron cambios y mantener una referencia a esta licencia.
- No comercial: no puede usar el material con fines comerciales sin autorizacion expresa.
- Compartir igual: si modifica o crea a partir de este material, debe distribuir sus contribuciones bajo la misma licencia.

Texto legal de referencia: https://creativecommons.org/licenses/by-nc-sa/4.0/

