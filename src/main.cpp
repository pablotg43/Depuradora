#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include "Adafruit_SHT31.h"
#include <EnvironmentCalculations.h>
#include <BME280I2C.h>



AsyncWebServer server(80);

Adafruit_SHT31 sht31 = Adafruit_SHT31();
BME280I2C bme;

int errors=0;

const char *ssid = "ESP32-Access-Point";
const char *password = "123456789";

const char *ssidhs = "ESP32-Access-Point";
const char *passwordhs = "123456789";

// Entradas Digitales
const int Entrada_configuracion = 12;
int Valor_entrada_configuracion = 0;
const int Entradas[2] = {13,14};

// Salidas Digitales
const int Salidas[1] = {15};

enum Estados {
    Reposo,
    Esperando_inicio,
    Esperando_ciclo,
    Activa_manual,
    Activa_ciclo,
    Configuracion,
};

Estados estado = Estados::Reposo;

// Inicialización Variables
unsigned long horas = 3600000;
unsigned long minutos = 60000;
unsigned long segundos = 1000;

// unsigned long horas = 60000;
// unsigned long minutos = 1000;
// unsigned long segundos = 1000;

//Temporizadores
unsigned long now = 0;
unsigned long temp_Tmedida = 0;
unsigned long temp_Tcomms = 0;
unsigned long Temp_Tinicio = 0;
unsigned long Temp_Tciclo = 0;
unsigned long Temp_Tduracion = 0;

//Tiempos
unsigned long Tmedida = 10000; //Intervalo Comunicaciones (seg)
unsigned long Tcomms = 10000; //Intervalo Comunicaciones (seg)
unsigned long Tinicio = 10000; //Tiempo espera hasta primer ciclo (mins)
unsigned long Tciclo = 10000; // Tiempo ciclo (hrs)
unsigned long Tduracion = 10000; // Tiempo duración activo (mins) 

float Temperatura=0;
float Humedad=0;
float Presion=0;
float temp=0;
float hum=0; 
float pres=0;
float dewPoint=0;
float heatIndex=0;
BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
BME280::PresUnit presUnit(BME280::PresUnit_hPa);
EnvironmentCalculations::TempUnit envTempUnit =  EnvironmentCalculations::TempUnit_Celsius;

//Ordenes
boolean ordenes[2] = {false,false};
boolean entradas[2] = {false,false};

//Varios
boolean cambio_estado=false;
String s1c[2] = {"s1", "s2"};
String s1ctext[2] = {"off", "off"};
String sa="";
String sb[5]={"","","","",""};
String sc[2]={"",""};
String sd[2]={"",""};

// Nombres equipos y señales
int numero_puerto_MQTT = 1883;
const char *nmqtt = "ptg43.mooo.com";
const char *nombre_dispositivo = "wemo_depuradora_1";
String nombre_estado = "Salidas_1";
String nombre_completo_ordenes[2] = {"Activacion","Paro"};
String nombre_completo_entrada_configuracion = "Entrada_Configuracion";
String nombre_completo_temporizadores[3] = {"Tinicio", "Tciclo", "Tduracion"};
String nombre_completo_entradas[2] = {"start", "stop"};
String nombre_completo_sensor[5] = {"Temperatura", "Humedad","Presion","Punto_Rocio","Heat_Index"};
boolean conf = false;


// Valores funcionamiento normal wifi y mqtt
WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];
int value = 0;

#include <servidor.txt>

