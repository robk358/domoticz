#include "stdafx.h"
#include "Helper.h"
#ifdef WIN32
#include "dirent_windows.h"
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#if !defined(WIN32)
#include <sys/ptrace.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <math.h>
#include <algorithm>
#include <sstream>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <chrono>
#include <limits.h>
#include <cstring>
#include <stdarg.h>
#include <locale>
#include <codecvt>
#include <random>

#if defined WIN32
#include "../msbuild/WindowsHelper.h"
#endif

#include "RFXtrx.h"
#include "../hardware/hardwaretypes.h"

// Includes for SystemUptime()
#if defined(__linux__) || defined(__linux) || defined(linux)
#include <sys/sysinfo.h>
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#include <time.h>
#include <errno.h>
#include <sys/sysctl.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <time.h>
#endif

#if defined(__FreeBSD__) 
// Check if OpenBSD or DragonFly need that at well?
#include <pthread_np.h>
#ifndef PTHREAD_MAX_MAMELEN_NP
#define PTHREAD_MAX_NAMELEN_NP 32 	// Arbitrary
#endif
#endif

namespace
{
	constexpr std::array<uint8_t, 256> crc8_tab{
		0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5, 0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d,
		0x4c, 0x1f, 0x2e, 0x43, 0x72, 0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb,
		0x98, 0xa9, 0x3e, 0x0f, 0x5c, 0x6d, 0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20,
		0x11, 0x3f, 0x0e, 0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8, 0xc5, 0xf4, 0xa7, 0x96,
		0x01, 0x30, 0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb, 0x3d,
		0x0c, 0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71,
		0x22, 0x13, 0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6, 0xa5,
		0x94, 0x03, 0x32, 0x61, 0x50, 0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e, 0x1d, 0x2c,
		0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95, 0xf8, 0xc9, 0x9a, 0xab, 0x3c,
		0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4, 0xe7, 0xd6, 0x7a, 0x4b,
		0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2, 0xa1, 0x90, 0x07, 0x36, 0x65,
		0x54, 0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc, 0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3,
		0x44, 0x75, 0x26, 0x17, 0xfc, 0xcd, 0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45,
		0x74, 0x27, 0x16, 0x81, 0xb0, 0xe3, 0xd2, 0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a,
		0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91, 0x47, 0x76, 0x25,
		0x14, 0x83, 0xb2, 0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69,
		0x04, 0x35, 0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79,
		0x48, 0x1b, 0x2a, 0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49,
		0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef, 0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77, 0x24,
		0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac
	};

	constexpr std::array<unsigned int, 256> crc32_tab{
		0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
		0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
		0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
		0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
		0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
		0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
		0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
		0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
		0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
		0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
		0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
		0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
		0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
		0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
		0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
		0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
		0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
		0x2d02ef8d
	};
} // namespace

uint8_t Crc8(uint8_t crc, const uint8_t* buf, size_t size)
{
	if (buf == nullptr)
		return 0xff;
	while (size--)
		crc = crc8_tab[crc ^ *buf++];
	return crc;
}

