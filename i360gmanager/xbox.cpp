#include <Windows.h>
#include "xbox.h"

/**
 * XboxDisc constructer this will initialize an iso
 * @param path a string pointing to the iso
 */
XboxDisc::XboxDisc(string path)
{
	//Default values
	_hash = 0;
	_realHandle = NULL;
	_type = NO;

	//Some extra logic
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	_granularity = info.dwAllocationGranularity;

	_handle = _open(path.c_str(), _O_BINARY | _O_RDONLY);
	_mapHandle = CreateFileMapping(getHandle(), NULL, PAGE_READONLY, 0, 0, NULL);
	isValidMedia();
}

/**
 * XboxDisc destructor, this will cleanup all open stuff
 */
XboxDisc::~XboxDisc()
{
	map<uint,SectorData*>::iterator it;
	for(it = _sectors.begin(); it != _sectors.end(); it++)
		delete it->second;

	CloseHandle(_mapHandle);
	_close(_handle);  //Close the handle to the iso, (this will also close the get_osfhandle)
}

/**
 * getHandle opens up a real handle to the iso, it does its own caching so you can call this many times
 * @return HANDLE to the iso
 */
HANDLE XboxDisc::getHandle()
{
	if(_realHandle == NULL)
		_realHandle = (HANDLE)_get_osfhandle(_handle);
	return _realHandle;
}

/**
 * getMapOfFile creates a View of a file starting from address with a size
 * WARNING!!! Because of granularity the address can be off,
 * use outOffset to see how much offset you have and add that to the address.
 * @param address a uint64 address
 * @param size the size to read (this can get changed by this function)
 * @param outOffset a pointer to a uint where we can store the offset in
 * @return a pointer to the beginning of the file map
 */
void *XboxDisc::getMapOfFile(uint64 address, uint size, void *outOffset)
{
	uint offset = address % _granularity;
	address -= offset;
	size += offset; 
	DWORD high = HIDWORD(address);
	DWORD low = LODWORD(address);

	if(outOffset != NULL)
		*(uint*)outOffset = offset;

	return MapViewOfFile(_mapHandle, FILE_MAP_READ, high, low, size);
}

/**
 * saveFile will save a file, to identify what file pass a XboxFileInfo struct to this function
 * @param out string path where to save (note this will note make directories)
 * @param file a pointer to a XboxFileInfo struct
 * @returns bytes written, or -1 if it was a dir
 */
int64 XboxDisc::saveFile(string &path, XboxFileInfo *file)
{
	path.append("/");
	path.append((char*)file->name, file->length);

	if(file->isDir())
	{
		if(CreateDirectoryA(path.c_str(), NULL))
			return 0;
	}
	else
	{
		uint offset;
		DWORD written;
		
		void *data = getMapOfFile(getAddress(file->sector), file->size, &offset);
		HANDLE fileOut = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, NULL, NULL);
		if(!WriteFile(fileOut, (void*)((char*)data+offset), file->size, &written, NULL))
			written = -1;

		//Cleanup
		CloseHandle(fileOut);
		UnmapViewOfFile(data);
		return written;
	}
}

/**
 * getHash will return a murmer3 hash for this disc iso
 * @return uint hash
 */
uint XboxDisc::getHash()
{
	if(_hash != 0)    //Cache
		return _hash;

	//We read from the video offset -64 sectors to +64 sectors, why? Because currently (04-01-12) abgx uses 16 stealth sectors and we allow it to increase a lot more and
	//the game sector starts at video offset + 32 sectors so we read also 32 sectors of the game partition. So as far as i know i have covered my bases. And jeez my grammar sucks so much!
	uint size = 128*SECTOR_SIZE;
	uint address = (_type - (size/2));
	uint offset;
	//Get data pointer
	void *data = getMapOfFile(address, size, &offset);

	//Calculate hash
	_hash = 42;
	long clk = clock();
	MurmurHash3_x86_32(data, (size+offset), 42, &_hash);
	hashTime = clock()-clk;

	//Cleanup
	UnmapViewOfFile(data);
	return _hash;
}

/**
 * getDiscType returns the type of disc, see DiscType enum
 * @return DiscType
 */
DiscType XboxDisc::getDiscType()
{
	return _type;
}

/**
 * getRootAddress returns the address to the root of this disc
 * @return uint64 containing the root address
 */
uint64 XboxDisc::getRootAddress()
{
	return getAddress(getRootSector());
}

