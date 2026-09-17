#!/usr/bin/env python3
"""Ida y vuelta automatica del CarroESP32.

Deja el carro con la configuracion de CONFIGURACION_CARRO, activa el PID con
la correccion invertida, y repite en bucle: avanza unos segundos, para,
retrocede otros tantos, para. Pensado para medir si el carro vuelve al punto
de partida en linea recta.

Uso:
    python3 ida_y_vuelta.py                # 3 s adelante, 3 s atras, sin parar
    python3 ida_y_vuelta.py 5              # 5 s adelante, 5 s atras
    python3 ida_y_vuelta.py 5 2            # 5 s adelante, 2 s atras
    python3 ida_y_vuelta.py --ciclos 4     # solo 4 idas y vueltas
    python3 ida_y_vuelta.py --puerto /dev/rfcomm1

Antes de correrlo el puerto tiene que existir (ver README, seccion Linux):
    sudo rfcomm bind 0 XX:XX:XX:XX:XX:XX

Ctrl+C en cualquier momento manda 'f' y para el carro.
"""

import argparse
import re
import sys
import time

import serial

# ----------------------------------------------------------- CONFIGURACION
# Cambia aqui los valores por defecto. Los tiempos tambien se pueden pasar
# por linea de comandos sin tocar el archivo.

SEGUNDOS_ADELANTE = 3.0
SEGUNDOS_ATRAS = 3.0
PAUSA_ENTRE_TRAMOS = 1.0     # Segundos parado entre ida y vuelta (y entre ciclos)
CICLOS = 0                   # Idas y vueltas a hacer. 0 = sin parar hasta Ctrl+C
PUERTO = "/dev/rfcomm0"
BAUDIOS = 115200
ESPERA_CALIBRACION = 2.0     # Segundos quieto al conectar (calibra el giroscopio)

# Configuracion con la que se hace el movimiento. El script lee la del carro
# (tecla t) y pulsa las teclas de ajuste las veces necesarias hasta igualarla.
# Son los mismos nombres que imprime el carro con la tecla t.
CONFIGURACION_CARRO = {
    "Kp":    4.0,
    "Ki":    0.0,
    "Kd":    0.8,
    "base":  100,     # PWM en recta
    "giro":  220,     # PWM al girar
    "compA": 0,
    "compB": 0,
    "invA":  False,   # invA=no
    "invB":  True,    # invB=SI
}
PID_ACTIVADO = True         # Tecla 0
CORRECCION_INVERTIDA = True  # Tecla i  (sentido=-1 en la tecla t)

# --------------------------------------------------------------------------

# Para cada campo numerico: (tecla para bajar, tecla para subir, paso)
TECLAS_AJUSTE = {
    "Kp":    ("1", "2", 0.5),
    "Ki":    ("3", "4", 0.1),
    "Kd":    ("5", "6", 0.2),
    "base":  ("-", "+", 10),   # Solo ajusta la base si el carro NO esta girando
    "giro":  ("c", "v", 10),
    "compA": ("7", "8", 5),
    "compB": ("9", "o", 5),
}
TECLAS_INVERTIR = {"invA": "j", "invB": "k"}


def leer_respuesta(puerto, segundos=0.6, mostrar=True):
    """Junta todo lo que el carro conteste durante unos milisegundos."""
    fin = time.time() + segundos
    texto = ""
    while time.time() < fin:
        pendiente = puerto.in_waiting
        if pendiente:
            texto += puerto.read(pendiente).decode("utf-8", errors="replace")
        else:
            time.sleep(0.02)
    if mostrar:
        for linea in texto.splitlines():
            if linea.strip():
                print("   carro:", linea.strip())
    return texto


def enviar(puerto, tecla, segundos_respuesta=0.6, mostrar=True):
    if mostrar:
        print(f"-> {tecla}")
    puerto.write(tecla.encode())
    puerto.flush()
    return leer_respuesta(puerto, segundos_respuesta, mostrar)


def parsear_configuracion(texto):
    """Convierte la linea de la tecla t en un diccionario.
    'Kp=4.0 ... invA=no invB=SI PID=OFF ...' -> {'Kp': 4.0, ..., 'invB': True}"""
    lineas = [l for l in texto.splitlines() if "Kp=" in l]
    if not lineas:
        return None
    config = {}
    for clave, valor in re.findall(r"(\w+)=(\S+)", lineas[-1]):
        if valor in ("SI", "ON"):
            config[clave] = True
        elif valor in ("no", "OFF"):
            config[clave] = False
        else:
            try:
                config[clave] = float(valor) if "." in valor else int(valor)
            except ValueError:
                config[clave] = valor
    return config


def leer_configuracion(puerto):
    config = parsear_configuracion(enviar(puerto, "t", mostrar=False))
    if config is None:
        print("[!] El carro no respondio a la tecla t. Esta encendido y conectado?")
        sys.exit(1)
    return config