uint16_t crc16ccitt(const uint8_t* buf, size_t size)
{
	uint16_t crc = 0xFFFF; // Initial value
	while (size--)
	{
		crc ^= (*buf++ << 8);
		for (int i = 0; i < 8; ++i)
		{
			if (crc & 0x8000)
			{
				crc = (crc << 1) ^ 0x1021;
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	return crc;
}

unsigned int Crc32(unsigned int crc, const uint8_t* buf, size_t size)
{
	const unsigned char* p = buf;
	crc = crc ^ ~0U;
	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ ~0U;
}

uint8_t Crc8_strMQ(uint8_t crc, const uint8_t* buf, size_t size)
{
	crc = 0xff;
	if (buf == NULL)
		return crc;
	while (size--)
		crc += *buf++;
	return crc;
}

void StringSplit(std::string str, const std::string& delim, std::vector<std::string>& results)
{
	results.clear();
	size_t cutAt;
	while ((cutAt = str.find(delim)) != std::string::npos)
	{
		results.push_back(str.substr(0, cutAt));
		str = str.substr(cutAt + delim.size());
	}
	if (!str.empty())
	{
		results.push_back(str);
	}
}

uint64_t hexstrtoui64(const std::string& str)
{
	uint64_t ul;
	std::stringstream ss;
	ss << std::hex << str;
	ss >> ul;
	return ul;
}

std::string ToHexString(const uint8_t* pSource, const size_t length)
{
	if (pSource == nullptr)
		return "";
	std::string ret;
	char szTmp[10];
	size_t index = 0;
	while (index < length)
	{
		sprintf(szTmp, "0x%02X", pSource[index]);
		if (index)
			ret += " ";
		ret += szTmp;
		index++;
	}
	return ret;
}

std::vector<char> HexToBytes(const std::string& hex) {
	std::vector<char> bytes;

	if (hex.size() % 2 != 0)
		return bytes; //invalid length

	for (unsigned int i = 0; i < hex.length(); i += 2) {
		std::string byteString = hex.substr(i, 2);
		char byte = (char)strtol(byteString.c_str(), nullptr, 16);
		bytes.push_back(byte);
	}

	return bytes;
}

void stdreplace(
	std::string& inoutstring,
	const std::string& replaceWhat,
	const std::string& replaceWithWhat)
{
	size_t pos = 0;
	while (std::string::npos != (pos = inoutstring.find(replaceWhat, pos)))
	{
		inoutstring.replace(pos, replaceWhat.size(), replaceWithWhat);
		pos += replaceWithWhat.size();
	}
}

void stdreplace(
	std::wstring& inoutstring,
	const std::wstring& replaceWhat,
	const std::wstring& replaceWithWhat)
{
	size_t pos = 0;
	while (std::wstring::npos != (pos = inoutstring.find(replaceWhat, pos)))
	{
		inoutstring.replace(pos, replaceWhat.size(), replaceWithWhat);
		pos += replaceWithWhat.size();
	}
}


bool std_ends_with(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

void stdupper(std::string& inoutstring)
{
	for (char& i : inoutstring)
		i = toupper(i);
}

void stdlower(std::string& inoutstring)
{
	std::transform(inoutstring.begin(), inoutstring.end(), inoutstring.begin(), ::tolower);
}

void stdupper(std::wstring& inoutstring)
{
	for (wchar_t& i : inoutstring)
		i = towupper(i);
}

void stdlower(std::wstring& inoutstring)
{
	std::transform(inoutstring.begin(), inoutstring.end(), inoutstring.begin(), ::towlower);
}

std::vector<std::string> GetSerialPorts(bool& bUseDirectPath)
{
	bUseDirectPath = false;

	std::vector<std::string> ret;
#if defined WIN32
	//windows

	std::vector<int> ports;
	std::vector<std::string> friendlyNames;
	char szPortName[40];

	EnumSerialFromWMI(ports, friendlyNames);

	bool bFoundPort = false;
	if (!ports.empty())
	{
		bFoundPort = true;
		for (const auto& port : ports)
		{
			sprintf(szPortName, "COM%d", port);
			ret.push_back(szPortName);
		}
	}

	if (bFoundPort)
		return ret;

	for (int ii = 1; ii < 255; ii++) // checking ports from COM0 to COM255
	{
		sprintf(szPortName, "COM%d", ii);

		TCHAR lpTargetPath[200]; // buffer to store the path of the COMPORTS
		if (QueryDosDevice(szPortName, (LPSTR)lpTargetPath, sizeof(lpTargetPath)))
		{
			ret.push_back(szPortName);
		}
	}

	if (bFoundPort)
		return ret;

	typedef ULONG(__stdcall GETCOMMPORTS)(PULONG, ULONG, PULONG);
	HMODULE hDLL = LoadLibrary("api-ms-win-core-comm-l1-1-0.dll");
	if (hDLL != nullptr)
	{
		//Running Windows 10+
		GETCOMMPORTS* pGetCommPorts = reinterpret_cast<GETCOMMPORTS*>(GetProcAddress(hDLL, "GetCommPorts"));
		if (pGetCommPorts != nullptr)
		{
			std::vector<ULONG> intPorts;
			intPorts.resize(255);
			ULONG nPortNumbersFound = 0;
			const ULONG nReturn = pGetCommPorts(&(intPorts[0]), static_cast<ULONG>(intPorts.size()), &nPortNumbersFound);
			if (nReturn == ERROR_SUCCESS)
			{
				for (ULONG i = 0; i < nPortNumbersFound; i++)
				{
					sprintf(szPortName, "COM%d", intPorts[i]);
					ret.push_back(szPortName);
				}
			}
		}
		FreeLibrary(hDLL);
	}

	if (bFoundPort)
		return ret;

	/*
		//Scan old fashion way (SLOW!)
		COMMCONFIG cc;
		DWORD dwSize = sizeof(COMMCONFIG);
		for (int ii = 0; ii < 256; ii++)
		{
			sprintf(szPortName, "COM%d", ii);
			if (GetDefaultCommConfig(szPortName, &cc, &dwSize))
			{
				bFoundPort = true;
				sprintf(szPortName, "COM%d", ii);

				//Check if we did not already have it
				bool bFound = false;
				for (const auto &port : ret)
				{
					if (port == szPortName)
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
					ret.push_back(szPortName); // add port
			}
		}
		// Method 2: CreateFile, slow
		// ---------
		if (!bFoundPort) {
			for (int ii = 0; ii < 256; ii++)
			{
				sprintf(szPortName, "\\\\.\\COM%d", ii);
				bool bSuccess = false;
				HANDLE hPort = ::CreateFile(szPortName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
				if (hPort == INVALID_HANDLE_VALUE) {
					DWORD dwError = GetLastError();
					//Check to see if the error was because some other app had the port open
					if (dwError == ERROR_ACCESS_DENIED)
						bSuccess = TRUE;
				}
				else {
					//The port was opened successfully
					bSuccess = TRUE;
					//Don't forget to close the port, since we are going to do nothing with it anyway
					CloseHandle(hPort);
				}
				if (bSuccess) {
					bFoundPort = true;
					sprintf(szPortName, "COM%d", ii);
					ret.push_back(szPortName); // add port
				}
				// --------------
			}
		}
	*/
	// Method 3: EnumSerialPortsWindows, often fails
	// ---------
	if (!bFoundPort) {
		std::vector<SerialPortInfo> serialports;
		EnumSerialPortsWindows(serialports);
		if (!serialports.empty())
		{
			for (const auto& port : serialports)
			{
				ret.push_back(port.szPortName); // add port
			}
		}
	}

#else
	//scan /dev for /dev/ttyUSB* or /dev/ttyS* or /dev/tty.usbserial* or /dev/ttyAMA* or /dev/ttySAC* or /dev/ttymxc*
	//also scan /dev/serial/by-id/* on Linux

	bool bHaveTtyAMAfree = false;
	std::string sLine;
	std::ifstream infile;

	infile.open("/boot/cmdline.txt");
	if (infile.is_open())
	{
		if (!infile.eof())
		{
			getline(infile, sLine);
			bHaveTtyAMAfree = (sLine.find("ttyAMA0") == std::string::npos);
		}
	}

	DIR* d = nullptr;
	d = opendir("/dev");
	if (d != nullptr)
	{
		struct dirent* de = nullptr;
		// Loop while not nullptr
		while ((de = readdir(d)))
		{
			// Only consider character devices and symbolic links
			if ((de->d_type == DT_CHR) || (de->d_type == DT_LNK))
			{
				std::string fname = de->d_name;
				if (fname.find("ttyUSB") != std::string::npos)
				{
					ret.push_back("/dev/" + fname);
				}
				else if (fname.find("tty.usbserial") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
				else if (fname.find("ttyACM") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
				else if (fname.find("ttySAC") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
				else if (fname.find("ttymxc") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
#if defined (__FreeBSD__) || defined (__OpenBSD__) || defined (__NetBSD__)
				else if (fname.find("ttyU") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
				else if (fname.find("cuaU") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
#endif
#ifdef __APPLE__
				else if (fname.find("cu.") != std::string::npos)
				{
					bUseDirectPath = true;
					ret.push_back("/dev/" + fname);
				}
#endif
				if (bHaveTtyAMAfree)
				{
					if (fname.find("ttyAMA0") != std::string::npos)
					{
						ret.push_back("/dev/" + fname);
						bUseDirectPath = true;
					}
					// By default, this is the "small UART" on Rasberry 3 boards
					if (fname.find("ttyS0") != std::string::npos)
					{
						ret.push_back("/dev/" + fname);
						bUseDirectPath = true;
					}
					// serial0 and serial1 are new with Rasbian Jessie
					// Avoids confusion between Raspberry 2 and 3 boards
					// More info at http://spellfoundry.com/2016/05/29/configuring-gpio-serial-port-raspbian-jessie-including-pi-3/
					if (fname.find("serial") != std::string::npos)
					{
						ret.push_back("/dev/" + fname);
						bUseDirectPath = true;
					}
				}
			}
		}
		closedir(d);
	}
	//also scan in /dev/usb
	d = opendir("/dev/usb");
	if (d != nullptr)
	{
		struct dirent* de = nullptr;
		// Loop while not nullptr
		while ((de = readdir(d)))
		{
			std::string fname = de->d_name;
			if (fname.find("ttyUSB") != std::string::npos)
			{
				bUseDirectPath = true;
				ret.push_back("/dev/usb/" + fname);
			}
		}
		closedir(d);
	}

#if defined(__linux__) || defined(__linux) || defined(linux)
	d = opendir("/dev/serial/by-id");
	if (d != nullptr)
	{
		struct dirent* de = nullptr;
		// Loop while not nullptr
		while ((de = readdir(d)))
		{
			// Only consider symbolic links
			if (de->d_type == DT_LNK)
			{
				std::string fname = de->d_name;
				ret.push_back("/dev/serial/by-id/" + fname);
			}
		}
		closedir(d);
	}

#endif
#endif
	return ret;
}

bool file_exist(const char* filename)
{
	struct stat sbuffer;
	return (stat(filename, &sbuffer) == 0);
}

bool file_exist_not_empty(const char* filename)
{
	struct stat sbuffer;
	if (stat(filename, &sbuffer) != 0)
		return false;
	if (sbuffer.st_size == 0)
		return false;
	return true;
}


double CalculateAltitudeFromPressure(double pressure)
{
	double seaLevelPressure = 101325.0;
	double altitude = 44330.0 * (1.0 - pow((pressure / seaLevelPressure), 0.1903));
	return altitude;
}

/**************************************************************************/
/*!
Calculates the altitude (in meters) from the specified atmospheric
pressure (in hPa), sea-level pressure (in hPa), and temperature (in °C)
@param seaLevel Sea-level pressure in hPa
@param atmospheric Atmospheric pressure in hPa
@param temp Temperature in degrees Celsius
*/
/**************************************************************************/
float pressureToAltitude(float seaLevel, float atmospheric, float temp)
{
	/* Hyposometric formula: */
	/* */
	/* ((P0/P)^(1/5.257) - 1) * (T + 273.15) */
	/* h = ------------------------------------- */
	/* 0.0065 */
	/* */
	/* where: h = height (in meters) */
	/* P0 = sea-level pressure (in hPa) */
	/* P = atmospheric pressure (in hPa) */
	/* T = temperature (in °C) */
	return (((float)pow((seaLevel / atmospheric), 0.190223F) - 1.0F)
		* (temp + 273.15F)) / 0.0065F;
}

/**************************************************************************/
/*!
Calculates the sea-level pressure (in hPa) based on the current
altitude (in meters), atmospheric pressure (in hPa), and temperature
(in °C)
@param altitude altitude in meters
@param atmospheric Atmospheric pressure in hPa
@param temp Temperature in degrees Celsius
*/
/**************************************************************************/
float pressureSeaLevelFromAltitude(float altitude, float atmospheric, float temp)
{
	/* Sea-level pressure: */
	/* */
	/* 0.0065*h */
	/* P0 = P * (1 - ----------------- ) ^ -5.257 */
	/* T+0.0065*h+273.15 */
	/* */
	/* where: P0 = sea-level pressure (in hPa) */
	/* P = atmospheric pressure (in hPa) */
	/* h = altitude (in meters) */
	/* T = Temperature (in °C) */
	return atmospheric * (float)pow((1.0F - (0.0065F * altitude) /
		(temp + 0.0065F * altitude + 273.15F)), -5.257F);
}

//Haversine formula to calculate distance between two points

#define earthRadiusKm 6371.0

// This function converts decimal degrees to radians
double deg2rad(double deg)
{
	return (deg * 3.14159265358979323846264338327950288 / 180.0);
}

/**
 * Returns the distance between two points on the Earth.
 * Direct translation from http://en.wikipedia.org/wiki/Haversine_formula
 * @param lat1d Latitude of the first point in degrees
 * @param lon1d Longitude of the first point in degrees
 * @param lat2d Latitude of the second point in degrees
 * @param lon2d Longitude of the second point in degrees
 * @return The distance between the two points in kilometers
 */
double distanceEarth(double lat1d, double lon1d, double lat2d, double lon2d)
{
	double lat1r, lon1r, lat2r, lon2r, u, v;
	lat1r = deg2rad(lat1d);
	lon1r = deg2rad(lon1d);
	lat2r = deg2rad(lat2d);
	lon2r = deg2rad(lon2d);
	u = sin((lat2r - lat1r) / 2);
	v = sin((lon2r - lon1r) / 2);
	return 2.0 * earthRadiusKm * asin(sqrt(u * u + cos(lat1r) * cos(lat2r) * v * v));
}

// trim only the space character
std::string& stdstring_ltrim(std::string& s)
{
	return s.erase(0, s.find_first_not_of(' '));
}

std::string& stdstring_rtrim(std::string& s)
{
	return s.erase(s.find_last_not_of(' ') + 1);
}
std::string& stdstring_trim(std::string& s)
{
	return stdstring_ltrim(stdstring_rtrim(s));
}
// trim all whitespace
std::string& stdstring_ltrimws(std::string& s)
{
	return s.erase(0, s.find_first_not_of(WHITESPACE));
}
std::string& stdstring_rtrimws(std::string& s)
{
	return s.erase(s.find_last_not_of(WHITESPACE) + 1);
}
std::string& stdstring_trimws(std::string& s)
{
	return stdstring_ltrimws(stdstring_rtrimws(s));
}

double CalculateDewPoint(double temp, double humidity)
{
	if (humidity == 0)
		return temp;
	double dew_numer = 243.04 * (log(humidity / 100.0) + ((17.625 * temp) / (temp + 243.04)));
	double dew_denom = 17.625 - log(humidity / 100.0) - ((17.625 * temp) / (temp + 243.04));
	if (dew_numer == 0)
		dew_numer = 1;
	return dew_numer / dew_denom;
}

uint32_t IPToUInt(const std::string& ip)
{
	int a, b, c, d;
	uint32_t addr = 0;

	if (sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
		return 0;

	addr = a << 24;
	addr |= b << 16;
	addr |= c << 8;
	addr |= d;
	return addr;
}

bool isInt(const std::string& s)
{
	return std::all_of(s.begin(), s.end(), ::isdigit);
}

void sleep_seconds(const long seconds)
{
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void sleep_milliseconds(const long milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int createdir(const char* szDirName, int secattr)
{
	int ret = 0;
#ifdef WIN32
	ret = _mkdir(szDirName);
#else
	ret = mkdir(szDirName, secattr);
#endif
	return ret;
}

int mkdir_deep(const char* szDirName, int secattr)
{
	char DirName[260];
	DirName[0] = 0;
	const char* p = szDirName;
	char* q = DirName;
	int ret = 0;
	while (*p)
	{
		if (('\\' == *p) || ('/' == *p))
		{
			if ((p > szDirName) && (':' != *(p - 1)))
			{
				ret = createdir(DirName, secattr);
			}
		}
		*q++ = *p++;
		*q = '\0';
	}
	if (DirName[0])
	{
		ret = createdir(DirName, secattr);
	}
	return ret;
}

int RemoveDir(const std::string& dirnames, std::string& errorPath)
{
	std::vector<std::string> splitresults;
	StringSplit(dirnames, "|", splitresults);
	int returncode = 0;
	if (!splitresults.empty())
	{
#ifdef WIN32
		for (size_t i = 0; i < splitresults.size(); i++)
		{
			if (!file_exist(splitresults[i].c_str()))
				continue;
			size_t s_szLen = strlen(splitresults[i].c_str());
			if (s_szLen < MAX_PATH)
			{
				char deletePath[MAX_PATH + 1];
				strcpy_s(deletePath, splitresults[i].c_str());
				deletePath[s_szLen + 1] = '\0'; // SHFILEOPSTRUCT needs an additional null char

				SHFILEOPSTRUCT shfo
					= { nullptr, FO_DELETE, deletePath, nullptr, FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION,
						FALSE,   nullptr,	nullptr };
				if (returncode = SHFileOperation(&shfo))
				{
					errorPath = splitresults[i];
					break;
				}
			}
		}
#else
		for (auto& splitresult : splitresults)
		{
			if (!file_exist(splitresult.c_str()))
				continue;
			ExecuteCommandAndReturn("rm -rf \"" + splitresult + "\"", returncode);
			if (returncode)
			{
				errorPath = splitresult;
				break;
			}
		}
#endif
	}
	return returncode;
}

double ConvertToCelsius(const double Fahrenheit)
{
	return (Fahrenheit - 32.0) * 0.5556;
}

double ConvertToFahrenheit(const double Celsius)
{
	return (Celsius * 1.8) + 32.0;
}

double RoundDouble(const long double invalue, const short numberOfPrecisions)
{
	int64_t p = (int64_t)pow(10.0L, numberOfPrecisions);
	double ret = (int64_t)(invalue * p + 0.5L) / (double)p;
	return ret;
}

double ConvertTemperature(const double tValue, const unsigned char tSign)
{
	if (tSign == 'C')
		return tValue;
	return RoundDouble(ConvertToFahrenheit(tValue), 1);
}

std::vector<std::string> ExecuteCommandAndReturn(const std::string& szCommand, int& returncode)
{
	std::vector<std::string> ret;

	try
	{
		FILE* fp;

		/* Open the command for reading. */
#ifdef WIN32
		fp = _popen(szCommand.c_str(), "r");
#else
		fp = popen(szCommand.c_str(), "r");
#endif
		if (fp != nullptr)
		{
			char path[1035];
			/* Read the output a line at a time - output it. */
			while (fgets(path, sizeof(path) - 1, fp) != nullptr)
			{
				ret.push_back(path);
			}
			/* close */
#ifdef WIN32
			returncode = _pclose(fp);
#else
			returncode = pclose(fp);
#endif
		}
	}
	catch (...)
	{

	}
	return ret;
}

time_t GetClockTicks()
{
#ifdef CLOCK_REALTIME
	struct timespec ts;

	if (!clock_gettime(CLOCK_REALTIME, &ts))
		return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
	struct timeval tv;

	gettimeofday(&tv, nullptr);
	return(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void CurrentDateTimeMillisecond(tm& timeinfo, timeval& tv)
{
#ifdef CLOCK_REALTIME
	struct timespec ts;
	if (!clock_gettime(CLOCK_REALTIME, &ts))
	{
		tv.tv_sec = ts.tv_sec;
		tv.tv_usec = ts.tv_nsec / 1000;
	}
	else
#endif
		gettimeofday(&tv, nullptr);

#ifdef WIN32
	time_t tv_sec = tv.tv_sec;
	localtime_r(&tv_sec, &timeinfo);
#else
	localtime_r(&tv.tv_sec, &timeinfo);
#endif
}

std::string TimeToString(const time_t* ltime, const _eTimeFormat format)
{
	struct tm timeinfo;
	struct timeval tv;
	std::stringstream sstr;
	if (ltime == nullptr) // current time
		CurrentDateTimeMillisecond(timeinfo, tv);
	else
		localtime_r(ltime, &timeinfo);

	if (format > TF_Time)
	{
		//Date
		sstr << (timeinfo.tm_year + 1900) << "-"
			<< std::setw(2) << std::setfill('0') << (timeinfo.tm_mon + 1) << "-"
			<< std::setw(2) << std::setfill('0') << timeinfo.tm_mday;
	}

	if (format != TF_Date)
	{
		//Time
		if (format > TF_Time)
			sstr << " ";
		sstr
			<< std::setw(2) << std::setfill('0') << timeinfo.tm_hour << ":"
			<< std::setw(2) << std::setfill('0') << timeinfo.tm_min << ":"
			<< std::setw(2) << std::setfill('0') << timeinfo.tm_sec;
	}

	if (format > TF_DateTime && ltime == nullptr)
		sstr << "." << std::setw(3) << std::setfill('0') << ((int)tv.tv_usec / 1000);

	return sstr.str();
}

std::string GenerateMD5Hash(const std::string& InputString, const std::string& Salt)
{
	std::string cstring = InputString + Salt;
	unsigned char digest[EVP_MAX_MD_SIZE + 1];
	digest[EVP_MAX_MD_SIZE] = 0;
	unsigned int hash_length = 0;

	auto md5ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);

	EVP_DigestInit(md5ctx.get(), EVP_md5());
	EVP_DigestUpdate(md5ctx.get(), cstring.c_str(), cstring.size());
	EVP_DigestFinal(md5ctx.get(), digest, &hash_length);

	char mdString[(EVP_MAX_MD_SIZE * 2) + 1];
	mdString[EVP_MAX_MD_SIZE * 2] = 0;
	for (int i = 0; i < 16; i++)
		sprintf(&mdString[i * 2], "%02x", (unsigned int)digest[i]);

	return mdString;
}

void hsb2rgb(const float hue, const float saturation, const float vlue, int& outR, int& outG, int& outB, const double maxValue/* = 100.0 */)
{
	double      hh, p, q, t, ff;
	long        i;

	if (saturation <= 0.0) {
		outR = int(vlue * maxValue);
		outG = int(vlue * maxValue);
		outB = int(vlue * maxValue);
	}
	hh = hue;
	if (hh >= 360.0) hh = 0.0;
	hh /= 60.0;
	i = (long)hh;
	ff = hh - i;
	p = vlue * (1.0 - saturation);
	q = vlue * (1.0 - (saturation * ff));
	t = vlue * (1.0 - (saturation * (1.0 - ff)));

	switch (i) {
	case 0:
		outR = int(vlue * maxValue);
		outG = int(t * maxValue);
		outB = int(p * maxValue);
		break;
	case 1:
		outR = int(q * maxValue);
		outG = int(vlue * maxValue);
		outB = int(p * maxValue);
		break;
	case 2:
		outR = int(p * maxValue);
		outG = int(vlue * maxValue);
		outB = int(t * maxValue);
		break;

	case 3:
		outR = int(p * maxValue);
		outG = int(q * maxValue);
		outB = int(vlue * maxValue);
		break;
	case 4:
		outR = int(t * maxValue);
		outG = int(p * maxValue);
		outB = int(vlue * maxValue);
		break;
	case 5:
	default:
		outR = int(vlue * maxValue);
		outG = int(p * maxValue);
		outB = int(q * maxValue);
		break;
	}
}

void rgb2hsb(const int r, const int g, const int b, float hsbvals[3])
{
	float hue, saturation, brightness;
	if (hsbvals == nullptr)
		return;
	int cmax = (r > g) ? r : g;
	if (b > cmax) cmax = b;
	int cmin = (r < g) ? r : g;
	if (b < cmin) cmin = b;

	brightness = ((float)cmax) / 255.0F;
	if (cmax != 0)
		saturation = ((float)(cmax - cmin)) / ((float)cmax);
	else
		saturation = 0;
	if (saturation == 0)
		hue = 0;
	else {
		float redc = ((float)(cmax - r)) / ((float)(cmax - cmin));
		float greenc = ((float)(cmax - g)) / ((float)(cmax - cmin));
		float bluec = ((float)(cmax - b)) / ((float)(cmax - cmin));
		if (r == cmax)
			hue = bluec - greenc;
		else if (g == cmax)
			hue = 2.0F + redc - bluec;
		else
			hue = 4.0F + greenc - redc;
		hue = hue / 6.0F;
		if (hue < 0)
			hue = hue + 1.0F;
	}
	hsbvals[0] = hue;
	hsbvals[1] = saturation;
	hsbvals[2] = brightness;
}

bool is_number(const std::string& s)
{
	std::string::const_iterator it = s.begin();
	while (it != s.end() && (isdigit(*it) || (*it == '.') || (*it == '-') || (*it == ' ') || (*it == 0x00))) ++it;
	return !s.empty() && it == s.end();
}

void padLeft(std::string& str, const size_t num, const char paddingChar)
{
	if (num > str.size())
		str.insert(0, num - str.size(), paddingChar);
}

bool IsLightOrSwitch(const int dType, const int dSubType)
{
	bool bIsLightSwitch = false;
	switch (dType)
	{
	case pTypeLighting1:
	case pTypeLighting2:
	case pTypeLighting3:
	case pTypeLighting4:
	case pTypeLighting5:
	case pTypeLighting6:
	case pTypeFan:
	case pTypeColorSwitch:
	case pTypeSecurity1:
	case pTypeSecurity2:
	case pTypeCurtain:
	case pTypeBlinds:
	case pTypeChime:
	case pTypeRFY:
	case pTypeThermostat2:
	case pTypeThermostat3:
	case pTypeThermostat4:
	case pTypeRemote:
	case pTypeGeneralSwitch:
	case pTypeHomeConfort:
	case pTypeFS20:
	case pTypeHunter:
	case pTypeDDxxxx:
	case pTypeHoneywell_AL:
		bIsLightSwitch = true;
		break;
	case pTypeRadiator1:
		bIsLightSwitch = (dSubType == sTypeSmartwaresSwitchRadiator);
		break;
	}
	return bIsLightSwitch;
}

bool IsTemp(const int dType, const int dSubType)
{
	return (
		(dType == pTypeTEMP_HUM)
		|| (dType == pTypeTEMP_HUM_BARO)
		|| (dType == pTypeTEMP)
		|| (dType == pTypeHUM)
		|| (dType == pTypeTEMP_BARO)
		|| (dType == pTypeEvohomeZone)
		|| (dType == pTypeEvohomeWater)
		|| ((dType == pTypeWIND) && (dSubType == sTypeWIND4))
		|| ((dType == pTypeUV) && (dSubType == sTypeUV3))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeSystemTemp))
		|| (dType == pTypeThermostat1)
		|| ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorTemp))
		|| (dType == pTypeRego6XXTemp)
		);
}

bool IsWeather(const int dType, const int dSubType)
{
	return (
		(dType == pTypeWIND)
		|| (dType == pTypeRAIN)
		|| (dType == pTypeTEMP_HUM_BARO)
		|| (dType == pTypeTEMP_BARO)
		|| (dType == pTypeUV)
		|| ((dType == pTypeGeneral) && (dSubType == sTypeVisibility))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation))
		);
};

bool IsUtility(const int dType, const int dSubType)
{
	return (
		(dType == pTypeP1Power)
		|| (dType == pTypeP1Gas)
		|| ((dType == pTypeGeneral) && (dSubType == sTypeKwh))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeVoltage))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeTextStatus))
		|| ((dType == pTypeGeneral) && (dSubType == sTypePercentage))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeCounterIncremental))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeManagedCounter))
		|| ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorVolt))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeWaterflow))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeCustom))
		|| ((dType == pTypeSetpoint) && (dSubType == sTypeSetpoint))
		|| ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorAD))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeAlert))
		|| ((dType == pTypeGeneral) && (dSubType == sTypePressure))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeSoilMoisture))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeLeafWetness))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeFan))
		|| ((dType == pTypeGeneral) && (dSubType == sTypeDistance))
		|| (dType == pTypeLux)
		|| (dType == pTypeCURRENT)
		|| (dType == pTypeCURRENTENERGY)
		|| (dType == pTypeENERGY)
		|| (dType == pTypePOWER)
		|| (dType == pTypeYouLess)
		|| (dType == pTypeAirQuality)
		|| (dType == pTypeUsage)
		|| (dType == pTypeWEIGHT)
		|| (dType == pTypeRFXMeter)
		|| ((dType == pTypeRego6XXValue) && (dSubType == sTypeRego6XXCounter))
		|| ((dType == pTypeRadiator1) && (dSubType == sTypeSmartwares))
		|| ((dType == pTypeSetpoint) && (dSubType == sTypeSetpoint))
		);
}

