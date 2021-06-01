//bibliotecile pentru senzori
#include <DHT.h> 
#include <Wire.h> 
#include <Adafruit_MLX90614.h>
#include <Adafruit_NeoPixel.h>
#include <SFE_BMP180.h>        
 
//diferenta de temperatura permisa
//Cand ventilatorul incalzeste, se opreste la o temperatura cu 2 grade mai mica 
//pentru ca apoi se incalzeste din inertie pana la temperatura dorita
// la fel si la racire
#define tempDifCaldura 2  
#define tempDifRacire 0.4
 
#define temp_covidThreshold 2   //cresterea de temperatura pentru detectarea persoanei
#define temp_covidAlert 31    //alerta peste 31 deoarece masuram temperatura palmei si nu ajunge pana la 37/38
 
#define luminiPin   6   //led
#define alarmaPin   7   //alarma foc

#define ventPin 5        //ventilator
#define calduraPin 3     //ventilator caldura
#define racirePin 4      //ventilator racire
 
#define neopixelPin 2  //pin temperatura
 
#define sensorIntrarePin 43   //senzor ostacole intrare
#define sensorIesirePin  45   //sonzor obstacole iesire
 
#define dhtPin         8   //temperatura si umiditate
#define luminaDetector  49    //senzor lumina
#define flacaraDetector 47     //senzor flacara
 
DHT dht(dhtPin, DHT22);             //senzor temperatura si umiditate
Adafruit_MLX90614 mlx = Adafruit_MLX90614();          //senzor temperatura infrarosu
Adafruit_NeoPixel pixel(1, neopixelPin, NEO_GRB + NEO_KHZ800);       //pixel temperatura
SFE_BMP180 pressure;     //senzor de presiune          
 
String command = "";    //initializarea stringului de comanda
 
unsigned long lastTick = 0;      
unsigned long lastTickTempSensor = 0;  
 
float temperaturaDorita = 23;    
bool incalzire = false;       
                 
float baselineTemp = 0;     //tempCorporala
 
bool luminaOverride = false;
bool lumina = false;
 
bool alarmaFoc = false;
 
bool iesireSensorTrigger = false;   
bool intrareSensorTrigger = false;
 
double tempAfara = 0;    
float presAfara = 0;     
 
void setup() {
   Serial.begin(9600);
   
   dht.begin();
   mlx.begin();
   pressure.begin();
   pixel.begin();
   pixel.clear();
 
   baselineTemp = mlx.readObjectTempC();
 
   pinMode(luminiPin, OUTPUT);
   pinMode(alarmaPin, OUTPUT);
 
   pinMode(ventPin, OUTPUT);
   pinMode(calduraPin, OUTPUT);
   pinMode(racirePin, OUTPUT);
 
   pinMode(luminaDetector, INPUT);
   pinMode(flacaraDetector, INPUT);
   pinMode(sensorIntrarePin, INPUT);
   pinMode(sensorIesirePin, INPUT);
 
   digitalWrite(luminiPin, HIGH);
   digitalWrite(alarmaPin, HIGH);
   digitalWrite(ventPin, HIGH);
   digitalWrite(calduraPin, HIGH);
   digitalWrite(racirePin, HIGH);
 
   if(temperaturaDorita > dht.readTemperature())
      incalzire = true;
}
 
void loop() 
{
   awaitCommand();   //asteapta comanda de la raspberry
   logic();
 
   if ((millis() - lastTick) > 3000)  //delay de 3 secunde pana cand dam comanda pentru lumina/foc
   {
      lumina = !digitalRead(luminaDetector);      //daca nu e detectata lumina, se aprinde ledul
      alarmaFoc = digitalRead(flacaraDetector);   //daca e detectata flacara, se activeaza alarma pentru incendiu
      sendInformation();         
      lastTick = millis();  //punem iar timpul pe 0
   }
}
 
void sendInformation()
{   
   //pressure.startTemperature e functie pentru masurarea presiunii pentru bmp280 
   //ia presiunea si apoi ia si temperatura   
   delay(pressure.startTemperature());    
   pressure.getTemperature(tempAfara);   
   
   //trimitem informatiile catre Node-Red despartite prin $ 
   Serial.println("0$" + String(alarmaFoc) 
                 + "$" + String(!lumina) 
                 + "$" + String(dht.readTemperature()) 
                 + "$" + String(dht.readHumidity()) 
                 + "$" + String(tempAfara) 
                 + "$" + String(getPressure())
                 + "$" + String(mlx.readAmbientTempC()));
}
 
void awaitCommand()
{
  //asculta comunicatia serial
   if (Serial.available() > 0) 
   {
      int incomingByte = Serial.read();  //asteapta un caracter de tipul #1,#2, venit de la node_red  
      command += (char)incomingByte;     
      if (command != "" && incomingByte == (int)'#')   //daca nu avem alta comanda si comanda primita este in regula
      {
         processCommand(command);      //apelam functia pentru procesarea comenzii
         command = "";                
      }
   }
}

 //pentru procesarea comenzii
