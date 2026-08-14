#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth no habilitado. Selecciona una placa ESP32 con Bluetooth Classic.
#endif

BluetoothSerial BTSerial; // Bluetooth interno del ESP32

// Definir pines del L293D
const int ENA = 13; // Habilitador del motor A    -> pata 1  (EN1)
const int ENB = 32; // Habilitador del motor B    -> pata 9  (EN2)
const int IN1 = 27; // Motor A sentido horario     -> pata 2  (1A)
const int IN2 = 26; // Motor A sentido antihorario -> pata 7  (2A)
const int IN3 = 25; // Motor B sentido antihorario -> pata 10 (3A)
const int IN4 = 33; // Motor B sentido horario     -> pata 15 (4A)

// LED azul integrado de la placa. Indica el estado de la conexion:
// parpadeando = esperando cliente, fijo = conectado
const int LED = 2;

// Velocidad de 0 a 255. Con 255 los motores reciben ~5.5 V,
// equivalente a las 4 pilas de 1.5 V originales del kit.
const int VELOCIDAD = 255;

bool clienteConectado = false;
bool ledEncendido = false;
unsigned long ultimoParpadeo = 0;

void setup() {

  // Configurar pines del L293D a salida
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);

  // Apagar motores antes de cualquier otra cosa
  detenerMotores();

  Serial.begin(115200);         // Solo util si hay cable USB conectado
  BTSerial.begin("CarroESP32"); // Nombre visible al emparejar

  registrar("Sistema listo. Esperando conexion Bluetooth.");
}

void loop() {

  // Vigilar el estado del enlace Bluetooth
  bool hayCliente = BTSerial.hasClient();

  if (hayCliente != clienteConectado) {
    clienteConectado = hayCliente;

    if (clienteConectado) {
      registrar("Conectado. Comandos: w=adelante s=atras a=izq d=der f=parar");
    } else {
      detenerMotores(); // Seguridad: enlace perdido, el carro se detiene
      registrar("Cliente desconectado. Motores detenidos.");
    }
  }

  actualizarLed();

  // Comandos por Bluetooth
  if (BTSerial.available()) {
    procesarComando(BTSerial.read());
  }

  // Comandos por USB (util durante el desarrollo)
  if (Serial.available()) {
    procesarComando(Serial.read());
  }
}

// Ejecuta la accion asociada a un comando, venga de donde venga
void procesarComando(char command) {

  switch (command) {

    case 'w': // Adelante
      moverMotores(HIGH, LOW, HIGH, LOW);
      registrar("Adelante");
      break;

    case 's': // Atras
      moverMotores(LOW, HIGH, LOW, HIGH);
      registrar("Atras");
      break;

    case 'a': // Izquierda
      moverMotores(LOW, HIGH, HIGH, LOW);
      registrar("Izquierda");
      break;

    case 'd': // Derecha
      moverMotores(HIGH, LOW, LOW, HIGH);
      registrar("Derecha");
      break;

    case 'f': // Parar
      detenerMotores();
      registrar("Parar");
      break;

    default:
      // Ignorar comando desconocido (incluye saltos de linea)
      break;
  }
}

// Metodo para mover los motores
void moverMotores(int in1, int in2, int in3, int in4) {
  digitalWrite(IN1, in1);
  digitalWrite(IN2, in2);
  digitalWrite(IN3, in3);
  digitalWrite(IN4, in4);
  analogWrite(ENA, VELOCIDAD);
  analogWrite(ENB, VELOCIDAD);
}

// Metodo para detener los motores
void detenerMotores() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

// LED fijo cuando hay cliente conectado, parpadeando mientras espera
void actualizarLed() {
  if (clienteConectado) {
    digitalWrite(LED, HIGH);
    return;
  }

  if (millis() - ultimoParpadeo > 400) {
    ultimoParpadeo = millis();
    ledEncendido = !ledEncendido;
    digitalWrite(LED, ledEncendido);
  }
}

// Envia mensajes al movil por Bluetooth, y por USB si hay cable
void registrar(const char *mensaje) {
  Serial.println(mensaje);
  if (BTSerial.hasClient()) {
    BTSerial.println(mensaje);
  }
}