int MStoBeaufort(const float ms)
{
	if (ms < 0.3F)
		return 0;
	if (ms < 1.5F)
		return 1;
	if (ms < 3.3F)
		return 2;
	if (ms < 5.5F)
		return 3;
	if (ms < 8.0F)
		return 4;
	if (ms < 10.8F)
		return 5;
	if (ms < 13.9F)
		return 6;
	if (ms < 17.2F)
		return 7;
	if (ms < 20.7F)
		return 8;
	if (ms < 24.5F)
		return 9;
	if (ms < 28.4F)
		return 10;
	if (ms < 32.6F)
		return 11;
	return 12;
}

void FixFolderEnding(std::string& folder)
{
#if defined(WIN32)
	if (folder.at(folder.length() - 1) != '\\')
		folder += "\\";
#else
	if (folder.at(folder.length() - 1) != '/')
		folder += "/";
#endif
}

bool dirent_is_directory(const std::string& dir, struct dirent* ent)
{
	if (ent->d_type == DT_DIR)
		return true;
#ifndef WIN32
	if (ent->d_type == DT_LNK)
		return true;
	if (ent->d_type == DT_UNKNOWN) {
		std::string fname = dir + "/" + ent->d_name;
		struct stat st;
		if (!stat(fname.c_str(), &st))
			return S_ISDIR(st.st_mode);
	}
#endif
	return false;
}

