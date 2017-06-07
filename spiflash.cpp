#include "Particle.h"

#include "spiflash.h"

static SpiFlash *_spiFlash = NULL;
static CompletionCallback _callback = NULL;
static void *_callbackParam = NULL;

SpiFlash::SpiFlash(SPIClass &spi, int cs) : spi(spi), cs(cs) {

}

SpiFlash::~SpiFlash() {

}

void SpiFlash::begin() {
	spi.begin(cs);
	if (!sharedBus) {
		setSpiSettings();
	}
}

void SpiFlash::beginTransaction() {
	if (sharedBus) {
		setSpiSettings();
		// TODO: Figure out what this delay should be
		delay(1);
	}
	digitalWrite(cs, LOW);
}

void SpiFlash::endTransaction() {
	digitalWrite(cs, HIGH);
}

void SpiFlash::setSpiSettings() {
	spi.setBitOrder(MSBFIRST);
	spi.setClockSpeed(30, MHZ);
	spi.setDataMode(SPI_MODE0);
}


bool SpiFlash::isValidChip() {
	uint8_t manufacturerId = 0, deviceId1 = 0, deviceId2 = 0;

	jedecIdRead(manufacturerId, deviceId1, deviceId2);

	Serial.printlnf("manufacturerId=%02x deviceId1=%02x deviceId2=%02x", manufacturerId, deviceId1, deviceId2);

	return manufacturerId == 0x9d && deviceId1 == 0x13 && deviceId2 == 0x44;
}


void SpiFlash::jedecIdRead(uint8_t &manufacturerId, uint8_t &deviceId1, uint8_t &deviceId2) {

	uint8_t txBuf[4], rxBuf[4];
	txBuf[0] = 0x9f;

	beginTransaction();
	SPI.transfer(txBuf, rxBuf, sizeof(txBuf), NULL);
	endTransaction();

	manufacturerId = rxBuf[1];
	deviceId1 = rxBuf[2];
	deviceId2 = rxBuf[3];
}

uint8_t SpiFlash::readStatus() {
	uint8_t txBuf[2], rxBuf[2];
	txBuf[0] = 0x05; // RDSR

	beginTransaction();
	SPI.transfer(txBuf, rxBuf, sizeof(txBuf), NULL);
	endTransaction();

	return rxBuf[1];
}

bool SpiFlash::isWriteInProgress() {
	return (readStatus() & STATUS_WIP) != 0;
}

void SpiFlash::waitForWriteComplete() {
	while(isWriteInProgress()) {
		delay(1);
	}
}


void SpiFlash::writeStatus(uint8_t status) {
	waitForWriteComplete();

	uint8_t txBuf[2];
	txBuf[0] = 0x01; // WRSR
	txBuf[1] = status;

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	endTransaction();
}

void SpiFlash::readDataSync(size_t addr, void *buf, size_t bufLen) {
	uint8_t *curBuf = (uint8_t *)buf;

	while(bufLen > 0) {
		size_t pageOffset = addr % SpiFlash::PAGE_SIZE;
		size_t pageStart = addr - pageOffset;

		size_t count = (pageStart + SpiFlash::PAGE_SIZE) - addr;
		if (count > bufLen) {
			count = bufLen;
		}
		readPageSync(addr, curBuf, count);

		addr += count;
		curBuf += count;
		bufLen -= count;
	}
}

void SpiFlash::readPageSync(size_t addr, void *buf, size_t bufLen) {
	readPageCommon(addr, buf, bufLen, NULL);
	endTransaction();
}

void SpiFlash::readPageAsync(size_t addr, void *buf, size_t bufLen, CompletionCallback callback, void *param) {

	_spiFlash = this;
	_callback = callback;
	_callbackParam = param;
	readPageCommon(addr, buf, bufLen, _completion);
}

void SpiFlash::setInstWithAddr(uint8_t inst, size_t addr, uint8_t *buf) {
	buf[0] = inst;
	buf[1] = (uint8_t) (addr >> 16);
	buf[2] = (uint8_t) (addr >> 8);
	buf[3] = (uint8_t) addr;
}

void SpiFlash::readPageCommon(size_t addr, void *buf, size_t bufLen, wiring_spi_dma_transfercomplete_callback_t completion) {
	uint8_t txBuf[4];

	setInstWithAddr(0x03, addr, txBuf); // READ

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	SPI.transfer(NULL, buf, bufLen, completion);
}

void SpiFlash::writeDataSync(size_t addr, const void *buf, size_t bufLen) {
	uint8_t *curBuf = (uint8_t *)buf;

	while(bufLen > 0) {
		size_t pageOffset = addr % SpiFlash::PAGE_SIZE;
		size_t pageStart = addr - pageOffset;

		size_t count = (pageStart + SpiFlash::PAGE_SIZE) - addr;
		if (count > bufLen) {
			count = bufLen;
		}
		writePageSync(addr, curBuf, count);

		addr += count;
		curBuf += count;
		bufLen -= count;
	}

}

void SpiFlash::writePageSync(size_t addr, const void *buf, size_t bufLen) {
	waitForWriteComplete();

	writePageCommon(addr, buf, bufLen, NULL);
	endTransaction();

	waitForWriteComplete();
}

void SpiFlash::writePageAsync(size_t addr, const void *buf, size_t bufLen, CompletionCallback callback, void *param) {
	// There is no good way to asynchronously wait for pending writes to complete because it requires polling
	// the status register - there's no interrupt pin for it.
	waitForWriteComplete();

	_spiFlash = this;
	_callback = callback;
	_callbackParam = param;
	writePageCommon(addr, buf, bufLen, _completion);
}

void SpiFlash::writePageCommon(size_t addr, const void *buf, size_t bufLen, wiring_spi_dma_transfercomplete_callback_t completion) {
	uint8_t txBuf[4];

	setInstWithAddr(0x02, addr, txBuf); // PAGE_PROG

	writeEnable();

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	SPI.transfer(const_cast<void *>(buf), NULL, bufLen, completion);
}

void SpiFlash::sectorErase(size_t addr) {
	waitForWriteComplete();

	uint8_t txBuf[4];

	setInstWithAddr(0xD7, addr, txBuf); // SECTOR_ER

	writeEnable();

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	endTransaction();

	waitForWriteComplete();
}

void SpiFlash::blockErase(size_t addr) {
	waitForWriteComplete();

	uint8_t txBuf[4];

	setInstWithAddr(0xD8, addr, txBuf); // BLOCK_ER

	writeEnable();

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	endTransaction();

	waitForWriteComplete();

}

void SpiFlash::chipErase() {
	waitForWriteComplete();

	uint8_t txBuf[1];

	txBuf[0] = 0xC7; // CHIP_ER

	writeEnable();

	beginTransaction();
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	endTransaction();

	waitForWriteComplete();
}


void SpiFlash::writeEnable() {
	uint8_t txBuf[1];

	beginTransaction();
	txBuf[0] = 0x06; // WREN
	SPI.transfer(txBuf, NULL, sizeof(txBuf), NULL);
	endTransaction();

	// Write enable is always followed by a write, but CE must go high for a tres for it
	// to take effect. tres = 3 us
	delayMicroseconds(3);
}


// [static]
void SpiFlash::_completion() {
	_spiFlash->endTransaction();
	_spiFlash = NULL;

	if (_callback != NULL) {
		_callback(_callbackParam);
		_callback = NULL;
		_callbackParam = NULL;
	}
}



