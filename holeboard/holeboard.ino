#include <CD74HC4067.h>
#include <SdFat.h>
//#include <EEPROM.h>
#include <Blink.h>
#include <Flag.h>

// -----------------------------------------------------------------
// pines y clase del holeboard
#define PINEN   B10000000 // EN Pin 7
#define PINS3S0 B01111000 // S3-S0 Pines 6-3 
#define DISPLACE 3 // bits que se desplaza el canal para encajar en PINS3S0
#define PINH A0 // entrada horizontal de cada hole
#define PINV A1 // entrada vertical de cada hole
#define PINUMBRAL A2 // entrada del nivel de umbral
CD74HC4067 mux; // clase multiplexor

int percentUmbral; // porcentaje de amplitud para considerar que se activo un hole
//int outV, outH; // almacena los valores de sensor horizontal y vertical
int outVmax[CHANNEL_SIZE]; // valores maximos de sensores verticales para la calibracion
int outHmax[CHANNEL_SIZE]; // valores maximos de sensores horizontales para la calibracion

// -----------------------------------------------------------------
// pin y clase de la memoria SD
const int SD_CS = 9; // pin CS del modulo de memoria SD
uint8_t fIndex = 0; // es el valor numerico del nombre del archivo: 0 - 255
#define FSIZE 13
char fName[FSIZE+1]; // nombre del archivo en formato 8.3: 00000012.csv
char fRow[100]; // almaceno cada renglon del archivo con los datos registrados
SdFat SD; // clase SD para manejar la memoria SD
SdFile file; // clase file para manejar archivos

//-------------------------------------------------------------------
// pulsador y led de aviso
#define PINLED 2 // pin donde esta conectado el LED
Blink blink; // clase blink que maneja el parpadeo del led
#define PINSWT 8 // pin donde esta conectado el pulsador a masa
Flag swt; // con la clase Flag pregunto si se esta prsionando el pulsador

//-------------------------------------------------------------------
// variables generales
// registrar = true indica que se esta en proceso de registro, esto es
// crear arachivo, leer holeboard, blink led, etc. Si es false, se cierra
// el archivo y no se hace nada mas hasta que vuelva a estar en true
bool registrar = false;
uint32_t inicio, start, length, time; // manejo de tiempos 

//-------------------------------------------------------------------
void setup()
{
  // inicializo puerto serie. Siempre voy a enviar datos aunque no haya
  // una terminal conectada para leerlos
  delay(1000);
  Serial.begin(9600);

  // inicializo el boton de switch, con pullup interno, porque esta conectado
  // entre el pin PINSWT y masa. Le doy un delay de 100mseg para debaucing
  swt.begin(PINSWT, INPUT_PULLUP);
  swt.setFlagDelay(100);

  // inicializo la clase blink para el parpadeo del led. esta funcion asigna
  // el pin fisico donde esta soldado el led, y lo deja apagado
  blink.init(PINLED, 0);

  // siempre debe ir esta funcion despues de inicializar el puerto serie
  mux.init(PINS3S0, DISPLACE, PINEN, PINH, PINV);
 
  // verifico que este puesta la memoria SD
  if(!SD.begin(SD_CS))
  {
    // sino esta presente o hay error, aviso por puerto serie
    // y detengo el programa
    Serial.println(F("Inserte Memoria SD"));
    blink.setRate(1000);
    blink.start();
    while(!SD.begin(SD_CS)) blink.blink();
    blink.stop();
    asm volatile ("jmp 0"); // por las dudas, reinicio el sistema
  }
  Serial.println(F("Memoria SD presente"));
  
  // espero un segundo para que todo se estabilice, envio
  // un parpadeo rapido para saber que se inicio el programa
  blink.setRate(100);
  blink.start();
  inicio = millis();
  while(millis()-inicio < 1000) blink.blink();
  blink.stop();
  Serial.println(F("Dispositivo listo"));
}

