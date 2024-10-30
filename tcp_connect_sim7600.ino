#include <HardwareSerial.h>

HardwareSerial SIM7600(1);  // Serial1 en pines 17 (TX) y 18 (RX)

const char *apns[] = {
    "ott.iot.attmex.mx",
    "internet.itelcel.com"
};
const char* tcp_server = "34.196.135.179";
const int tcp_port = 5200;
const char *headers[] = {
    "STT",
    "ALT",
    "RES",
    "CMD"
};
const int model = 24; //2024
String sw_ver = "1.0.0";
unsigned long lastSendTime = 0;
bool gprsConnected = false;
bool tcpConnected = false;
String imei, raw_imei, iccid, mcc, mnc, lac, cellID, rxLev;
int msg_type = 1; //realtime 1 buffer 0
String  report_map = "3FFFFF";
int in1 = 0, in2 = 0, out1 = 0, out2 = 0;
String modem_date = "20241029";
String modem_time = "14:21:34";

void setup() {
  Serial.begin(115200);      // Monitor serial
  SIM7600.begin(115200, SERIAL_8N1, 18, 17); // Comunicación con SIM7600
  
  delay(1000);
  while(checkSIMConnection() ){
    if(CheckConfigureAPN(apns[1])){
      configureAPN(apns[1]);
    }
  }
}

void loop() {
  handleSerialInput();
  
  // Envía mensaje cada 10 segundos si GPRS y TCP están conectados
  if (millis() - lastSendTime >= 10000) {
    if (gprsConnected && tcpConnected) {
      sendTCPMessage();
      //Serial.println("Listo para enviar cadenas a servidor");
    } else {
      // Reintentar conexiones si alguna está caída
      if (!gprsConnected) {
        connectToGPRS();
      }
      if (!tcpConnected) {
        connectToTCPServer();
      }
    }
    lastSendTime = millis();
  }

  // Monitorea periódicamente la conexión GPRS
  //monitorNetworkConnection();
}

// Verifica la comunicación inicial con el módulo SIM
bool checkSIMConnection() {
  SIM7600.println("AT");  // Enviar un comando AT simple
  delay(1000);
  if (SIM7600.available()) {
    Serial.println("Comunicación con el módulo SIM establecida.");
    return true;
  } else {
    Serial.println("No hay comunicación con el módulo SIM. Verifica conexiones.");
    return false;
  }
  return false;
}

// Función para enviar comandos AT y mostrar respuesta completa
String sendCommandWithResponse(const char* command, int timeout = 5000) {
  Serial.print("Enviando comando: ");
  Serial.println(command);
  SIM7600.println(command); // Envía el comando al módulo SIM
  String response = "";
  long int time = millis();
  while ((millis() - time) < timeout) {
    if (SIM7600.available()) {
      char c = SIM7600.read();
      response += c;
    }
  }
  Serial.print("Respuesta: ");
  Serial.println(response);
  return response;
}

// Función para conectarse a la red GPRS con validación de error
void connectToGPRS() {
  // Intenta adjuntar a la red de datos GPRS solo una vez, sin bucle
  /*String response = sendCommandWithResponse("AT+CGATT=1");
  if (!response.endsWith("OK")) {
    Serial.println("No se pudo adjuntar a la red. Procediendo de todas formas.");
  }*/
  sendCommandWithResponse("AT+CFUN?");
  delay(5000);
  // Configura el APN //valida si ya está registrado su APN o VALIDA que comañia es con un comando y manda el apn
  //sendCommandWithResponse("AT+CGDCONT=1,\"IP\",\"internet.itelcel.com\"");
  String respuestaIMEI;
  bool exito = responseFormatCommand("AT+SIMEI?", respuestaIMEI);

  if (exito) {
    Serial.println("Respuesta de IMEI obtenida correctamente:");

    imei = formatIMEI(respuestaIMEI);
    Serial.println(imei);  // Imprime solo el IMEI
  } else {
    Serial.println("Error al obtener el IMEI");
  }
  //MANDAR HASTA TENER CONEXION GPRS
  if (parseCPSIResponse("AT+CPSI?", mcc, mnc, lac, cellID, rxLev)) {
    Serial.println("CPSI data parsed successfully.");
  } else {
    Serial.println("Failed to parse CPSI data.");
  }

  // Verifica y activa el contexto PDP si es necesario
  if (checkCGACTStatus()) {
    if (checkNetOpenStatus()) {
      // Confirmado registro en red, ahora obtiene la IP
      if (checkIPAddress()) {
        Serial.println("Conectado a la red GPRS con IP válida.");
        gprsConnected = true;
        connectToTCPServer(); // Intenta conexión TCP si GPRS es exitoso
      } else {
        Serial.println("No se pudo obtener una IP.");
      }
    } else {
      Serial.println("No se pudo abrir la red con NETOPEN.");
    }
  } else {
    Serial.println("Error en activación PDP. Reiniciando módulo...");
    //resetModule(); // Reinicia si falla la activación del contexto PDP
  }
}