void processCommand(String cmd)  //primeste un string
{
   String args[16] = "";    
   int currArg = 0, lastChar = 0;  
   for (int i = 0; i <= cmd.length(); i++)
   {
      if (cmd.charAt(i) == '$' || cmd.charAt(i) == '#')  //citind # ne dam seama ca vine o comanda si & e pentru a diferentia comenzile
      {
         args[currArg] = cmd.substring(lastChar, i);    //cauta substringul de la ultimul caracter pana la # sau $
         lastChar = i;                                  //daca gaseste un $ trece la urmatoarea valuta, daca gaseste # executa comanda de la RPi
         lastChar++;
         currArg++;
      }
   }
 
   switch (args[0].toInt())    
   {
      case 0:
         luminaOverride = args[1].toInt();  //comanda se transforma in int deoarece o pun pe 1 sau pe 0-aprinsa sau stinsa
         break;
 
      case 1:
         float tempReceived = args[1].toFloat();     //comanda se transforma in float pentru a primi gradele pe care le dorim
         if(dht.readTemperature() >= tempReceived)   //daca temperatura primita de senzor e mai mare decat cea pe care o setam in interfata
            incalzire = true;      //punem bool-ul pe true. ne ajuta mai jos la modulul peltier                
         else
            incalzire = false;
 
         temperaturaDorita = args[1].toFloat();   //temperatura dorita o schimbam in float 
         break;
   }
}
 
void logic()
{
   if ((millis() - lastTickTempSensor) > 500)   
   {
      if (!digitalRead(sensorIntrarePin) && !intrareSensorTrigger)        //trebuie sa citim temperatura intai pentru a accepta sau nu persoana
      {
         if (mlx.readObjectTempC() - baselineTemp > temp_covidThreshold)  //daca diferenta dintre temperatura acceptata si temperatura primita de senzor
         {                                                                 //e mai mare de 2 grade
            if (mlx.readObjectTempC() < temp_covidAlert)                   //si e mai mica decat temperatura setata ca fiind maxima
            {
               Serial.println("1$" + String(mlx.readObjectTempC()));       
               pixel.setPixelColor(0, pixel.Color(0, 255, 0));             //se va aprinde lumina verde
               pixel.show();
            } else {
               Serial.println("2$" + String(mlx.readObjectTempC()));
               pixel.setPixelColor(0, pixel.Color(255, 0, 0));             //altfel se va aprinde lumina rosie
               pixel.show();
            }
 
            intrareSensorTrigger = true;    //se aduna nr de persoane si ia temperatura
         }
      }
 
      if (digitalRead(sensorIntrarePin)) { 
         intrareSensorTrigger = false;        //oprirea ledului daca nu e detectata persoana
         pixel.setPixelColor(0, pixel.Color(0, 0, 0));
         pixel.show();
      }
 
      if (!digitalRead(sensorIesirePin) && !iesireSensorTrigger) {
         iesireSensorTrigger = true;          //scade nr de persoane 
         Serial.println("1$0");   
      }
 
      if (digitalRead(sensorIesirePin)) {
         iesireSensorTrigger = false;      //setare trigger pe false
      }
 
      lastTickTempSensor = millis();
   }
 
   // Aplicam override-ul de pe dashboard
   if(luminaOverride)
      lumina = false; 
 
   digitalWrite(luminiPin, lumina);
   digitalWrite(alarmaPin, alarmaFoc);
 
   //Logica Peltier
   if (dht.readTemperature() <= temperaturaDorita - tempDifCaldura && !incalzire)
     //pornirea caldurii
   {
      digitalWrite(racirePin, HIGH);
      digitalWrite(calduraPin, LOW);
      digitalWrite(ventPin, LOW);
   }
   else if (dht.readTemperature() >= temperaturaDorita + tempDifRacire && incalzire)
   {  //pornire racire
    
      digitalWrite(calduraPin, HIGH);
      digitalWrite(racirePin, LOW);
      digitalWrite(ventPin, LOW);
   } else {
    //oprire
      digitalWrite(calduraPin, HIGH);
      digitalWrite(racirePin, HIGH);
      digitalWrite(ventPin, HIGH);
   }
}
 

// BMP180 BLOB
//functia exemplu din libraria senzorului
double getPressure()
{
  char status;
  double T,P,p0,a;
 
  // You must first get a temperature measurement to perform a pressure reading.
  // Start a temperature measurement:
  // If request is successful, the number of ms to wait is returned.
  // If request is unsuccessful, 0 is returned.
 
  status = pressure.startTemperature();
  if (status != 0)
  {
    delay(status);
 
    status = pressure.getTemperature(T);
    if (status != 0)
    {
      // Start a pressure measurement:
      // The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
      // If request is successful, the number of ms to wait is returned.
      // If request is unsuccessful, 0 is returned.
 
      status = pressure.startPressure(3);
      if (status != 0)
      {
        // Wait for the measurement to complete:
        delay(status);
 
        status = pressure.getPressure(P,T);
        if (status != 0)
        {
          return(P);
        }
        else Serial.println("error retrieving pressure measurement\n");
      }
      else Serial.println("error starting pressure measurement\n");
    }
    else Serial.println("error retrieving temperature measurement\n");
  }
  else Serial.println("error starting temperature measurement\n");
}