void proximoEstado() 
{
    switch (estado) {
        case Estados::Reposo:
            if (Tciclo>0) {estado=Estados::Esperando_inicio; cambio_estado=true; Temp_Tinicio=now;}
            else if (ordenes[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
            else if (entradas[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
        break;
        case Estados::Esperando_inicio:
            if (Tciclo==0) {estado=Estados::Reposo; cambio_estado=true;}
            else if (ordenes[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
            else if (entradas[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
            else if (now - Temp_Tinicio > Tinicio) { estado=Estados::Activa_ciclo; cambio_estado=true; Temp_Tduracion=now; Temp_Tciclo=now;}
        break;
        case Estados::Esperando_ciclo:
            if (Tciclo==0) {estado=Estados::Reposo; cambio_estado=true;}
            else if (ordenes[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
            else if (entradas[0]) {estado=Estados::Activa_manual; cambio_estado=true; Temp_Tduracion=now;}
            else if (now - Temp_Tciclo > Tciclo) { estado=Estados::Activa_ciclo; cambio_estado=true; Temp_Tduracion=now; Temp_Tciclo=now;}
        break;
        case Estados::Activa_manual:
            if ((now - Temp_Tduracion > Tduracion) && (Tciclo==0) ) {estado=Estados::Reposo; cambio_estado=true;}
            else if ((now - Temp_Tduracion > Tduracion) && (Tciclo>0) ) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
            else if ((ordenes[1]) && (Tciclo==0) ) {estado=Estados::Reposo; cambio_estado=true;}
            else if ((ordenes[1]) && (Tciclo>0) ) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
            else if ((entradas[1]) && (Tciclo==0) ) {estado=Estados::Reposo; cambio_estado=true;}
            else if ((entradas[1]) && (Tciclo>0) ) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
        break;
        case Estados::Activa_ciclo:
            if (ordenes[1]) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
            else if (entradas[1]) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
            else if (now - Temp_Tduracion > Tduracion) {estado=Estados::Esperando_ciclo; cambio_estado=true;}
        break;
        case Estados::Configuracion:
        break;
    }
}

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

String readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\r\n", path);
    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory())
    {
        Serial.println("- empty file or failed to open file");
        return String();
    }
    Serial.println("- read from file:");
    String fileContent;
    while (file.available())
    {
        fileContent += String((char)file.read());
    }
    file.close();
    Serial.println(fileContent);
    return fileContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);
    File file = LittleFS.open(path, "w");
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

String processor(const String &var)
{
    Serial.print("var: ");
    Serial.println(var);

    if (var == "inputssid")
    {
        return readFile(LittleFS, "/inputssid.txt");
    }
    else if (var == "inputpassword")
    {
        return readFile(LittleFS, "/inputpassword.txt");
    }
    else if (var == "servidor_MQTT")
    {
        return readFile(LittleFS, "/servidor_MQTT.txt");
    }
    else if (var == "puerto_MQTT")
    {
        return readFile(LittleFS, "/puerto_MQTT.txt");
    }
    else if (var == "dispositivo")
    {
        return readFile(LittleFS, "/dispositivo.txt");
    }
    else if (var == "Tinicio")
    {
        return readFile(LittleFS, "/Tinicio.txt");
    }
    else if (var == "Tciclo")
    {
        return readFile(LittleFS, "/Tciclo.txt");
    }
    else if (var == "Tduracion")
    {
        return readFile(LittleFS, "/Tduracion.txt");
    }
    else if (var == "estado_wifi")
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            return "Conectada";
        }
        else
        {
            return "Desconectada";
        }
    }
    else if (var == "estado_MQTT")
    {
        if (client.connected())
        {
            return "Conectado";
        }
        else
        {
            return "Desconectado";
        }
    }
    else if (var == "Temperatura")
    {
        return (String)Temperatura;
    }
    else if (var == "Humedad")
    {
        return (String)Humedad;
    }
    else if (var == "Presion")
    {
        return (String)Presion;
    }
    else if (var == "Puntorocio")
    {
        return (String)dewPoint;
    }
    else if (var == "Heatindex")
    {
        return (String)heatIndex;
    }
    return String();
}

void setup_wifi()
{
    delay(10);

    Serial.println();
    Serial.print("Conectando a red: ");

    Serial.println(ssid);
    WiFi.hostname(nombre_dispositivo);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(250);
        errors++;
        if (errors>200) {
            errors=0;
            ESP.restart();
        }
    }
    errors=0;
    Serial.println("");
    Serial.println("WiFi Conectada");
    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());
}

void s1(int i, boolean estado)
{
    ordenes[i]=estado;
}

void connect(const String& host, int port) 
{ 
    static char pHost[64] = {0}; 
    strcpy(pHost, host.c_str()); 
    client.setServer(pHost, port); 
}

void callback(char *topic, byte *message, unsigned int length)
{
    Serial.println("");
    Serial.print("Mensaje recibido en topic: ");
    Serial.println(topic);
    Serial.println("Mensaje: ");

    String mensaje;

    for (unsigned int i = 0; i < length; i++)
    {
        Serial.print((char)message[i]);
        mensaje += (char)message[i];
    }

    if (String(topic) == nombre_completo_ordenes[0])
    {
        if (mensaje == "on")
        {
            s1(0,true);
        }

        String stopc = nombre_completo_ordenes[0] + "c";
        client.publish(stopc.c_str(), "off");
    }
    
    if (String(topic) == nombre_completo_ordenes[1])
    {
        if (mensaje == "on")
        {
            s1(1,true);
        }
        String stopc = nombre_completo_ordenes[1] + "c";
        client.publish(stopc.c_str(), "off");
    }

    if (String(topic) == nombre_completo_temporizadores[0])
    {
        int duracion = mensaje.toInt();
        String nombre = "Tinicio.txt";
        Tinicio = duracion * minutos;
        Serial.println("Tinicio: " + Tinicio);
        writeFile(LittleFS, nombre.c_str(), mensaje.c_str());
    }

    if (String(topic) == nombre_completo_temporizadores[1])
    {
        int duracion = mensaje.toInt();
        String nombre = "Tciclo.txt";
        Tciclo = duracion * horas;
        Serial.println("Tciclo: " + Tciclo);
        writeFile(LittleFS, nombre.c_str(), mensaje.c_str());    
        }

    if (String(topic) == nombre_completo_temporizadores[2])
    {
        int duracion = mensaje.toInt();
        String nombre = "Tduracion.txt";
        Tduracion = duracion * minutos;
        Serial.println("Tduracion: " + Tduracion);
        writeFile(LittleFS, nombre.c_str(), mensaje.c_str());    }
}

void reconnect()
{
    // Loop until we're reconnected

    while (!client.connected())
    {
        if ((WiFi.status() != WL_CONNECTED))
        {
            setup_wifi();
        }

        Serial.print("Conectando a servidor MQTT...");
        // Attempt to connect
        String clientId = "WemoClient-";
        clientId += String(random(0xffff), HEX);

        if (client.connect(clientId.c_str()))
        {
            Serial.println("Conectado");

            Serial.println("");
            Serial.println("Ordenes");
            for (int i = 0; i <= 1; i++)
            {
                Serial.println(nombre_completo_ordenes[i]);
            }
            for (int i = 0; i <= 1; i++)
            {
                Serial.println(nombre_completo_entradas[i]);
            }            
            for (int i = 0; i <= 2; i++)
            {
                Serial.println(nombre_completo_temporizadores[i]);
            }            
            for (int i = 0; i <= 4; i++)
            {
                Serial.println(nombre_completo_sensor[i]);
            }    


            Serial.println(nombre_estado);

            Serial.println("");

            client.subscribe(nombre_completo_ordenes[0].c_str(), 1);
            client.subscribe(nombre_completo_ordenes[1].c_str(), 1);
            client.subscribe(nombre_completo_temporizadores[0].c_str(), 1);
            client.subscribe(nombre_completo_temporizadores[1].c_str(), 1);
            client.subscribe(nombre_completo_temporizadores[2].c_str(), 1);
            errors=0;
        }

        else
        {
            Serial.print("Fallo, rc=");
            Serial.print(client.state());
            Serial.println(" reintantando en 5 segundos");
            delay(5000);
            errors+=1;
            if (errors>10) {
                errors=0;
                ESP.restart();
            }
        }
    }
}

void servidorhttp()
{

    // Send web page with input fields to client
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send_P(200, "text/html", index_html, processor); });

    // Send a GET request to <ESP_IP>/get?inputString=<inputMessage>
    server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      String inputMessage;
      // GET paramssid value on <ESP_IP>/get?inputssid=<inputMessage>
      if (request->hasParam("inputssid")) {
        inputMessage = request->getParam("inputssid")->value();
        writeFile(LittleFS, "/inputssid.txt", inputMessage.c_str());
      }
      // GET parampassword value on <ESP_IP>/get?inputpassword=<inputMessage>
      else if (request->hasParam("inputpassword")) {
        inputMessage = request->getParam("inputpassword")->value();
        writeFile(LittleFS, "/inputpassword.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("servidor_MQTT")) {
        inputMessage = request->getParam("servidor_MQTT")->value();
        writeFile(LittleFS, "/servidor_MQTT.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("puerto_MQTT")) {
        inputMessage = request->getParam("puerto_MQTT")->value();
        writeFile(LittleFS, "/puerto_MQTT.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("dispositivo")) {
        inputMessage = request->getParam("dispositivo")->value();
        writeFile(LittleFS, "/dispositivo.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("Tinicio")) {
        inputMessage = request->getParam("Tinicio")->value();
        Tinicio=inputMessage.toInt()*minutos;
        writeFile(LittleFS, "/Tinicio.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("Tciclo")) {
        inputMessage = request->getParam("Tciclo")->value();
        Tciclo=inputMessage.toInt()*horas;
        writeFile(LittleFS, "/Tciclo.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("Tduracion")) {
        inputMessage = request->getParam("Tduracion")->value();
        Tduracion=inputMessage.toInt()*minutos;
        writeFile(LittleFS, "/Tduracion.txt", inputMessage.c_str());
      } 
      else if (request->hasParam("reiniciar")) {
        inputMessage = request->getParam("reiniciar")->value();
        if (inputMessage == "1") {
          ESP.restart();
        }
      } 
      else {
        inputMessage = "No message sent";
      }
      Serial.println(inputMessage);
      request->send(200, "text/text", inputMessage); });
    server.onNotFound(notFound);
    server.begin();
}

void setup()
{
    Serial.begin(115200);

    pinMode(Entrada_configuracion, INPUT_PULLUP);

    Valor_entrada_configuracion = digitalRead(Entrada_configuracion);
    Serial.print("Estado configuracion: ");
    Serial.print(Valor_entrada_configuracion);

    // Initialize LittleFS
    if (!LittleFS.begin())
    {
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    if (Valor_entrada_configuracion == HIGH)
    {
        conf = true;
    }
    else
    {
        conf = false;
    }

    if (conf == true)
    {
        // Inicializa Hotspot
        for (int i = 0; i <= 1; i++)
        {
            pinMode(ordenes[i], OUTPUT);
            digitalWrite(ordenes[i], LOW);
        }

        WiFi.softAP(ssidhs, passwordhs);
        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(IP);

        String ssida = readFile(LittleFS, "/inputssid.txt");
        String passworda = readFile(LittleFS, "/inputpassword.txt");
        ssid = ssida.c_str();
        password = passworda.c_str();
        estado=Estados::Configuracion;
        servidorhttp();
    }

    else
    {
        String ssida = readFile(LittleFS, "/inputssid.txt");
        String passworda = readFile(LittleFS, "/inputpassword.txt");
        String servidor_MQTTa = readFile(LittleFS, "/servidor_MQTT.txt");
        String puerto_MQTTa = readFile(LittleFS, "/puerto_MQTT.txt");
        String dispositivoa = readFile(LittleFS, "/dispositivo.txt");
        String Tinicioa = readFile(LittleFS, "/Tinicio.txt");
        String Tcicloa = readFile(LittleFS, "/Tciclo.txt");
        String Tduraciona = readFile(LittleFS, "/Tduracion.txt");
 
        Tinicio = atol(Tinicioa.c_str());
        Tciclo = atol(Tcicloa.c_str());
        Tduracion = atol(Tduraciona.c_str());
        Tinicio=Tinicio*minutos;
        Tciclo=Tciclo*horas;
        Tduracion=Tduracion*minutos;


        ssid = ssida.c_str();
        password = passworda.c_str();
        int str_len = servidor_MQTTa.length() + 1;
        char n1[str_len];
        servidor_MQTTa.toCharArray(n1, str_len);

        Serial.print("n1: ");
        Serial.println(n1);
        
        numero_puerto_MQTT = puerto_MQTTa.toInt();
        nombre_dispositivo = dispositivoa.c_str();

        setup_wifi();

        nombre_completo_ordenes[0] = String(nombre_dispositivo) + "/" + "Activacion";
        nombre_completo_ordenes[1] = String(nombre_dispositivo) + "/" + "Paro";
        nombre_completo_entradas[0] = String(nombre_dispositivo) + "/" + nombre_completo_entradas[0];
        nombre_completo_entradas[1] = String(nombre_dispositivo) + "/" + nombre_completo_entradas[1];
        nombre_estado = String(nombre_dispositivo) + "/" + "estado";
        nombre_completo_temporizadores[0] = String(nombre_dispositivo) + "/" + nombre_completo_temporizadores[0];
        nombre_completo_temporizadores[1] = String(nombre_dispositivo) + "/" + nombre_completo_temporizadores[1];
        nombre_completo_temporizadores[2] = String(nombre_dispositivo) + "/" + nombre_completo_temporizadores[2];
        nombre_completo_sensor[0] = String(nombre_dispositivo) + "/" + "Temperatura";
        nombre_completo_sensor[1] = String(nombre_dispositivo) + "/" + "Humedad";
        nombre_completo_sensor[2] = String(nombre_dispositivo) + "/" + "Presion";
        nombre_completo_sensor[3] = String(nombre_dispositivo) + "/" + "Punto_Rocio";
        nombre_completo_sensor[4] = String(nombre_dispositivo) + "/" + "Heat_Index";

        connect(servidor_MQTTa, numero_puerto_MQTT);
        client.setCallback(callback);

        pinMode(Entrada_configuracion, INPUT_PULLUP);
        pinMode(Entradas[0], INPUT_PULLUP);
        pinMode(Entradas[1], INPUT_PULLUP);
        pinMode(Salidas[0], OUTPUT);
        pinMode(Salidas[1], OUTPUT);

        if (! sht31.begin(0x44)) {   
            Serial.println("Couldn't find SHT31");
        }
        else{
            Serial.println("SHT31 conectado");
        }
        
        if (!bme.begin()) {   
            Serial.println("Couldn't find BME280");
        }
        else{
            Serial.println("BME280 conectado");
        }


        
        sht31.heater(false);
        servidorhttp();
        proximoEstado();
    }
}

void comms()
{
    temp_Tcomms = now;

    unsigned long t1 = Tinicio/minutos;
    unsigned long t2 = Tciclo/horas;
    unsigned long t3 = Tduracion/minutos;

    
    String T1a=(String)t1;
    String T2a=(String)t2;
    String T3a=(String)t3;

    sa = nombre_estado + "c";
    String a="";
    String c[2]={"",""};
    String d[2]={"",""};

    for (int i = 0; i <= 2; i++)
    {
        sb[i] = nombre_completo_temporizadores[i]+ "c";
    }

    for (int i = 0; i <= 1; i++)
    {
        sc[i] = nombre_completo_entradas[i]+ "c";
        c[i]=(String)entradas[i];
    }

    for (int i = 0; i <= 1; i++)
    {
        sd[i] = nombre_completo_ordenes[i]+ "c";
        d[i]=(String)ordenes[i];

    }
    
    switch (estado) {
        case Reposo:
            a="Reposo";
        break;
        case Esperando_inicio:
            a="Esperando inicio";
        break;
        case Esperando_ciclo:
            a="Esperando ciclo";
        break;
        case Activa_manual:
            a="Activa manual";
        break;
        case Activa_ciclo:
            a="Activa ciclo";
        break;
        case Configuracion:
            a="Configuracion";
        break;
    }

    Serial.print("Inicio Reporte: ");
    Serial.println(millis());
    Serial.print("estado: ");
    Serial.println(estado);
    Serial.print("nombre_estado: ");
    Serial.println(a);
    Serial.print("errors: ");
    Serial.println(errors);
    Serial.println(" ---------------------------------");

    client.publish(sb[0].c_str(), T1a.c_str());
    client.publish(sb[1].c_str(), T2a.c_str());
    client.publish(sb[2].c_str(), T3a.c_str());
    client.publish(sc[0].c_str(), c[0].c_str());
    client.publish(sc[1].c_str(), c[1].c_str());
    client.publish(sd[0].c_str(), d[0].c_str());
    client.publish(sd[1].c_str(), d[1].c_str());
    client.publish(sa.c_str(), a.c_str());

}

void medida(){

    Tmedida=now;

    for (int i = 0; i <= 4; i++)
    {
        sb[i] = nombre_completo_sensor[i]+ "c";
    }


    bme.read(pres, temp, hum, tempUnit, presUnit);

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    h=h-7.0;
    
    float dp = EnvironmentCalculations::DewPoint(t, h, envTempUnit);
    float hi = EnvironmentCalculations::HeatIndex(t, h, envTempUnit);
    // Serial.print("Punto Rocío = "); Serial.println(dp);
    // Serial.print("HeatIndex = "); Serial.println(hi);    
    
    if (! isnan(t)) {  // check if 'is not a number'
        if ((t > Temperatura+0.3) || (t <Temperatura-0.3)){
            Temperatura=t;
            Serial.print("Temp *C = "); Serial.print(t); Serial.print("\t\t");
            String Ta=(String)Temperatura;
            client.publish(sb[0].c_str(), Ta.c_str());
        }
    } 
    else { 
        Serial.println("Failed to read temperature");
    }
  
    if (! isnan(h)) {  // check if 'is not a number'
        if ((h > Humedad+0.3) || (h <Humedad-0.3)){
            Humedad=h;
            Serial.print("Hum. % = "); Serial.println(h);
            String Ha=(String)Humedad;
            client.publish(sb[1].c_str(), Ha.c_str());
        }
    } 
    else { 
        Serial.println("Failed to read humidity");
    }

    if (! isnan(pres)) {  // check if 'is not a number'
        if ((pres > Presion+0.3) || (pres <Presion-0.3)){
            Presion=pres;
            Serial.print("Pres. % = "); Serial.println(pres);
            String Pr=(String)Presion;
            client.publish(sb[2].c_str(), Pr.c_str());
        }
    } 
    else { 
        Serial.println("Failed to read pressure");
    }

    if (! isnan(dp)) {  // check if 'is not a number'
        if ((dp > dewPoint+0.3) || (dp <dewPoint-0.3)){
            dewPoint=dp;
            Serial.print("Punto Rocío = "); Serial.println(dp);
            String dpc=(String)dewPoint;
            client.publish(sb[3].c_str(), dpc.c_str());
        }
    } 
    else { 
        Serial.println("Failed to read DP");
    }

    if (! isnan(hi)) {  // check if 'is not a number'
        if ((hi > heatIndex+0.3) || (hi <heatIndex-0.3)){
            heatIndex=hi;
            Serial.print("HeatIndex = "); Serial.println(hi);
            String hic=(String)heatIndex;
            client.publish(sb[4].c_str(), hic.c_str());
        }
    } 
    else { 
        Serial.println("Failed to read HI");
    }
}

void loop()
{
    Valor_entrada_configuracion = digitalRead(Entrada_configuracion);

    if(digitalRead(Entradas[0])==1){
        entradas[0]=false;
    }
    if(digitalRead(Entradas[0])==0){
        entradas[0]=true;
    }
    if(digitalRead(Entradas[1])==1){
        entradas[1]=false;
    }
    if(digitalRead(Entradas[1])==0){
        entradas[1]=true;
    }

    if (Valor_entrada_configuracion == true)
    {
        Serial.println("En Configuración");
        delay(1000);
    }
    else
    {
        if (!client.connected())
        {
            Serial.println("Equipo desconectado");
            reconnect();
        }
        client.loop();

        now = millis();

        proximoEstado();
     
        if (now - temp_Tcomms > Tcomms) // Lazo envío estado
        {
            medida();
            comms();
        }
                
        if (cambio_estado)
        {
            switch (estado)
            {
                case Estados::Reposo:
                {
                    digitalWrite(Salidas[0], LOW);
                    ordenes[0] = false;                    
                    ordenes[1] = false;                    
                    cambio_estado=false;
                }
                break;
                case Estados::Esperando_inicio:
                {
                    digitalWrite(Salidas[0], LOW);
                    ordenes[0] = false;                    
                    ordenes[1] = false;                    
                    cambio_estado=false;
                }
                break;
                case Estados::Esperando_ciclo:
                {
                    digitalWrite(Salidas[0], LOW);
                    ordenes[0] = false;                    
                    ordenes[1] = false;                    
                    cambio_estado=false;
                }
                break;
                case Estados::Activa_manual:
                {
                    digitalWrite(Salidas[0], HIGH);
                    ordenes[0] = false;                    
                    ordenes[1] = false;                    
                    cambio_estado=false;
                }
                break;
                case Estados::Activa_ciclo:
                {
                    digitalWrite(Salidas[0], HIGH);
                    ordenes[0] = false;                    
                    ordenes[1] = false;                    
                    cambio_estado=false;
                }
                break;
                case Estados::Configuracion:
                {

                }
                break;                
            }
            comms();
        }
    }
}
