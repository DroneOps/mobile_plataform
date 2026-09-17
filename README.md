# CarroESP32 — Cómo conectarse y hacer pruebas

Carro con ESP32 controlado por Bluetooth con las teclas
`w a s d f`.

# Lo primero que hay que saber

El nombre del dispositivo es CarroESP32.

Solo se conecta un dispositivo a la vez. Si alguien del equipo tiene el carro
conectado, nadie más puede entrar hasta que cierre su sesión.

Necesitas una terminal serie para mandar las teclas de forma inalámbrica:
**PuTTY** en Windows, **picocom** (o PuTTY) en Linux.

---


## Conexión desde Windows

1. `Configuración → Bluetooth y dispositivos` y empareja con **CarroESP32**.
2. Entra en `Más opciones de Bluetooth → Puertos COM`. Verás **dos** puertos:
   uno *Entrante* y uno **Saliente**. Anota el número del **Saliente**.
3. Abre **PuTTY**: modo *Serial*, escribe ese COM, velocidad `115200`, y *Open*.

> **No uses el Monitor Serie del IDE de Arduino para el puerto Bluetooth.**
> Intenta identificar una placa en ese puerto, no la encuentra y falla con
> "el puerto seleccionado no existe".

PuTTY manda cada tecla al instante, sin necesidad de pulsar Enter.

---

## Conexión desde Linux

El carro usa Bluetooth Classic (SPP). En Linux se hace lo mismo que en Windows:
emparejar, crear un puerto serie sobre Bluetooth (`/dev/rfcomm0` en lugar de un
`COM`) y abrir una terminal serie a `115200`.

### 1. Instalar la terminal serie (una sola vez)

```bash
sudo apt install picocom
```

Tu usuario tiene que estar en el grupo `dialout` (`groups` para comprobarlo,
`sudo usermod -aG dialout $USER` y volver a iniciar sesión si no está).

### 2. Emparejar el carro (una sola vez)

Enciende el carro (LED azul parpadeando) y abre `bluetoothctl`:

```bash
bluetoothctl
```

Dentro:

```
power on
agent on
default-agent
scan on
```

Espera a la línea `[NEW] Device XX:XX:XX:XX:XX:XX CarroESP32`, anota la MAC y:

```
scan off
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
quit
```

### 3. Crear el puerto serie (cada vez que lo uses)

```bash
sudo rfcomm bind 0 XX:XX:XX:XX:XX:XX
```

Esto crea `/dev/rfcomm0`, el equivalente al COM *Saliente* de Windows.

### 4. Abrir la terminal

```bash
picocom -b 115200 /dev/rfcomm0
```

Igual que PuTTY: cada tecla se envía al instante, sin Enter. Para salir:
`Ctrl+A` y luego `Ctrl+X`. Al cerrar la conexión el carro se detiene solo.

Cuando termines, libera el puerto:

```bash
sudo rfcomm release 0
```

> **Tampoco uses el Monitor Serie del IDE de Arduino con `/dev/rfcomm0`**, por
> la misma razón que en Windows.

| Problema en Linux | Qué hacer |
|---|---|
| `pair` falla o se queda colgado | Emparejamiento corrupto: `remove XX:XX...` en `bluetoothctl`, reinicia la ESP32 con **EN** y empareja de nuevo |
| `rfcomm bind`: "Address already in use" | `sudo rfcomm release 0` y vuelve a hacer el `bind` |
| `picocom`: "Permission denied" | Falta el grupo `dialout` (ver paso 1) |
| `picocom` abre pero no aparece nada | Normal si el carro está detenido: no hay telemetría. Manda `t` para ver la configuración |

---


## Arrancar y que vaya derecho

El carro lleva un giroscopio (IMU) y un PID que reparte potencia entre las
ruedas para mantener el rumbo en las rectas. **El PID arranca apagado** y se
pierde al reiniciar, así que cada vez que enciendas el carro:

1. Conéctate y deja el carro **quieto sobre una superficie plana** un par de
   segundos: al arrancar calibra el giroscopio.
2. Manda `0` → debe responder `PID activado`.
3. Manda `i` → debe responder `Sentido de correccion: INVERTIDO`.
   Con el cableado actual el PID corrige hacia el lado equivocado si no se
   invierte: en vez de enderezar el carro, lo desvía más.
4. Manda `w` y comprueba que va recto. Si se desvía **cada vez más**, el sentido
   está al revés: manda `i` otra vez.

