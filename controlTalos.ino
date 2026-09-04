// CarroESP32 — control por Bluetooth con rumbo estabilizado por IMU.
//
// Circuito:
//   Bateria 7.4 V -> fusible -> interruptor -+-> L293D pata 8 (Vcc2, motores)
//                                            +-> Buck 5.0 V -> ESP32 VIN
//                                                           -> L293D pata 16 (Vcc1)
//   IMU HW-123 (MPU6050) por I2C: VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
//
// El giroscopio mantiene el rumbo en las rectas: si un motor jala mas que el
// otro, el PID reparte la potencia entre las ruedas para compensar.
//
// Comandos (Bluetooth o Monitor Serie a 115200):
//   w = adelante   s = atras   a = giro izq   d = giro der   f = parar
//   r = recalibrar giroscopio      0 = activar/desactivar PID
//   i = invertir el sentido de la correccion del PID
//   j / k = invertir el motor A / el motor B (sin tocar cables)
//   m / n = mover SOLO el motor A / SOLO el motor B, hacia ADELANTE
//   g / h = mover SOLO el motor A / SOLO el motor B, hacia ATRAS
//   p = prueba automatica de potencia: compara los dos motores
//   7 / 8 = compensacion del motor A     9 / o = compensacion del motor B
//   + / - = velocidad del modo actual    c / v = velocidad de giro
//   1 / 2 = Kp    3 / 4 = Ki    5 / 6 = Kd    t = ver configuracion

#include <Wire.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth no habilitado. Selecciona una placa ESP32 con Bluetooth Classic.
#endif

BluetoothSerial BTSerial;

// ------------------------------------------------------------------ pines

// OJO: este mapeo sigue el cableado REAL de la placa, no el orden
// convencional. El motor A (izquierdo) cuelga del par de canales 3-4 del
// L293D y el B (derecho) del par 1-2. Verificado con las teclas m/g/n/h.
const int ENA = 32; // Motor A, IZQUIERDO  -> pata 9  (EN2)
const int IN1 = 27; //   adelante          -> pata 10 (3A)
const int IN2 = 33; //   atras             -> pata 15 (4A)
const int ENB = 13; // Motor B, DERECHO    -> pata 1  (EN1)
const int IN3 = 26; //   adelante          -> pata 2  (1A)
const int IN4 = 25; //   atras             -> pata 7  (2A)
const int LED = 2;  // LED azul integrado

const int PIN_SDA = 21;
const int PIN_SCL = 22;

// ------------------------------------------------- IMU HW-123 (MPU6050)

const uint8_t REG_PWR_MGMT_1  = 0x6B;
const uint8_t REG_CONFIG      = 0x1A;
const uint8_t REG_GYRO_CONFIG = 0x1B;
const uint8_t REG_GYRO_ZOUT_H = 0x47;

// A fondo de escala +-250 grados/s el sensor da 131 cuentas por grado/s
const float CUENTAS_POR_GRADO_S = 131.0;

uint8_t direccionMPU = 0x68;   // Se autodetecta entre 0x68 y 0x69
bool imuPresente = false;

int16_t  ultimoCrudo = 0;      // Ultima lectura valida del giroscopio
uint32_t erroresI2C = 0;       // Total de fallos de lectura
uint32_t erroresSeguidos = 0;  // Fallos consecutivos, dispara la recuperacion

// Leer el giroscopio a 100 Hz es de sobra para este carro, y deja el bus I2C
// y la pila de Bluetooth respirar. Antes se leia en cada vuelta del bucle.
const unsigned long INTERVALO_IMU_US = 10000;

// ---------------------------------------------------------------- PID

// Unicas tres ganancias a ajustar. Empieza solo con Kp, luego Kd, y Ki al final.
float Kp = 4.0;   // Reaccion al error actual
float Ki = 0.0;   // Corrige la desviacion que se acumula
float Kd = 0.8;   // Amortigua para que no oscile

