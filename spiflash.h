#ifndef __SPIFLASH_H
#define __SPIFLASH_H

// Class for interfacing with a 25LQ080 8 Mbit (1 Mbyte x 8 bit) SPI NAND flash chip

typedef void (*CompletionCallback)(void *);

/**
 * Object for interfacing with a 25LQ080 8 Mbit (1 Mbyte x 8 bit) SPI NAND flash chip
 *
 * Typically you create one of these as a global object. The first object to the constructor
 * is typically SPI or SPI1, and the second is the pin for the slave select/chip select for
 * the flash SPI device.
 */
class SpiFlash {
public:
	SpiFlash(SPIClass &spi, int cs);
	virtual ~SpiFlash();

	/**
	 * Call begin, probably from setup(). The initializes the SPI object.
	 */
	void begin();

	/**
	 * Returns true if there appears to be a valid flash RAM chip on the specified SPI bus at with the
	 * specified CS pin.
	 */
	bool isValidChip();

	/**
	 * Gets the actual values for JEDEC ID read. Using isValidChip() is usually easier.
	 */
	void jedecIdRead(uint8_t &manufacturerId, uint8_t &deviceId1, uint8_t &deviceId2);

	/**
	 * Reads the status register
	 */
	uint8_t readStatus();

	/**
	 * Checks the status register and returns true if a write is in progress
	 */
	bool isWriteInProgress();

	/**
	 * Waits for any pending write operations to complete.
	 *
	 * Calls delay(1) internally, so the cloud connection will be serviced in non-threaded mode.
	 */
	void waitForWriteComplete();

	/**
	 * Writes the status register.
	 */
	void writeStatus(uint8_t status);

	/**
	 * Reads data synchronously. Reads data correctly across page boundaries.
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to read
	 */
	void readDataSync(size_t addr, void *buf, size_t bufLen);

	/**
	 * Reads data synchronously.
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to read; should be 1 <= bufLen <= 256.
	 */
	void readPageSync(size_t addr, void *buf, size_t bufLen);

	/**
	 * Reads data asynchronously and calls the callback when done
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to read; should be 1 <= bufLen <= 256.
	 * callback The function to call when done. It has the prototype:
	 * 		void callbackFn(void *param);
	 * param This is passed to the callback and is not interpeted by the SpiFlash module
	 */
	void readPageAsync(size_t addr, void *buf, size_t bufLen, CompletionCallback callback, void *param);

	/**
	 * Writes data synchronously. Can write data across page boundaries.
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to write
	 */
	void writeDataSync(size_t addr, const void *buf, size_t bufLen);

	/**
	 * Writes data synchronously.
	 *
	 * This is actually a page write, so before of the following:
	 *
	 * Pages are 256 bytes. If the write crosses a page boundary the writes continues from the beginning
	 * of these same page, not the next page! This is not the way read works, and this is probably not
	 * what you intended to do!
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to read; should be 1 <= bufLen <= 256.
	 */
	void writePageSync(size_t addr, const void *buf, size_t bufLen);

	/**
	 * Writes data asynchronously.
	 *
	 * This is actually a page write, so before of the following:
	 *
	 * Pages are 256 bytes. If the write crosses a page boundary the writes continues from the beginning
	 * of these same page, not the next page! This is not the way read works, and this is probably not
	 * what you intended to do!
	 *
	 * When the completion callback is called you can recycle buf, but the write might not actually be
	 * finished. You need to check isWriteInProgress() or use waitForWriteComplete() to know when the
	 * write is fully complete. This is done internally for the synchronous write, but because it requires
	 * polling the status register, it's not part of the async write call since it would require a timer
	 * and often you don't care if the write is still in progress.
	 *
	 * addr The address to read from
	 * buf The buffer to store data in
	 * bufLen The number of bytes to read; should be 1 <= bufLen <= 256.
	 */
	void writePageAsync(size_t addr, const void *buf, size_t bufLen, CompletionCallback callback, void *param);

	/**
	 * Erases a sector. Sectors are 4K (4096 bytes) and the smallest unit that can be erased.
	 *
	 * This call blocks (calling delay(1), so the cloud will be handled when the system thread is not used)
	 * until the writes are no longer in progress.

	 * addr Address of the beginning of the sector
	 */
	void sectorErase(size_t addr);

	/**
	 * Erases a block. Blocks are 64K (65536 bytes) or 16 sectors. There are 16 blocks on the device.
	 *
	 * This call blocks (calling delay(1), so the cloud will be handled when the system thread is not used)
	 * until the writes are no longer in progress.

	 * addr Address of the beginning of the block
	 */
	void blockErase(size_t addr);

	/**
	 * Erases the entire chip.
	 *
	 * This call blocks (calling delay(1), so the cloud will be handled when the system thread is not used)
	 * until the writes are no longer in progress. This may take a while.
	 */
	void chipErase();

	// Flags for the status register
	static const uint8_t STATUS_WIP 	= 0x01;
	static const uint8_t STATUS_WEL 	= 0x02;
	static const uint8_t STATUS_SRWD 	= 0x80;

	static const size_t PAGE_SIZE = 256;
	static const size_t SECTOR_SIZE = 4096;
	static const size_t NUM_SECTORS = 256;
	static const size_t BLOCK_SIZE = 65536;
	static const size_t NUM_BLOCKS = 16;

private:
	/**
	 * Enables writes to the status register, flash writes, and erases.
	 *
	 * This is used internally before the functions that require it.
	 */
	void writeEnable();

	/**
	 * Begins an SPI transaction, setting the CS line LOW.
	 * Also sets the SPI speed and mode settings if sharedBus == true
	 */
	void beginTransaction();

	/**
	 * Ends an SPI transaction, basically just setting the CS line high.
	 */
	void endTransaction();

	/**
	 * Sets the SPI bus speed, mode and byte order
	 *
	 * This is done in begin() normally or in beginTransaction() if sharedBus == true.
	 *
	 * The issue is that changing the bus speed and settings requires a delay for things to
	 * sync back up. If the SPI flash is the only thing on that bus, the delay is unnecessary
	 * because the speed and mode can be set during begin() instead and just left that way.
	 */
	void setSpiSettings();

	/**
	 * Sets a instruction code and an address (3-byte, 24-bit, big endian value).
	 *
	 */
	void setInstWithAddr(uint8_t inst, size_t addr, uint8_t *buf);

	/**
	 * Used internally by readPageSync() and readPageAsync()
	 */
	void readPageCommon(size_t addr, void *buf, size_t bufLen, wiring_spi_dma_transfercomplete_callback_t completion);

	/**
	 * Used internally by writePageSync() and writePageAsync()
	 */
	void writePageCommon(size_t addr, const void *buf, size_t bufLen, wiring_spi_dma_transfercomplete_callback_t completion);

	/**
	 * Used internally by readPageAsync() and writePageAsync()
	 */
	static void _completion();

	SPIClass &spi;
	int cs;
	bool sharedBus = false;
};

#endif /* __SPIFLASH_H */



