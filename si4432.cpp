#include "si4432.h"

#define MAX_TRANSMIT_TIMEOUT 200
// #define DEBUG

//values here are kept in khz x 10 format (for not to deal with decimals) - look at AN440 page 26 for whole table
const uint16_t IFFilterTable[][2] = { { 322, 0x26 }, { 3355, 0x88 }, { 3618, 0x89 }, { 4202, 0x8A }, { 4684, 0x8B }, {
		5188, 0x8C }, { 5770, 0x8D }, { 6207, 0x8E } };

Si4432::Si4432(uint8_t csPin, uint8_t sdnPin, uint8_t InterruptPin) :
		_csPin(csPin), _sdnPin(sdnPin), _intPin(InterruptPin), _freqCarrier(433000000), _freqChannel(0), _kbps(100), _packageSign(
				0xDEAD) { // default is 450 mhz

}

void Si4432::setFrequency(unsigned long baseFrequencyMhz) {

	if ((baseFrequencyMhz < 240) || (baseFrequencyMhz > 930))
		return; // invalid frequency

	_freqCarrier = baseFrequencyMhz;
	byte highBand = 0;
	if (baseFrequencyMhz >= 480) {
		highBand = 1;
	}

	//double fPart = (baseFrequencyMhz / (10 * (highBand + 1))) - 24;
	double fPart = ((double)baseFrequencyMhz / (10 * (highBand + 1))) - 24;
	
	uint8_t freqband = (uint8_t) fPart; // truncate the int

	uint16_t freqcarrier = (fPart - freqband) * 64000;

	// sideband is always on (0x40) :
	byte vals[3] = { 0x40 | (highBand << 5) | (freqband & 0x3F), freqcarrier >> 8, freqcarrier & 0xFF };

	BurstWrite(REG_FREQBAND, vals, 3);

}

void Si4432::setCommsSignature(uint16_t signature) {
	_packageSign = signature;

	ChangeRegister(REG_TRANSMIT_HEADER3, _packageSign >> 8); // header (signature) byte 3 val
	ChangeRegister(REG_TRANSMIT_HEADER2, (_packageSign & 0xFF)); // header (signature) byte 2 val

	ChangeRegister(REG_CHECK_HEADER3, _packageSign >> 8); // header (signature) byte 3 val for receive checks
	ChangeRegister(REG_CHECK_HEADER2, (_packageSign & 0xFF)); // header (signature) byte 2 val for receive checks

#ifdef DEBUG
	Serial.println("Package signature is set!");
#endif
}

bool Si4432::init(SPIClass* spi) {
	_spi = spi;
	
	if (_intPin != 0)
		pinMode(_intPin, INPUT);

	if(_sdnPin != 0) {
		pinMode(_sdnPin, OUTPUT);
		turnOff();
	} else delay(50);

	pinMode(_csPin, OUTPUT);
	digitalWrite(_csPin, HIGH); // set pin high, so chip would know we don't use it. - well, it's turned off anyway but...

	_spi->begin();
	//remove regacy mode
	//_spi->setBitOrder(MSBFIRST);
	//_spi->setClockDivider(SPI_CLOCK_DIV16); // 16/ 2 = 8 MHZ. Max. is 10 MHZ, so we're cool.
	//_spi->setDataMode(SPI_MODE0);

#ifdef DEBUG
	Serial.println("SPI is initialized now.");
#endif

	hardReset();
	
	// Check Sync Word
	byte syncWord3 = ReadRegister(REG_SYNC_WORD3);
	byte syncWord2 = ReadRegister(REG_SYNC_WORD2);
	byte syncWord1 = ReadRegister(REG_SYNC_WORD1);
	byte syncWord0 = ReadRegister(REG_SYNC_WORD0);
	Serial.print("syncWord3=");
	Serial.print(syncWord3, HEX);
	Serial.print(" syncWord2=");
	Serial.print(syncWord2, HEX);
	Serial.print(" syncWord1=");
	Serial.print(syncWord1, HEX);
	Serial.print(" syncWord0=");
	Serial.print(syncWord0, HEX);
	Serial.println();
	
	if (syncWord3 != 0x2D || syncWord2 != 0xD4) return false;
	return true;
}