bool dirent_is_file(const std::string& dir, struct dirent* ent)
{
	if (ent->d_type == DT_REG)
		return true;
#ifndef WIN32
	if (ent->d_type == DT_UNKNOWN) {
		std::string fname = dir + "/" + ent->d_name;
		struct stat st;
		if (!stat(fname.c_str(), &st))
			return S_ISREG(st.st_mode);
	}
#endif
	return false;
}

/*!
 * List entries of a directory.
 * @param entries A string vector containing the result
 * @param dir Target directory for listing
 * @param bInclDirs Boolean flag to include directories in the result
 * @param bInclFiles Boolean flag to include regular files in the result
 */
void DirectoryListing(std::vector<std::string>& entries, const std::string& dir, bool bInclDirs, bool bInclFiles)
{
	DIR* d = nullptr;
	struct dirent* ent;
	if ((d = opendir(dir.c_str())) != nullptr)
	{
		while ((ent = readdir(d)) != nullptr)
		{
			std::string name = ent->d_name;
			if (bInclDirs && dirent_is_directory(dir, ent) && name != "." && name != "..") {
				entries.push_back(name);
				continue;
			}
			if (bInclFiles && dirent_is_file(dir, ent)) {
				entries.push_back(name);
				continue;
			}
		}
		closedir(d);
	}
}