/**
 * getRootData gets/reads the data of the root sector
 * @return SectorData containing the rootsector
 */
SectorData *XboxDisc::getRootData()
{
	return getSector(getRootSector(), getRootSize());
}

/**
 * getRootSector returns the sector of this discs root
 * @return uint sector number of the root
 */
uint XboxDisc::getRootSector()
{
	return _mediaInfo.rootSector;
}

/**
 * getRootSize returns the size of this root sector
 * @return uint size of the root sector
 */
uint XboxDisc::getRootSize()
{
	return _mediaInfo.rootSize;
}

/**
 * getAddress calculates the pure address from a sector
 * @return uint64 address of the sector
 */
uint64 XboxDisc::getAddress(uint sector)
{
	return ((uint64)sector*SECTOR_SIZE)+_type;
}

/**
 * isXex checks if a XboxFile is indeed a xex file
 * @param file a pointer to a XboxFileInfo struct
 * @return True if that is a XeX file, else flase
 */
bool XboxDisc::isXex(XboxFileInfo *file)
{
	uint magic;
	_lseeki64(_handle, getAddress(file->sector), SEEK_SET);
	_read(_handle, (void*)&magic, sizeof(uint));

	return (magic == XEX_MAGIC_BYTE);
}

/**
 * getFileInfo gets a XboxFileInfo struct from a file in a sector
 * @param sector a pointer to SectorData from where to read
 * @param offset The offset from where to read the data from that sector
 * @return XboxFileInfo* struct or NULL when tried to read outside area
 */
XboxFileInfo *XboxDisc::getFileInfo(SectorData *sector, uint offset)
{
	if(sector->size <= offset)
		return NULL;           //Tried to read outside this sector that wrong!
	return (XboxFileInfo*)((char*)sector->data+offset);
}

/**
 * getSector returns the data for that sector (or reads it if it not yet has been read)
 * @param sector The sector number
 * @param size A uint defining the length of this sector
 * @return SectorData containing a pointer to data and size of the sector
 */
SectorData *XboxDisc::getSector(uint sector, uint size)
{
	SectorData *sectorData = _sectors[sector];
	if(sectorData != NULL)
	{
		if(sectorData->isInit() && sectorData->size == size)
			return sectorData;
	}

	sectorData = new SectorData();

	//Set this sector data
	sectorData->init(size);

	//Read the data
	_lseeki64(_handle, getAddress(sector), SEEK_SET);
	_read(_handle, sectorData->data, sectorData->size);

	//Push it in the cach
	_sectors[sector] = sectorData;
	return sectorData;
}

/**
 * readMagicMedia checks if we can find the MEDIA_MAGIC_BYTE string at a specefic offset defined by DiscType
 * if we found a good one it will set this DiscType to the correct type
 * @param type contains the type of disc (XSF, GDFX, XGD3)
 * @return true if valid disc, else false
 */
bool XboxDisc::readMagicMedia(DiscType type)
{
	_lseeki64(_handle, (GAME_SECTOR*SECTOR_SIZE)+type, SEEK_SET);
	_read(_handle, (char*)&_mediaInfo, sizeof(MediaInfo));
	bool isValid = isXboxMedia();
	if(isValid)
	{
		_type = type;
		if(_mediaInfo.rootSize > MAX_SECTOR)   //It has a freaking big root sector, so truncate it and hope for the best
			_mediaInfo.rootSize = MAX_SECTOR;
	}
	return isValid;
}

/**
 * isXboxMedia compares the magic bytes of the mediaInfo struct to see if this is a valid media
 * @return true if this is an valid Xbox media
 */
bool XboxDisc::isXboxMedia()
{
	return (memcmp(_mediaInfo.magic, MEDIA_MAGIC_BYTE, MEDIA_MAGIC_BYTE_SIZE) == 0);
}

/**
 * isValidMedia tries all different types disc images known to this program and if it was able to find 
 * a pre defined magic byte it will return true
 * @return true if this is a valid xbox media, else false
 */
bool XboxDisc::isValidMedia()
{
	if(_handle < 0)
		return false;                 //Valid to initalize the iso
	if(_type != NO)
		return true;                  //We already read the disc, and saw it was a good one!

	if(!readMagicMedia(XSF))          //Check for Xbox 1 disc
		if(!readMagicMedia(GDFX))     //Check for Xbox 360 disc
			if(!readMagicMedia(XGD3)) //Check for Xbox 360 XGD3 disc
				return false;         //There was no magic bytes at any of the specific offsets
	return true;
}