void Si4432::boot() {
	/*
	 byte currentFix[] = { 0x80, 0x40, 0x7F };
	 BurstWrite(REG_CHARGEPUMP_OVERRIDE, currentFix, 3); // refer to AN440 for reasons

	 ChangeRegister(REG_GPIO0_CONF, 0x0F); // tx/rx data clk pin
	 ChangeRegister(REG_GPIO1_CONF, 0x00); // POR inverted pin
	 ChangeRegister(REG_GPIO2_CONF, 0x1C); // clear channel pin
	 */
	ChangeRegister(REG_AFC_TIMING_CONTROL, 0x02); // refer to AN440 for reasons
	ChangeRegister(REG_AFC_LIMITER, 0xFF); // write max value - excel file did that.
	ChangeRegister(REG_AGC_OVERRIDE, 0x60); // max gain control
	ChangeRegister(REG_AFC_LOOP_GEARSHIFT_OVERRIDE, 0x3C); // turn off AFC
	ChangeRegister(REG_DATAACCESS_CONTROL, 0xAD); // enable rx packet handling, enable tx packet handling, enable CRC, use CRC-IBM
	ChangeRegister(REG_HEADER_CONTROL1, 0x0C); // no broadcast address control, enable check headers for bytes 3 & 2
	ChangeRegister(REG_HEADER_CONTROL2, 0x22);  // enable headers byte 3 & 2, no fixed package length, sync word 3 & 2
	ChangeRegister(REG_PREAMBLE_LENGTH, 0x08); // 8 * 4 bits = 32 bits (4 bytes) preamble length
	ChangeRegister(REG_PREAMBLE_DETECTION, 0x3A); // validate 7 * 4 bits of preamble  in a package
	ChangeRegister(REG_SYNC_WORD3, 0x2D); // sync byte 3 val
	ChangeRegister(REG_SYNC_WORD2, 0xD4); // sync byte 2 val

	ChangeRegister(REG_TX_POWER, 0x1F); // max power

	ChangeRegister(REG_CHANNEL_STEPSIZE, 0x64); // each channel is of 1 Mhz interval

	setFrequency(_freqCarrier); // default freq
	setBaudRate(_kbps); // default baud rate is 100kpbs
	setChannel(_freqChannel); // default channel is 0
	setCommsSignature(_packageSign); // default signature

	switchMode(Ready);

}


bool Si4432::sendPacket(uint8_t length, const byte* data) {

	clearTxFIFO();
	ChangeRegister(REG_PKG_LEN, length);

	BurstWrite(REG_FIFO, data, length);

	ChangeRegister(REG_INT_ENABLE1, 0x04); // set interrupts on for package sent
	ChangeRegister(REG_INT_ENABLE2, 0x00); // set interrupts off for anything else
	//read interrupt registers to clean them
	ReadRegister(REG_INT_STATUS1);
	ReadRegister(REG_INT_STATUS2);

	switchMode(TXMode | Ready);

	uint64_t enterMillis = millis();
 
	while (millis() - enterMillis < MAX_TRANSMIT_TIMEOUT) {

		if ((_intPin != 0) && (digitalRead(_intPin) != 0)) {
			continue;
		}

		byte intStatus = ReadRegister(REG_INT_STATUS1);
		//ReadRegister(REG_INT_STATUS2);
#ifdef DEBUG
		Serial.print("sendPacket REG_INT_STATUS1=");
		Serial.println(intStatus, HEX);
#endif
		
		//if (intStatus & 0x04) { // Packet Sent Interrupt
		if ( (intStatus & 0x04) == 0x04) { // TX FIFO Almost Empty.
			switchMode(Ready | TuneMode);
			//Serial.println("softReset");
			//softReset(); // nop
			return true;
		} // endif
		yield();
	} // end while

	//timeout occurred.
//#ifdef DEBUG
	Serial.println("Timeout in Transit -- ");
//#endif
#if 0
	switchMode(Ready);

	if (ReadRegister(REG_DEV_STATUS) & 0x80) {
		clearFIFO();
	}
#endif

	if(_sdnPin != 0) hardReset(); // nop
	else softReset();

	return false;
}


