/*
 ****************************************************************************
 *  Copyright (c) 2014 Uriah Liggett <freelaserscanner@gmail.com>           *
 *	This file is part of FreeLSS.                                           *
 *                                                                          *
 *  FreeLSS is free software: you can redistribute it and/or modify         *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  FreeLSS is distributed in the hope that it will be useful,              *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with FreeLSS.  If not, see <http://www.gnu.org/licenses/>.       *
 ****************************************************************************
*/

/**
The 3D coordinate system is defined as positive X going to the right, 
positive Y going up into the air, and negative Z going into the scene (camera look direction).
This is also the standard OpenGL coordinate system.
The origin is the center of the turn table.
*/

// LIBPNG
#define PNG_DEBUG 3
#include <png.h>

// MMAL/BCM
#include <bcm_host.h>
#include <interface/vcos/vcos.h>
#include <mmal/mmal.h>
#include <mmal/mmal_buffer.h>
#include <mmal_util.h>
#include <mmal_util_params.h>
#include <mmal_default_components.h>
#include <mmal_connection.h>

// MICROHTTPD
#include <microhttpd.h>

// LIBJPEG
#include <jpeglib.h>

// LIBIW
#include <iwlib.h>

// OpenSSL
#include <openssl/sha.h>

// Eigen
#include <Eigen/Geometry>

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <exception>
#include <memory>
#include <math.h>
#include <pthread.h>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>
#include <setjmp.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <sys/mount.h>
#include <pwd.h>

// Non-configurable settings
#define FREELSS_VERSION_MAJOR 1
#define FREELSS_VERSION_MINOR 22
#define FREELSS_VERSION_NAME "FreeLSS 1.22"

#define PI 3.14159265359

#define RADIANS_TO_DEGREES(r) ((r / (2 * PI)) * 360)
#define DEGREES_TO_RADIANS(d) ((d / 360.0) * (2 * PI))
#define ROUND(d)((int)(d + 0.5))

#define ABS(a)	   (((a) < 0) ? -(a) : (a))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MIN3
#define MIN3(a, b, c) (MIN(MIN(a, b), c))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MAX3
#define MAX3(a, b, c) (MAX(MAX(a, b), c))
#endif

namespace freelss
{

typedef std::string Exception;
typedef unsigned char byte;
typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned char uint8;
typedef float real32;
typedef double real64;
typedef float real;

/**
 * Defines a name/value pair.
 */
struct Property
{
	Property() : name(""), value("") { }
	Property(const std::string& name, const std::string& value) : name(name), value(value) { }

	std::string name;
	std::string value;
};

/** 
 * 2D Pixel Location.  
 * Image coordinates are from 0 to (MAX - 1). 
 * Pixel layout is top to bottom and left to right.
 */
struct PixelLocation
{	
	real x;
	real y;
};

/** 3D Real Location */
struct Vector3
{
	Vector3() : x(0), y(0), z(0){ }
	Vector3(real x, real y, real z) : x(x), y(y), z(z) { }

	void normalize()
	{
		real len = sqrt(x * x + y * y + z * z);
		x /= len;
		y /= len;
		z /= len;
	}
	
	real dot(const Vector3& a) const
	{
		return x * a.x + y * a.y + z * a.z;
	}

	void cross (Vector3& out, const Vector3& v) const
	{
	    out.x = y * v.z - z * v.y;
	    out.y = z * v.x - x * v.z;
	    out.z = x * v.y - y * v.x;
	}

	real x;
	real y;
	real z;
};

struct ColoredPoint
{
	real32 x;
	real32 y;
	real32 z;

	Vector3 normal;

	unsigned char r;
	unsigned char g;
	unsigned char b;
};

/** Color in the Hue, Saturation, Value format */
struct Hsv
{
	float h;
	float s;
	float v;
};

struct Plane
{
	/** The plane normal */
	Vector3 normal;
	
	/** A point in the plane */
	Vector3 point;
};

struct FaceMap
{
	/** The triangles */
	std::vector<unsigned> triangles;
};

struct Ray
{
	Vector3 origin;
	Vector3 direction;
};

struct DataPoint
{
	/** Populates frameC with the next frame from the results vector starting at index resultIndex */
	static bool readNextFrame(std::vector<DataPoint>& out, const std::vector<DataPoint>& results, size_t & resultIndex);

	/** Updates resultIndex with the ending index of the next frame. */
	static bool readNextFrame(const std::vector<DataPoint>& results, size_t & resultIndex);

	/**
	 * Reduce the number of result rows and filter out some of the noise
	 * @param maxNumRows - The number of rows in the image the produced the frame.
	 * @param numRowBins - The total number of row bins in the entire image, not necessarily what is returned by this function.
	 */
	static void lowpassFilter(std::vector<DataPoint>& output, std::vector<DataPoint>& frame, unsigned maxNumRows, unsigned numRowBins);

	/**
	 * Computes the average of all the records in the bin.
	 */
	static void computeAverage(const std::vector<DataPoint>& bin, DataPoint& out);

	PixelLocation pixel;
	ColoredPoint point;
	real rotation;
	uint16 frame;
	uint8 laserSide;
	uint16 pseudoFrame;
	uint32 index;
};

struct ScanResultFile
{
	std::string extension;
	time_t creationTime;
	__off_t fileSize;
};

struct ScanResult
{
	time_t getScanDate() const;
	std::vector<ScanResultFile> files;
};

struct SoftwareUpdate
{
	std::string name;
	std::string description;
	std::string url;
	std::string releaseDate;
	int majorVersion;
	int minorVersion;
};

/** Units of length */
enum UnitOfLength { UL_UNKNOWN, UL_MILLIMETERS, UL_INCHES, UL_CENTIMETERS };

/** PLY format types */
enum PlyDataFormat { PLY_ASCII, PLY_BINARY };

/** Returns the current point in time in ms */
double GetTimeInSeconds();

/** Returns the amount of space free on the filesystem in megabytes */
int GetFreeSpaceMb();

real ConvertUnitOfLength(real value, UnitOfLength valueUnits, UnitOfLength destinationUnits);
std::string ToString(UnitOfLength unit);
std::string ToString(real value);
std::string ToString(int value);
std::string ToString(bool value);
std::string ToHexString(unsigned char * data, size_t dataLength);
real ToReal(const std::string& str);
int ToInt(const std::string& str);
bool ToBool(const std::string& str);
bool EndsWith(const std::string& str, const std::string& ending);
bool StartsWith(const std::string& str, const std::string& start);
void HtmlEncode(std::string& data);
void LoadProperties();
void SaveProperties();
void MigrateHome();
std::string UrlDecode(const std::string& in);
std::string GetAppHomeDir();
std::string GetScanOutputDir();
std::string GetDebugOutputDir();
std::string GetPropertiesFile();
std::string GetUpdateDir();
std::string TrimString(const std::string& str);
}


// Include wiringPi
#include <wiringPi.h>


