
#include <PCA9534.h>
#include <Arduino.h>
#include <Wire.h>

PCA9534::PCA9534() {
	_i2caddr = 0;
	_port = 0x00;
	_invport = 0x00;
}

void PCA9534::begin(uint8_t i2caddr) {
	
	_i2caddr = i2caddr;
	
	Wire.begin();
}

uint8_t PCA9534::getIo()
{
    setCONFREG(0x3C);
	uint8_t c = Read_IP_REGISTER();   
    char buf[8];
    itoa(c,buf,2);
    setCONFREG(0xC3);
    uint8_t r = Read_IP_REGISTER();
    char bufr[8];
    itoa(r,bufr,2);
    char ris[8];
    uint8_t z = (r ^ c);  
    itoa(z,ris,2);
    return z;
    // _delay_ms(50);   
}

void PCA9534::setporteIoExp(uint8_t OPREG,uint8_t INVREG,uint8_t CONFREG) {
	
	// _i2caddr = i2caddr;
	
	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_OP_REGISTER);
	//Wire.write(0xC3);
	Wire.write(OPREG);
	Wire.endTransmission();

	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_INV_REGISTER);
	//Wire.write(0x00);
	Wire.write(INVREG);
	Wire.endTransmission();

	_delay_ms(10);
	
	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_CONF_REGISTER);
	//Wire.write(0x3C);
	Wire.write(CONFREG);
	Wire.endTransmission();

	_delay_ms(10);

}

void PCA9534::setCONFREG(uint8_t CONFREG) {
	
	//_i2caddr = i2caddr;

	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_CONF_REGISTER);
	//Wire.write(0x3C); // Identifico la Colonna
	//Wire.write(0xC3); // Identifico la riga
	Wire.write(CONFREG);
	Wire.endTransmission();
	
	//_delay_ms(10);
	
}

void PCA9534::pinMode(uint8_t pin, uint8_t mode) {
	switch (mode) {
		case OUTPUT:
		// Clear the pin on the configuration register for output
		_port &= ~(1 << pin);
		break;
		case INPUT_INVERTED:
		// Set the pin on the configuration register for input
		_port |= (1 << pin);
		// Set the pin on the polarity inversion register for inverted input
		_invport |= (1 << pin);
		break;
		case INPUT:
		default:
		// Set the pin on the configuration register for input
		_port |= (1 << pin);
		// Clear the pin on the polarity inversion register for normal input
		_invport &= ~(1 << pin);
		break;
	}

	// Write the configuration of the individual pins as inputs or outputs
	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_CONF_REGISTER);
	Wire.write(_port);
	Wire.endTransmission();

	if (mode == INPUT_INVERTED || mode == INPUT) {
		// Write the input polarity configuration of the individual pins
		Wire.beginTransmission(_i2caddr);
		Wire.write(PCA9534_INV_REGISTER);
		Wire.write(_invport);
		Wire.endTransmission();
	}
}

void PCA9534::digitalWrite(uint8_t pin, uint8_t value) {
	switch (value) {
		case HIGH:
		// Set the pin HIGH on the output register
		_port |= (1 << pin);
		break;
		case LOW:
		default:
		// Set the pin LOW on the output register
		_port &= ~(1 << pin);
		break;
	}
	// Write the status of the pins on the output register
	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_OP_REGISTER);
	Wire.write(_port);
	Wire.endTransmission();
}

uint8_t PCA9534::digitalRead(uint8_t pin) {
	uint8_t buff = 0;

	// We need to write to the input register first to get the status of the pins.
	// As per the specs: "a write transmission is sent with the command byte to
	// let the I2C device know that the Input Port register will be accessed
	// next."
	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_IP_REGISTER);
	Wire.write(_port);
	Wire.endTransmission();

	// We only need to read 1 byte of data to get the pins
	Wire.requestFrom(_i2caddr, 1);
	while (Wire.available()) {
		buff = Wire.read();
	}
	return (buff & (1 << pin)) ? HIGH : LOW;
}

uint8_t PCA9534::Read_IP_REGISTER() {
	uint8_t buff = 0;

	//_i2caddr = i2caddr;

	Wire.beginTransmission(_i2caddr);
	Wire.write(PCA9534_IP_REGISTER);
	// Wire.write(_port);
	Wire.endTransmission();

	// We only need to read 1 byte of data to get the pins
	Wire.requestFrom(_i2caddr, 1);
	while (Wire.available()) {
		buff = Wire.read();
	}
	return buff;
}