void Si4432::getPacketReceived(uint8_t* length, byte* readData) {

	*length = ReadRegister(REG_RECEIVED_LENGTH);

	BurstRead(REG_FIFO, readData, *length);

	clearRxFIFO(); // which will also clear the interrupts
}

void Si4432::setChannel(byte channel) {

	ChangeRegister(REG_FREQCHANNEL, channel);

}

void Si4432::switchMode(byte mode) {
#ifdef DEBUG
	Serial.print("switchMode mode=0x");
	Serial.println(mode, HEX);
#endif
	ChangeRegister(REG_STATE, mode); // receive mode
#ifdef DEBUG
	byte val = ReadRegister(REG_DEV_STATUS);
	if (val == 0 || val == 0xFF) {
		Serial.print(val, HEX);
		Serial.println(" -- WHAT THE HELL!!");
	}
#endif
}

void Si4432::ChangeRegister(Registers reg, byte value) {
	BurstWrite(reg, &value, 1);

#ifdef DEBUG
	byte _value;
	BurstRead(reg, &_value, 1);
	if (value != _value) {
		Serial.println("ChangeRegister Fail");
		Serial.print("reg=0x");
		Serial.print(reg, HEX);
		Serial.print(" value=0x");
		Serial.println(value, HEX);
	}
#endif
}

void Si4432::setBaudRate(uint16_t kbps) {

	// chip normally supports very low bps values, but they are cumbersome to implement - so I just didn't implement lower bps values
	if ((kbps > 256) || (kbps < 1))
		return;
	_kbps = kbps;

	byte freqDev = kbps <= 10 ? 15 : 150;		// 15khz / 150 khz
	//byte modulationValue = _kbps < 30 ? 0x4c : 0x0c;		// use FIFO Mode, GFSK, low baud mode on / off
	byte modulationValue = _kbps < 30 ? 0x2c : 0x0c;		// use FIFO Mode, GFSK, low baud mode on / off

	byte modulationVals[] = { modulationValue, 0x23, round((freqDev * 1000.0) / 625.0) }; // msb of the kpbs to 3rd bit of register
	BurstWrite(REG_MODULATION_MODE1, modulationVals, 3); // 0x70

	// set data rate
	uint16_t bpsRegVal = round((kbps * (kbps < 30 ? 2097152 : 65536.0)) / 1000.0);
	byte datarateVals[] = { bpsRegVal >> 8, bpsRegVal & 0xFF };

	BurstWrite(REG_TX_DATARATE1, datarateVals, 2); // 0x6E

	//now set the timings
	uint16_t minBandwidth = (2 * (uint32_t) freqDev) + kbps;
#ifdef DEBUG
	Serial.print("min Bandwidth value: ");
	Serial.println(minBandwidth, HEX);
#endif
	byte IFValue = 0xff;
	//since the table is ordered (from low to high), just find the 'minimum bandwidth which is greater than required'
	for (byte i = 0; i < 8; ++i) {
		if (IFFilterTable[i][0] >= (minBandwidth * 10)) {
			IFValue = IFFilterTable[i][1];
			break;
		}
	}
#ifdef DEBUG
	Serial.print("Selected IF value: ");
	Serial.println(IFValue, HEX);
#endif

	ChangeRegister(REG_IF_FILTER_BW, IFValue);

	byte dwn3_bypass = (IFValue & 0x80) ? 1 : 0; // if msb is set
	byte ndec_exp = (IFValue >> 4) & 0x07; // only 3 bits

	uint16_t rxOversampling = round((500.0 * (1 + 2 * dwn3_bypass)) / ((pow(2, ndec_exp - 3)) * (double ) kbps));

	uint32_t ncOffset = ceil(((double) kbps * (pow(2, ndec_exp + 20))) / (500.0 * (1 + 2 * dwn3_bypass)));

	uint16_t crGain = 2 + ((65535 * (int64_t) kbps) / ((int64_t) rxOversampling * freqDev));
	byte crMultiplier = 0x00;
	if (crGain > 0x7FF) {
		crGain = 0x7FF;
	}
#ifdef DEBUG
	Serial.print("dwn3_bypass value: ");
	Serial.println(dwn3_bypass, HEX);
	Serial.print("ndec_exp value: ");
	Serial.println(ndec_exp, HEX);
	Serial.print("rxOversampling value: ");
	Serial.println(rxOversampling, HEX);
	Serial.print("ncOffset value: ");
	Serial.println(ncOffset, HEX);
	Serial.print("crGain value: ");
	Serial.println(crGain, HEX);
	Serial.print("crMultiplier value: ");
	Serial.println(crMultiplier, HEX);

#endif

	byte timingVals[] = { rxOversampling & 0x00FF, ((rxOversampling & 0x0700) >> 3) | ((ncOffset >> 16) & 0x0F),
			(ncOffset >> 8) & 0xFF, ncOffset & 0xFF, ((crGain & 0x0700) >> 8) | crMultiplier, crGain & 0xFF };

	BurstWrite(REG_CLOCK_RECOVERY_OVERSAMPLING, timingVals, 6);

}

