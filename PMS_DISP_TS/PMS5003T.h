#ifndef PMS5003T_H  //PMS5003T.h
#define PMS5003T_H

//checksum check
char checkValue(unsigned char *thebuf, char leng)
{
  char receiveflag=0;
  int receiveSum=0;

  for(int i=0; i<(leng-2); i++){
  receiveSum=receiveSum+thebuf[i];
  }
  receiveSum=receiveSum + 0x42;

  if(receiveSum == ((thebuf[leng-2]<<8)+thebuf[leng-1]))  //check the serial data
  {
    receiveSum = 0;
    receiveflag = 1;
  }
  return receiveflag;
}

//transmit PM1 value
int16_t transmitPM01(unsigned char *buffer)
{
  int16_t PM01Val;
  PM01Val=((buffer[3]<<8) + buffer[4]); //count PM1.0 value of the air detector module
  return PM01Val;
}

//transmit PM2.5 value
int16_t transmitPM2_5(unsigned char *buffer)
{
  int16_t PM2_5Val;
  PM2_5Val=((buffer[5]<<8) + buffer[6]);//count PM2.5 value of the air detector module
  return PM2_5Val;
}

//transmit PM10 value
int16_t transmitPM10(unsigned char *buffer)
{
  int16_t PM10Val;
  PM10Val=((buffer[7]<<8) + buffer[8]); //count PM10 value of the air detector module
  return PM10Val;
}

//transmit Temperature value
int16_t transmitTemp(unsigned char *buffer){
  int16_t tempVal;
  tempVal=((buffer[23]<<8) + buffer[24]); //count temperature value of the air detector module
  return tempVal/10;
}

//transmit Humidity value
int16_t transmitHumid(unsigned char *buffer){
  int16_t humidVal;
  humidVal=((buffer[25]<<8) + buffer[26]); //count humidity value of the air detector module
  return humidVal/10;
}

#endif  //PMS5003T.h