// Sentido de la correccion. Si al mandar w el carro se desvia MAS en lugar de
// corregir, esta invertido: pulsa la tecla i para darle la vuelta en caliente.
float sentidoCorreccion = 1.0;

const int   MAX_CORRECCION = 80;   // Tope de reparto entre ruedas
const float MAX_INTEGRAL   = 40.0; // Anti-windup

// Arranca APAGADO a proposito: primero se verifica que los motores giran
// bien (teclas j y k), y solo entonces se activa el rumbo con la tecla 0.
bool pidActivo = false;

// ---------------------------------------------------------- velocidades

// Invierte el sentido de un motor sin tocar el cableado. Las teclas j y k
// los cambian en caliente; cuando sepas cual es, deja el valor puesto aqui.
bool invertirA = false;   // Motor A, izquierdo: gira bien tal cual
bool invertirB = true;    // Motor B, derecho: viene cableado al reves

// PWM extra para un motor que rinde menos que el otro. Compensa diferencias
// de fabricacion o una caida de tension en su canal. Teclas 7/8 y 9/o.
int compensacionA = 0;
int compensacionB = 0;

int velocidadBase = 200;            // PWM de crucero en recta
int velocidadGiro = 220;            // PWM girando sobre el eje (pivotar cuesta
                                    // mas que avanzar: las ruedas raspan de lado)
const int ACELERACION = 10;         // Suavidad de la rampa
const unsigned long PASO_RAMPA_MS = 20;

// --------------------------------------------------------------- estado

enum Modo { DETENIDO, ADELANTE, ATRAS, GIRO_IZQ, GIRO_DER,
            SOLO_A, SOLO_B, SOLO_A_ATRAS, SOLO_B_ATRAS };
Modo modo = DETENIDO;

float sesgoGiroZ = 0.0;        // Lectura del giroscopio en reposo
float rumbo = 0.0;             // Grados acumulados desde el ultimo arranque
float velocidadAngular = 0.0;  // Grados por segundo, ahora mismo
float integral = 0.0;
float correccion = 0.0;

int velocidadActual = 0;
int velocidadObjetivo = 0;

// PWM final que se escribio en cada canal. Sirve para distinguir un problema
// de codigo (los dos valores distintos) de uno de hardware (iguales y aun asi
// un motor rinde menos).
int pwmAplicadoA = 0;
int pwmAplicadoB = 0;

unsigned long ultimoPasoRampa = 0;
unsigned long ultimoCicloUs = 0;
unsigned long ultimaTelemetria = 0;

bool clienteConectado = false;
bool ledEncendido = false;
unsigned long ultimoParpadeo = 0;

// ================================================================== SETUP

void setup() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);

  detenerMotores();

  Serial.begin(115200);
  BTSerial.begin("CarroESP32");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setTimeOut(50);   // Sin esto, un bus colgado bloquea el bucle entero
  imuPresente = detectarMPU();

  if (imuPresente) {
    iniciarMPU();
    calibrarGiroscopio();
  } else {
    pidActivo = false;
    registrar("[!] IMU no detectada. El carro funciona, pero sin control de rumbo.");
    registrar("    Revisa VCC a 3V3, GND, SDA en GPIO21 y SCL en GPIO22.");
  }

  ultimoCicloUs = micros();
  registrar("Listo. w s a d = mover | f = parar | t = configuracion");
}

// =================================================================== LOOP

void loop() {

  vigilarEnlace();

  if (BTSerial.available()) procesarComando(BTSerial.read());
  if (Serial.available())   procesarComando(Serial.read());

  actualizarRumbo();   // Lee el giroscopio e integra el rumbo
  actualizarPID();     // Calcula cuanto repartir entre las ruedas
  actualizarRampa();   // Suaviza los cambios de velocidad
  aplicarMotores();    // Escribe el resultado en el puente H

  emitirTelemetria();
  actualizarLed();

  delay(1);   // Cede tiempo a la pila de Bluetooth y al watchdog
}

