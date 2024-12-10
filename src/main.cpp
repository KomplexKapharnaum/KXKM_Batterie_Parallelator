#define I2C_Speed 50 // I2C speed in KHz
#define alert_bat_min_voltage 24000 // Battery undervoltage threshold in mV
#define alert_bat_max_voltage 30000 // Battery overvoltage threshold in mV
#define alert_bat_max_current 1    // Battery overcurrent threshold in A

#include <Arduino.h>
#include "pin_mapppings.h"
#include "INA_Func.h"
#include "TCA_Func.h"

int Nb_switch_max = 5;
int Nb_switch [16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int check_switch [16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

bool print_message = true;

long reconnect_time [16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int  reconnect_delay = 10000;

int TCA_num = 0;
int OUT_num = 0;

void setup()
{
  Serial.begin(115200);
  Wire.begin(32, 33);              // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // set I2C clock at 50 KHz
  // Find all I2C devices
  byte count = 0;
  for (byte i = 8; i < 120; i++)
  {
    Wire.beginTransmission(i);
    delay(50);
    if (Wire.endTransmission() == 0)
    {
      Serial.print("Found address: ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);
      Serial.println(")");
      count++;
      delay(1); // maybe unneeded?
    } // end of good response
  } // end of for loop
  Serial.println("Done.");
  Serial.print("Found ");
  Serial.print(count, DEC);
  Serial.println(" device(s).");




  setup_tca();
  Serial.println("TCA setup done");


    setup_ina();
  Serial.println("INA setup done");

check_INA_TCA_address();

}


void loop()
{

  
 // for (int i = 0; i < Nb_INA; i++)
  for (int i = 0; i < Nb_INA; i++)
  {
    if (print_message) Serial.printf("Reading INA %d\n", i);
    read_INA(i,print_message);
    if ((INA.getDeviceAddress(i)>=64)&&(INA.getDeviceAddress(i)<=67))
    {
    TCA_num=0;
    OUT_num=INA.getDeviceAddress(i)-64;
    }
        if ((INA.getDeviceAddress(i)>=68)&&(INA.getDeviceAddress(i)<=71))
    {
    TCA_num=1;
    OUT_num=INA.getDeviceAddress(i)-68;
    }
        if ((INA.getDeviceAddress(i)>=72)&&(INA.getDeviceAddress(i)<=75))
    {
    TCA_num=2;
    OUT_num=INA.getDeviceAddress(i)-72;
    }
        if ((INA.getDeviceAddress(i)>=76)&&(INA.getDeviceAddress(i)<=79))
    {
    TCA_num=3;
    OUT_num=INA.getDeviceAddress(i)-76;
    }
   
    /*
0 Batt switch 4
1 Batt switch 3
2 Batt switch 2
3 Batt switch 1
4 Alert 4
5 Alert 3
6 Alert 2
7 Alert 1
8 Red 1
9 Green 1
10 Red 2
11 Green 2
12 Red 3
13 Green 3
14 Red 4
15 Green 4
*/

    
        if (read_volt(i) < alert_bat_min_voltage / 1000)
        {
             if (print_message) Serial.println("Battery voltage is too low");
          TCA_write(TCA_num,  OUT_num, 0); // switch off the battery
          TCA_write(TCA_num,  OUT_num * 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
          if (check_switch[i]==0){
          Nb_switch[i]++;
          check_switch[i]=1;
          }
        }
        else if (read_volt(i) > alert_bat_max_voltage / 1000)
        {
            if (print_message)  Serial.println("Battery voltage is too high");
          TCA_write(TCA_num,  OUT_num, 0);
          TCA_write(TCA_num,  OUT_num * 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
         if (check_switch[i]==0){
          Nb_switch[i]++;
          check_switch[i]=1;
          }
        }
        else if (read_current(i) > alert_bat_max_current)
        {
            if (print_message)  Serial.println("Battery current is too high");
          TCA_write(TCA_num,  OUT_num, 0);
          TCA_write(TCA_num,  OUT_num* 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
         if (check_switch[i]==0){
          Nb_switch[i]++;
          check_switch[i]=1;
          }
        }
        else if (read_current(i) < -alert_bat_max_current)
        {
            if (print_message)  Serial.println("Battery current is too high");
          TCA_write(TCA_num,  OUT_num, 0);
          TCA_write(TCA_num,  OUT_num * 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
         if (check_switch[i]==0){
          Nb_switch[i]++;
          check_switch[i]=1;
          }
        }
        else
        { 
          check_switch[i]=0;
          
          if (Nb_switch[i]<Nb_switch_max){
          if (print_message)  Serial.println("Battery voltage and current are good");
          TCA_write(TCA_num,  OUT_num, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 8, 0);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 1);

        }
        else if (Nb_switch[i]==Nb_switch_max){

          
          if (Nb_switch[i]>=Nb_switch_max)
          {

        if (print_message)  Serial.println("too many cut off battery, try to reconnect in" + String(reconnect_delay/1000)+"s");
          TCA_write(TCA_num,  OUT_num, 0);
          TCA_write(TCA_num,  OUT_num * 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
          check_switch[i]=1;
          if (reconnect_time[i]==0){
          reconnect_time[i]=millis();
          }


           if ((millis()-reconnect_time[i]>reconnect_delay)&&(Nb_switch[i]==Nb_switch_max)){
          TCA_write(TCA_num,  OUT_num, 1); 
          TCA_write(TCA_num,  OUT_num * 2 + 8, 0);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 1);

             Nb_switch[i]++;
             check_switch[i]=0;
                  }
          }


        }
        else if (Nb_switch[i]>Nb_switch_max+1){
          if (print_message)  Serial.println("too many cut off battery, constant cut off");
          TCA_write(TCA_num,  OUT_num, 0);  
          TCA_write(TCA_num,  OUT_num * 2 + 8, 1);
          TCA_write(TCA_num,  OUT_num * 2 + 9, 0);
          check_switch[i]=1;
          
        }
        
  }
/*
  Serial.printf("Reading TCA %d\n", TCA_num);
  for (int j = 0; j < 16; j++)
  {
    Serial.printf("Pin %d: %d\n", j, TCA_read(TCA_num, j));
  }
*/
  }
  delay(500);
} // of loop