byte Si4432::ReadRegister(Registers reg) {
	byte val[1];
	BurstRead(reg, val, 1);
	return val[0];
}

void Si4432::BurstWrite(Registers startReg, const byte value[], uint8_t length) {

	byte regVal = (byte) startReg | 0x80; // set MSB

	digitalWrite(_csPin, LOW);
	_spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
	_spi->transfer(regVal);

	for (byte i = 0; i < length; ++i) {
#ifdef DEBUG
		Serial.print("Writing: ");
		Serial.print((regVal != 0xFF ? (regVal + i) & 0x7F : 0x7F), HEX);
		Serial.print(" | ");
		Serial.println(value[i], HEX);
#endif
		_spi->transfer(value[i]);

	}

	digitalWrite(_csPin, HIGH);
	_spi->endTransaction();
}

void Si4432::BurstRead(Registers startReg, byte value[], uint8_t length) {

	byte regVal = (byte) startReg & 0x7F; // clear MSB

	digitalWrite(_csPin, LOW);
	_spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
	_spi->transfer(regVal);

	for (byte i = 0; i < length; ++i) {
		value[i] = _spi->transfer(0xFF);

#ifdef DEBUG
		Serial.print("Reading: ");
		Serial.print((regVal != 0x7F ? (regVal + i) & 0x7F : 0x7F), HEX);
		Serial.print(" | ");
		Serial.println(value[i], HEX);
#endif
	}

	digitalWrite(_csPin, HIGH);
	_spi->endTransaction();
}

void Si4432::readAll() {

	byte allValues[0x7F];

	BurstRead(REG_DEV_TYPE, allValues, 0x7F);

	for (byte i = 0; i < 0x7f; ++i) {
		Serial.print("REG(");
		Serial.print((int) REG_DEV_TYPE + i, HEX);
		Serial.print(") : ");
		Serial.println((int) allValues[i], HEX);
	}

}

void Si4432::clearTxFIFO() {
	ChangeRegister(REG_OPERATION_CONTROL, 0x01);
	ChangeRegister(REG_OPERATION_CONTROL, 0x00);
}