// Función para verificar el estado de CGACT y activar contexto si necesario
bool checkCGACTStatus() {
  String response = sendCommandWithResponse("AT+CGACT?");
  if (response.indexOf("+CGACT: 1,0") != -1) {
    Serial.println("Contexto PDP (Packet Data Protocol) inactivo. Intentando activar...");
    response = sendCommandWithResponse("AT+CGACT=1,1");
    if (response.indexOf("+CME ERROR") != -1) {
      Serial.println("Error activando PDP: +CME ERROR. Reiniciando módulo...");
      //resetModule();

      return false;
    }
  }
  Serial.println("Contexto PDP activado.");
  return true;
}

// Función para verificar el estado de NETOPEN y abrir red si necesario
bool checkNetOpenStatus() {
  String response = sendCommandWithResponse("AT+NETOPEN?");
  if (response.indexOf("+NETOPEN: 0") != -1) {
    Serial.println("La red está cerrada. Intentando abrirla...");
    response = sendCommandWithResponse("AT+NETOPEN");
    if (!response.endsWith("OK")) {
      Serial.println("Error al abrir red.");
      return false;
    }
  } else if (response.indexOf("+NETOPEN: 1") != -1) {
    Serial.println("La red está abierta.");
    return true;
  }
  Serial.println("Error al verificar el estado de NETOPEN.");
  return false;
}

// Función para verificar si el módulo tiene una IP asignada
bool checkIPAddress() {
  String response = sendCommandWithResponse("AT+CGPADDR=1");
  delay(1000);
  // Confirma que la respuesta tiene "+CGPADDR: 1," y no contiene "0.0.0.0"
  if (response.indexOf("+CGPADDR: 1,") != -1 && response.indexOf("0.0.0.0") == -1) {
    Serial.println("IP asignada correctamente.");
    return true;
  }
  Serial.println("No se ha asignado IP.");
  return false;
}

// Función para monitorear la conexión de red y reconectar si es necesario
void monitorNetworkConnection() {
  String response = sendCommandWithResponse("AT+CGREG?");
  if (response.indexOf("+CME ERROR:") != -1) {
    Serial.println("Error de red detectado.");
  }
}

// Función para reiniciar el módulo SIM solo al recibir +CME ERROR en AT+CGACT=1,1
void resetModule() {
  //sendCommandWithResponse("AT+CRESET");
  sendCommandWithResponse("AT+CFUN=0");
  delay(5000);  // Espera para que el módulo reinicie completamente
}

// Función para conectarse al servidor TCP
void connectToTCPServer() {
  if (!gprsConnected) return; // Asegura que esté conectado a GPRS
  
  String cmd = "AT+CIPOPEN=0,\"TCP\",\"" + String(tcp_server) + "\"," + String(tcp_port);
  String response = sendCommandWithResponse(cmd.c_str());
  if (response.indexOf("OK") != -1) {
    tcpConnected = true;
    Serial.println("Conectado al servidor TCP.");
  } else {
    Serial.println("Error en conexión al servidor TCP.");
    tcpConnected = false;
    String response2 = sendCommandWithResponse("AT+CIPCLOSE=0");//volver dinamico el numero 1
    if (!response2.endsWith("OK")) {
      Serial.println("Error al cerrar el servidor");
    }

  }
}