//-------------------------------------------------------------------
void loop()
{
  // primero que nada leo el switch, si se pulso, puede significar que
  // se comienza un registro o que se termina. Entonces pregunto si
  // hubo cambio en el estaado del pulsador
  if(swt.isChanged() == true)
  {
    // si hubo cambio de estado, solo me interesa el estado LOW (que es
    // cuando se presiona el boton. HIGH es cuando se suelta y no interesa)
    if(swt.getState() == LOW)
    {
      // simplemente invierto el estado de "registrar" para pasar de modo
      // registrar a modo no registrar
      registrar = !registrar;
      // si ahora paso a modo registrar, inicio el blink del led para indicar
      // que se esta registrando. Si paso a modo no registrar, dejo de hacer
      // parpadear el led. Aqui tambien asgino y abro archivo de datos a registrar
      if(registrar == true)
      {
        // calibracion del holeboard. Se hace cada vez que se empieza un nuevo registro
        Serial.println(F("calibrando..."));
        
        // si no pude calibrar, reinicio el sistema
        if(!calibrarHoleboard())
        {
          asm volatile ("jmp 0");
        }

        // asingo nombre de archivo
        getNextFileName(fName);
        //fIndex = EEPROM.read(0);
        //sprintf(fName, "%08u.csv", fIndex);
        Serial.print(F("asignando archivo "));
        Serial.println(fName);

        // actualizo la eeprom para el proximo nombre de archivo
        //fIndex = fIndex + 1;
        //EEPROM.update(0, fIndex);

        // abro el archivo para almacenar datos
        if(!file.open(fName, O_RDWR | O_CREAT))
        {
          // si no puedo crear/abrir el archivo, aviso por puerto
          // serie y detengo el programa
          Serial.print(F("Error File "));
          Serial.println(fName);
          blink.setRate(500);
          blink.start();
          while(!file.open(fName, O_RDWR | O_CREAT)) blink.blink();
          blink.stop();
        }
        Serial.print(fName);
        Serial.println(F(" creado correctamente"));
        file.println(F("# hole;tiempo al inicio;duracion de cada evento"));
        sprintf(fRow, "umbral %d %%", percentUmbral);
        file.println(fRow);

        // si llegue aca es porque esta presente la memoria SD, 
        // y pude crear y abrir el nuevo archivo de datos
        Serial.println("registrando...");
        inicio = millis();
        blink.start();
      }
      else
      {
        // cierro archivo
        Serial.println("finalizando...");
        Serial.print(F("cerrando archivo "));
        Serial.println(fName);
        file.close();

        // termino el parpadeo del led que indica que ya se cerro
        // el archivo de datos y no se registra mas
        blink.stop();
      }
    }
  }
  // la funcion blink() esta presente siempre y dependiendo de start() y stop()
  // va a hacer que el led parpadee al rate que se configuro al inicio del programa
  blink.blink();

  // aqui miro el estado de registrar para saber si hago el registro o no
  if(registrar == true)
  {
    // estoy registrando
    registrarHoleboard();
  }
  else
  {
    // si no estoy haciendo nada multiplexo los canales para poder ver en el 
    // osciloscopio el nivel de cada hole
    for(uint8_t i=0; i<CHANNEL_SIZE; i++)
    {
      // recorro los 16 agujeros del holeboard
      mux.channel(i);
      delay(10);
    }
  }
}

//-------------------------------------------------------------------
// realiza la calibracion del holeboard. Si alguno de los sensores indica
// menos de 50%. Se concluye que hay error y se detiene el programa
bool calibrarHoleboard(void)
{
  int umbral;

  // leo el valor del umbral que está en el potenciometro
  umbral = analogRead(PINUMBRAL);
  percentUmbral = (float) umbral * 100.0 / 1023.0;
  Serial.print(F("Umbral "));
  Serial.print(percentUmbral);
  Serial.println("%");

  // recorro los holes y leo los valores maximos medidos en cada uno
  // si alguno esta por debajo del umbral, considero que no se puede registrar
  // y se entra en modo de alerta, titilando el led
  for(uint8_t i=0; i<CHANNEL_SIZE; i++)
  {
    // recorro los 16 agujeros del holeboard
    // si alguno de ellos disminuyo su amplitud al 500 cuentas, considero que el
    // esta registrando mal ese hole, por ejemplo, hay mucha luz ambiente
    if(!lecturaMaximosHole(i, umbral))
    {
      Serial.print(F("Error de amplitud en el hole "));
      Serial.println(i+1);
      blink.setRate(250);
      blink.start();
      while(!lecturaMaximosHole(i, umbral))
      {
        blink.blink();
        // puedo o bien ver que pasa y seguir la calibracion, o abortarla
        if(swt.isChanged() == true)
          if(swt.getState() == LOW)
          {
            blink.stop();
            return false;
          }
      }
      blink.stop();
    }
  }
  Serial.println(F("Holeboard calibrado"));
  return true;
}