// ============================================================ GIROSCOPIO

// El HW-123 responde en 0x68 con AD0 a GND, o en 0x69 con AD0 a VCC.
bool detectarMPU() {
  for (uint8_t direccion = 0x68; direccion <= 0x69; direccion++) {
    Wire.beginTransmission(direccion);
    if (Wire.endTransmission() == 0) {
      direccionMPU = direccion;
      char buffer[48];
      snprintf(buffer, sizeof(buffer), "IMU detectada en 0x%02X", direccion);
      registrar(buffer);
      return true;
    }
  }
  return false;
}

void iniciarMPU() {
  escribirRegistro(REG_PWR_MGMT_1, 0x00);   // Despertar el sensor
  escribirRegistro(REG_CONFIG, 0x04);       // Filtro paso bajo ~20 Hz
  escribirRegistro(REG_GYRO_CONFIG, 0x00);  // Fondo de escala +-250 grados/s
  delay(100);
}

void escribirRegistro(uint8_t registro, uint8_t valor) {
  Wire.beginTransmission(direccionMPU);
  Wire.write(registro);
  Wire.write(valor);
  Wire.endTransmission();
}

// Lectura cruda con deteccion de fallo. Devuelve false si el bus no respondio.
bool leerGiroZ(int16_t &valor) {
  Wire.beginTransmission(direccionMPU);
  Wire.write(REG_GYRO_ZOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)direccionMPU, 2) != 2) return false;
  valor = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

// El ruido de los motores puede colgar el bus I2C. Si pasa, lo reiniciamos
// en vez de quedarnos esperando o de meter lecturas falsas en el rumbo.
void recuperarI2C() {
  erroresSeguidos = 0;
  Wire.end();
  delay(5);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setTimeOut(50);
  iniciarMPU();
  registrar("[!] Bus I2C reiniciado tras varios fallos seguidos.");
}

// Ante un fallo devuelve la ULTIMA lectura buena. Devolver 0, como hacia
// antes, metia una velocidad angular falsa y enorme dentro del rumbo.
int16_t leerGiroZcrudo() {
  int16_t valor = 0;

  if (leerGiroZ(valor)) {
    ultimoCrudo = valor;
    erroresSeguidos = 0;
    return valor;
  }

  erroresI2C++;
  erroresSeguidos++;
  if (erroresSeguidos >= 20) recuperarI2C();

  return ultimoCrudo;
}

// Un giroscopio en reposo nunca marca cero exacto. Medimos ese offset una vez
// y lo restamos siempre. Sin esto el rumbo se va solo con el carro quieto.
void calibrarGiroscopio() {
  registrar("Calibrando giroscopio: NO MUEVAS EL CARRO (3 s)...");
  delay(500);

  long suma = 0;
  const int MUESTRAS = 600;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += leerGiroZcrudo();
    delay(4);
  }

  sesgoGiroZ = (float)suma / MUESTRAS;
  rumbo = 0.0;
  integral = 0.0;

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "Calibrado. Sesgo = %.1f cuentas", sesgoGiroZ);
  registrar(buffer);
}

// Integra la velocidad angular para obtener el rumbo en grados.
void actualizarRumbo() {
  if (!imuPresente) return;

  unsigned long ahora = micros();
  if (ahora - ultimoCicloUs < INTERVALO_IMU_US) return;   // Limita a 100 Hz

  float dt = (ahora - ultimoCicloUs) / 1000000.0;
  ultimoCicloUs = ahora;

  if (dt > 0.5) return;   // Tras una pausa larga, descarta ese intervalo

  velocidadAngular = (leerGiroZcrudo() - sesgoGiroZ) / CUENTAS_POR_GRADO_S;

  // Por debajo de 0.5 grados/s es ruido del sensor, no giro real.
  if (fabs(velocidadAngular) < 0.5) velocidadAngular = 0.0;

  rumbo += velocidadAngular * dt;
}