Si el carro se desvía siempre hacia el mismo lado aunque el PID esté bien, un
motor rinde menos que el otro. Mira [Ajustar los motores](#ajustar-los-motores).

---

## Controles

### Movimiento

| Tecla | Acción |
|---|---|
| `w` | Adelante (fija el rumbo actual como objetivo) |
| `s` | Atrás |
| `a` | Giro a la izquierda sobre el eje |
| `d` | Giro a la derecha sobre el eje |
| `f` | **Parar** |

> ### Importante para quien haga pruebas
> **El carro no se detiene solo.** Al mandar `w` arranca y sigue avanzando
> indefinidamente hasta que alguien mande `f`. No es como mantener pulsado un
> botón. Ten siempre la `f` lista antes de mandar cualquier otro comando.

El carro **sí se detiene solo** si se pierde la conexión Bluetooth (te alejas,
se cierra la app, se apaga el teléfono). Eso está cubierto por el firmware.

### PID y giroscopio

| Tecla | Acción |
|---|---|
| `0` | Activar / desactivar el PID |
| `i` | Invertir el sentido de la corrección del PID |
| `r` | Recalibrar el giroscopio (para el carro; déjalo quieto mientras lo hace) |
| `1` / `2` | Kp −0.5 / +0.5 |
| `3` / `4` | Ki −0.1 / +0.1 |
| `5` / `6` | Kd −0.2 / +0.2 |
| `t` | Ver la configuración actual |

Para ajustar el PID empieza solo con Kp, luego Kd y Ki al final.

### Velocidad

| Tecla | Acción |
|---|---|
| `+` / `-` | Velocidad del modo actual ±10 PWM: en recta ajusta la base, girando ajusta la de giro |
| `v` / `c` | Velocidad de giro +10 / −10 PWM (siempre) |

### Pruebas y ajuste de motores

| Tecla | Acción |
|---|---|
| `p` | Prueba automática de potencia: compara los dos motores (carro **en el suelo**) |
| `m` / `n` | Mover **solo** el motor A / el motor B hacia adelante |
| `g` / `h` | Mover **solo** el motor A / el motor B hacia atrás |
| `j` / `k` | Invertir el sentido del motor A / del motor B (sin tocar cables) |
| `7` / `8` | Compensación del motor A −5 / +5 PWM |
| `9` / `o` | Compensación del motor B −5 / +5 PWM |

Motor **A** = izquierdo, motor **B** = derecho. Todo lo que cambies con estas
teclas se pierde al reiniciar; cuando encuentres el valor bueno, escríbelo en
el `.ino` (`invertirA`, `invertirB`, `compensacionA`, `compensacionB`,
`velocidadBase`, `velocidadGiro`, `Kp`, `Ki`, `Kd`) y vuelve a subirlo por USB.

### Telemetría

Mientras el carro se mueve imprime cada 200 ms:

```
rumbo=+0.3  giro=-0.1  corr=+0  pwmA=200  pwmB=200
```

- `rumbo`: grados acumulados desde que empezó la recta (objetivo 0).
- `giro`: velocidad angular actual en grados/s.
- `corr`: cuánto PWM está moviendo el PID de una rueda a la otra.
- `pwmA` / `pwmB`: PWM que realmente se escribió en cada motor. Si son distintos
  es el código (PID o compensación); si son iguales y aun así un motor rinde
  menos, es hardware.

---

## Ajustar los motores

Si con el PID apagado el carro se va siempre hacia el mismo lado, un motor es
más flojo que el otro.

1. **Confirmar que les llega lo mismo.** Con el carro dado vuelta y las llantas
   en el aire, mide con el multímetro el voltaje en los cables de cada motor
   mandando `m` (solo A) y luego `n` (solo B). Antes comprueba con `t` que
   `compA=0 compB=0`. Si el voltaje es distinto, el problema está antes del
   motor: canal del L293D, soldadura o cable.
2. **Comparar rendimiento.** Si el voltaje es igual, manda `p` con el carro en
   el suelo. Mide con el giroscopio cuánto empuja cada motor al mismo PWM y te
   dice el porcentaje de diferencia y cuál es el flojo.
3. **Corregir.**
   - Diferencia < 10 %: normal, el PID lo compensa.
   - 10–25 %: súmale compensación al motor flojo (`8` para A, `o` para B) de 5
     en 5 hasta que las dos llantas giren parecido, y guarda el valor en el
     `.ino`.
   - > 25 %: con el carro apagado gira cada llanta con la mano. Si una roza o
     hace ruido, revisa la caja de engranes; si giran igual, cambia el motor.

---

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