//-------------------------------------------------------------------
// es el algoritmo que lee los valores de calibracion del hole board
// deberian ser cercanos al maximo valor de lectura del ADC
uint8_t lecturaMaximosHole(uint8_t iesimo, int umbral)
{
  // habilito el  i-esimo canal
  mux.channel(iesimo);

  // leo los valores de ambos sensores IR (horizontal y vertical)
  // del i-esimo hole
  outHmax[iesimo] = 0;
  outVmax[iesimo] = 0;
  for(uint8_t j=0; j<10; j++)
  {
    outHmax[iesimo] = outHmax[iesimo] + mux.readOutH();
    outVmax[iesimo] = outVmax[iesimo] + mux.readOutV();
    delay(10);
  }
  outHmax[iesimo] = outHmax[iesimo] / 10;
  outVmax[iesimo] = outVmax[iesimo] / 10;

  // si esta debajo del umbral, no puedo usar el hole
  if(outHmax[iesimo] < umbral || outVmax[iesimo] < umbral) return false;
  else return true;
}

//-------------------------------------------------------------------
// realiza la medicion del holeboard. Para ello recorre todos los agujeros
// y se fija si alguno disminuyo al 50% de su amplitud maxima para el menos
// uno de los dos sensores de cada agujero. En ese caso se considera que el
// raton metio la cabeza. Entonces se registran los datos en el archivo
void registrarHoleboard(void)
{
  static int outV, outH; // almacena los valores de sensor horizontal y vertical

  for(uint8_t i=0; i<CHANNEL_SIZE; i++)
  {
    // recorro los 16 agujeros del holeboard
    mux.channel(i);
    // leo los valores de ambos sensores IR (horizontal y vertical)
    outH = mux.readOutHpercent(outHmax[i]);
    outV = mux.readOutVpercent(outVmax[i]);
    // si alguno de ellos disminuyo su amplitud al 50%, considero que el
    // animal metio la cabeza adentro
    if(outH < percentUmbral || outV < percentUmbral)
    {
      // obtengo el momento en que metio la cabeza desde que comenzo el registro,
      // o sea, desde que se abrio el archivo para comenzar a registrar
      time = millis()-inicio;

      // ahora, mientras alguno de los sensores este por debajo del 50% me quedo
      // esperando, esto es, voy a medir cuanto tiempo esta el roedor con la cabeza
      // metida dentro del agujero
      blink.stop();
      start = millis();
      while(outH < percentUmbral || outV < percentUmbral)
      {
        outH = mux.readOutHpercent(outHmax[i]);
        outV = mux.readOutVpercent(outVmax[i]);
      }
      length = millis()-start;
      blink.start();

      // considero metidas de cabeza en el hole mayores a 10mseg
      //if(length > 10)
      //{
        // ahora armo el array de salida con la siguiente informacion:
        // nro de hole, tiempo desde el inicio del registro, duracion con la cabeza
        // dentro del agujero, amplitudes horizontal y vertical respectivamente
        sprintf(fRow, "%d;%lu;%lu", i+1, time, length);

        // almaceno la info en el archivo y tambien la envio por puerto serie
        file.println(fRow);
        Serial.println(fRow);
      //}
    }
  }
}

//-----------------------------------------------------------------------
// esta funcion cuenta cuántos archivos csv hay en la memoria SD empezando
// consecutivamente desde el 00000000.csv en adelante. Cuando uno de esos
// no lo encuentra, significa que se llego hasta el archivo anterior. Por ej
// si no existe el archivo 00000017.csv es porque en la memoria SD estan
// desde el 00000000.csv hasta el 00000016.csv. Cada vez que se borra la 
// memoria, se comienza nuevamente desde el 0000000.csv
void getNextFileName(char *fName)
{
  uint16_t c = 0;
  while(1)
  {
    // pregunto en orden creciente por 0000000 a 0000xxxx.csv
    sprintf(fName, "%08u.csv", c);
    // si existe, me fijo el siguiente archivo en orden creciente
    // si no existe, entonces devuevo ese nombre de archivo
    if(SD.exists(fName)) c++; 
    else break;              
  }
}