std::string GenerateUserAgent()
{
	std::random_device rd;  // a seed source for the random number engine
	std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
	std::uniform_int_distribution<> distrib_FFFF(0, 0xFFFF);

	int cversion = distrib_FFFF(gen) % 0xFFFF;
	int mversion = distrib_FFFF(gen) % 3;
	int sversion = distrib_FFFF(gen) % 3;
	std::stringstream sstr;
	sstr << "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/" << (601 + sversion) << "." << (36 + mversion) << " (KHTML, like Gecko) Chrome/" << (53 + mversion) << ".0." << cversion << ".0 Safari/" << (601 + sversion) << "." << (36 + sversion);
	return sstr.str();
}

std::string MakeHtml(const std::string& txt)
{
	std::string sRet = txt;

	stdreplace(sRet, "&", "&amp;");
	stdreplace(sRet, "\"", "&quot;");
	stdreplace(sRet, "'", "&apos;");
	stdreplace(sRet, "<", "&lt;");
	stdreplace(sRet, ">", "&gt;");
	stdreplace(sRet, "\r\n", "<br/>");
	return sRet;
}

//Prevent against XSS (Cross Site Scripting)
std::string SafeHtml(const std::string& txt)
{
	std::string sRet = txt;

	stdreplace(sRet, "\"", "&quot;");
	stdreplace(sRet, "'", "&apos;");
	stdreplace(sRet, "<", "&lt;");
	stdreplace(sRet, ">", "&gt;");
	return sRet;
}