// =================================================================== PID

void actualizarPID() {

  // El PID solo actua en las rectas. Al girar, el desvio es intencional.
  bool enRecta = (modo == ADELANTE || modo == ATRAS);

  if (!imuPresente || !pidActivo || !enRecta) {
    integral = 0.0;
    correccion = 0.0;
    return;
  }

  static unsigned long anteriorUs = 0;
  unsigned long ahora = micros();
  float dt = (anteriorUs == 0) ? 0.01 : (ahora - anteriorUs) / 1000000.0;
  anteriorUs = ahora;
  if (dt <= 0 || dt > 0.5) dt = 0.01;

  // El objetivo es rumbo 0: el que tenia el carro al arrancar la recta.
  float error = 0.0 - rumbo;

  // --- P: proporcional al error de ahora mismo ---
  float termino_P = Kp * error;

  // --- I: acumula el error que P no consigue eliminar ---
  integral += error * dt;
  integral = constrain(integral, -MAX_INTEGRAL, MAX_INTEGRAL);
  float termino_I = Ki * integral;

  // --- D: se opone a la velocidad de cambio ---
  // La derivada del rumbo ES la velocidad angular, y el giroscopio ya la mide.
  // No derivamos numericamente: menos ruido y sin picos.
  float termino_D = -Kd * velocidadAngular;

  correccion = sentidoCorreccion * (termino_P + termino_I + termino_D);
  correccion = constrain(correccion, -MAX_CORRECCION, MAX_CORRECCION);
}

// =============================================================== MOTORES

void actualizarRampa() {
  if (millis() - ultimoPasoRampa < PASO_RAMPA_MS) return;
  ultimoPasoRampa = millis();

  if (velocidadActual < velocidadObjetivo) {
    velocidadActual = min(velocidadActual + ACELERACION, velocidadObjetivo);
  } else if (velocidadActual > velocidadObjetivo) {
    velocidadActual = max(velocidadActual - ACELERACION, velocidadObjetivo);
  }
}

void sentidoMotor(int pinA, int pinB, bool adelante, bool invertido) {
  if (invertido) adelante = !adelante;
  digitalWrite(pinA, adelante ? HIGH : LOW);
  digitalWrite(pinB, adelante ? LOW  : HIGH);
}

// La correccion se suma a una rueda y se resta a la otra: el carro gira para
// volver al rumbo sin que cambie su velocidad media.
void aplicarMotores() {

  if (modo == DETENIDO || velocidadActual <= 0) {
    detenerMotores();
    return;
  }

  int corr = (int)correccion;
  int izquierda = velocidadActual;
  int derecha   = velocidadActual;

  switch (modo) {

    case ADELANTE:
      sentidoMotor(IN1, IN2, true, invertirA);
      sentidoMotor(IN3, IN4, true, invertirB);
      izquierda = velocidadActual + corr;
      derecha   = velocidadActual - corr;
      break;

    case ATRAS:
      sentidoMotor(IN1, IN2, false, invertirA);
      sentidoMotor(IN3, IN4, false, invertirB);
      // Marcha atras: la geometria se invierte y la correccion tambien.
      izquierda = velocidadActual - corr;
      derecha   = velocidadActual + corr;
      break;

    case GIRO_IZQ:
      sentidoMotor(IN1, IN2, false, invertirA);
      sentidoMotor(IN3, IN4, true, invertirB);
      break;

    case GIRO_DER:
      sentidoMotor(IN1, IN2, true, invertirA);
      sentidoMotor(IN3, IN4, false, invertirB);
      break;

    // Modos de prueba: mueven un solo motor para ver su sentido sin ambiguedad
    case SOLO_A:
      sentidoMotor(IN1, IN2, true, invertirA);
      derecha = 0;
      break;

    case SOLO_B:
      sentidoMotor(IN3, IN4, true, invertirB);
      izquierda = 0;
      break;

    // Los mismos motores pero en reversa. Aislan cada medio puente del L293D.
    case SOLO_A_ATRAS:
      sentidoMotor(IN1, IN2, false, invertirA);
      derecha = 0;
      break;

    case SOLO_B_ATRAS:
      sentidoMotor(IN3, IN4, false, invertirB);
      izquierda = 0;
      break;

    default:
      detenerMotores();
      return;
  }

  // Compensacion: solo se aplica al motor que debe estar girando.
  if (izquierda > 0) izquierda += compensacionA;
  if (derecha   > 0) derecha   += compensacionB;

  pwmAplicadoA = constrain(izquierda, 0, 255);
  pwmAplicadoB = constrain(derecha,   0, 255);

  analogWrite(ENA, pwmAplicadoA);
  analogWrite(ENB, pwmAplicadoB);
}