// Función para enviar mensajes TCP
void sendTCPMessage() {
  if (!tcpConnected) {
    connectToTCPServer(); // Reintenta conexión TCP si no está conectada
    return;
  }

  String message = String(headers[0])+";"+imei+";"+report_map+";"+model+";"+sw_ver+";"+modem_date+";"+modem_time+";"+cellID+";"+mcc+";"+mnc+";"+lac;
  Serial.println(message);
  /*String cmd = "AT+CIPSEND=0," + String(message.length());
  String response = sendCommandWithResponse(cmd.c_str());
  if (response.indexOf(">") != -1) {
    SIM7600.print(message);               // Envía el mensaje
    delay(1000);                          // Espera un poco para asegurar envío

    // Lee la respuesta del servidor
    if (SIM7600.available()) {
      Serial.println("Respuesta del servidor:");
      while (SIM7600.available()) {
        Serial.write(SIM7600.read());
      }
    } else {
      Serial.println("No se recibió respuesta del servidor.");
    }
  } else {
    Serial.println("Error al enviar mensaje TCP. Intentando reconexión...");
    tcpConnected = false;
  }*/
}

// Función para manejar entrada de comandos AT desde el monitor serial
void handleSerialInput() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    String response = sendCommandWithResponse(command.c_str());  // Envía comando AT al módulo
  }
}
bool responseFormatCommand(const String &comando, String &respuesta) {
  SIM7600.println(comando);  // Enviar el comando al módulo SIM7600G
  delay(500);  // Espera un tiempo para recibir la respuesta

  // Leer la respuesta completa en una cadena temporal
  String respuestaCompleta = "";
  while (SIM7600.available()) {
    respuestaCompleta += (char)SIM7600.read();
  }
  
  // Aplicar trim() para eliminar caracteres de nueva línea al inicio y fin
  respuestaCompleta.trim();

  // Imprimir la respuesta completa para depuración
  Serial.println("Respuesta completa : " + respuestaCompleta);

  // Buscar la posición de "+SIMEI: " y "OK"
  int posInicio = respuestaCompleta.indexOf(": ");
  int posFin = respuestaCompleta.indexOf("\nOK");

  // Verificar si se encontró tanto el inicio como el final de la respuesta
  if (posInicio != -1 && posFin != -1 && posFin > posInicio) {
    respuesta = respuestaCompleta.substring(posInicio + 2, posFin);
    respuesta.trim();  // Limpiar caracteres extra en la respuesta final
    return true;  // El comando fue exitoso y devolvió "OK"
  } else {
    respuesta = "";  // Limpiamos la respuesta en caso de error
    Serial.println("Error: Respuesta no válida o comando fallido");
    return false;  // Retorna false si no se encuentra "OK" o si hay un error
  }
}