#if defined WIN32
//FILETIME of Jan 1 1970 00:00:00
static const uint64_t epoch = (const uint64_t)(116444736000000000);

int gettimeofday(timeval* tp, void* tzp)
{
	FILETIME    file_time;
	SYSTEMTIME  system_time;
	ULARGE_INTEGER ularge;
	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	ularge.LowPart = file_time.dwLowDateTime;
	ularge.HighPart = file_time.dwHighDateTime;
	tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}
#endif

int getclock(struct timeval* tv) {
#ifdef CLOCK_MONOTONIC
	struct timespec ts;
	if (!clock_gettime(CLOCK_MONOTONIC, &ts)) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
		return 0;
	}
#endif
	return gettimeofday(tv, nullptr);
}
int timeval_subtract(struct timeval* result, struct timeval* x, struct timeval* y) {
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait.
	   tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

namespace
{
	constexpr std::array<const char*, 13> szInsecureArgumentOptions{ "import", "socket", "process", "os", "|", ";", "&", "$", "<", ">", "`", "\n", "\r" };
} // namespace

bool IsArgumentSecure(const std::string& arg)
{
	std::string larg(arg);
	std::transform(larg.begin(), larg.end(), larg.begin(), ::tolower);
	return std::all_of(szInsecureArgumentOptions.begin(), szInsecureArgumentOptions.end(), [&](const char* arg) { return larg.find(arg) == std::string::npos; });
}

uint32_t SystemUptime()
{
#if defined(WIN32)
	return static_cast<uint32_t>(GetTickCount64() / 1000u);
#elif defined(__linux__) || defined(__linux) || defined(linux)
	struct sysinfo info;
	if (sysinfo(&info) != 0)
		return -1;
	return info.uptime;
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
	struct timeval boottime;
	std::size_t len = sizeof(boottime);
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	if (sysctl(mib, 2, &boottime, &len, nullptr, 0) < 0)
		return -1;
	return time(nullptr) - boottime.tv_sec;
#elif (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)) && defined(CLOCK_UPTIME)
	struct timespec ts;
	if (clock_gettime(CLOCK_UPTIME, &ts) != 0)
		return -1;
	return ts.tv_sec;
#else
	return 0;
#endif
}

// True random number generator (source: http://www.azillionmonkeys.com/qed/random.html)
static struct
{
	int which;
	time_t t;
	clock_t c;
	int counter;
} entropy = { 0, (time_t)0, (clock_t)0, 0 };

static unsigned char* p = (unsigned char*)(&entropy + 1);
static int accSeed = 0;

int GenerateRandomNumber(const int range)
{
	if (p == ((unsigned char*)(&entropy + 1)))
	{
		switch (entropy.which)
		{
		case 0:
			entropy.t += time(nullptr);
			accSeed ^= entropy.t;
			break;
		case 1:
			entropy.c += clock();
			break;
		case 2:
			entropy.counter++;
			break;
		}
		entropy.which = (entropy.which + 1) % 3;
		p = (unsigned char*)&entropy.t;
	}
	accSeed = ((accSeed * (UCHAR_MAX + 2U)) | 1) + (int)*p;
	p++;
	srand(accSeed);
	return (rand() / (RAND_MAX / range));
}

int GetDirFilesRecursive(const std::string& DirPath, std::map<std::string, int>& _Files)
{
	DIR* dir;
	if ((dir = opendir(DirPath.c_str())) != nullptr)
	{
		struct dirent* ent;
		while ((ent = readdir(dir)) != nullptr)
		{
			if (dirent_is_directory(DirPath, ent))
			{
				if ((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0) && (strcmp(ent->d_name, ".svn") != 0))
				{
					std::string nextdir = DirPath + ent->d_name + "/";
					if (GetDirFilesRecursive(nextdir, _Files))
					{
						closedir(dir);
						return 1;
					}
				}
			}
			else
			{
				std::string fname = DirPath + ent->d_name;
				_Files[fname] = 1;
			}
		}
	}
	closedir(dir);
	return 0;
}

#ifdef WIN32
// From https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
int SetThreadName(const std::thread::native_handle_type& thread, const char* threadName) {
	DWORD dwThreadID = ::GetThreadId(static_cast<HANDLE>(thread));
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = threadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
#pragma warning(pop)
	return 0;
}
#else
// Based on https://stackoverflow.com/questions/2369738/how-to-set-the-name-of-a-thread-in-linux-pthreads
int SetThreadName(const std::thread::native_handle_type& thread, const char* name)
{
#if defined(__linux__) || defined(__linux) || defined(linux)
	char name_trunc[16];
	strncpy(name_trunc, name, sizeof(name_trunc));
	name_trunc[sizeof(name_trunc) - 1] = '\0';
	return pthread_setname_np(thread, name_trunc);
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
	// Not possible to set name of other thread: https://stackoverflow.com/questions/2369738/how-to-set-the-name-of-a-thread-in-linux-pthreads
	return 0;
#elif defined(__NetBSD__)
	char name_trunc[PTHREAD_MAX_NAMELEN_NP];
	strncpy(name_trunc, name, sizeof(name_trunc));
	name_trunc[sizeof(name_trunc) - 1] = '\0';
	return pthread_setname_np(thread, "%s", (void*)name_trunc);
#elif defined(__OpenBSD__) || defined(__DragonFly__)
	char name_trunc[PTHREAD_MAX_NAMELEN_NP];
	strncpy(name_trunc, name, sizeof(name_trunc));
	name_trunc[sizeof(name_trunc) - 1] = '\0';
	pthread_setname_np(thread, name_trunc);
	return 0;
#elif defined(__FreeBSD__)
	char name_trunc[PTHREAD_MAX_NAMELEN_NP];
	strncpy(name_trunc, name, sizeof(name_trunc));
	name_trunc[sizeof(name_trunc) - 1] = '\0';
	pthread_set_name_np(thread, name_trunc);
	return 0;
#endif
	return 0;
}
#endif

#if !defined(WIN32)
bool IsDebuggerPresent()
{
#if defined(__linux__)
	// Linux implementation: Search for 'TracerPid:' in /proc/self/status
	char buf[4096];

	const int status_fd = ::open("/proc/self/status", O_RDONLY);
	if (status_fd == -1)
		return false;

	const ssize_t num_read = ::read(status_fd, buf, sizeof(buf) - 1);
	if (num_read <= 0)
		return false;

	buf[num_read] = '\0';
	constexpr char tracerPidString[] = "TracerPid:";
	const auto tracer_pid_ptr = ::strstr(buf, tracerPidString);
	if (!tracer_pid_ptr)
		return false;

	for (const char* characterPtr = tracer_pid_ptr + sizeof(tracerPidString) - 1; characterPtr <= buf + num_read; ++characterPtr)
	{
		if (::isspace(*characterPtr))
			continue;
		return ::isdigit(*characterPtr) != 0 && *characterPtr != '0';
	}

	return false;
#else
	// MacOS X / BSD: TODO
#	ifdef _DEBUG
	return true;
#	else
	return false;
#	endif
#endif
}
#endif

std::string GenerateUUID() // DCE/RFC 4122
{
	const std::string hexCHARS = "0123456789abcdef";
	std::string uuid = std::string(36, ' ');

	uuid[8] = '-';
	uuid[13] = '-';
	uuid[14] = '4'; //M
	uuid[18] = '-';
	//uuid[19] = ' '; //N Variant 1 UUIDs (10xx N=8..b, 2 bits)
	uuid[23] = '-';

	std::random_device rd;  // a seed source for the random number engine
	std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
	std::uniform_int_distribution<> distrib_F(0, 0x0F);
	std::uniform_int_distribution<> distrib_3(0, 0x03);

	for (size_t ii = 0; ii < uuid.size(); ii++)
	{
		if (uuid[ii] == ' ')
		{
			uuid[ii] = hexCHARS[(ii == 19) ? (8 + (distrib_3(gen) & 0x03)) : distrib_F(gen) & 0x0F];
		}
	}
	return uuid;
}

bool isHexRepresentation(const std::string& input)
{
	const std::string hexCHARS = "0123456789abcdef";
	if (input.empty())
		return false;
	bool bIsHex = true;
	for (auto itt = input.begin(); itt != input.end(); ++itt)
		bIsHex &= (hexCHARS.find(*itt) != std::string::npos);
	return bIsHex;
}

double round_digits(double dIn, const int totDigits)
{
	std::stringstream sstr;
	sstr << std::setprecision(totDigits) << std::fixed << dIn;
	return std::stod(sstr.str());
}

const std::string std_format(const char* szFormat, ...)
{
	va_list vaArgs;
	va_start(vaArgs, szFormat);

	va_list vaCopy;
	va_copy(vaCopy, vaArgs);
	const int iLength = std::vsnprintf(NULL, 0, szFormat, vaCopy);
	va_end(vaCopy);

	std::vector<char> zc(iLength + 1, 0);
	std::vsnprintf(zc.data(), zc.size(), szFormat, vaArgs);
	va_end(vaArgs);
	return std::string(zc.data(), zc.size() - 1);
}

std::string utf8_to_string(const std::string& utf8str)
{
	std::wstring wstr = utf8_to_wstring(utf8str);
	std::string str(wstr.length(), 0);
	std::transform(wstr.begin(), wstr.end(), str.begin(), [](wchar_t c) {
		return (char)c;
		});
	return str;
}

std::wstring utf8_to_wstring(const std::string& utf8str)
{
	// UTF-8 to wstring
	std::wstring_convert<std::codecvt_utf8<wchar_t>> wconv;
	return wconv.from_bytes(utf8str);
}

std::string sha256hex(const std::string& input)
{
	const std::string hexCHARS = "0123456789abcdef";

	unsigned char digest[33] = { 0 };
	char hexdigest[65] = { 0 };
	size_t idxb, idxh;

	SHA256((const unsigned char*)input.c_str(), input.length(), digest);

	for (idxb = 0, idxh = 0; idxb < 32; idxb++, idxh += 2)
	{
		uint8_t bval = digest[idxb] & 0xFF;
		hexdigest[idxh] = hexCHARS[(bval >> 4) & 0xf];
		hexdigest[idxh + 1] = hexCHARS[bval & 0xF];
	}
	hexdigest[idxh] = 0;
	return std::string(hexdigest);
}

std::string sha256raw(const std::string& input)
{
	unsigned char digest[33] = { 0 };
	SHA256((const unsigned char*)input.c_str(), input.length(), digest);
	return std::string((const char*)digest, 32);
}

#ifdef _WIN32
#define gmtime_r(timep, result) gmtime_s(result, timep)
#endif

constexpr std::array<const char*, 12> months{ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
constexpr std::array<const char*, 7> wkdays{ "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

char* make_web_time(const time_t rawtime)
{
	static char buffer[256];
	struct tm gmt;
#ifdef _WIN32
	if (gmtime_r(&rawtime, &gmt)) //windows returns errno_t, which returns zero when successful
#else
	if (gmtime_r(&rawtime, &gmt) == nullptr)
#endif
	{
		strcpy(buffer, "Thu, 01 Jan 1970 00:00:00 GMT");
	}
	else
	{
		sprintf(buffer, "%s, %02d %s %04d %02d:%02d:%02d GMT",
			wkdays[gmt.tm_wday],
			gmt.tm_mday,
			months[gmt.tm_mon],
			gmt.tm_year + 1900,
			gmt.tm_hour,
			gmt.tm_min,
			gmt.tm_sec);
	}
	return buffer;
}

const std::string base32RFC4648 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567=";
bool base32_decode(const std::string& input, std::string& output)
{
	if ((input.size() % 8) != 0)
		return false;

	std::vector<uint8_t> outTotal;

	for (uint16_t j = 0; j < (input.size() / 8); j++)
	{
		// pack 8 bytes
		uint64_t buffer = 0;
		for (uint8_t i = 0; i < 8; i++)
		{
			if (i != 0)
			{
				buffer = (buffer << 5);
			}
			// input check
			size_t pos = base32RFC4648.find(input[(j * 8) + i]);
			if (pos == std::string::npos)
			{
				return false;
			}
			else if (pos == 32)		// '=' is padding sign, we skip it
			{
				buffer = buffer | 0;
			}
			else
			{
				buffer = buffer | base32RFC4648.find(input[(j * 8) + i]);
			}
		}
		// output 5 bytes
		for (int8_t x = 4; x >= 0; x--)
		{
			outTotal.push_back((unsigned char)(buffer >> (x * 8)));
		}
	}

	output.assign(std::string(outTotal.begin(), outTotal.end()));
	return true;
}

bool base32_encode(const std::string& input, std::string& output)
{
	if (input.empty())
		return false;

	std::vector<uint8_t> outTotal;

	for (uint16_t j = 0; j < (input.size() / 5); j++)
	{
		// pack 5 bytes
		uint64_t buffer = 0;
		for (uint8_t i = 0; i < 5; i++)
		{
			if (i != 0)
			{
				buffer = (buffer << 8);
			}
			buffer = buffer | input[(j * 5) + i];
		}
		// output 8 bytes
		for (int8_t x = 7; x >= 0; x--)
		{
			outTotal.push_back(base32RFC4648[(buffer >> (x * 5)) & 0x1F]);
		}
	}

	output.assign(std::string(outTotal.begin(), outTotal.end()));
	return true;
}

std::string vector_2_string(std::vector<std::string> const& strings, const std::string& delim)
{
	std::stringstream ss;
	for (const auto& itt : strings)
	{
		if (!ss.str().empty())
			ss << delim;
		ss << itt;
	}
	return ss.str();
}

#define AES_KEY_LENGTH 128/8

bool AESEncryptData(const std::string& szInputBuffer, std::string& szOutputBuffer, const uint8_t* pKey16)
{
	//The following block makes it compatible with RTSS dongle code, but in reality you do not need to make a new string that is longer!
	const size_t encs_length = ((szInputBuffer.size() + AES_BLOCK_SIZE) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;

	uint8_t* pInBuf = new uint8_t[encs_length];
	memset(pInBuf, 0, encs_length);

	memcpy(pInBuf, (const uint8_t*)&szInputBuffer[0], szInputBuffer.size());

	std::string szInputBufffer((const uint8_t*)pInBuf, (const uint8_t*)pInBuf + encs_length);

	delete[] pInBuf;

	// max ciphertext len for a n bytes of plaintext is
   // n + AES_BLOCK_SIZE - 1 bytes
	int nLen = (int)szInputBufffer.size();
	int nCLen = nLen + AES_BLOCK_SIZE;
	int nFLen = 0;

	unsigned char aes_key[AES_KEY_LENGTH];
	memcpy(&aes_key, pKey16, 16);

	unsigned char iv_enc[AES_BLOCK_SIZE];
	memset(iv_enc, 0, AES_BLOCK_SIZE);
	std::string szIV = "7E973805E90B4CE5";
	memcpy(iv_enc, szIV.c_str(), szIV.size());

	// Prepare output buffer
	szOutputBuffer.resize(nCLen);

	bool fOk = false;

	// Perform the encryption
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

	if (!ctx)
		return false;

	EVP_CIPHER_CTX_init(ctx);

	fOk = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, iv_enc);
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
		goto exit_sub;
	}
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	fOk = EVP_EncryptUpdate(ctx, (uint8_t*)&szOutputBuffer[0], &nCLen, (const uint8_t*)&szInputBufffer[0], nLen);
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
		goto exit_sub;
	}

	fOk = EVP_EncryptFinal_ex(ctx, (uint8_t*)(&szOutputBuffer[0]) + nCLen, &nFLen);
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
		goto exit_sub;
	}
exit_sub:
	EVP_CIPHER_CTX_cleanup(ctx);
	EVP_CIPHER_CTX_free(ctx);

	if (!fOk)
		return false;

	szOutputBuffer.resize(nCLen + nFLen);
	return true;
}