void Si4432::clearRxFIFO() {
	ChangeRegister(REG_OPERATION_CONTROL, 0x02);
	ChangeRegister(REG_OPERATION_CONTROL, 0x00);
}

void Si4432::clearFIFO() {
	ChangeRegister(REG_OPERATION_CONTROL, 0x03);
	ChangeRegister(REG_OPERATION_CONTROL, 0x00);
}

void Si4432::softReset() {
	ChangeRegister(REG_STATE, 0x01);
	ChangeRegister(REG_STATE, 0x80);

	byte reg = ReadRegister(REG_INT_STATUS2);
	while ((reg & 0x02) != 0x02) {
		delay(1);
		reg = ReadRegister(REG_INT_STATUS2);
	}

	boot();
}

void Si4432::hardReset() {
	// toggle Shutdown Pin
	if(_sdnPin != 0) {
		turnOff();
		turnOn();
	}

	byte reg = ReadRegister(REG_INT_STATUS2);
	while ((reg & 0x02) != 0x02) {
		delay(1);
		reg = ReadRegister(REG_INT_STATUS2);
	}

	boot();
}

void Si4432::startListening() {

	clearRxFIFO(); // clear first, so it doesn't overflow if packet is big

	ChangeRegister(REG_INT_ENABLE1, 0x03); // set interrupts on for package received and CRC error

#ifdef DEBUG
	ChangeRegister(REG_INT_ENABLE2, 0xC0);
#else
	ChangeRegister(REG_INT_ENABLE2, 0x00); // set other interrupts off
#endif
	//read interrupt registers to clean them
	ReadRegister(REG_INT_STATUS1);
	ReadRegister(REG_INT_STATUS2);

	switchMode(RXMode | Ready);
}

bool Si4432::isPacketReceived() {

	if ((_intPin != 0) && (digitalRead(_intPin) != 0)) {
		return false; // if no interrupt occurred, no packet received is assumed (since startListening will be called prior, this assumption is enough)
	}
	// check for package received status interrupt register
	byte intStat = ReadRegister(REG_INT_STATUS1);
#ifdef DEBUG
	Serial.print("isPacketReceived REG_INT_STATUS1=");
	Serial.println(REG_INT_STATUS1, HEX);
#endif

#ifdef DEBUG
	byte intStat2 = ReadRegister(REG_INT_STATUS2);

	if (intStat2 & 0x40) { //interrupt occurred, check it && read the Interrupt Status1 register for 'preamble '

		Serial.print("HEY!! HEY!! Valid Preamble detected -- ");
		Serial.println(intStat2, HEX);

	}

	if (intStat2 & 0x80) { //interrupt occurred, check it && read the Interrupt Status1 register for 'preamble '

		Serial.print("HEY!! HEY!! SYNC WORD detected -- ");
		Serial.println(intStat2, HEX);

	}
#else
	ReadRegister(REG_INT_STATUS2);
#endif

	if (intStat & 0x02) { //interrupt occurred, check it && read the Interrupt Status1 register for 'valid packet'
		switchMode(Ready | TuneMode); // if packet came, get out of Rx mode till the packet is read out. Keep PLL on for fast reaction
#ifdef DEBUG
				Serial.print("Packet detected -- ");
				Serial.println(intStat, HEX);
#endif
		return true;
	} else if (intStat & 0x01) { // packet crc error
		switchMode(Ready); // get out of Rx mode till buffers are cleared
//#ifdef DEBUG
		Serial.print("CRC Error in Packet detected!-- ");
		Serial.println(intStat, HEX);
//#endif
		clearRxFIFO();
		switchMode(RXMode | Ready); // get back to work
		return false;
	}

	//no relevant interrupt? no packet!

	return false;
}

void Si4432::turnOn() {
	digitalWrite(_sdnPin, LOW); // turn on the chip now
	delay(20);
}

void Si4432::turnOff() {
	digitalWrite(_sdnPin, HIGH); // turn off the chip now
	delay(1);
}
