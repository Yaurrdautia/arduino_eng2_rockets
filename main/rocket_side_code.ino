/*
   Main rocket progeam.
   This code is for putting on the rocket.
   command list also lives up here:
   to rocket:
    1:arm rocket
    2: disarm rocket
    3:turn on debug
    4:test deploy/deploy when not armed
    5:transferr all data
*/

//imports
//gyro
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"

//sd
#include <SD.h>

//mq135
#include <MQ135.h>

//rf
#include<RF24.h>

//servo
#include <Servo.h>

//bmp
#include <SFE_BMP180.h>

//variables and definitions and pins
//gyro
#define INTERRUPT_PIN 2
MPU6050 mpu;
#define OUTPUT_READABLE_YAWPITCHROLL
int mpuIntStatus;   // holds actual interrupt status byte from MPU
volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high

//servo
const int servopin = 0;
const int servo_rot = 90;
const int servo_init = 0;

//rocket status vars
int done_setups = 0;
bool setup_done = false;
int arm_time;
bool armed = false;

//pressure
double baseline;

//mq135
#define mq135_pin A0

//sd
int filenumber = 0;
const int chipSelect = 4;
String buff;

//sensor reading values
//data1 aka the sending data
struct data1 {
  int ypr[3];
  double alt;
  double pres;
  int flight_time;
  double tem;
  float ppm;
  int current_stat; //0= not ready 1=ready 2=transfer data ongoing 3= file transfer done 4=setup done
};

//data2 aka receive data aka control data
struct data2 {
  int message;
  bool debugmode;
};

//initializing the structures
data1 s_data;
data2 r_data;


//initializing vars
RF24 radio(10,9);
File datafile;
SFE_BMP180 pressure;
MQ135 mq_135(mq135_pin);
Servo myservo;


//functions
//gyro
void dmpDataReady() {
  mpuInterrupt = true;
}

//rf
void senddata() {
  radio.stopListening();
  radio.write(&s_data, sizeof(s_data));
  radio.startListening();
}


//sensors
void takereadings() {
  VectorFloat gravity;    // [x, y, z]            gravity vector
  float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector
  uint8_t fifoBuffer[64]; // FIFO storage buffer
  Quaternion q;           // [w, x, y, z]         quaternion container

  //ypr
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    //convvert from rad to deg
    s_data.ypr[0] = ypr[0] * 180 / M_PI;
    s_data.ypr[1] = ypr[1] * 180 / M_PI;
    s_data.ypr[2] = ypr[2] * 180 / M_PI;
  }
  double T;
  s_data.pres = getPressure();
  s_data.alt = pressure.altitude(s_data.pres, baseline);
  pressure.getTemperature(T);
  s_data.tem = T;
  float ppm = mq_135.getCorrectedPPM(T, 70.0);
  s_data.ppm = ppm;
}

//bmp
double getPressure()
{
  char status;
  double T, P, p0, a;
  status = pressure.startTemperature();
  if (status != 0)
  {
    // Wait for the measurement to complete:

    delay(status);

    // Retrieve the completed temperature measurement:
    // Note that the measurement is stored in the variable T.
    // Use '&T' to provide the address of T to the function.
    // Function returns 1 if successful, 0 if failure.

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

        // Retrieve the completed pressure measurement:
        // Note that the measurement is stored in the variable P.
        // Use '&P' to provide the address of P.
        // Note also that the function requires the previous temperature measurement (T).
        // (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
        // Function returns 1 if successful, 0 if failure.

        status = pressure.getPressure(P, T);
        if (status != 0)
        {
          return (P);
        }

      }
    }
  }
};

//servo
void deploy() {
  myservo.write(servo_rot);
  delay(200);
  myservo.write(servo_init);
}

//sd
void createnewfile() {
  while (SD.exists(String(filenumber) + ".txt")) {
    filenumber++;
  };
  datafile = SD.open(String(filenumber), FILE_WRITE);
  datafile.println("tm,pres,alt,y,p,r,tem,ppm");
};

void setup() {
  createnewfile();
  //gyro comment out serial stuff later
  myservo.attach(servopin);
  myservo.write(servo_init);
  // put your setup code here, to run once:
  Wire.begin();
  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);
  mpu.dmpInitialize();
  // supply your own gyro offsets here, scaled for min sensitivity
  mpu.setXGyroOffset(220);
  mpu.setYGyroOffset(76);
  mpu.setZGyroOffset(-85);
  mpu.setZAccelOffset(1788); // 1688 factory default for my test chip

  // Calibration Time: generate offsets and calibrate our MPU6050
  mpu.CalibrateAccel(6);
  mpu.CalibrateGyro(6);
  mpu.PrintActiveOffsets();
  // turn on the DMP, now that it's ready
  mpu.setDMPEnabled(true);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
  mpuIntStatus = mpu.getIntStatus();

  done_setups++;
  // get expected DMP packet size for later comparison
  (void)mpu.dmpGetFIFOPacketSize();

  //rf init
  if (radio.begin()) {
    done_setups++;
  }

  radio.openWritingPipe((uint64_t)(byte *)(F("02")));
  radio.openReadingPipe(1, (uint64_t)(byte *)(F("01")));
  radio.setPALevel(RF24_PA_MAX);
  //channel 35 has the least chatter on it
  radio.setChannel(35);
  radio.setDataRate(RF24_250KBPS);

  if (SD.begin(chipSelect)) {
    done_setups++;
    // don't do anything more:
  };

  if (pressure.begin())
    done_setups++;


  if (done_setups == 4) {
    setup_done = true;
    s_data.current_stat = 4;
  }
}



void loop() {

  takereadings();
  //send data only if armed is false because this clogs the data collection. when armed rocket will only listen and send nothing.
  if (armed == false) {
    senddata();
    if (radio.available()) {
      radio.read(&r_data, sizeof(r_data));
      if (r_data.message == 1) {
        armed = true;
        arm_time = millis();
        r_data.message = 999; //done to avoid doing same process twice
      }

      if (r_data.message == 3) {
        deploy();
        r_data.message = 999;
      }
      if (r_data.message == 5) {

        r_data.message = 999;
        radio.stopListening();
        s_data.current_stat = 2;
        radio.write(&s_data, sizeof(s_data)); //to notify status of rocket
        datafile = SD.open(String(filenumber) + ".txt");
        while (datafile.available()) {
          buff = datafile.readStringUntil('\n');
          radio.write(&buff, sizeof(buff));
        }
        s_data.current_stat = 3; //notify controler transfer done
      }
    }
  }
  while (armed) {
    takereadings();
    s_data.flight_time=millis()-arm_time;
    datafile = SD.open(String(filenumber) + ".txt", FILE_WRITE);
    datafile.println(String(s_data.flight_time)+","+String(s_data.pres)+","+String(s_data.alt)+","+String(s_data.ypr[0])+","+String(s_data.ypr[1])+","+String(s_data.ypr[2])+","+String(s_data.tem)+","+String(s_data.ppm));
    datafile.close();
    


    radio.read(&r_data, sizeof(r_data));
    if (r_data.message == 3) {
      deploy();
      r_data.message = 999;
    }
    if (r_data.message == 2) {
      armed = false;
      r_data.message = 999;
    }
  }
}
