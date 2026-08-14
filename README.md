# CarroESP32 — Cómo conectarse y hacer pruebas

Carro con ESP32 controlado por Bluetooth con las teclas
`w a s d f`.

# Lo primero que hay que saber

El nombre del dispositivo es CarroESP32.

Solo se conecta un dispositivo a la vez. Si alguien del equipo tiene el carro
conectado, nadie más puede entrar hasta que cierre su sesión.

Necesitaras desgargar la aplicacion **PuTTY** para mandar instrucciones de manera inalambrica
---


## Conexion con Laptop con Windows (la unica que probe desconosco funcionamiento con linux)

1. `Configuración → Bluetooth y dispositivos` y empareja con **CarroESP32**.
2. Entra en `Más opciones de Bluetooth → Puertos COM`. Verás **dos** puertos:
   uno *Entrante* y uno **Saliente**. Anota el número del **Saliente**.
3. Abre **PuTTY**: modo *Serial*, escribe ese COM, velocidad `115200`, y *Open*.

> **No uses el Monitor Serie del IDE de Arduino para el puerto Bluetooth.**
> Intenta identificar una placa en ese puerto, no la encuentra y falla con
> "el puerto seleccionado no existe".

PuTTY manda cada tecla al instante, sin necesidad de pulsar Enter.

---


## Controles

| Tecla | Acción |
|---|---|
| `w` | Adelante |
| `s` | Atrás |
| `a` | Izquierda |
| `d` | Derecha |
| `f` | **Parar** |

> ### Importante para quien haga pruebas
> **El carro no se detiene solo.** Al mandar `w` arranca y sigue avanzando
> indefinidamente hasta que alguien mande `f`. No es como mantener pulsado un
> botón. Ten siempre la `f` lista antes de mandar cualquier otro comando.

El carro **sí se detiene solo** si se pierde la conexión Bluetooth (te alejas,
se cierra la app, se apaga el teléfono). Eso está cubierto por el firmware.


## Encender el carro

1. **Desconecta el cable USB.** Nunca USB y batería a la vez: comparten el mismo
   riel interno y se retroalimenta el puerto de la laptop.
2. Conecta la bateria externa.
3. Comprueba que el LED azul parpadea.
4. Conéctate desde la app.

**El código ya está grabado en la placa de forma permanente.** No hay que volver
a subirlo cada vez que se enciende: se sube una vez por USB y se queda en la
memoria flash para siempre.

### Antes de bajarlo al piso

Prueba las cinco teclas **con las ruedas en el aire**, sin que toquen el suelo.
Comprueba que las cuatro direcciones son las correctas y que `f` detiene de verdad.

---

## Si no funciona

| Síntoma | Qué hacer |
|---|---|
| Empareja pero no conecta | Normal. Falta pulsar el botón de conectar dentro de la app |
| No aparece `CarroESP32` al buscar | El carro no está encendido, o alguien más ya está conectado |
| Desde iPhone no lo veo | iOS no soporta este tipo de Bluetooth. Usa Android o laptop |
| "El puerto COM no existe" | Estás abriendo el COM Bluetooth desde el IDE. Usa PuTTY |
| Conecta pero no responde a las teclas | Revisa que la app no esté añadiendo caracteres extra. El carro espera letras sueltas |
| Los motores no se mueven pero sí responde con texto | Falta alimentación de motores (pata 8 del L293D) o el GND común |
| Un motor gira al revés | Intercambia sus dos cables, o los pines `IN3`/`IN4` en el código |
| Se reinicia al arrancar los motores | Caída de tensión: revisa los condensadores y el buck |

**Emparejamiento corrupto:** si Android empareja pero la app nunca logra
conectar, elimina el dispositivo (`Olvidar`), reinicia la ESP32 con el botón
**EN**, y empareja de nuevo. Pasa a menudo cuando se reprograma la placa varias
veces.

---


## Referencia de conexiones

Solo para quien tenga que reparar o rehacer cableado.

| Función | GPIO ESP32 | Pata L293D |
|---|---|---|
| ENA — habilitador motor A | 13 | 1 (EN1) |
| IN1 — motor A horario | 27 | 2 (1A) |
| IN2 — motor A antihorario | 26 | 7 (2A) |
| ENB — habilitador motor B | 32 | 9 (EN2) |
| IN3 — motor B antihorario | 25 | 10 (3A) |
| IN4 — motor B horario | 33 | 15 (4A) |
| LED de estado | 2 | — (integrado) |

Salidas a motores: patas **3 y 6** al motor A, patas **11 y 14** al motor B.

**Alimentación:** la batería va **directa a la pata 8** (Vcc2, motores) — el
L293D pierde ~2 V, así que a los motores les llegan ~5.5 V, equivalente a las
4 pilas de 1.5 V originales del kit. El buck de 5 V alimenta **solo** el pin VIN
del ESP32 y la pata 16 (Vcc1). GND común entre todo.

> No alimentes la pata 8 desde el buck: los motores recibirían ~3.2 V y apenas
> arrancarían.

**Nunca conectes 5 V al pin 3V3.** Es la salida del regulador interno; meterle
5 V mata el ESP32 al instante y sin ninguna señal previa.

**Pines reservados:** GPIO 34, 35, 36 y 39 quedan libres a propósito para
sensores analógicos futuros.