String formatIMEI(String input) {
  // Verifica que el string tenga al menos 10 caracteres
  if (input.length() >= 10) {
    // Devuelve los últimos 10 caracteres
    return input.substring(input.length() - 13);
  } else {
    // Si el string es menor que 10 caracteres, devuelve el string completo
    return input;
  }
}
//poner en un loop la lectura de datos en tiempo real
bool parseCPSIResponse(const String &comando, String &mcc, String &mnc, String &lac, String &cellID, String &rxLev) {
  // Check if the response contains "+CPSI:"
  SIM7600.println(comando);  // Enviar el comando al módulo SIM7600G
  delay(500);  // Espera un tiempo para recibir la respuesta

  // Leer la respuesta completa en una cadena temporal
  String respuestaCompleta = "";
  while (SIM7600.available()) {
    respuestaCompleta += (char)SIM7600.read();
  }

  // Imprimir la respuesta completa para depuración
  Serial.println("Respuesta completa : " + respuestaCompleta);
  if (respuestaCompleta.indexOf("+CPSI:") == -1) {
    Serial.println("Error: No CPSI data found.");
    return false;
  }
  int inicioDatos = respuestaCompleta.indexOf(":") + 2;
  int finDatos = respuestaCompleta.indexOf("OK");
  

  String datos = respuestaCompleta.substring(inicioDatos, finDatos);
  datos.trim();
    // Dividir en partes los datos utilizando la coma como delimitador
  int posicion = 0;
  String partes[14]; // Array para almacenar las 14 partes de la respuesta WCDMA
  for (int i = 0; i < 14 && datos.length() > 0; i++) {
    posicion = datos.indexOf(",");
    if (posicion != -1) {
      partes[i] = datos.substring(0, posicion);
      datos = datos.substring(posicion + 1);
    } else {
      partes[i] = datos; // Última parte
      datos = "";
    }
  }

  // Separar MCC y MNC
  mcc = partes[2].substring(0, partes[2].indexOf("-"));
  mnc = partes[2].substring(partes[2].indexOf("-") + 1);

  // Quitar prefijo "0x" del LAC si está presente
  lac = partes[3];
  if (lac.startsWith("0x")) {
    lac = lac.substring(2);  // Eliminar "0x"
  }
  if(partes[4] != ""){
      cellID = partes[4];
  }
  if(partes[12] != ""){
      rxLev = partes[4];
  }
  // Mostrar los datos en el formato solicitado para WCDMA
  Serial.println("===== Información CPSI (WCDMA) =====");
  Serial.println("Modo de Sistema: " + partes[0]);               // System Mode
  Serial.println("Modo de Operación: " + partes[1]);             // Operation Mode
  Serial.println("MCC: " + mcc);                                 // MCC separado
  Serial.println("MNC: " + mnc);                                 // MNC separado
  Serial.println("LAC (Hexadecimal sin '0x'): " + lac);          // LAC en hexadecimal sin "0x"
  Serial.println("Cell ID: " +  cellID);                       // Cell ID
  Serial.println("Banda de Frecuencia: " + partes[5]);           // Frequency Band
  Serial.println("PSC: " + partes[6]);                           // Primary Scrambling Code (PSC)
  Serial.println("Frecuencia de Bajada (Freq): " + partes[7]);   // Downlink Frequency
  Serial.println("SSC: " + partes[8]);                           // Secondary Scrambling Code (SSC)
  Serial.println("EC/IO: " + partes[9]);                         // EC/IO (Received Signal Code Power)
  Serial.println("RSCP: " + partes[10]);                         // RSCP (Received Signal Code Power)
  Serial.println("Calidad (Qual): " + partes[11]);               // Quality Indicator
  Serial.println("Nivel de Rx (RxLev): " + rxLev);          // Rx Level
  Serial.println("Potencia de Transmisión (TxPower): " + partes[13]); // Tx Power
  Serial.println("===================================");
  return true;
}
bool CheckConfigureAPN(String apnDeseado){
    SIM7600.println("AT+CGDCONT?");
  delay(500);
  
  String respuesta = "";
  while (SIM7600.available()) {
    respuesta += (char)SIM7600.read();
  }

  // Verificar la respuesta
  Serial.println("Respuesta de AT+CGDCONT?:");
  Serial.println(respuesta);

  // Comprobar si el APN deseado está en la respuesta
  if (respuesta.indexOf(apnDeseado) != -1) {
    Serial.print("El APN");
    Serial.println(apnDeseado);
    Serial.print("ya está configurado correctamente.");
    return true;
  } else {
    Serial.println("APN no configurado. Configurando...");
    return false;
  }
  return false;
}
void configureAPN(String apn){
  // Configura el APN en el contexto 1
  String comando = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";
  SIM7600.println(comando);
  delay(500);

  // Leer y mostrar la respuesta
  String respuesta = "";
  while (SIM7600.available()) {
    respuesta += (char)SIM7600.read();
  }
  Serial.println("Respuesta de configuración de APN:");
  Serial.println(respuesta);//si respuesta es "OK" configura red GPRS aquí con la validación o en void
  connectToGPRS();

}