def aplicar_configuracion(puerto):
    """Lee la configuracion del carro y pulsa las teclas necesarias para
    dejarla como CONFIGURACION_CARRO, con el PID y el sentido pedidos."""
    actual = leer_configuracion(puerto)
    print("Configuracion actual :", actual)

    for campo, objetivo in CONFIGURACION_CARRO.items():
        if campo in TECLAS_INVERTIR:
            if actual[campo] != objetivo:
                enviar(puerto, TECLAS_INVERTIR[campo], 0.3)
            continue

        bajar, subir, paso = TECLAS_AJUSTE[campo]
        pulsaciones = round((objetivo - actual[campo]) / paso)
        tecla = subir if pulsaciones > 0 else bajar
        for _ in range(abs(pulsaciones)):
            enviar(puerto, tecla, 0.15, mostrar=False)
        if pulsaciones:
            print(f"-> {tecla} x{abs(pulsaciones)}  ({campo}: {actual[campo]} -> {objetivo})")

    if actual["PID"] != PID_ACTIVADO:
        enviar(puerto, "0", 0.3)
    sentido_deseado = -1 if CORRECCION_INVERTIDA else 1
    if actual["sentido"] != sentido_deseado:
        enviar(puerto, "i", 0.3)

    # Verificar que todo quedo como se pidio
    final = leer_configuracion(puerto)
    print("Configuracion final  :", final)
    esperado = dict(CONFIGURACION_CARRO, PID=PID_ACTIVADO, sentido=sentido_deseado)
    fallos = [f"{k}: quedo {final.get(k)} y se pedia {v}"
              for k, v in esperado.items()
              if k not in final or abs(float(final[k]) - float(v)) > 0.01]
    if fallos:
        print("[!] La configuracion no quedo como se pidio:")
        for f in fallos:
            print("   ", f)
        print("    Abortando sin mover el carro.")
        enviar(puerto, "f", 0.2)
        sys.exit(1)
    if not final.get("IMU", False):
        print("[!] El carro no detecta la IMU: el PID no va a corregir nada. Abortando.")
        sys.exit(1)


def mover(puerto, tecla, segundos, nombre):
    """Manda la tecla de movimiento y muestra la telemetria mientras dura."""
    print(f"\n=== {nombre} durante {segundos:.1f} s ===")
    puerto.write(tecla.encode())
    puerto.flush()
    fin = time.time() + segundos
    while time.time() < fin:
        leer_respuesta(puerto, min(0.25, fin - time.time()))
    enviar(puerto, "f", 0.4)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("adelante", nargs="?", type=float, default=SEGUNDOS_ADELANTE,
                        help=f"segundos hacia adelante (defecto {SEGUNDOS_ADELANTE})")
    parser.add_argument("atras", nargs="?", type=float, default=None,
                        help="segundos hacia atras (defecto: igual que adelante)")
    parser.add_argument("--pausa", type=float, default=PAUSA_ENTRE_TRAMOS,
                        help=f"segundos parado entre tramos (defecto {PAUSA_ENTRE_TRAMOS})")
    parser.add_argument("--ciclos", type=int, default=CICLOS,
                        help=f"idas y vueltas a hacer, 0 = sin parar (defecto {CICLOS})")
    parser.add_argument("--puerto", default=PUERTO,
                        help=f"puerto serie (defecto {PUERTO})")
    args = parser.parse_args()

    seg_adelante = args.adelante
    seg_atras = args.atras if args.atras is not None else (
        SEGUNDOS_ATRAS if args.adelante == SEGUNDOS_ADELANTE else args.adelante)

    try:
        puerto = serial.Serial(args.puerto, BAUDIOS, timeout=0)
    except serial.SerialException as e:
        print(f"[!] No pude abrir {args.puerto}: {e}")
        print("    Haz primero: sudo rfcomm bind 0 <MAC del carro>")
        sys.exit(1)

    try:
        print(f"Conectado a {args.puerto}. Carro quieto {ESPERA_CALIBRACION:.0f} s "
              "para calibrar el giroscopio...")
        time.sleep(ESPERA_CALIBRACION)
        leer_respuesta(puerto, 0.3, mostrar=False)   # Vaciar el buffer

        enviar(puerto, "f", 0.3)         # Empezar siempre desde parado
        aplicar_configuracion(puerto)

        if args.ciclos > 0:
            print(f"\nHaciendo {args.ciclos} idas y vueltas. Ctrl+C para parar antes.")
        else:
            print("\nIda y vuelta en bucle. Ctrl+C para parar.")

        ciclo = 0
        while args.ciclos <= 0 or ciclo < args.ciclos:
            ciclo += 1
            print(f"\n##### CICLO {ciclo}" + (f" de {args.ciclos}" if args.ciclos > 0 else ""))
            mover(puerto, "w", seg_adelante, "ADELANTE")
            print(f"\nPausa {args.pausa:.1f} s")
            time.sleep(args.pausa)
            mover(puerto, "s", seg_atras, "ATRAS")
            print(f"\nPausa {args.pausa:.1f} s")
            time.sleep(args.pausa)

        print("\nListo. Carro detenido.")

    except KeyboardInterrupt:
        print("\n[Ctrl+C] Parando el carro...")
        enviar(puerto, "f", 0.3)
    finally:
        try:
            puerto.write(b"f")
            puerto.flush()
        except Exception:
            pass
        puerto.close()


if __name__ == "__main__":
    main()