bool AESDecryptData(const std::string& szInputBuffer, std::string& szOutputBuffer, const uint8_t* pKey16)
{
	// plaintext will always be equal to or lesser than length of ciphertext
	int nLen = (int)szInputBuffer.size();
	int nPLen = nLen, nFLen = 0;

	unsigned char aes_key[AES_KEY_LENGTH];
	memcpy(&aes_key, pKey16, 16);

	unsigned char iv_enc[AES_BLOCK_SIZE];
	memset(iv_enc, 0, AES_BLOCK_SIZE);
	std::string szIV = "7E973805E90B4CE5";
	memcpy(iv_enc, szIV.c_str(), szIV.size());

	// Prepare output buffer
	szOutputBuffer.resize(nPLen);

	bool fOk = true;

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, iv_enc);
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
		goto exit_sub;
	}
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	if (fOk) fOk = EVP_DecryptUpdate(ctx, (uint8_t*)&szOutputBuffer[0], &nPLen, (const uint8_t*)&szInputBuffer[0], nLen) != 0;
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
		goto exit_sub;
	}

	if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (uint8_t*)(&szOutputBuffer[0]) + nPLen, &nFLen) != 0;
	if (!fOk)
	{
		ERR_print_errors_fp(stderr);
	}
exit_sub:
	EVP_CIPHER_CTX_free(ctx);

	if (!fOk)
		return false;

	szOutputBuffer.resize(nPLen + nFLen);
	return true;
}

