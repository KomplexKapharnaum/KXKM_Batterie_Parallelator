#include "TCA9555.h" // TCA9555 library

const byte TCA_address[8] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27}; // TCA9535 address array

// TCA9535 objects
TCA9535 TCA_0(TCA_address[0]);
TCA9535 TCA_1(TCA_address[1]);
TCA9535 TCA_2(TCA_address[2]);
TCA9535 TCA_3(TCA_address[3]);
TCA9535 TCA_4(TCA_address[4]);
TCA9535 TCA_5(TCA_address[5]);
TCA9535 TCA_6(TCA_address[6]);
TCA9535 TCA_7(TCA_address[7]);

int Nb_TCA = 0;
byte TCA_address_connected [8] = {0,0,0,0,0,0,0,0};

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
// TCA init
void setup_tca()
{
Serial.println();
  for (int i = 0; i < 8; i++)
  {
    Wire.beginTransmission(TCA_address[i]);
    delay(50);
    if (Wire.endTransmission() == 0)
    {
      Nb_TCA ++;
      TCA_address_connected[Nb_TCA-1]=TCA_address[i];

  
      if (i == 0)
      {
        TCA_0.begin();
        Serial.println("TCA_0 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_0.pinMode1(i, OUTPUT);
          TCA_0.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_0.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_0.pinMode1(i, OUTPUT);
          TCA_0.write1(i, LOW);
        }
      }
      if (i == 1)
      {
        TCA_1.begin();
        Serial.println("TCA_1 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_1.pinMode1(i, OUTPUT);
          TCA_1.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_1.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_1.pinMode1(i, OUTPUT);
          TCA_1.write1(i, LOW);
        }
      }
      if (i == 2)
      {
        TCA_2.begin();
        Serial.println("TCA_2 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_2.pinMode1(i, OUTPUT);
          TCA_2.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_2.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_2.pinMode1(i, OUTPUT);
          TCA_2.write1(i, LOW);
        }
      }
      if (i == 3)
      {
        TCA_3.begin();
        Serial.println("TCA_3 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_3.pinMode1(i, OUTPUT);
          TCA_3.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_3.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_3.pinMode1(i, OUTPUT);
          TCA_3.write1(i, LOW);
        }
      }
      if (i == 4)
      {
        TCA_4.begin();
        Serial.println("TCA_4 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_4.pinMode1(i, OUTPUT);
          TCA_4.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_4.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_4.pinMode1(i, OUTPUT);
          TCA_4.write1(i, LOW);
        }
      }
      if (i == 5)
      {
        TCA_5.begin();
        Serial.println("TCA_5 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_5.pinMode1(i, OUTPUT);
          TCA_5.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_5.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_5.pinMode1(i, OUTPUT);
          TCA_5.write1(i, LOW);
        }
      }
      if (i == 6)
      {
        TCA_6.begin();
        Serial.println("TCA_6 is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_6.pinMode1(i, OUTPUT);
          TCA_6.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_6.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_6.pinMode1(i, OUTPUT);
          TCA_6.write1(i, LOW);
        }
      }
      if (i == 7)
      {
        TCA_7.begin();
        Serial.println("TCA_7 at address " + String(TCA_address[i])+" is connected");
        for (int i = 0; i < 4; i++)
        {
          TCA_7.pinMode1(i, OUTPUT);
          TCA_7.write1(i, LOW);
        }
        for (int i = 5; i < 7; i++)
        {
          TCA_7.pinMode1(i, INPUT);
        }
        for (int i = 8; i < 16; i++)
        {
          TCA_7.pinMode1(i, OUTPUT);
          TCA_7.write1(i, LOW);
        }
      }
    }
  }
  Serial.print("found ");
  Serial.print(Nb_TCA);
  Serial.println(" devices");

}

// TCA functions read (TCA_num, pin) TCA_num is the TCA number, pin is the pin number return read value
bool TCA_read(int TCA_num, int pin) // TCA_num is the TCA number, pin is the pin number return read value
{
  bool val;
  if (TCA_num == 0)
    val = TCA_0.read1(pin);
  if (TCA_num == 1)
    val = TCA_1.read1(pin);
  if (TCA_num == 2)
    val = TCA_2.read1(pin);
  if (TCA_num == 3)
    val = TCA_3.read1(pin);
  if (TCA_num == 4)
    val = TCA_4.read1(pin);
  if (TCA_num == 5)
    val = TCA_5.read1(pin);
  if (TCA_num == 6)
    val = TCA_6.read1(pin);
  if (TCA_num == 7)
    val = TCA_7.read1(pin);
  if (TCA_num > 7)
    return false;

  Serial.print(val);
  Serial.print('\t');
  return val;
}

// TCA functions write (TCA_num, pin, value) TCA_num is the TCA number, pin is the pin number, value is the value to write
bool TCA_write(int TCA_num, int pin, bool value) // write value to pin
{
  if (TCA_num == 0)
    TCA_0.write1(pin, value);
  if (TCA_num == 1)
    TCA_1.write1(pin, value);
  if (TCA_num == 2)
    TCA_2.write1(pin, value);
  if (TCA_num == 3)
    TCA_3.write1(pin, value);
  if (TCA_num == 4)
    TCA_4.write1(pin, value);
  if (TCA_num == 5)
    TCA_5.write1(pin, value);
  if (TCA_num == 6)
    TCA_6.write1(pin, value);
  if (TCA_num == 7)
    TCA_7.write1(pin, value);
  if (TCA_num <= 7)
    return true;
  if (TCA_num > 7)
    return false;
  else
    return false;
}




void check_INA_TCA_address(){

Serial.println();
  Serial.print("found : ");
  Serial.print(Nb_INA);
  Serial.print(" INA and ");
  Serial.print(Nb_TCA);
  Serial.println(" TCA");

  if (Nb_TCA != Nb_INA/4)
  {
    Serial.println("Error : Number of TCA and INA are not correct");
    if (Nb_INA%4!=0){
      Serial.println("Error : Missing INA");
    }
    else{
      Serial.println("Error : Missing TCA");
    }

  }
  else
  {
    Serial.println("Number of TCA and INA are correct");
  }

}