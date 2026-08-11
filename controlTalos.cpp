#include <SoftwareSerial.h> // Permite comunicación con el modulo usando los pines digitales

// Definir pines para HC05
SoftwareSerial BTSerial(5, 6); // RX | TX

// Definir pines del L293D
const int ENA = 9; // Enhabilitador del motor A
const int ENB = 10; // Enhabilitador del motor B
const int IN1 = 7; // Girar motor A en sentido horario
const int IN2 = 8; // Girar motor A en sentido antihorario
const int IN3 = 11; // Girar motor B en sentido anthorario
const int IN4 = 12; // Girar motor B en sentido horario


void setup() {
  
  // Configurar pines del L293D a salida
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Configurar pin enhabilitador de HC05
  pinMode(4, OUTPUT); // Enhabilitador: Pin digital 4
  digitalWrite(4, HIGH);
  
  Serial.begin(9600);
  BTSerial.begin(9600);
  
  // Apagar motores al iniciar
  detenerMotores();
}

void loop() {
  
  // Leer del Bluetooth
  if (BTSerial.available()) { // Bytes listos para lectura
    
    // Lectura del carácter
    char command = BTSerial.read();
    
    switch (command) {
      
      case 'w': // Adelante
        moverMotores(HIGH, LOW, HIGH, LOW);
        break;
      
      case 's': // Atrás
        moverMotores(LOW, HIGH, LOW, HIGH);
        break;
      
      case 'a': // Izquierda
        moverMotores(LOW, HIGH, HIGH, LOW);
        break;
      
      case 'd': // Derecha
        moverMotores(HIGH, LOW, LOW, HIGH);
        break;
      
      case 'f': // Parar
        detenerMotores();
        break;
      
      default:
        // Ignorar comando desconocido
        break;
    }
  }
}

// Metodo para mover los motores
void moverMotores(int in1, int in2, int in3, int in4) {
  digitalWrite(IN1, in1); 
  digitalWrite(IN2, in2); 
  digitalWrite(IN3, in3); 
  digitalWrite(IN4, in4);
  digitalWrite(ENA, 1); // Velocidad maxima
  digitalWrite(ENB, 1); // Velocidad maxima
}

// Metodo para detener los motores
void detenerMotores() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  digitalWrite(ENA, 0); // Velocidad igual a 0
  digitalWrite(ENB, 0); // Velocidad igual a 0
}