void detenerMotores() {
  pwmAplicadoA = 0;
  pwmAplicadoB = 0;
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

void parar() {
  modo = DETENIDO;
  velocidadObjetivo = 0;
  velocidadActual = 0;
  integral = 0.0;
  correccion = 0.0;
  rumbo = 0.0;          // El rumbo actual pasa a ser la nueva referencia
  detenerMotores();
}

// ====================================================== PRUEBA DE MOTORES

// Mide el valor absoluto del giro promediado durante unos milisegundos.
float medirGiroPromedio(int milisegundos) {
  unsigned long fin = millis() + milisegundos;
  float suma = 0.0;
  int muestras = 0;

  while (millis() < fin) {
    suma += fabs((leerGiroZcrudo() - sesgoGiroZ) / CUENTAS_POR_GRADO_S);
    muestras++;
    delay(5);
  }
  return (muestras > 0) ? suma / muestras : 0.0;
}

// Sube el PWM poco a poco hasta que el carro empieza a pivotar de verdad.
// Un motor con engranes rozando o escobillas gastadas necesita mas PWM.
int medirUmbral(int pinA, int pinB, int pinEN, bool invertido) {
  sentidoMotor(pinA, pinB, true, invertido);

  for (int pwm = 40; pwm <= 255; pwm += 5) {
    analogWrite(pinEN, pwm);
    delay(150);
    if (medirGiroPromedio(80) > 10.0) {
      analogWrite(pinEN, 0);
      return pwm;
    }
  }
  analogWrite(pinEN, 0);
  return -1;   // No arranco en todo el rango
}

// Velocidad de giro que produce un motor a un PWM fijo. Es la medida de fuerza.
float medirEmpuje(int pinA, int pinB, int pinEN, bool invertido, int pwm) {
  sentidoMotor(pinA, pinB, true, invertido);
  analogWrite(pinEN, pwm);
  delay(500);                       // Dejar que arranque y se estabilice
  float valor = medirGiroPromedio(1000);
  analogWrite(pinEN, 0);
  return valor;
}

void pruebaMotores() {

  if (!imuPresente) {
    registrar("[!] Esta prueba necesita la IMU. Cancelada.");
    return;
  }

  parar();
  registrar("--- PRUEBA DE MOTORES ---");
  registrar("Pon el carro EN EL SUELO, con espacio para pivotar.");
  registrar("Va a girar solo. Empieza en 3 segundos...");
  delay(3000);

  int umbralA = medirUmbral(IN1, IN2, ENA, invertirA);
  delay(800);
  int umbralB = medirUmbral(IN3, IN4, ENB, invertirB);
  delay(800);

  float empujeA = medirEmpuje(IN1, IN2, ENA, invertirA, velocidadGiro);
  delay(800);
  float empujeB = medirEmpuje(IN3, IN4, ENB, invertirB, velocidadGiro);

  detenerMotores();

  char buffer[128];
  snprintf(buffer, sizeof(buffer), "Umbral de arranque : A=%d   B=%d   (PWM)",
           umbralA, umbralB);
  registrar(buffer);
  snprintf(buffer, sizeof(buffer), "Giro a PWM %-3d     : A=%.1f  B=%.1f  (grados/s)",
           velocidadGiro, empujeA, empujeB);
  registrar(buffer);

  float mayor = (empujeA > empujeB) ? empujeA : empujeB;
  float menor = (empujeA < empujeB) ? empujeA : empujeB;

  if (mayor < 3.0) {
    registrar("[!] Apenas hubo giro. Estaba el carro en el suelo?");
  } else {
    float diferencia = 100.0 * (mayor - menor) / mayor;
    snprintf(buffer, sizeof(buffer), "Diferencia         : %.0f%%   (el flojo es el motor %s)",
             diferencia, (empujeA < empujeB) ? "A" : "B");
    registrar(buffer);

    if (diferencia < 10.0) {
      registrar("  -> Normal. Es la tolerancia tipica entre dos motores.");
    } else if (diferencia < 25.0) {
      registrar("  -> Notable, pero el PID lo compensa sin problema.");
    } else {
      registrar("  -> Alta. Revisa el motor flojo antes de cambiarlo:");
      registrar("     gira su eje con la mano y escucha si roza o se traba.");
    }
  }

  rumbo = 0.0;
  ultimoCicloUs = micros();
  registrar("--- FIN DE LA PRUEBA ---");
}

// ============================================================== COMANDOS

void procesarComando(char c) {
  switch (c) {

    case 'w':
      rumbo = 0.0;      // Fija el rumbo actual como objetivo de la recta
      integral = 0.0;
      modo = ADELANTE;
      velocidadObjetivo = velocidadBase;
      registrar("Adelante");
      break;

    case 's':
      rumbo = 0.0;
      integral = 0.0;
      modo = ATRAS;
      velocidadObjetivo = velocidadBase;
      registrar("Atras");
      break;

    case 'a':
      modo = GIRO_IZQ;
      velocidadObjetivo = velocidadGiro;
      registrar("Giro izquierda");
      break;

    case 'd':
      modo = GIRO_DER;
      velocidadObjetivo = velocidadGiro;
      registrar("Giro derecha");
      break;

    case 'f':
      parar();
      registrar("Parar");
      break;

    case 'r':
      parar();
      if (imuPresente) calibrarGiroscopio();
      else registrar("[!] No hay IMU que calibrar.");
      break;

    case '0':
      pidActivo = !pidActivo;
      registrar(pidActivo ? "PID activado" : "PID desactivado");
      break;

    case 'p':
      pruebaMotores();
      break;

    case '7': compensacionA = constrain(compensacionA - 5, 0, 120);
              mostrarConfiguracion(); break;
    case '8': compensacionA = constrain(compensacionA + 5, 0, 120);
              mostrarConfiguracion(); break;
    case '9': compensacionB = constrain(compensacionB - 5, 0, 120);
              mostrarConfiguracion(); break;
    case 'o': compensacionB = constrain(compensacionB + 5, 0, 120);
              mostrarConfiguracion(); break;

    case 'm':
      modo = SOLO_A;
      velocidadObjetivo = velocidadGiro;
      registrar("Solo motor A hacia adelante");
      break;

    case 'n':
      modo = SOLO_B;
      velocidadObjetivo = velocidadGiro;
      registrar("Solo motor B hacia adelante");
      break;

    case 'g':
      modo = SOLO_A_ATRAS;
      velocidadObjetivo = velocidadGiro;
      registrar("Solo motor A hacia ATRAS");
      break;

    case 'h':
      modo = SOLO_B_ATRAS;
      velocidadObjetivo = velocidadGiro;
      registrar("Solo motor B hacia ATRAS");
      break;

    case 'j':
      invertirA = !invertirA;
      registrar(invertirA ? "Motor A INVERTIDO" : "Motor A normal");
      break;

    case 'k':
      invertirB = !invertirB;
      registrar(invertirB ? "Motor B INVERTIDO" : "Motor B normal");
      break;

    case 'i':
      sentidoCorreccion = -sentidoCorreccion;
      integral = 0.0;
      rumbo = 0.0;
      registrar(sentidoCorreccion > 0 ? "Sentido de correccion: NORMAL"
                                      : "Sentido de correccion: INVERTIDO");
      break;

    // Mientras giras, +/- ajustan la velocidad de giro. En cualquier otro
    // momento ajustan la de recta. Las teclas c y v tocan siempre la de giro.
    case '+': ajustarVelocidad(+10); break;
    case '-': ajustarVelocidad(-10); break;

    case 'v': ajustarVelocidadGiro(+10); break;
    case 'c': ajustarVelocidadGiro(-10); break;

    case '1': Kp = max(0.0f, Kp - 0.5f); mostrarConfiguracion(); break;
    case '2': Kp += 0.5f;                mostrarConfiguracion(); break;
    case '3': Ki = max(0.0f, Ki - 0.1f); mostrarConfiguracion(); break;
    case '4': Ki += 0.1f;                mostrarConfiguracion(); break;
    case '5': Kd = max(0.0f, Kd - 0.2f); mostrarConfiguracion(); break;
    case '6': Kd += 0.2f;                mostrarConfiguracion(); break;

    case 't': mostrarConfiguracion(); break;

    default:
      break;  // Ignora saltos de linea y comandos desconocidos
  }
}

// Ajusta la velocidad del modo en el que estas ahora mismo.
void ajustarVelocidad(int delta) {
  if (modo == GIRO_IZQ || modo == GIRO_DER) {
    ajustarVelocidadGiro(delta);
    return;
  }

  velocidadBase = constrain(velocidadBase + delta, 0, 255);
  if (modo == ADELANTE || modo == ATRAS) velocidadObjetivo = velocidadBase;
  mostrarConfiguracion();
}

void ajustarVelocidadGiro(int delta) {
  velocidadGiro = constrain(velocidadGiro + delta, 0, 255);
  if (modo != DETENIDO && modo != ADELANTE && modo != ATRAS) {
    velocidadObjetivo = velocidadGiro;
  }
  mostrarConfiguracion();
}

void mostrarConfiguracion() {
  char buffer[192];
  snprintf(buffer, sizeof(buffer),
           "Kp=%.1f Ki=%.1f Kd=%.1f base=%d giro=%d sentido=%+.0f "
           "compA=%d compB=%d invA=%s invB=%s PID=%s IMU=%s errI2C=%lu",
           Kp, Ki, Kd, velocidadBase, velocidadGiro, sentidoCorreccion,
           compensacionA, compensacionB,
           invertirA ? "SI" : "no", invertirB ? "SI" : "no",
           pidActivo ? "ON" : "OFF", imuPresente ? "ok" : "NO", erroresI2C);
  registrar(buffer);
}

// ============================================================ TELEMETRIA

void emitirTelemetria() {
  if (modo == DETENIDO) return;
  if (millis() - ultimaTelemetria < 200) return;
  ultimaTelemetria = millis();

  char buffer[96];
  snprintf(buffer, sizeof(buffer),
           "rumbo=%+.1f  giro=%+.1f  corr=%+.0f  pwmA=%d  pwmB=%d",
           rumbo, velocidadAngular, correccion, pwmAplicadoA, pwmAplicadoB);
  registrar(buffer);
}

// ================================================================ ENLACE

void vigilarEnlace() {
  bool hayCliente = BTSerial.hasClient();

  if (hayCliente != clienteConectado) {
    clienteConectado = hayCliente;

    if (clienteConectado) {
      registrar("Conectado. w=adelante s=atras a=izq d=der f=parar");
    } else {
      parar();   // Seguridad: enlace perdido, el carro se detiene
      registrar("Cliente desconectado. Motores detenidos.");
    }
  }
}

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

void registrar(const char *mensaje) {
  Serial.println(mensaje);
  if (BTSerial.hasClient()) BTSerial.println(mensaje);
}